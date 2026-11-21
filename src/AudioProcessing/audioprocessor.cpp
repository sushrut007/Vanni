#include "AudioProcessing/audioprocessor.h"
#include "AudioProcessing/MicrophoneResolver.h"
#include "Utils/VaaniLogging.h"
#include <chrono>
#include <QDebug>
#include <QMetaObject>
#include <QThread>
#include <memory>
#include <algorithm>   // FCR S525, B-1: std::copy in the callback

#ifdef Q_OS_WIN
// Session 596 review, B-9: the Windows endpoint id behind each PortAudio WASAPI
// device. NOMINMAX and WIN32_LEAN_AND_MEAN are defined project-wide (CMakeLists).
#include <windows.h>
#include <objbase.h>        // CoTaskMemFree
#include <mmdeviceapi.h>    // IMMDevice::GetId
#include <pa_win_wasapi.h>  // PaWasapi_GetIMMDevice, PaWasapiStreamInfo
#endif

// Structure to pass to the PortAudio callback
struct AudioProcessorData {
    AudioProcessor* processor;
    std::atomic<bool>* shouldStop;
    std::atomic<bool>* isReceivingAudio;
};

// FCR S525, A-3: the frames-per-buffer value was a bare 1024 at the Pa_OpenStream
// call site and is now named, because the real-time scratch buffer is sized from
// it and the two must not drift apart.
static constexpr unsigned long kFramesPerBuffer = 1024;

namespace {

// Session 596 review, B-9: every PortAudio capture device of the Windows audio
// session API (WASAPI) with the endpoint id Windows gives it -- the id Settings
// stores and the level meter opens. Other host APIs (MME, DirectSound) do not
// expose an endpoint id, so a chosen microphone is always opened through WASAPI.
//
// Loopback entries (a playback device offered for recording) carry the id of the
// PLAYBACK endpoint, and Settings lists capture endpoints only, so a loopback
// entry can never match a stored id and needs no separate test.
std::vector<MicrophoneResolver::Candidate> wasapiCaptureDevices()
{
    std::vector<MicrophoneResolver::Candidate> devices;
#ifdef Q_OS_WIN
    const PaHostApiIndex api = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    if (api < 0)
        return devices;
    const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(api);
    if (!apiInfo)
        return devices;
    for (int i = 0; i < apiInfo->deviceCount; ++i) {
        const PaDeviceIndex device = Pa_HostApiDeviceIndexToDeviceIndex(api, i);
        if (device < 0)
            continue;
        const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
        if (!info || info->maxInputChannels <= 0)
            continue;
        void* raw = nullptr;
        if (PaWasapi_GetIMMDevice(device, &raw) != paNoError || !raw)
            continue;
        // PortAudio keeps ownership: PaWasapi_GetIMMDevice() hands out its own
        // pointer without AddRef(), so it must not be released here.
        LPWSTR endpointId = nullptr;
        if (SUCCEEDED(static_cast<IMMDevice*>(raw)->GetId(&endpointId)) && endpointId) {
            devices.push_back({ device, QString::fromWCharArray(endpointId) });
            CoTaskMemFree(endpointId);
        }
    }
#endif
    return devices;
}

} // namespace

AudioProcessor::AudioProcessor(QObject* parent)
    : QObject(parent)
{
    // FCR S525, B-1: a queued connection needs the argument type known to the
    // meta-object system. Registering a type twice is harmless, so doing it here
    // rather than in main() keeps it beside the signal it belongs to.
    static const int audioVecTypeId = qRegisterMetaType<QVector<qint16>>("QVector<qint16>");
    Q_UNUSED(audioVecTypeId);

}

AudioProcessor::~AudioProcessor()
{
    stop();

    // Session 626 review, B-14: stop() returns at once while the microphone is still
    // being opened (the worker finishes the cancellation itself). Closing the program
    // then freed the callback data and this object's members under the worker, which
    // wrote to them when Windows' permission prompt was answered. The destructor waits
    // for the worker -- a few seconds at most, a prompt can stay open for ever.
    bool workerStillRunning = false;
    if (m_startThread && !m_startThread->wait(kStartThreadWaitMs))
        workerStillRunning = true;
    if (workerStillRunning) {
        // Given up: from here the worker owns `data` and touches nothing of this
        // object (see start()). Nothing it still uses is freed, and PortAudio is not
        // shut down under it; the process is ending.
        std::lock_guard<std::mutex> guard(m_startLifetime->lock);
        m_startLifetime->abandoned = true;
        m_callbackData = nullptr;
        return;
    }

    // Bug 1 Fix (Session 46): Safety cleanup in case stop() wasn't called
    if (m_callbackData) {
        delete static_cast<AudioProcessorData*>(m_callbackData);
        m_callbackData = nullptr;
    }

    if (m_paInitialized) {
        Pa_Terminate();
        m_paInitialized = false;
    }
}

bool AudioProcessor::initializePortAudio(bool announceFailure)
{
    const PaError err = Pa_Initialize();
    if (err != paNoError) {
        // S383: Pa_Initialize failure is almost always an audio subsystem
        // conflict (another app holds exclusive control) or low memory.
        // Log the technical code for diagnostics; show a plain message.
        qWarning() << "AudioProcessor: Pa_Initialize failed:" << Pa_GetErrorText(err);
        if (announceFailure)
            emit errorOccurred(tr("Dictation Pro could not initialise the audio system. "
                "Please close other audio applications and try again. "
                "If the problem continues, restart your computer."));
        m_paInitialized = false;
        return false;
    }
    m_paInitialized = true;
    return true;
}

bool AudioProcessor::refreshDeviceList()
{
    // Never under an open stream: Pa_Terminate() would close it.
    if (m_stream)
        return true;
    if (m_paInitialized) {
        Pa_Terminate();
        m_paInitialized = false;
    }
    return initializePortAudio();
}

void AudioProcessor::setPreferredEndpointId(const QString& endpointId)
{
    m_preferredEndpointId = endpointId.trimmed();
}

int AudioProcessor::resolveCaptureDevice(bool* chosenByEndpoint)
{
    *chosenByEndpoint = false;
    if (!m_preferredEndpointId.isEmpty()) {
        const int chosen = MicrophoneResolver::resolve(m_preferredEndpointId, wasapiCaptureDevices());
        if (chosen >= 0) {
            *chosenByEndpoint = true;
            return chosen;
        }
        if (MicrophoneResolver::storedDeviceMissing(m_preferredEndpointId, chosen)) {
            qCDebug(lcAudio) << "AudioProcessor: chosen microphone not connected; using the default";
            emit microphoneFallback(tr("The microphone chosen in Settings is not connected. "
                "Dictation Pro is using the Windows default microphone."));
        }
    }
    return Pa_GetDefaultInputDevice();
}

bool AudioProcessor::initialize(float sampleRate)
{
    m_sampleRate = sampleRate;
    if (!m_paInitialized && !initializePortAudio(false))   // Session 596 review, B-19
        return false;

    // Issue 8 (FCR Pass 8): no microphone at launch is not fatal. Session 596
    // review (B-9, B-20): the device is resolved at every start(), so nothing is
    // chosen here.
    if (Pa_GetDefaultInputDevice() == paNoDevice)
        qCDebug(lcAudio) << "No default input device at initialization; will re-check in start()";
    return true;
}

bool AudioProcessor::start()
{
    // Guard: already capturing or async start already in progress
    if (m_isCapturing.load())
        return true;
    if (m_startPending.load()) {
        // Session 623 Pass 9 review, B-6: an open is already under way. If a stop()
        // landed on it (the customer cancelled the Loading button), that cancel
        // belonged to the session before this one; this start adopts the open, so the
        // flag is cleared and the open finishes as the new session's.
        m_shouldStop.store(false);
        return true;
    }

    m_shouldStop.store(false);
    m_isReceivingAudio.store(false);
    // Reset so a stale tick from a previous session cannot make a fresh start
    // look healthy, or an old one look stalled.
    m_lastAudioTickMs.store(0, std::memory_order_relaxed);
    m_errorCount = 0;  // Bug 2 Fix (Session 46): Reset error count for clean start

    // Session 596 review, B-20: a microphone plugged in after launch. PortAudio's
    // device list is fixed when it initialises, so it is refreshed here, while no
    // stream is open (the guard above), before anything is looked up in it.
    if (!refreshDeviceList())
        return false;

    // Session 596 review, B-9: open the microphone chosen in Settings, or Windows'
    // default when none is chosen or the chosen one is not connected.
    bool chosenByEndpoint = false;
    m_deviceId = resolveCaptureDevice(&chosenByEndpoint);
    if (m_deviceId == paNoDevice) {
        emit errorOccurred(tr("Dictation Pro could not find a microphone. "
            "Please connect a microphone and try again."));
        return false;
    }

    // Validate device synchronously -- Pa_GetDeviceInfo is fast (cached by PortAudio)
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(m_deviceId);
    if (!deviceInfo) {
        // S383: Pa_GetDeviceInfo returns null for invalid/disconnected device.
        qWarning() << "AudioProcessor: Pa_GetDeviceInfo returned null for device" << m_deviceId;
        emit errorOccurred(tr("The selected microphone is no longer available. "
            "Please check your microphone connection and try again."));
        return false;
    }
    m_currentDeviceName = QString::fromUtf8(deviceInfo->name);

    // Bug 1 Fix (Session 46): Store data pointer for proper cleanup in stop()
    auto* data = new AudioProcessorData();
    data->processor = this;
    data->shouldStop = &m_shouldStop;
    data->isReceivingAudio = &m_isReceivingAudio;
    m_callbackData = data;

    // Snapshot parameters used inside the worker to avoid data-race on members
    const int      capturedDeviceId  = m_deviceId;
    const float    capturedSampleRate = m_sampleRate;
    const double   suggestedLatency  = deviceInfo->defaultLowInputLatency;
    const bool     useWasapiConversion = chosenByEndpoint;

    m_startPending.store(true);

    // SU-4: Run Pa_OpenStream + Pa_StartStream on a worker thread.
    // Windows WASAPI COM initialisation dispatches messages to the calling
    // thread's message loop. Blocking the GUI thread here prevents COM from
    // completing its own re-entrant dispatch -- classic deadlock.
    // The worker emits audioStreamStarted() (success) or errorOccurred()
    // (failure) via QueuedConnection so the GUI thread receives them safely.
    // Session 626 review, B-14: the worker outlives a quick shutdown only if the
    // destructor could not wait for it. `lifetime` says so: once the destructor has given
    // up waiting it sets `abandoned` under the lock, and from then on the worker touches
    // no member of this object, closes what it opened, frees what it owns and ends.
    auto lifetime = m_startLifetime;
    auto* startThread = QThread::create([this, data, capturedDeviceId,
                                         capturedSampleRate, suggestedLatency,
                                         useWasapiConversion, lifetime]() {
        // Runs `work` while this object is known to be alive; false when it was given up.
        const auto whileAlive = [&lifetime](const std::function<void()>& work) {
            std::lock_guard<std::mutex> guard(lifetime->lock);
            if (lifetime->abandoned)
                return false;
            work();
            return true;
        };

        PaStreamParameters inputParameters;
        inputParameters.device                    = capturedDeviceId;
        inputParameters.channelCount              = 1;
        inputParameters.sampleFormat              = paInt16;
        inputParameters.suggestedLatency          = suggestedLatency;
        inputParameters.hostApiSpecificStreamInfo = nullptr;

#ifdef Q_OS_WIN
        // Session 596 review, B-9: a WASAPI device in shared mode runs at the
        // format Windows mixes at (typically 48 kHz stereo). Auto-convert has
        // Windows deliver the 16 kHz mono the engine needs. Lives until
        // Pa_OpenStream() returns, which is all PortAudio needs.
        PaWasapiStreamInfo wasapiInfo{};
        if (useWasapiConversion) {
            wasapiInfo.size        = sizeof(PaWasapiStreamInfo);
            wasapiInfo.hostApiType = paWASAPI;
            wasapiInfo.version     = 1;
            wasapiInfo.flags       = paWinWasapiAutoConvert;
            inputParameters.hostApiSpecificStreamInfo = &wasapiInfo;
        }
#else
        Q_UNUSED(useWasapiConversion);
#endif

        PaStream* stream = nullptr;   // published to m_stream only while this object is alive
        PaError err = Pa_OpenStream(
            &stream,
            &inputParameters,
            nullptr,
            capturedSampleRate,
            kFramesPerBuffer,
            paClipOff,
            staticCallback,
            data
        );

        if (err != paNoError) {
            const int errCode = static_cast<int>(err);
            delete data;
            whileAlive([&]() {
                m_callbackData = nullptr;
                m_startPending.store(false);
                QMetaObject::invokeMethod(this, [this, errCode]() {
                    emitStreamErrorMessage(errCode);
                }, Qt::QueuedConnection);
            });
            return;
        }

        // stop() may have been called while Pa_OpenStream was running.
        // Honour the cancellation: close the just-opened stream and exit.
        bool cancelled = false;
        if (!whileAlive([&]() { m_stream = stream; cancelled = m_shouldStop.load(); })) {
            Pa_CloseStream(stream);   // given up at shutdown: never started
            delete data;
            return;
        }
        if (cancelled) {
            Pa_CloseStream(stream);
            delete data;
            whileAlive([&]() {
                m_stream = nullptr;
                m_callbackData = nullptr;
                m_startPending.store(false);
            });
            return;  // Cancelled -- SRM stop() already handles recognition state
        }

        err = Pa_StartStream(stream);
        if (err != paNoError) {
            const int errCode = static_cast<int>(err);
            Pa_CloseStream(stream);
            delete data;
            whileAlive([&]() {
                m_stream = nullptr;
                m_callbackData = nullptr;
                m_startPending.store(false);
                QMetaObject::invokeMethod(this, [this, errCode]() {
                    emitStreamErrorMessage(errCode);
                }, Qt::QueuedConnection);
            });
            return;
        }

        // Session 623 Pass 9 review, B-6: stop() can land between the check before
        // Pa_StartStream and here (it only sets the flag while an open is pending).
        // The stream is then closed with capture off and no start is announced --
        // announcing it put the toolbar back on Stop with the recognizer already
        // stopped, so nothing the customer said was typed.
        bool stoppedMeanwhile = false;
        const bool alive = whileAlive([&]() {
            stoppedMeanwhile = m_shouldStop.load();
            if (stoppedMeanwhile)
                return;
            m_isCapturing.store(true);
            m_startPending.store(false);
            QMetaObject::invokeMethod(this, [this]() { emit audioStreamStarted(); }, Qt::QueuedConnection);
        });
        if (!alive || stoppedMeanwhile) {
            Pa_AbortStream(stream);
            Pa_CloseStream(stream);
            delete data;
            if (alive) {
                whileAlive([&]() {
                    m_stream = nullptr;
                    m_callbackData = nullptr;
                    m_startPending.store(false);
                });
            }
            return;
        }
    });

    m_startThread = startThread;   // Session 626 review, B-14: the destructor waits on it
    startThread->setObjectName(QStringLiteral("AudioStartThread"));
    connect(startThread, &QThread::finished, startThread, &QObject::deleteLater);
    startThread->start();
    return true;  // Startup initiated -- result arrives via signals
}

void AudioProcessor::stop()
{
    // SU-4: Set stop intent unconditionally so the async start worker (if
    // running) sees it after Pa_OpenStream returns and aborts cleanly.
    m_shouldStop.store(true);

    if (!m_isCapturing.load() && !m_startPending.load()) {
        // Neither capturing nor starting -- nothing to do.
        m_shouldStop.store(false);  // Reset for next start attempt
        return;
    }

    // Async start in progress but stream not yet running.
    // The worker will see m_shouldStop=true and close any opened stream.
    if (!m_isCapturing.load()) {
        return;
    }

    // Standard active-stream teardown
    if (m_stream) {
        // Issue 2b fix: Use Pa_AbortStream() instead of a Pa_Sleep() polling loop.
        // Pa_AbortStream() signals the PortAudio callback to stop immediately and
        // returns as soon as the stream is no longer active. This avoids blocking
        // the calling thread (which may be the main thread during closeEvent) for
        // up to 3 seconds. Pa_StopStream() drains the buffer gracefully but can
        // block; Pa_AbortStream() discards pending buffers and returns promptly.
        PaError abortErr = Pa_AbortStream(m_stream);
        if (abortErr != paNoError && abortErr != paStreamIsStopped) {
            qCDebug(lcAudio) << "AudioProcessor: Pa_AbortStream error:" << Pa_GetErrorText(abortErr);
            // Fallback: brief non-blocking wait using QThread::msleep (not Pa_Sleep)
            // to give PortAudio a chance to quiesce before closing.
            const int maxWaitMs = 200;
            const int checkIntervalMs = 20;
            int elapsedMs = 0;
            while (Pa_IsStreamActive(m_stream) && elapsedMs < maxWaitMs) {
                QThread::msleep(static_cast<unsigned long>(checkIntervalMs));
                elapsedMs += checkIntervalMs;
            }
        }

        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }

    // Bug 1 Fix (Session 46): Clean up callback data to prevent memory leak
    // The callback may not have run after shouldStop was set, so we must delete here
    if (m_callbackData) {
        delete static_cast<AudioProcessorData*>(m_callbackData);
        m_callbackData = nullptr;
    }

    m_isCapturing.store(false);
}

QStringList AudioProcessor::listAudioDevices() const
{
    QStringList devices;
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo && deviceInfo->maxInputChannels > 0) {
            devices << QString::fromUtf8(deviceInfo->name);
        }
    }
    return devices;
}

// S387: monotonic milliseconds. steady_clock cannot jump backwards when the
// system clock is adjusted, which a wall clock can.
static qint64 nowTickMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

qint64 AudioProcessor::msSinceLastAudio() const
{
    const qint64 last = m_lastAudioTickMs.load(std::memory_order_relaxed);
    if (last == 0)
        return -1;   // no buffer has arrived yet this session
    return nowTickMs() - last;
}

int AudioProcessor::staticCallback(const void* inputBuffer, void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData)
{
    // C4100 (Pass 8): outputBuffer and timeInfo are part of the PortAudio API
    // signature and must be present, but this capture-only callback never writes
    // output audio or inspects timing. Suppress with (void) rather than removing
    // the parameter name so future readers know they are intentionally unused.
    (void)outputBuffer;
    (void)timeInfo;

    auto* data = static_cast<AudioProcessorData*>(userData);
    const short* input = static_cast<const short*>(inputBuffer);

    // Bug 1 Fix (Session 46): Don't delete here - stop() will handle cleanup
    // This prevents double-delete and ensures cleanup even if callback doesn't run
    if (data->shouldStop->load()) {
        return paComplete;
    }

    // Bug 3 Fix (Session 45): Check for device errors before processing
    data->processor->handleStatusFlags(statusFlags);

    return data->processor->processAudio(input, framesPerBuffer);
}

int AudioProcessor::processAudio(const short* input, unsigned long framesPerBuffer)
{
    if (input != nullptr) {
        m_isReceivingAudio.store(true);
        // One relaxed atomic store per buffer (roughly every 20-30 ms). This is
        // the only thing added to the realtime path: no allocation, no lock and
        // no system call, so it cannot cause an audio dropout.
        m_lastAudioTickMs.store(nowTickMs(), std::memory_order_relaxed);
        // FCR S525, B-1: carry the samples, not the pointer. See audioprocessor.h.
        QVector<qint16> samples(static_cast<qsizetype>(framesPerBuffer));
        std::copy(input, input + framesPerBuffer, samples.begin());
        emit audioDataReady(std::move(samples));
    }
    return paContinue;
}

// Bug 3 Fix (Session 45): Handle PortAudio status flags for device disconnect detection
void AudioProcessor::handleStatusFlags(PaStreamCallbackFlags statusFlags)
{
    // Bug 2 Fix (Session 46): Use member variable instead of static
    // This ensures each AudioProcessor instance has its own error tracking

    // Check for input/output overflow/underflow which can indicate device issues
    if (statusFlags & paInputOverflow) {
        qCDebug(lcAudio) << "AudioProcessor: Input overflow detected";
    }

    if (statusFlags & paInputUnderflow) {
        qCDebug(lcAudio) << "AudioProcessor: Input underflow detected";
    }

    // Check for stream abort - this indicates a serious device error (often disconnect)
    if (statusFlags & (paInputOverflow | paOutputOverflow | paInputUnderflow | paOutputUnderflow)) {
        m_errorCount++;

        // If we get multiple consecutive errors, likely device disconnected
        if (m_errorCount > 10) {
            qCDebug(lcAudio) << "AudioProcessor: Multiple stream errors - device likely disconnected";

            // Emit device disconnected signal with device name
            emit deviceDisconnected(m_currentDeviceName);

            // Stop capture to prevent further errors
            m_shouldStop.store(true);

            m_errorCount = 0;  // Reset counter after handling
        }
    } else {
        // No errors - reset counter
        m_errorCount = 0;
    }
}

// FCR1 Bug 7: Specialize PortAudio error reporting. paUnanticipatedHostError is
// the WASAPI signal for AUDCLNT_E_DEVICE_INVALIDATED, which fires both for
// physical device disconnect and for runtime permission revocation. Route it to
// a permission-focused message + deviceDisconnected (which has an "Open Audio
// Settings" path) instead of the generic "PortAudio error: <code>" dialog.
void AudioProcessor::emitStreamErrorMessage(int paErrorCode)
{
    if (paErrorCode == paUnanticipatedHostError) {
        // SA-1: paUnanticipatedHostError on WASAPI signals mic access revocation or denial
        // below the registry pre-check level. Route to microphonePermissionDenied so the
        // UI opens ms-settings:privacy-microphone instead of ms-settings:sound.
        emit microphonePermissionDenied();
        emit deviceDisconnected(m_currentDeviceName);
    } else if (paErrorCode == paInsufficientMemory) {
        qWarning() << "AudioProcessor: PortAudio out of memory:" << Pa_GetErrorText(paErrorCode);
        emit errorOccurred(tr("Dictation Pro ran out of memory while accessing your microphone. "
            "Please close unused applications and try again."));
    } else {
        // Log the raw PortAudio error code for developer diagnosis.
        qWarning() << "AudioProcessor: PortAudio stream error:" << Pa_GetErrorText(static_cast<PaError>(paErrorCode));
        emit errorOccurred(tr("A microphone error occurred. Please check that your microphone is connected "
            "and not in use by another application."));
    }
}

bool AudioProcessor::hasAvailableInputDevice() const
{
    // Issue 9 (FCR Pass 8): Live hardware check using PortAudio cached device list.
    // Used by SpeechRecognitionManager::hasActiveMicrophoneDevice() on non-Windows
    // or as a cross-platform fallback. Returns true if any input device is present.
    const int deviceId = (m_deviceId != paNoDevice) ? m_deviceId
                                                     : Pa_GetDefaultInputDevice();
    if (deviceId == paNoDevice) return false;
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    return info && info->maxInputChannels > 0;
}

#pragma once

#include <QObject>
#include <portaudio.h>
#include <atomic>
#include <functional>   // Session 626 review, B-14
#include <memory>
#include <mutex>
#include <QPointer>
#include <QThread>
#include <vector>
#include <QString>
#include <QVector>   // FCR S525, B-1: audioDataReady now carries the samples
class AudioProcessor : public QObject
{
    Q_OBJECT

public:
    explicit AudioProcessor(QObject* parent = nullptr);
    ~AudioProcessor();

    bool initialize(float sampleRate = 16000.0f);
    // SU-4: start() initiates async audio startup (Pa_OpenStream on a worker
    // thread). Returns true when the startup sequence has been initiated; false
    // for immediate sync failures (bad device, permission denied at caller).
    // Completion is signalled via audioStreamStarted() (success) or
    // errorOccurred() (failure).
    bool start();
    void stop();

    bool isCapturing() const { return m_isCapturing.load(); }
    bool isAudioDetected() const { return m_isReceivingAudio.load(); }

    // S387: milliseconds since the audio callback last delivered a buffer, or
    // -1 if it has not delivered one since capture started.
    //
    // This is NOT a silence measure. PortAudio keeps delivering buffers full of
    // near-zero samples while the user is simply not speaking, so a rising value
    // means the callback itself has stopped -- which is what happens when the
    // microphone is unplugged or taken over by another application mid-session.
    qint64 msSinceLastAudio() const;

    QStringList listAudioDevices() const;

    // Session 596 review, B-9: the microphone to capture from, as the Windows
    // endpoint id Settings stores. Empty means "Default microphone" -- whatever
    // Windows' default input device is at the moment capture starts. It takes
    // effect at the next start(): the device is resolved every time capture
    // starts, after the device list is refreshed (B-20), so a PortAudio device
    // index is never kept from one session to the next. That is also why the
    // old selectAudioDevice(int) is gone: an index does not survive a refresh,
    // and nothing called it.
    void setPreferredEndpointId(const QString& endpointId);
    QString preferredEndpointId() const { return m_preferredEndpointId; }

    bool hasAvailableInputDevice() const;

signals:
    // Emitted when audio data is available.
    //
    // FCR S525, B-1 (P0, release blocker). This used to be
    // audioDataReady(const short* data, int length), emitted from inside the PortAudio
    // real-time callback with a pointer PortAudio owns and that is valid only until the
    // callback returns. The single connection to it is Qt::QueuedConnection
    // (speechrecognitionmanager.cpp:67-68), which copies the ARGUMENTS -- so it copied
    // the address, not the 320 to 480 samples behind it. By the time the interface thread
    // dereferenced it at :1730 the audio thread had usually overwritten the buffer, and
    // after the stop path (Pa_AbortStream at :239, Pa_CloseStream at :253) the memory was
    // not merely stale but freed. Reading freed memory is undefined behaviour, and it is
    // the shape of the fragment-and-dropout reports this product keeps receiving.
    //
    // Carrying the samples BY VALUE is the whole fix: a QVector is copied properly across
    // a queued connection and lives as long as the slot does. Registered once with
    // qRegisterMetaType in the constructor, because a queued connection needs the type
    // known to the meta-object system.
    void audioDataReady(QVector<qint16> data);

    // Emitted when audio capture starts.
    // SU-4: audioStreamStarted replaced the old synchronous start notification.
    // It is emitted from the worker thread (via QueuedConnection) once
    // Pa_StartStream succeeds, keeping the GUI thread free during startup.
    //
    // Session 404: there is deliberately no stop-side counterpart. This class has
    // exactly one host, SpeechRecognitionManager, which calls stop() itself and so
    // already knows the moment capture ends. captureStopped() announced that moment
    // to nobody. Add it back only in the same change as the code that receives it.
    void audioStreamStarted();

    // Emitted if an error occurs
    void errorOccurred(const QString& message);
    void microphonePermissionDenied();

    // Bug 3 Fix (Session 45): Emitted when audio device disconnected
    void deviceDisconnected(const QString& deviceName);

    // Session 596 review, B-9: the microphone chosen in Settings is not connected,
    // so capture uses the Windows default instead. Carries the sentence to show.
    void microphoneFallback(const QString& message);

private:
    // FCR1 Bug 7: Route paUnanticipatedHostError (WASAPI permission revocation /
    // AUDCLNT_E_DEVICE_INVALIDATED) to a more informative error string.
    void emitStreamErrorMessage(int paErrorCode);

    // Session 596 review, B-20: Pa_Initialize() with the customer-facing message
    // on failure. Records the result in m_paInitialized. Session 596 review,
    // B-19: at launch the failure is not announced -- the speech manager records
    // it and the customer is told once, when they press Start.
    bool initializePortAudio(bool announceFailure = true);
    // Session 596 review, B-20: PortAudio reads the device list once, at
    // initialisation, so a microphone plugged in later is invisible to it.
    // Re-initialising when no stream is open is PortAudio's documented way to
    // see it.
    bool refreshDeviceList();
    // Session 596 review, B-9: the PortAudio device to open for the preferred
    // endpoint, or Windows' default. `chosenByEndpoint` is set when the stored
    // endpoint was found, which is when the stream needs the WASAPI format
    // conversion.
    int resolveCaptureDevice(bool* chosenByEndpoint);

    static int staticCallback(const void* inputBuffer, void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData);

    int processAudio(const short* input, unsigned long framesPerBuffer);

    // Bug 3 Fix (Session 45): Handle PortAudio status flags for device errors
    void handleStatusFlags(PaStreamCallbackFlags statusFlags);

    PaStream* m_stream = nullptr;
    float m_sampleRate = 16000.0f;
    std::atomic<bool> m_shouldStop{ false };
    std::atomic<bool> m_isCapturing{ false };
    std::atomic<bool> m_isReceivingAudio{ false };
    // Written from the realtime audio callback, read from the UI thread.
    // A steady-clock tick is used rather than wall time so the value cannot be
    // disturbed by the user or the system changing the clock.
    std::atomic<qint64> m_lastAudioTickMs{ 0 };
    // SU-4: True while the async start worker thread is running.
    // Guards against concurrent starts and signals stop() to defer cleanup.
    std::atomic<bool> m_startPending{ false };

    // Session 626 review, B-14: the start worker and the destructor. `abandoned` is set
    // under `lock` when the destructor gave up waiting; the worker (which holds a
    // copy of the pointer) then touches no member of this object.
    struct StartLifetime {
        std::mutex lock;
        bool abandoned = false;
    };
    std::shared_ptr<StartLifetime> m_startLifetime = std::make_shared<StartLifetime>();
    QPointer<QThread> m_startThread;
    static constexpr unsigned long kStartThreadWaitMs = 5000;
    int m_deviceId = -1;
    // Session 596 review, B-20: whether THIS object holds PortAudio initialised,
    // so every Pa_Terminate() pairs with the last Pa_Initialize(). It replaces
    // two function-local static flags that assumed one initialise per process.
    bool m_paInitialized = false;
    // Session 596 review, B-9: the Windows endpoint id of the chosen microphone.
    QString m_preferredEndpointId;
    QString m_currentDeviceName;  // Bug 3 Fix (Session 45): Track device name for disconnect notification
    void* m_callbackData = nullptr;  // Bug 1 Fix (Session 46): Track callback data for proper cleanup
    int m_errorCount = 0;  // Bug 2 Fix (Session 46): Track consecutive errors per instance (not static)
};

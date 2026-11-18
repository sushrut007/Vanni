#include "SpeechRecognition/speechrecognitionmanager.h"

#include "AudioProcessing/AdaptiveProcessingManager.h"
#include "AudioProcessing/audioprocessor.h"
#include "SpeechRecognition/WhisperRecognizer.h"
#include "LanguageProcessing/LanguageDetector.h"
#include "Utils/LanguageUtils.h"
#include "Utils/VaaniLogging.h"

#include <QDebug>
#include <QDir>
#include <QMetaType>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <mmdeviceapi.h>
#include <windows.h>
#endif

SpeechRecognitionManager::SpeechRecognitionManager(QObject* parent)
    : QObject(parent)
{
    m_audioProcessor = std::make_unique<AudioProcessor>(this);
    m_adaptiveManager = std::make_unique<AdaptiveProcessingManager>(this);

    connect(m_audioProcessor.get(), &AudioProcessor::audioDataReady, this,
            &SpeechRecognitionManager::handleAudioData, Qt::QueuedConnection);
    connect(m_audioProcessor.get(), &AudioProcessor::audioStreamStarted, this,
            &SpeechRecognitionManager::onAudioStreamStarted, Qt::QueuedConnection);
    connect(m_audioProcessor.get(), &AudioProcessor::errorOccurred, this,
            &SpeechRecognitionManager::errorOccurred, Qt::QueuedConnection);
    connect(m_audioProcessor.get(), &AudioProcessor::deviceDisconnected, this,
            &SpeechRecognitionManager::deviceDisconnected, Qt::QueuedConnection);
    connect(m_audioProcessor.get(), &AudioProcessor::microphoneFallback, this,
            &SpeechRecognitionManager::statusUpdate, Qt::QueuedConnection);
    connect(m_audioProcessor.get(), &AudioProcessor::microphonePermissionDenied, this,
            &SpeechRecognitionManager::microphonePermissionDenied, Qt::QueuedConnection);

    connect(m_adaptiveManager.get(), &AdaptiveProcessingManager::configurationChanged, this,
            &SpeechRecognitionManager::handleConfigurationChanged, Qt::QueuedConnection);

    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            &SpeechRecognitionManager::onApplicationStateChanged, Qt::QueuedConnection);
}

SpeechRecognitionManager::~SpeechRecognitionManager()
{
    if (m_whisperRecognizer)
        m_whisperRecognizer->shutdown();
    stop();
}

bool SpeechRecognitionManager::initialize(const QString& modelPath)
{
    m_modelDirectory = modelPath;

    QDir dir(modelPath);
    const QStringList bins = dir.entryList({QStringLiteral("ggml-*.bin")}, QDir::Files);
    for (const QString& name : bins) {
        if (!name.contains(QStringLiteral("silero"), Qt::CaseInsensitive)) {
            m_whisperModelPath = dir.filePath(name);
            break;
        }
    }
    if (m_whisperModelPath.isEmpty())
        m_whisperModelPath = dir.filePath(QStringLiteral("ggml-base-q8_0.bin"));

    if (!m_audioProcessor->initialize()) {
        m_engineFailure = EngineFailure::AudioSystem;
        return false;
    }
    m_engineFailure = EngineFailure::None;

    if (m_whisperRecognizer)
        return m_engineFailure == EngineFailure::None;

    m_whisperRecognizer = std::make_unique<WhisperRecognizer>(m_whisperModelPath, this);

    qRegisterMetaType<RecognitionOwner>("RecognitionOwner");

    connect(m_whisperRecognizer.get(), &WhisperRecognizer::resultReady, this,
            &SpeechRecognitionManager::handleWhisperResult);
    connect(m_whisperRecognizer.get(), &WhisperRecognizer::errorOccurred, this,
            &SpeechRecognitionManager::errorOccurred);
    connect(m_whisperRecognizer.get(), &WhisperRecognizer::confidenceReady, this,
            &SpeechRecognitionManager::handleConfidence);
    connect(m_whisperRecognizer.get(), &WhisperRecognizer::noticeReady, this,
            &SpeechRecognitionManager::statusUpdate);
    connect(m_whisperRecognizer.get(), &WhisperRecognizer::shouldStopDictation, this,
            [this](const QString& reason) { emit dictationStopRequested(reason); });
    connect(m_whisperRecognizer.get(), &WhisperRecognizer::sessionFinished, this,
            &SpeechRecognitionManager::sessionFinished);
    connect(m_whisperRecognizer.get(), &WhisperRecognizer::speechDetected, this,
            &SpeechRecognitionManager::speechActivityDetected);

    bool whisperInitialized = false;
    if (!m_whisperRecognizer->initialize()) {
        qCWarning(lcSpeech) << "Failed to initialize Whisper recognizer.";
        m_engineFailure = EngineFailure::SpeechModel;
    } else {
        whisperInitialized = true;
    }

    m_initialized = true;
    return whisperInitialized;
}

bool SpeechRecognitionManager::reloadWhisperModel(const QString& modelDirectory)
{
    if (isRunning())
        cancel();
    if (m_whisperRecognizer) {
        m_whisperRecognizer->shutdown();
        m_whisperRecognizer.reset();
    }
    m_initialized = false;
    m_engineFailure = EngineFailure::None;
    return initialize(modelDirectory);
}

bool SpeechRecognitionManager::retryInitialization()
{
    if (m_engineFailure == EngineFailure::AudioSystem && !m_modelDirectory.isEmpty())
        initialize(m_modelDirectory);
    return m_initialized && m_engineFailure == EngineFailure::None;
}

bool SpeechRecognitionManager::start(RecognitionOwner owner, CtrlMode ctrlMode)
{
    m_retryOwner = owner;
    m_retryAllowed = (ctrlMode == CtrlMode::FollowKeyboard);

    if (m_engineFailure != EngineFailure::None) {
        qCInfo(lcSpeech) << "Dictation start refused: speech engine not ready";
        return false;
    }
    if (!m_initialized) {
        qCInfo(lcSpeech) << "Dictation start refused: speech engine still loading";
        return false;
    }
    if (!isMicrophonePermissionGranted()) {
        qCWarning(lcSpeech) << "Microphone permission denied";
        m_pendingMicPermissionRetry = true;
        emit microphonePermissionDenied();
        return false;
    }
    if (!hasActiveMicrophoneDevice()) {
        qCWarning(lcSpeech) << "No active microphone device";
        return false;
    }

    stop();

    m_activeRecognizer = m_whisperRecognizer.get();

    if (m_activeRecognizer) {
        m_activeRecognizer->setLanguage(m_dictationLanguage);
        if (!m_whisperRecognizer->startFor(owner, ctrlMode)) {
            emit errorOccurred(tr("Dictation could not start. Please check your microphone and try again."));
            return false;
        }
    }

    m_audioProcessor->start();
    startAudioWatchdog();
    return true;
}

void SpeechRecognitionManager::startAudioWatchdog()
{
    if (!m_audioProcessor)
        return;
    if (!m_audioWatchdog) {
        m_audioWatchdog = new QTimer(this);
        m_audioWatchdog->setInterval(2000);
        connect(m_audioWatchdog, &QTimer::timeout, this, [this]() {
            if (!m_audioProcessor)
                return;
            const qint64 idleMs = m_audioProcessor->msSinceLastAudio();
            if (idleMs < 0 || idleMs < 5000)
                return;
            stopAudioWatchdog();
            emit errorOccurred(
                tr("Vaani stopped receiving audio. Check that your microphone is connected and not in use by "
                   "another app, then start dictation again."));
        });
    }
    m_audioWatchdog->start();
}

void SpeechRecognitionManager::stopAudioWatchdog()
{
    if (m_audioWatchdog)
        m_audioWatchdog->stop();
}

bool SpeechRecognitionManager::isLanguageSupported(const QString& lang) const
{
    const QString code = lang.trimmed().toLower();
    if (code.isEmpty())
        return false;
    return LanguageUtils::supportedDictationLanguageCodes().contains(code);
}

void SpeechRecognitionManager::onAudioStreamStarted()
{
    if (!isRunning())
        return;
    emit recognitionStarted();
}

QString SpeechRecognitionManager::stop()
{
    return stopSession(-1);
}

QString SpeechRecognitionManager::stopAfterRelease(qint64 msSinceRelease)
{
    return stopSession(std::max<qint64>(0, msSinceRelease));
}

QString SpeechRecognitionManager::stopSession(qint64 msSinceRelease)
{
    QString finalResult;
    const bool wasRunning = (m_activeRecognizer != nullptr);

    stopAudioWatchdog();
    m_audioProcessor->stop();

    if (m_activeRecognizer) {
        if (msSinceRelease >= 0 && m_activeRecognizer == m_whisperRecognizer.get())
            finalResult = m_whisperRecognizer->stopAfterRelease(msSinceRelease);
        else
            finalResult = m_activeRecognizer->stop();
        m_activeRecognizer = nullptr;
    }

    if (wasRunning) {
        m_smoothedMicLevel = 0.f;
        emit microphoneLevelChanged(0.f);
        emit recognitionStopped();
    }
    return finalResult;
}

RecognitionOwner SpeechRecognitionManager::finishingOwner() const
{
    return m_whisperRecognizer ? m_whisperRecognizer->finishingOwner() : RecognitionOwner::None;
}

void SpeechRecognitionManager::cancel()
{
    const bool wasRunning = (m_activeRecognizer != nullptr);
    stopAudioWatchdog();
    m_audioProcessor->stop();
    if (m_whisperRecognizer)
        m_whisperRecognizer->cancel();
    m_activeRecognizer = nullptr;
    if (wasRunning) {
        m_smoothedMicLevel = 0.f;
        emit microphoneLevelChanged(0.f);
        emit recognitionStopped();
    }
}

bool SpeechRecognitionManager::isMicrophonePermissionGranted()
{
#ifdef Q_OS_WIN
    auto readConsentValue = [](HKEY root, const wchar_t* keyPath) -> QString {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(root, keyPath, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
            return QString();

        wchar_t buf[64] = {};
        DWORD bufSize = sizeof(buf);
        DWORD type = 0;
        LONG st = RegQueryValueExW(hKey, L"Value", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bufSize);
        RegCloseKey(hKey);

        if (st != ERROR_SUCCESS || type != REG_SZ)
            return QString();

        return QString::fromWCharArray(buf).trimmed().toLower();
    };

    static constexpr wchar_t kGlobalKey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager"
        L"\\ConsentStore\\microphone";

    QString globalVal = readConsentValue(HKEY_CURRENT_USER, kGlobalKey);
    if (globalVal.isEmpty())
        globalVal = readConsentValue(HKEY_LOCAL_MACHINE, kGlobalKey);

    if (!globalVal.isEmpty() && globalVal != QLatin1String("allow"))
        return false;

    return true;
#else
    return true;
#endif
}

bool SpeechRecognitionManager::hasActiveMicrophoneDevice() const
{
#if defined(Q_OS_WIN)
    if (!isMicrophonePermissionGranted())
        return false;
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator)
        return false;
    IMMDeviceCollection* collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    enumerator->Release();
    if (FAILED(hr) || !collection)
        return false;
    UINT count = 0;
    collection->GetCount(&count);
    collection->Release();
    return count > 0;
#else
    return m_audioProcessor && m_audioProcessor->hasAvailableInputDevice();
#endif
}

void SpeechRecognitionManager::onApplicationStateChanged(Qt::ApplicationState state)
{
    if (state != Qt::ApplicationActive || !m_pendingMicPermissionRetry || !m_retryAllowed)
        return;

    m_pendingMicPermissionRetry = false;

    if (isMicrophonePermissionGranted()) {
        emit statusUpdate(tr("Microphone access granted — restarting dictation…"));
        start(m_retryOwner);
    } else {
        emit statusUpdate(tr("Microphone access still denied. Enable it in Windows Settings."));
    }
}

void SpeechRecognitionManager::setDictationLanguage(const QString& lang)
{
    if (m_dictationLanguage != lang)
        m_dictationLanguage = lang;
    if (m_whisperRecognizer)
        m_whisperRecognizer->setLanguage(lang);
}

QString SpeechRecognitionManager::getDictationLanguage() const
{
    return m_dictationLanguage;
}

void SpeechRecognitionManager::setProcessingMode(ProcessingMode mode)
{
    if (m_adaptiveManager)
        m_adaptiveManager->setUserPreferredMode(mode);
}

void SpeechRecognitionManager::setAdaptiveProcessingEnabled(bool enabled)
{
    if (m_adaptiveManager)
        m_adaptiveManager->setAdaptiveEnabled(enabled);
}

void SpeechRecognitionManager::setNotificationsEnabled(bool enabled)
{
    Q_UNUSED(enabled);
}

QStringList SpeechRecognitionManager::listAudioDevices() const
{
    return m_audioProcessor->listAudioDevices();
}

void SpeechRecognitionManager::setPreferredMicrophone(const QString& endpointId)
{
    m_audioProcessor->setPreferredEndpointId(endpointId);
}

bool SpeechRecognitionManager::isRunning() const
{
    return m_activeRecognizer && m_activeRecognizer->isRunning();
}

ProcessingConfig SpeechRecognitionManager::getCurrentProcessingConfig() const
{
    if (m_adaptiveManager)
        return m_adaptiveManager->getCurrentConfig();
    return ProcessingConfig{};
}

QStringList SpeechRecognitionManager::getSupportedLanguages() const
{
    if (m_whisperRecognizer && m_whisperRecognizer->getLanguageDetector())
        return m_whisperRecognizer->getLanguageDetector()->getSupportedLanguages();
    return QStringList{QStringLiteral("en")};
}

QPair<QString, float> SpeechRecognitionManager::getDetectedLanguage() const
{
    if (m_whisperRecognizer && m_whisperRecognizer->getLanguageDetector()) {
        auto* detector = m_whisperRecognizer->getLanguageDetector();
        return qMakePair(detector->getCurrentLanguage(), detector->getDetectionConfidence());
    }
    return qMakePair(QStringLiteral("en"), 1.0f);
}

void SpeechRecognitionManager::handleAudioData(const QVector<qint16>& data)
{
    if (!data.isEmpty()) {
        double sum = 0.0;
        for (qint16 sample : data)
            sum += static_cast<double>(sample) * static_cast<double>(sample);
        const float rms = static_cast<float>(std::sqrt(sum / data.size()) / 32768.0);
        const float attack = 0.42f;
        const float release = 0.14f;
        const float blend = rms > m_smoothedMicLevel ? attack : release;
        m_smoothedMicLevel = m_smoothedMicLevel * (1.f - blend) + rms * blend;
        m_smoothedMicLevel = qBound(0.f, m_smoothedMicLevel, 1.f);
        emit microphoneLevelChanged(m_smoothedMicLevel);
    }

    if (m_activeRecognizer && m_activeRecognizer->isRunning())
        m_activeRecognizer->processAudio(data.constData(), static_cast<int>(data.size()));
}

QString SpeechRecognitionManager::applyTrainedVocabulary(const QString& text, const QString& language)
{
    Q_UNUSED(language);
    return text;
}

void SpeechRecognitionManager::handleWhisperResult(const QString& text, bool isFinal, RecognitionOwner owner)
{
    handleRecognizerResult(text, isFinal, owner, true);
}

void SpeechRecognitionManager::handleRecognizerResult(const QString& text, bool isFinal, RecognitionOwner owner,
                                                      bool vocabularyAlreadyApplied)
{
    const QString corrected =
        vocabularyAlreadyApplied ? text : applyTrainedVocabulary(text, m_dictationLanguage);
    emit recognitionResult(corrected, isFinal, owner);
}

void SpeechRecognitionManager::handleConfigurationChanged(const ProcessingConfig& config)
{
    if (m_activeRecognizer)
        m_activeRecognizer->updateProcessingConfig(config);
    emit processingConfigChanged(config);
}

void SpeechRecognitionManager::handleConfidence(float confidence)
{
    emit confidenceReady(confidence);
}

QString SpeechRecognitionManager::engineToken() const
{
    return QStringLiteral("whisper");
}

void SpeechRecognitionManager::clearWhisperContext(const QString& reason)
{
    Q_UNUSED(reason);
    if (m_whisperRecognizer)
        m_whisperRecognizer->reset();
}

void SpeechRecognitionManager::setWhisperRawMode(bool enabled)
{
    if (m_whisperRecognizer)
        m_whisperRecognizer->setRawMode(enabled);
}
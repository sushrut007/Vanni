#pragma once

#include "AudioProcessing/ProcessingTypes.h"
#include "SpeechRecognition/IRecognizer.h"
#include "SpeechRecognition/RecognitionSessions.h"

#include <QGuiApplication>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <memory>

class AudioProcessor;
class AdaptiveProcessingManager;
class WhisperRecognizer;

class SpeechRecognitionManager : public QObject
{
    Q_OBJECT

public:
    explicit SpeechRecognitionManager(QObject* parent = nullptr);
    ~SpeechRecognitionManager();

    bool initialize(const QString& modelPath);
    bool reloadWhisperModel(const QString& modelDirectory);

    enum class EngineFailure {
        None,
        AudioSystem,
        SpeechModel
    };
    EngineFailure engineFailure() const { return m_engineFailure; }
    bool retryInitialization();

    bool isInitialized() const { return m_initialized; }
    bool start(RecognitionOwner owner, CtrlMode ctrlMode = CtrlMode::FollowKeyboard);
    QString stop();
    QString stopAfterRelease(qint64 msSinceRelease);
    void cancel();
    void setDictationLanguage(const QString& lang);
    QString getDictationLanguage() const;
    void setProcessingMode(ProcessingMode mode);
    void setAdaptiveProcessingEnabled(bool enabled);
    void setNotificationsEnabled(bool enabled);
    QStringList listAudioDevices() const;
    void setPreferredMicrophone(const QString& endpointId);
    bool isRunning() const;
    RecognitionOwner finishingOwner() const;
    void clearWhisperContext(const QString& reason = QString());
    void setWhisperRawMode(bool enabled);

    ProcessingConfig getCurrentProcessingConfig() const;
    QStringList getSupportedLanguages() const;
    QPair<QString, float> getDetectedLanguage() const;

    void startAudioWatchdog();
    void stopAudioWatchdog();

    bool isLanguageSupported(const QString& lang) const;
    bool hasActiveMicrophoneDevice() const;
    QString engineToken() const;

    static QString applyTrainedVocabulary(const QString& text, const QString& language);
    static bool isMicrophonePermissionGranted();

signals:
    void recognitionStarted();
    void recognitionStopped();
    void sessionFinished(RecognitionOwner owner);
    void recognitionResult(const QString& text, bool isFinal, RecognitionOwner owner);
    void errorOccurred(const QString& message);
    void microphonePermissionDenied();
    void deviceDisconnected(const QString& deviceName);
    void processingConfigChanged(const ProcessingConfig& config);
    void confidenceReady(float confidence);
    void statusUpdate(const QString& message);
    void dictationStopRequested(const QString& reason);
    void speechActivityDetected();
    void microphoneLevelChanged(float level);

private slots:
    void handleAudioData(const QVector<qint16>& data);
    void handleWhisperResult(const QString& text, bool isFinal, RecognitionOwner owner);
    void handleRecognizerResult(const QString& text, bool isFinal, RecognitionOwner owner,
                                bool vocabularyAlreadyApplied = false);
    void handleConfigurationChanged(const ProcessingConfig& config);
    void handleConfidence(float confidence);
    void onApplicationStateChanged(Qt::ApplicationState state);
    void onAudioStreamStarted();

private:
    QString stopSession(qint64 msSinceRelease);

    std::unique_ptr<AudioProcessor> m_audioProcessor;
    std::unique_ptr<WhisperRecognizer> m_whisperRecognizer;
    bool m_initialized = false;
    QTimer* m_audioWatchdog = nullptr;
    std::unique_ptr<AdaptiveProcessingManager> m_adaptiveManager;

    IRecognizer* m_activeRecognizer = nullptr;
    QString m_whisperModelPath;
    QString m_modelDirectory;
    EngineFailure m_engineFailure = EngineFailure::None;
    QString m_dictationLanguage = QStringLiteral("en");

    bool m_pendingMicPermissionRetry = false;
    RecognitionOwner m_retryOwner = RecognitionOwner::None;
    bool m_retryAllowed = true;
    float m_smoothedMicLevel = 0.f;
};
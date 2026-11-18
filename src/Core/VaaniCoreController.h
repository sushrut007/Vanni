#pragma once

#include "SpeechRecognition/RecognitionSessions.h"
#include "UI/VaaniListeningPill.h"

#include <QObject>
#include <QString>

class SpeechRecognitionManager;
class VaaniSettingsStore;
class ExternalTextInjector;
class GlobalPushToTalkMonitor;
class TrayController;
class SingleInstanceGuard;
class QThread;
class QTimer;

class VaaniCoreController : public QObject
{
    Q_OBJECT

public:
    enum class EngineState {
        Idle,
        Loading,
        Ready,
        Failed,
    };
    Q_ENUM(EngineState)

    enum class TrayStatus {
        Loading,
        Idle,
        Listening,
        Error,
    };
    Q_ENUM(TrayStatus)

    explicit VaaniCoreController(QObject* parent = nullptr);
    ~VaaniCoreController() override;

    EngineState engineState() const { return m_engineState; }
    QString engineError() const { return m_engineError; }

    void startEngineLoad();

    // Tray-first startup: show icon, then load speech on the next event-loop turn.
    void start();

    void attachSingleInstanceGuard(SingleInstanceGuard* guard);
    void handleSecondInstanceActivate();

    TrayController* trayPresenter() const { return m_trayPresenter; }

    TrayStatus trayStatus() const { return m_trayStatus; }
    QString trayStatusText() const;
    bool isTrayDictationActive() const;
    bool handsFreeDictationActive() const;
    void toggleTrayDictation();
    void prepareForQuit();

    void setPushToTalkCaptureActive(bool active);
    GlobalPushToTalkMonitor* pushToTalkMonitor() const { return m_holdMonitor; }

    SpeechRecognitionManager* speechManager() const { return m_speechManager; }
    VaaniSettingsStore* settings() const { return m_settings; }
    ExternalTextInjector* textInjector() const { return m_textInjector; }

signals:
    void engineStateChanged(VaaniCoreController::EngineState state, const QString& detail);
    void trayStatusChanged(VaaniCoreController::TrayStatus status, const QString& detail);
    void recognitionStarted();
    void recognitionStopped();
    void recognitionResult(const QString& text, bool isFinal);
    void errorOccurred(const QString& message);
    void microphonePermissionDenied();
    void deviceDisconnected(const QString& deviceName);
    void injectionFailed(const QString& userMessage);
    void dictationHudPhaseChanged(VaaniDictationHudPhase phase);

public:
    VaaniDictationHudPhase dictationHudPhase() const { return m_hudPhase; }

public slots:
    void applySettingsToSpeechEngine();
    void reloadSpeechEngine();

private slots:
    void runStartupFlow();
    void finishEngineLoadFromWorker(bool initializeOk, const QString& modelsDirectory,
                                    const QString& initError);
    void onPartialInjectDebounced();
    void onRecognizerResult(const QString& text, bool isFinal, RecognitionOwner owner);
    void onSpeechManagerError(const QString& message);
    void onRecognitionSessionStarted();
    void onRecognitionSessionStopped();
    void onSessionFinished(RecognitionOwner owner);
    void onInjectionFailed(const QString& message);
    void onHoldStarted();
    void onHoldEnded();
    void onHoldCancelled();
    void onHoldToggled();
    void onHoldTailStop();

private:
    void setEngineState(EngineState state, const QString& detail = QString());
    void applyPushToTalkHotkey();
    bool beginExternalDictation();
    void endExternalDictationWithTail();
    void cancelExternalDictation();
    void updateTrayStatus();
    void setDictationHudPhase(VaaniDictationHudPhase phase);

    SpeechRecognitionManager* m_speechManager = nullptr;
    GlobalPushToTalkMonitor* m_holdMonitor = nullptr;
    QTimer* m_holdTailTimer = nullptr;
    QTimer* m_partialInjectTimer = nullptr;
    VaaniSettingsStore* m_settings = nullptr;
    ExternalTextInjector* m_textInjector = nullptr;
    TrayController* m_trayPresenter = nullptr;
    QThread* m_loadThread = nullptr;
    QString m_modelsDirectory;
    EngineState m_engineState = EngineState::Idle;
    QString m_engineError;
    bool m_handsFreeSession = false;
    bool m_pttSessionActive = false;
    bool m_trayToggleSession = false;
    TrayStatus m_trayStatus = TrayStatus::Loading;
    bool m_pttPendingAfterLoad = false;
    QString m_pendingPartialInject;
    QString m_lastInjectedPartial;
    VaaniDictationHudPhase m_hudPhase = VaaniDictationHudPhase::Hidden;
};
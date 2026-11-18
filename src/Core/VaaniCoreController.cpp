#include "Core/VaaniCoreController.h"

#include "Core/VaaniSpeechTuning.h"
#include "Injection/ExternalTextInjector.h"
#include "Input/GlobalPushToTalkMonitor.h"
#include "Input/HoldReleaseTail.h"
#include "Settings/VaaniSettingsStore.h"
#include "SpeechRecognition/speechrecognitionmanager.h"
#include "Utils/VaaniColdStart.h"
#include "Tray/TrayController.h"
#include "Utils/SingleInstanceGuard.h"
#include "Models/VaaniModelDownloader.h"
#include "UI/VaaniOnboardingWizard.h"
#include "Utils/WhisperModelPaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QThread>
#include <QTimer>

VaaniCoreController::VaaniCoreController(QObject* parent)
    : QObject(parent)
    , m_speechManager(new SpeechRecognitionManager(this))
    , m_settings(new VaaniSettingsStore(this))
    , m_textInjector(new ExternalTextInjector(this))
    , m_holdMonitor(new GlobalPushToTalkMonitor(this))
    , m_holdTailTimer(new QTimer(this))
    , m_partialInjectTimer(new QTimer(this))
{
    m_settings->load();
    m_holdTailTimer->setSingleShot(true);
    m_partialInjectTimer->setSingleShot(true);
    m_partialInjectTimer->setInterval(Vaani::SpeechTuning::kPartialInjectDebounceMs);
    connect(m_partialInjectTimer, &QTimer::timeout, this,
            &VaaniCoreController::onPartialInjectDebounced);

    m_textInjector->setKeystrokeFallbackEnabled(m_settings->keystrokeFallbackOnPasteFailure());
    applyPushToTalkHotkey();
    m_holdMonitor->setDoubleTapToggleEnabled(m_settings->doubleTapToggleEnabled());

    connect(m_speechManager, &SpeechRecognitionManager::recognitionStarted,
            this, &VaaniCoreController::onRecognitionSessionStarted);
    connect(m_speechManager, &SpeechRecognitionManager::recognitionStopped,
            this, &VaaniCoreController::onRecognitionSessionStopped);
    connect(m_speechManager, &SpeechRecognitionManager::recognitionStarted,
            this, &VaaniCoreController::recognitionStarted);
    connect(m_speechManager, &SpeechRecognitionManager::recognitionStopped,
            this, &VaaniCoreController::recognitionStopped);
    connect(m_speechManager, &SpeechRecognitionManager::recognitionResult,
            this, &VaaniCoreController::onRecognizerResult);
    connect(m_speechManager, &SpeechRecognitionManager::recognitionStarted, this, [this]() {
        setDictationHudPhase(VaaniDictationHudPhase::Listening);
    });
    connect(m_speechManager, &SpeechRecognitionManager::sessionFinished, this,
            &VaaniCoreController::onSessionFinished);
    connect(m_speechManager, &SpeechRecognitionManager::speechActivityDetected, this, [this]() {
        if (m_speechManager && m_speechManager->isRunning())
            setDictationHudPhase(VaaniDictationHudPhase::Transcribing);
    });
    connect(m_speechManager, &SpeechRecognitionManager::errorOccurred,
            this, &VaaniCoreController::onSpeechManagerError);
    connect(m_speechManager, &SpeechRecognitionManager::microphonePermissionDenied,
            this, &VaaniCoreController::microphonePermissionDenied);
    connect(m_speechManager, &SpeechRecognitionManager::deviceDisconnected,
            this, &VaaniCoreController::deviceDisconnected);

    connect(m_settings, &VaaniSettingsStore::dictationLanguageChanged, this,
            [this](const QString& code) {
                m_speechManager->setDictationLanguage(code);
            });
    connect(m_settings, &VaaniSettingsStore::microphoneEndpointChanged, this,
            [this](const QString& id) {
                m_speechManager->setPreferredMicrophone(id);
            });
    connect(m_settings, &VaaniSettingsStore::keystrokeFallbackChanged, this,
            [this](bool enabled) {
                m_textInjector->setKeystrokeFallbackEnabled(enabled);
            });
    connect(m_settings, &VaaniSettingsStore::pushToTalkHotkeyChanged, this,
            [this](const VaaniHotkey& hotkey) {
                m_holdMonitor->setHotkey(hotkey);
                m_settings->save();
            });
    connect(m_settings, &VaaniSettingsStore::doubleTapToggleChanged, this,
            [this](bool enabled) {
                m_holdMonitor->setDoubleTapToggleEnabled(enabled);
                m_settings->save();
            });
    connect(m_textInjector, &ExternalTextInjector::injectionFailed,
            this, &VaaniCoreController::onInjectionFailed);

    connect(m_holdMonitor, &GlobalPushToTalkMonitor::holdStarted,
            this, &VaaniCoreController::onHoldStarted);
    connect(m_holdMonitor, &GlobalPushToTalkMonitor::holdEnded,
            this, &VaaniCoreController::onHoldEnded);
    connect(m_holdMonitor, &GlobalPushToTalkMonitor::holdCancelled,
            this, &VaaniCoreController::onHoldCancelled);
    connect(m_holdMonitor, &GlobalPushToTalkMonitor::holdToggled,
            this, &VaaniCoreController::onHoldToggled);
    connect(m_holdTailTimer, &QTimer::timeout, this, &VaaniCoreController::onHoldTailStop);

    m_trayPresenter = new TrayController(this, this);

    applySettingsToSpeechEngine();
    updateTrayStatus();
}

void VaaniCoreController::start()
{
    if (m_trayPresenter)
        m_trayPresenter->show();
    QTimer::singleShot(0, this, &VaaniCoreController::runStartupFlow);
}

void VaaniCoreController::runStartupFlow()
{
    const QString modelId = m_settings->selectedModelId();
    const bool installed = Vaani::isModelInstalled(modelId);
    const bool showSetup = !installed || !m_settings->onboardingCompleted();

    if (showSetup) {
        VaaniOnboardingWizard wizard(this);
        wizard.setWindowModality(Qt::ApplicationModal);
        if (wizard.exec() != QDialog::Accepted) {
            if (!Vaani::isModelInstalled(m_settings->selectedModelId())) {
                setEngineState(EngineState::Failed,
                               QStringLiteral("Download a speech model to use dictation."));
                updateTrayStatus();
                return;
            }
        }
    }

    startEngineLoad();
}

void VaaniCoreController::attachSingleInstanceGuard(SingleInstanceGuard* guard)
{
    if (m_trayPresenter)
        m_trayPresenter->setInstanceGuard(guard);
}

void VaaniCoreController::handleSecondInstanceActivate()
{
    if (m_trayPresenter)
        m_trayPresenter->onSecondInstanceActivate();
}

VaaniCoreController::~VaaniCoreController()
{
    if (m_loadThread) {
        m_loadThread->quit();
        m_loadThread->wait(5000);
    }
}

void VaaniCoreController::setPushToTalkCaptureActive(bool active)
{
    m_holdMonitor->setSuspended(active);
}

void VaaniCoreController::applyPushToTalkHotkey()
{
    m_holdMonitor->setHotkey(m_settings->pushToTalkHotkey());
}

void VaaniCoreController::startEngineLoad()
{
    if (m_engineState == EngineState::Loading)
        return;

    setEngineState(EngineState::Loading, QStringLiteral("Loading speech model…"));

    if (m_loadThread && m_loadThread->isRunning()) {
        m_loadThread->quit();
        m_loadThread->wait(5000);
    }

    m_loadThread = new QThread(this);
    m_speechManager->moveToThread(m_loadThread);

    connect(m_loadThread, &QThread::started, m_speechManager, [this]() {
        Vaani::ColdStart::PhaseTimer phase;

        const Vaani::ModelPathResult models = Vaani::resolveInstalledModels();
        Vaani::ColdStart::logPhase("model_ensure", phase.elapsedMs());
        if (!models.ok) {
            QMetaObject::invokeMethod(
                this, "finishEngineLoadFromWorker", Qt::QueuedConnection,
                Q_ARG(bool, false), Q_ARG(QString, QString()), Q_ARG(QString, models.errorMessage));
            return;
        }

        const QString initPath = models.modelsDirectory + QLatin1Char('/');
        phase.restart();
        bool ok = false;
        if (m_speechManager->isInitialized())
            ok = m_speechManager->reloadWhisperModel(initPath);
        else
            ok = m_speechManager->initialize(initPath);
        Vaani::ColdStart::logPhase("speech_manager_initialize", phase.elapsedMs());

        QMetaObject::invokeMethod(
            this, "finishEngineLoadFromWorker", Qt::QueuedConnection,
            Q_ARG(bool, ok), Q_ARG(QString, initPath), Q_ARG(QString, QString()));
    });
    connect(m_loadThread, &QThread::finished, m_loadThread, &QObject::deleteLater);
    m_loadThread->start();
}

void VaaniCoreController::reloadSpeechEngine()
{
    if (isTrayDictationActive())
        cancelExternalDictation();
    startEngineLoad();
}

void VaaniCoreController::finishEngineLoadFromWorker(bool initializeOk,
                                                       const QString& modelsDirectory,
                                                       const QString& initError)
{
    m_speechManager->moveToThread(QCoreApplication::instance()->thread());
    if (m_loadThread)
        m_loadThread->quit();

    if (!initError.isEmpty()) {
        setEngineState(EngineState::Failed, initError);
        return;
    }

    if (!modelsDirectory.isEmpty())
        m_modelsDirectory = modelsDirectory;

    if (initializeOk
        && m_speechManager->engineFailure() == SpeechRecognitionManager::EngineFailure::None) {
        m_speechManager->setProcessingMode(Vaani::SpeechTuning::kDefaultProcessingMode);
        m_speechManager->setAdaptiveProcessingEnabled(true);
        m_speechManager->setNotificationsEnabled(false);
        applySettingsToSpeechEngine();
        applyPushToTalkHotkey();
        setEngineState(EngineState::Ready, QString());
        qInfo() << "[Vaani] Speech engine ready";
        if (m_pttPendingAfterLoad) {
            m_pttPendingAfterLoad = false;
            m_holdTailTimer->stop();
            beginExternalDictation();
        }
    } else {
        const QString detail =
            m_speechManager->engineFailure() == SpeechRecognitionManager::EngineFailure::SpeechModel
                ? QStringLiteral("Speech model failed to load")
                : QStringLiteral("Audio system failed to initialize");
        setEngineState(EngineState::Failed, detail);
    }
}

void VaaniCoreController::applySettingsToSpeechEngine()
{
    if (!m_speechManager->isInitialized())
        return;
    m_speechManager->setDictationLanguage(m_settings->dictationLanguageCode());
    m_speechManager->setPreferredMicrophone(m_settings->microphoneEndpointId());
}

bool VaaniCoreController::beginExternalDictation()
{
    if (m_engineState != EngineState::Ready)
        return false;
    if (m_speechManager->isRunning())
        return true;

    const bool started = m_speechManager->start(
        RecognitionOwner::FloatingBar, CtrlMode::AlwaysDictation);
    if (started) {
        m_pttSessionActive = true;
        qInfo() << "[Vaani] External dictation started";
        updateTrayStatus();
    }
    return started;
}

void VaaniCoreController::endExternalDictationWithTail()
{
    m_handsFreeSession = false;
    if (!m_pttSessionActive && !m_speechManager->isRunning())
        return;

    m_holdTailTimer->stop();
    m_holdTailTimer->start(HoldReleaseTail::kTailMs);
}

void VaaniCoreController::cancelExternalDictation()
{
    m_handsFreeSession = false;
    m_pttSessionActive = false;
    m_trayToggleSession = false;
    m_holdTailTimer->stop();
    m_speechManager->cancel();
    updateTrayStatus();
    qInfo() << "[Vaani] External dictation cancelled";
}

void VaaniCoreController::onHoldTailStop()
{
    if (!m_speechManager->isRunning()) {
        m_pttSessionActive = false;
        return;
    }
    const QString tail = m_speechManager->stopAfterRelease(HoldReleaseTail::kTailMs);
    m_pttSessionActive = false;
    m_trayToggleSession = false;
    updateTrayStatus();
    qInfo() << "[Vaani] External dictation ended (tail); stop returned:" << tail;
}

void VaaniCoreController::onHoldStarted()
{
    if (m_engineState != EngineState::Ready) {
        m_pttPendingAfterLoad = true;
        return;
    }
    m_holdTailTimer->stop();
    beginExternalDictation();
}

void VaaniCoreController::onHoldEnded()
{
    endExternalDictationWithTail();
}

void VaaniCoreController::onHoldCancelled()
{
    cancelExternalDictation();
}

void VaaniCoreController::onHoldToggled()
{
    m_handsFreeSession = true;
    updateTrayStatus();
    qInfo() << "[Vaani] Hands-free dictation (PTT toggle)";
}

QString VaaniCoreController::trayStatusText() const
{
    switch (m_trayStatus) {
    case TrayStatus::Loading:
        return QStringLiteral("Status: Loading speech engine…");
    case TrayStatus::Idle:
        return QStringLiteral("Status: Idle");
    case TrayStatus::Listening:
        return m_handsFreeSession || m_trayToggleSession
            ? QStringLiteral("Status: Listening (hands-free)")
            : QStringLiteral("Status: Listening");
    case TrayStatus::Error:
        return QStringLiteral("Status: Error — %1").arg(m_engineError);
    }
    return QStringLiteral("Status: Unknown");
}

bool VaaniCoreController::isTrayDictationActive() const
{
    return m_speechManager && m_speechManager->isRunning();
}

bool VaaniCoreController::handsFreeDictationActive() const
{
    return m_handsFreeSession || m_trayToggleSession;
}

void VaaniCoreController::prepareForQuit()
{
    m_holdTailTimer->stop();
    m_partialInjectTimer->stop();
    if (m_holdMonitor) {
        m_holdMonitor->setSuspended(true);
    }
    cancelExternalDictation();
    if (m_speechManager) {
        if (m_speechManager->isRunning())
            m_speechManager->cancel();
    }
    if (m_loadThread && m_loadThread->isRunning()) {
        if (m_speechManager)
            m_speechManager->moveToThread(thread());
        m_loadThread->quit();
        m_loadThread->wait(3000);
    }
}

void VaaniCoreController::toggleTrayDictation()
{
    if (m_engineState != EngineState::Ready)
        return;

    if (m_speechManager->isRunning()) {
        m_holdTailTimer->stop();
        m_speechManager->stop();
        m_pttSessionActive = false;
        m_trayToggleSession = false;
        m_handsFreeSession = false;
        updateTrayStatus();
        qInfo() << "[Vaani] Tray dictation stopped";
        return;
    }

    if (beginExternalDictation()) {
        m_trayToggleSession = true;
        m_handsFreeSession = true;
        updateTrayStatus();
        qInfo() << "[Vaani] Tray dictation started (hands-free)";
    }
}

void VaaniCoreController::updateTrayStatus()
{
    TrayStatus next = TrayStatus::Idle;
    if (m_engineState == EngineState::Loading || m_engineState == EngineState::Idle)
        next = TrayStatus::Loading;
    else if (m_engineState == EngineState::Failed)
        next = TrayStatus::Error;
    else if (m_speechManager && m_speechManager->isRunning())
        next = TrayStatus::Listening;
    else
        next = TrayStatus::Idle;

    if (m_trayStatus == next)
        return;
    m_trayStatus = next;
    emit trayStatusChanged(next, trayStatusText());
}

void VaaniCoreController::onRecognizerResult(const QString& text, bool isFinal,
                                             RecognitionOwner owner)
{
    if (owner != RecognitionOwner::FloatingBar && owner != RecognitionOwner::None)
        return;

    if (m_speechManager && m_speechManager->isRunning() && !isFinal)
        setDictationHudPhase(VaaniDictationHudPhase::Transcribing);

    emit recognitionResult(text, isFinal);
    const QString kind = isFinal ? QStringLiteral("final") : QStringLiteral("partial");
    qDebug() << "[Vaani]" << kind << "recognition:" << text;

    if (text.isEmpty())
        return;

    const bool sessionOpen = m_textInjector->externalDictationSessionActive()
        || (m_speechManager
            && m_speechManager->finishingOwner() == RecognitionOwner::FloatingBar);
    if (!sessionOpen)
        return;

    if (isFinal) {
        setDictationHudPhase(VaaniDictationHudPhase::Transcribing);
        m_partialInjectTimer->stop();
        m_pendingPartialInject.clear();
        m_lastInjectedPartial.clear();
        m_textInjector->enqueueFinalPhrase(text);
        return;
    }

    if (!m_settings->injectPartialResults())
        return;

    m_pendingPartialInject = text;
    m_partialInjectTimer->start();
}

void VaaniCoreController::onPartialInjectDebounced()
{
    if (!m_textInjector->externalDictationSessionActive())
        return;
    if (m_pendingPartialInject.isEmpty()
        || m_pendingPartialInject == m_lastInjectedPartial) {
        return;
    }
    m_lastInjectedPartial = m_pendingPartialInject;
    m_textInjector->enqueueFinalPhrase(m_pendingPartialInject);
}

void VaaniCoreController::onRecognitionSessionStarted()
{
    m_textInjector->setExternalDictationSessionActive(true);
    updateTrayStatus();
}

void VaaniCoreController::onRecognitionSessionStopped()
{
    m_partialInjectTimer->stop();
    m_pendingPartialInject.clear();
    m_lastInjectedPartial.clear();
    m_pttSessionActive = false;
    m_trayToggleSession = false;
    updateTrayStatus();

    const RecognitionOwner finishing = m_speechManager ? m_speechManager->finishingOwner()
                                                       : RecognitionOwner::None;
    if (finishing == RecognitionOwner::FloatingBar) {
        setDictationHudPhase(VaaniDictationHudPhase::Transcribing);
        return;
    }

    m_textInjector->setExternalDictationSessionActive(false);
    setDictationHudPhase(VaaniDictationHudPhase::Hidden);
}

void VaaniCoreController::onSessionFinished(RecognitionOwner owner)
{
    if (owner != RecognitionOwner::FloatingBar)
        return;

    m_textInjector->setExternalDictationSessionActive(false);
    setDictationHudPhase(VaaniDictationHudPhase::Hidden);
}

void VaaniCoreController::setDictationHudPhase(VaaniDictationHudPhase phase)
{
    if (m_hudPhase == phase)
        return;
    m_hudPhase = phase;
    emit dictationHudPhaseChanged(phase);
}

void VaaniCoreController::onInjectionFailed(const QString& message)
{
    emit injectionFailed(message);
}

void VaaniCoreController::onSpeechManagerError(const QString& message)
{
    emit errorOccurred(message);
    qWarning() << "[Vaani] speech error:" << message;
}

void VaaniCoreController::setEngineState(EngineState state, const QString& detail)
{
    if (m_engineState == state && detail == m_engineError)
        return;
    m_engineState = state;
    m_engineError = detail;
    emit engineStateChanged(state, detail);
    updateTrayStatus();
}
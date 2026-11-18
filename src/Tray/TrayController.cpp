#include "Tray/TrayController.h"

#include "Core/VaaniCoreController.h"
#include "Injection/ExternalTextInjector.h"
#include "Settings/VaaniSettingsDialog.h"
#include "Settings/VaaniSettingsStore.h"
#include "SpeechRecognition/speechrecognitionmanager.h"
#include "UI/VaaniDictationCapturePanel.h"
#include "UI/VaaniListeningPill.h"
#include "Utils/SingleInstanceGuard.h"
#include "Utils/VaaniBrandIcon.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QMenu>
#include <QSystemTrayIcon>

TrayController::TrayController(VaaniCoreController* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_tray(new QSystemTrayIcon(this))
    , m_menu(new QMenu())
    , m_listeningPill(new VaaniListeningPill())
    , m_capturePanel(new VaaniDictationCapturePanel())
{
    m_listeningPill->setTrayIcon(m_tray);
    m_tray->setIcon(trayIconForState(VaaniCoreController::TrayStatus::Loading));
    m_tray->setToolTip(QStringLiteral("Vaani — external dictation"));

    m_statusAction = m_menu->addAction(QStringLiteral("Status: starting…"));
    m_statusAction->setEnabled(false);

    m_menu->addSeparator();
    m_dictationAction = m_menu->addAction(QStringLiteral("Start dictation"));
    connect(m_dictationAction, &QAction::triggered, this, &TrayController::toggleDictationFromTray);

    QAction* settingsAction = m_menu->addAction(QStringLiteral("Open Settings"));
    connect(settingsAction, &QAction::triggered, this, &TrayController::openSettings);

    m_menu->addSeparator();
    QAction* exitAction = m_menu->addAction(QStringLiteral("Exit"));
    connect(exitAction, &QAction::triggered, this, &TrayController::exitApplication);

    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated,
            this, &TrayController::onTrayActivated);

    connect(m_core, &VaaniCoreController::engineStateChanged,
            this, [this](VaaniCoreController::EngineState state, const QString& detail) {
                refreshMenu();
                refreshListeningFeedback();
                if (state == VaaniCoreController::EngineState::Ready)
                    onEngineReady();
                else if (state == VaaniCoreController::EngineState::Failed)
                    onEngineFailed(detail);
            });
    connect(m_core, &VaaniCoreController::trayStatusChanged,
            this, [this]() {
                refreshMenu();
                refreshListeningFeedback();
            });
    connect(m_core->settings(), &VaaniSettingsStore::listeningIndicatorChanged,
            this, &TrayController::refreshListeningFeedback);
    connect(m_core, &VaaniCoreController::dictationHudPhaseChanged,
            this, &TrayController::refreshDictationHud);
    connect(m_core->speechManager(), &SpeechRecognitionManager::microphoneLevelChanged,
            m_listeningPill, &VaaniListeningPill::setInputLevel);
    connect(m_core, &VaaniCoreController::recognitionResult,
            this, &TrayController::onRecognitionResult);
    connect(m_core, &VaaniCoreController::injectionFailed,
            this, [this](const QString& message) {
                showUserError(message, 6000);
            });
    connect(m_core->textInjector(), &ExternalTextInjector::phraseNeedsCapture,
            this, [this](const QString& text) { m_capturePanel->showPhrase(text); });
    connect(m_core, &VaaniCoreController::errorOccurred, this,
            [this](const QString& message) { showUserError(message); });
    connect(m_core, &VaaniCoreController::microphonePermissionDenied, this, [this]() {
        showUserError(QStringLiteral(
                          "Microphone access is denied. Allow desktop apps in Windows Privacy → Microphone."),
                      12000);
    });
    connect(m_core, &VaaniCoreController::deviceDisconnected, this,
            [this](const QString& deviceName) {
                showUserError(QStringLiteral("Microphone disconnected: %1").arg(deviceName), 10000);
            });
}

TrayController::~TrayController()
{
    delete m_capturePanel;
    delete m_listeningPill;
}

void TrayController::show()
{
    const QIcon icon = trayIconForState(m_core->trayStatus());
    m_tray->setIcon(icon);
    if (!m_tray->icon().pixmap(16).isNull()) {
        m_tray->setVisible(true);
        qInfo() << "[Vaani] System tray icon shown";
    } else {
        qWarning() << "[Vaani] Tray icon pixmap is empty — check Qt resources";
    }
    refreshMenu();
    refreshListeningFeedback();
}

QIcon TrayController::trayIconForState(VaaniCoreController::TrayStatus status) const
{
    Q_UNUSED(status);
    QIcon icon = Vaani::brandIcon();
    icon.addPixmap(Vaani::brandPixmap(16), QIcon::Normal, QIcon::Off);
    icon.addPixmap(Vaani::brandPixmap(22), QIcon::Normal, QIcon::On);
    icon.addPixmap(Vaani::brandPixmap(32), QIcon::Active);
    icon.addPixmap(Vaani::brandPixmap(32), QIcon::Selected);
    return icon;
}

void TrayController::refreshListeningFeedback()
{
    const auto mode = m_core->settings()->listeningIndicator();
    const bool listening = m_core->trayStatus() == VaaniCoreController::TrayStatus::Listening;

    VaaniCoreController::TrayStatus iconStatus = m_core->trayStatus();
    if (mode != VaaniSettingsStore::ListeningIndicator::TrayIcon
        && iconStatus == VaaniCoreController::TrayStatus::Listening) {
        iconStatus = VaaniCoreController::TrayStatus::Idle;
    }
    m_tray->setIcon(trayIconForState(iconStatus));

    refreshDictationHud();
}

void TrayController::refreshDictationHud()
{
    const auto mode = m_core->settings()->listeningIndicator();
    if (mode == VaaniSettingsStore::ListeningIndicator::Off) {
        m_listeningPill->setPhase(VaaniDictationHudPhase::Hidden);
        return;
    }
    m_listeningPill->setHandsFree(m_core->handsFreeDictationActive());
    m_listeningPill->setPhase(m_core->dictationHudPhase());
}

void TrayController::setInstanceGuard(SingleInstanceGuard* guard)
{
    m_instanceGuard = guard;
}

VaaniSettingsDialog* TrayController::ensureSettingsDialog()
{
    if (!m_settings) {
        m_settings = new VaaniSettingsDialog(m_core);
        if (m_instanceGuard)
            m_instanceGuard->setMainWindow(m_settings);
    }
    return m_settings;
}

void TrayController::openSettings()
{
    VaaniSettingsDialog* dialog = ensureSettingsDialog();
    dialog->reloadFromStore();
    dialog->applyThemeFromStore();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void TrayController::onSecondInstanceActivate()
{
    if (m_settings && m_settings->isVisible()) {
        m_tray->showMessage(QStringLiteral("Vaani"),
                            QStringLiteral("Vaani is already running."),
                            QSystemTrayIcon::Information, 4000);
        openSettings();
        return;
    }
    openSettings();
}

void TrayController::refreshMenu()
{
    if (m_statusAction)
        m_statusAction->setText(m_core->trayStatusText());

    const bool ready = m_core->engineState() == VaaniCoreController::EngineState::Ready;
    if (m_dictationAction) {
        m_dictationAction->setEnabled(ready);
        m_dictationAction->setText(m_core->isTrayDictationActive()
            ? QStringLiteral("Stop dictation")
            : QStringLiteral("Start dictation"));
    }
}

void TrayController::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick)
        openSettings();
}

void TrayController::toggleDictationFromTray()
{
    m_core->toggleTrayDictation();
    refreshMenu();
    refreshListeningFeedback();
}

void TrayController::exitApplication()
{
    if (m_listeningPill)
        m_listeningPill->setPhase(VaaniDictationHudPhase::Hidden);
    if (m_capturePanel)
        m_capturePanel->hide();
    m_tray->hide();
    m_core->prepareForQuit();
    QApplication::exit(0);
}

void TrayController::onEngineReady()
{
    if (m_readyBalloonShown)
        return;
    m_readyBalloonShown = true;
    m_tray->showMessage(QStringLiteral("Vaani"),
                        QStringLiteral("Vaani is ready. Hold your hotkey or use Start dictation."),
                        QSystemTrayIcon::Information, 5000);
}

void TrayController::onEngineFailed(const QString& detail)
{
    QString body = detail.trimmed();
    if (body.isEmpty())
        body = QStringLiteral("Speech engine failed to start.");
    if (!body.contains(QStringLiteral("download"), Qt::CaseInsensitive))
        body += QStringLiteral("\n\nOpen Settings or complete onboarding to download speech models.");
    m_tray->showMessage(QStringLiteral("Vaani — speech unavailable"), body,
                        QSystemTrayIcon::Critical, 20000);
}

void TrayController::onRecognitionResult(const QString& text, bool isFinal)
{
    if (text.isEmpty() || !isFinal)
        return;
}

void TrayController::showUserError(const QString& message, int timeoutMs)
{
    m_tray->showMessage(QStringLiteral("Vaani"), message, QSystemTrayIcon::Warning, timeoutMs);
}
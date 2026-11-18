#pragma once

#include "Core/VaaniCoreController.h"

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;
class VaaniSettingsDialog;
class VaaniListeningPill;
class VaaniDictationCapturePanel;
class SingleInstanceGuard;

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(VaaniCoreController* core, QObject* parent = nullptr);

    void setInstanceGuard(SingleInstanceGuard* guard);
    ~TrayController() override;

    void show();
    void openSettings();
    void onSecondInstanceActivate();
    void showUserError(const QString& message, int timeoutMs = 8000);

private slots:
    void refreshMenu();
    void refreshListeningFeedback();
    void refreshDictationHud();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleDictationFromTray();
    void exitApplication();
    void onEngineReady();
    void onEngineFailed(const QString& detail);
    void onRecognitionResult(const QString& text, bool isFinal);

private:
    VaaniSettingsDialog* ensureSettingsDialog();
    QIcon trayIconForState(VaaniCoreController::TrayStatus status) const;

    SingleInstanceGuard* m_instanceGuard = nullptr;

    VaaniCoreController* m_core = nullptr;
    VaaniSettingsDialog* m_settings = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_statusAction = nullptr;
    QAction* m_dictationAction = nullptr;
    VaaniListeningPill* m_listeningPill = nullptr;
    VaaniDictationCapturePanel* m_capturePanel = nullptr;
    bool m_readyBalloonShown = false;
};
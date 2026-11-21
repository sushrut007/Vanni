#pragma once

#include "Input/VaaniHotkey.h"

#include <QWidget>

class QFrame;
class QVBoxLayout;

class VaaniCoreController;
class VaaniHotkeyCaptureField;
class VaaniToggleSwitch;
class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
namespace Vaani {
class VaaniModelDownloader;
}

class VaaniSettingsDialog : public QWidget
{
    Q_OBJECT

public:
    explicit VaaniSettingsDialog(VaaniCoreController* core, QWidget* parent = nullptr);

    void reloadFromStore();
    void applyThemeFromStore();

signals:
    void backRequested();
    void settingsSaved();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onSave();
    void onHotkeyRefused(const QString& reason);
    void onHotkeyAccepted(const VaaniHotkey& hotkey);
    void onThemeComboChanged(int index);
    void onModelSelectionChanged(int index);
    void startModelDownload();
    void onModelDownloadFailed(const QString& message);
    void onModelDownloadFinished(const QString& directory);

private:
    void buildUi();
    QFrame* addSection(const QString& title, QVBoxLayout* parentLayout);
    void refreshMicrophoneList();
    void refreshModelSection();

    VaaniCoreController* m_core = nullptr;
    QComboBox* m_languageCombo = nullptr;
    QListWidget* m_primaryLangList = nullptr;
    QComboBox* m_addLangCombo = nullptr;
    QPushButton* m_addPrimaryBtn = nullptr;
    QPushButton* m_removePrimaryBtn = nullptr;
    QComboBox* m_micCombo = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QLabel* m_modelStatus = nullptr;
    QProgressBar* m_modelProgress = nullptr;
    QPushButton* m_modelDownloadBtn = nullptr;
    Vaani::VaaniModelDownloader* m_modelDownloader = nullptr;
    VaaniHotkeyCaptureField* m_pttCapture = nullptr;
    QLabel* m_pttRefusal = nullptr;
    VaaniToggleSwitch* m_doubleTapCheck = nullptr;
    VaaniToggleSwitch* m_keystrokeFallbackCheck = nullptr;
    VaaniToggleSwitch* m_injectPartialsCheck = nullptr;
    VaaniToggleSwitch* m_autoStartCheck = nullptr;
    QComboBox* m_listeningCombo = nullptr;
    QComboBox* m_themeCombo = nullptr;
};
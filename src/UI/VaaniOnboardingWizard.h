#pragma once

#include <QDialog>

class VaaniCoreController;
class VaaniSettingsStore;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class VaaniHotkeyCaptureField;
namespace Vaani {
class VaaniModelDownloader;
}

class VaaniOnboardingWizard : public QDialog
{
    Q_OBJECT

public:
    explicit VaaniOnboardingWizard(VaaniCoreController* core, QWidget* parent = nullptr);

    bool modelReady() const noexcept { return m_modelReady; }

private slots:
    void goBack();
    void goNext();
    void startDownload();
    void onDownloadFailed(const QString& message);
    void onDownloadFinished(const QString& directory);

private:
    void buildPages();
    void refreshMicrophones();
    void refreshModelPage();
    void updateDefaultLangCombo();
    void saveLanguagesFromOnboarding();
    void updateNav();

    VaaniCoreController* m_core = nullptr;
    VaaniSettingsStore* m_store = nullptr;
    QStackedWidget* m_stack = nullptr;
    QPushButton* m_back = nullptr;
    QPushButton* m_next = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QLineEdit* m_langSearch = nullptr;
    QListWidget* m_langList = nullptr;
    QComboBox* m_defaultLangCombo = nullptr;
    QComboBox* m_micCombo = nullptr;
    VaaniHotkeyCaptureField* m_hotkeyField = nullptr;
    QLabel* m_stepIndicator = nullptr;
    QLabel* m_downloadStatus = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_downloadBtn = nullptr;
    QLabel* m_hotkeyError = nullptr;
    QCheckBox* m_skipCheck = nullptr;
    Vaani::VaaniModelDownloader* m_downloader = nullptr;
    bool m_modelReady = false;
    int m_page = 0;
};
#pragma once

#include "Core/VaaniCoreController.h"
#include "Storage/VaaniHistoryStore.h"

#include <QWidget>
#include <QDateTime>

class QLabel;
class QPushButton;
class QLineEdit;
class QComboBox;
class QProgressBar;
class QScrollArea;
class QVBoxLayout;
class QStackedWidget;
class VaaniSettingsDialog;

class VaaniHomeWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VaaniHomeWindow(VaaniCoreController* core, QWidget* parent = nullptr);
    ~VaaniHomeWindow() override = default;

    void showHomePage();
    void showSettingsPage();

    void refreshState();
    void refreshHistory();
    void applyThemeFromStore();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onDictationsTabClicked();
    void onSettingsTabClicked();
    void onMicLevelChanged(float level);
    void onHistoryChanged();
    void onSearchTextChanged(const QString& query);
    void onLanguageChanged(int index);
    void onExportClicked();
    void onClearHistoryClicked();

private:
    void setupUi();
    QWidget* createHeaderWidget();
    QWidget* createDictationsPage();
    void rebuildHistoryList(const QString& filter = QString());
    QString formatTimestamp(const QDateTime& dt) const;
    void updateTabStyles();

    VaaniCoreController* m_core = nullptr;

    // Navigation & Stack
    QPushButton* m_dictationsTabBtn = nullptr;
    QPushButton* m_settingsTabBtn = nullptr;
    QStackedWidget* m_stack = nullptr;
    VaaniSettingsDialog* m_settingsWidget = nullptr;

    // Header status
    QLabel* m_statusDot = nullptr;
    QLabel* m_statusLabel = nullptr;

    // Hero guidance
    QLabel* m_hotkeyBadge = nullptr;
    QProgressBar* m_micLevelBar = nullptr;
    QLabel* m_modelBadge = nullptr;
    QComboBox* m_langCombo = nullptr;
    QLabel* m_micBadge = nullptr;

    // History controls
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_historyCountLabel = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QScrollArea* m_historyScroll = nullptr;
    QWidget* m_historyContainer = nullptr;
    QVBoxLayout* m_historyLayout = nullptr;
    QWidget* m_emptyStateWidget = nullptr;
};

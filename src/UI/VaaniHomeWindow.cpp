#include "UI/VaaniHomeWindow.h"

#include "Core/VaaniCoreController.h"
#include "Settings/VaaniSettingsDialog.h"
#include "Settings/VaaniSettingsStore.h"
#include "Settings/VaaniSettingsStyle.h"
#include "SpeechRecognition/speechrecognitionmanager.h"
#include "Utils/LanguageUtils.h"
#include "Utils/VaaniBrandIcon.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

VaaniHomeWindow::VaaniHomeWindow(VaaniCoreController* core, QWidget* parent)
    : QWidget(parent)
    , m_core(core)
{
    setObjectName(QStringLiteral("VaaniHomeRoot"));
    setWindowTitle(QStringLiteral("Vaani"));
    setWindowIcon(Vaani::brandIcon());
    resize(660, 680);
    setMinimumSize(540, 520);

    setupUi();
    applyThemeFromStore();

    // Wire Core signals
    connect(m_core, &VaaniCoreController::trayStatusChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core, &VaaniCoreController::engineStateChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core, &VaaniCoreController::dictationHudPhaseChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core->speechManager(), &SpeechRecognitionManager::microphoneLevelChanged,
            this, &VaaniHomeWindow::onMicLevelChanged);
    connect(m_core->historyStore(), &VaaniHistoryStore::historyChanged,
            this, &VaaniHomeWindow::onHistoryChanged);

    connect(m_core->settings(), &VaaniSettingsStore::dictationLanguageChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core->settings(), &VaaniSettingsStore::primaryLanguagesChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core->settings(), &VaaniSettingsStore::pushToTalkHotkeyChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core->settings(), &VaaniSettingsStore::selectedModelIdChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core->settings(), &VaaniSettingsStore::microphoneEndpointChanged,
            this, &VaaniHomeWindow::refreshState);
    connect(m_core->settings(), &VaaniSettingsStore::uiThemeChanged,
            this, &VaaniHomeWindow::applyThemeFromStore);

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &VaaniHomeWindow::applyThemeFromStore);

    refreshState();
    refreshHistory();
}

void VaaniHomeWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    hide();
}

void VaaniHomeWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange) {
        applyThemeFromStore();
    }
}

void VaaniHomeWindow::applyThemeFromStore()
{
    Vaani::applySettingsStyle(this, m_core->settings()->uiTheme());
    if (m_settingsWidget)
        m_settingsWidget->applyThemeFromStore();
}

void VaaniHomeWindow::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(14);

    mainLayout->addWidget(createHeaderWidget());

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(createDictationsPage());

    m_settingsWidget = new VaaniSettingsDialog(m_core, this);
    connect(m_settingsWidget, &VaaniSettingsDialog::backRequested,
            this, &VaaniHomeWindow::showHomePage);
    m_stack->addWidget(m_settingsWidget);

    mainLayout->addWidget(m_stack, 1);

    updateTabStyles();
}

QWidget* VaaniHomeWindow::createHeaderWidget()
{
    auto* header = new QWidget(this);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* iconLabel = new QLabel(header);
    iconLabel->setPixmap(Vaani::brandPixmap(30));
    iconLabel->setFixedSize(30, 30);
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel(QStringLiteral("Vaani"), header);
    titleLabel->setObjectName(QStringLiteral("vaaniSettingsTitle"));
    layout->addWidget(titleLabel);

    layout->addStretch();

    // Segmented Navigation Tabs
    auto* navFrame = new QFrame(header);
    navFrame->setObjectName(QStringLiteral("vaaniNavTabs"));
    auto* navLayout = new QHBoxLayout(navFrame);
    navLayout->setContentsMargins(2, 2, 2, 2);
    navLayout->setSpacing(4);

    m_dictationsTabBtn = new QPushButton(tr("Dictations"), navFrame);
    m_dictationsTabBtn->setProperty("class", QStringLiteral("vaaniNavTab"));
    m_dictationsTabBtn->setCursor(Qt::PointingHandCursor);
    connect(m_dictationsTabBtn, &QPushButton::clicked, this, &VaaniHomeWindow::onDictationsTabClicked);
    navLayout->addWidget(m_dictationsTabBtn);

    m_settingsTabBtn = new QPushButton(tr("Settings"), navFrame);
    m_settingsTabBtn->setProperty("class", QStringLiteral("vaaniNavTab"));
    m_settingsTabBtn->setCursor(Qt::PointingHandCursor);
    connect(m_settingsTabBtn, &QPushButton::clicked, this, &VaaniHomeWindow::onSettingsTabClicked);
    navLayout->addWidget(m_settingsTabBtn);

    layout->addWidget(navFrame);
    layout->addStretch();

    // Status pill
    auto* statusPill = new QFrame(header);
    statusPill->setObjectName(QStringLiteral("vaaniStatusPill"));
    auto* pillLayout = new QHBoxLayout(statusPill);
    pillLayout->setContentsMargins(10, 4, 10, 4);
    pillLayout->setSpacing(6);

    m_statusDot = new QLabel(statusPill);
    m_statusDot->setFixedSize(8, 8);
    pillLayout->addWidget(m_statusDot);

    m_statusLabel = new QLabel(QStringLiteral("Ready"), statusPill);
    m_statusLabel->setObjectName(QStringLiteral("vaaniStatusText"));
    pillLayout->addWidget(m_statusLabel);

    layout->addWidget(statusPill);

    return header;
}

QWidget* VaaniHomeWindow::createDictationsPage()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(14);

    // Hero Guidance Card
    auto* heroCard = new QFrame(page);
    heroCard->setProperty("class", QStringLiteral("vaaniSettingsSection"));
    auto* heroLayout = new QVBoxLayout(heroCard);
    heroLayout->setContentsMargins(16, 14, 16, 14);
    heroLayout->setSpacing(10);

    auto* heroTopRow = new QHBoxLayout();
    heroTopRow->setSpacing(10);

    auto* heroTextCol = new QVBoxLayout();
    heroTextCol->setSpacing(2);

    auto* heroHeading = new QLabel(tr("External Dictation"), heroCard);
    heroHeading->setProperty("class", QStringLiteral("vaaniSectionTitle"));
    heroTextCol->addWidget(heroHeading);

    auto* heroSub = new QLabel(
        tr("Hold your hotkey anywhere in Windows to speak. Speech is typed directly into your focused app upon release."),
        heroCard);
    heroSub->setProperty("class", QStringLiteral("vaaniSettingsHint"));
    heroSub->setWordWrap(true);
    heroTextCol->addWidget(heroSub);

    heroTopRow->addLayout(heroTextCol, 1);

    // Modern Hotkey Callout Badge with icon
    auto* hotkeyBadgeFrame = new QFrame(heroCard);
    hotkeyBadgeFrame->setObjectName(QStringLiteral("heroHotkeyDisplay"));
    auto* hotkeyLayout = new QHBoxLayout(hotkeyBadgeFrame);
    hotkeyLayout->setContentsMargins(12, 6, 14, 6);
    hotkeyLayout->setSpacing(8);

    auto* kbIcon = new QLabel(hotkeyBadgeFrame);
    kbIcon->setPixmap(QPixmap(QStringLiteral(":/icons/keyboard.png")).scaled(22, 22, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    kbIcon->setFixedSize(22, 22);
    hotkeyLayout->addWidget(kbIcon);

    m_hotkeyBadge = new QLabel(hotkeyBadgeFrame);
    m_hotkeyBadge->setObjectName(QStringLiteral("hotkeyText"));
    hotkeyLayout->addWidget(m_hotkeyBadge);

    heroTopRow->addWidget(hotkeyBadgeFrame);
    heroLayout->addLayout(heroTopRow);

    // Live audio level bar
    m_micLevelBar = new QProgressBar(heroCard);
    m_micLevelBar->setObjectName(QStringLiteral("micLevelBar"));
    m_micLevelBar->setRange(0, 100);
    m_micLevelBar->setValue(0);
    m_micLevelBar->setTextVisible(false);
    m_micLevelBar->setFixedHeight(4);
    heroLayout->addWidget(m_micLevelBar);

    // Badges Row
    auto* badgesLayout = new QHBoxLayout();
    badgesLayout->setSpacing(10);

    // Model Badge with CPU icon
    auto* modelFrame = new QFrame(heroCard);
    modelFrame->setProperty("class", QStringLiteral("vaaniInfoBadge"));
    auto* modelLayout = new QHBoxLayout(modelFrame);
    modelLayout->setContentsMargins(10, 6, 10, 6);
    modelLayout->setSpacing(8);
    auto* cpuIcon = new QLabel(modelFrame);
    cpuIcon->setPixmap(QPixmap(QStringLiteral(":/icons/cpu.png")).scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    cpuIcon->setFixedSize(20, 20);
    modelLayout->addWidget(cpuIcon);
    m_modelBadge = new QLabel(modelFrame);
    modelLayout->addWidget(m_modelBadge);
    badgesLayout->addWidget(modelFrame);

    // Language Badge with Globe icon
    auto* langFrame = new QFrame(heroCard);
    langFrame->setObjectName(QStringLiteral("langBadgeFrame"));
    auto* langLayout = new QHBoxLayout(langFrame);
    langLayout->setContentsMargins(10, 4, 8, 4);
    langLayout->setSpacing(8);
    auto* langIcon = new QLabel(langFrame);
    langIcon->setPixmap(QPixmap(QStringLiteral(":/icons/globe.png")).scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    langIcon->setFixedSize(20, 20);
    langLayout->addWidget(langIcon);

    m_langCombo = new QComboBox(langFrame);
    m_langCombo->setObjectName(QStringLiteral("heroLangCombo"));
    connect(m_langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VaaniHomeWindow::onLanguageChanged);
    langLayout->addWidget(m_langCombo);
    badgesLayout->addWidget(langFrame);

    // Mic Badge with Mic icon
    auto* micFrame = new QFrame(heroCard);
    micFrame->setProperty("class", QStringLiteral("vaaniInfoBadge"));
    auto* micLayout = new QHBoxLayout(micFrame);
    micLayout->setContentsMargins(10, 6, 10, 6);
    micLayout->setSpacing(8);
    auto* micIcon = new QLabel(micFrame);
    micIcon->setPixmap(QPixmap(QStringLiteral(":/icons/mic.png")).scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    micIcon->setFixedSize(20, 20);
    micLayout->addWidget(micIcon);
    m_micBadge = new QLabel(micFrame);
    micLayout->addWidget(m_micBadge);
    badgesLayout->addWidget(micFrame);

    heroLayout->addLayout(badgesLayout);
    pageLayout->addWidget(heroCard);

    // Transcriptions History Section
    auto* historySection = new QWidget(page);
    auto* historyLayout = new QVBoxLayout(historySection);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(10);

    auto* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(10);

    auto* title = new QLabel(tr("Recent Dictations"), historySection);
    title->setProperty("class", QStringLiteral("vaaniSectionTitle"));
    controlsRow->addWidget(title);

    m_historyCountLabel = new QLabel(QStringLiteral("(0)"), historySection);
    m_historyCountLabel->setProperty("class", QStringLiteral("vaaniSettingsHint"));
    controlsRow->addWidget(m_historyCountLabel);

    controlsRow->addSpacing(8);

    m_searchEdit = new QLineEdit(historySection);
    m_searchEdit->setObjectName(QStringLiteral("historySearch"));
    m_searchEdit->setPlaceholderText(tr("Search transcriptions…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->addAction(QIcon(QStringLiteral(":/icons/search.png")), QLineEdit::LeadingPosition);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &VaaniHomeWindow::onSearchTextChanged);
    controlsRow->addWidget(m_searchEdit, 1);

    m_exportBtn = new QPushButton(tr("Export"), historySection);
    m_exportBtn->setObjectName(QStringLiteral("vaaniSecondaryButton"));
    m_exportBtn->setIcon(QIcon(QStringLiteral(":/icons/export.png")));
    m_exportBtn->setIconSize(QSize(16, 16));
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    m_exportBtn->setToolTip(tr("Export dictations to a file"));
    connect(m_exportBtn, &QPushButton::clicked, this, &VaaniHomeWindow::onExportClicked);
    controlsRow->addWidget(m_exportBtn);

    m_clearBtn = new QPushButton(tr("Clear"), historySection);
    m_clearBtn->setObjectName(QStringLiteral("vaaniSecondaryButton"));
    m_clearBtn->setIcon(QIcon(QStringLiteral(":/icons/trash.png")));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    connect(m_clearBtn, &QPushButton::clicked, this, &VaaniHomeWindow::onClearHistoryClicked);
    controlsRow->addWidget(m_clearBtn);

    historyLayout->addLayout(controlsRow);

    // Scroll Area
    m_historyScroll = new QScrollArea(historySection);
    m_historyScroll->setObjectName(QStringLiteral("VaaniSettingsScroll"));
    m_historyScroll->setWidgetResizable(true);
    m_historyScroll->setFrameShape(QFrame::NoFrame);

    m_historyContainer = new QWidget();
    m_historyContainer->setObjectName(QStringLiteral("VaaniSettingsContent"));
    m_historyLayout = new QVBoxLayout(m_historyContainer);
    m_historyLayout->setContentsMargins(0, 0, 4, 0);
    m_historyLayout->setSpacing(8);
    m_historyLayout->addStretch();

    m_historyScroll->setWidget(m_historyContainer);
    historyLayout->addWidget(m_historyScroll, 1);

    // Modern Illustrated Empty state widget
    m_emptyStateWidget = new QWidget(m_historyScroll);
    m_emptyStateWidget->setObjectName(QStringLiteral("emptyStateWidget"));
    auto* emptyLayout = new QVBoxLayout(m_emptyStateWidget);
    emptyLayout->setContentsMargins(20, 36, 20, 36);
    emptyLayout->setSpacing(10);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto* emptyIcon = new QLabel(m_emptyStateWidget);
    emptyIcon->setPixmap(QPixmap(QStringLiteral(":/icons/empty_history.png")).scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    emptyIcon->setFixedSize(80, 80);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);

    auto* emptyTitle = new QLabel(tr("No transcriptions yet"), m_emptyStateWidget);
    emptyTitle->setObjectName(QStringLiteral("emptyStateTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);

    auto* emptySub = new QLabel(
        tr("Hold your hotkey in any text field to speak. Text will appear here and in your app."),
        m_emptyStateWidget);
    emptySub->setObjectName(QStringLiteral("emptyStateSubtitle"));
    emptySub->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptySub);

    pageLayout->addWidget(historySection, 1);

    return page;
}

void VaaniHomeWindow::showHomePage()
{
    m_stack->setCurrentIndex(0);
    updateTabStyles();
}

void VaaniHomeWindow::showSettingsPage()
{
    if (m_settingsWidget)
        m_settingsWidget->reloadFromStore();
    m_stack->setCurrentIndex(1);
    updateTabStyles();
}

void VaaniHomeWindow::onDictationsTabClicked()
{
    showHomePage();
}

void VaaniHomeWindow::onSettingsTabClicked()
{
    showSettingsPage();
}

void VaaniHomeWindow::updateTabStyles()
{
    const int idx = m_stack ? m_stack->currentIndex() : 0;
    m_dictationsTabBtn->setProperty("active", idx == 0);
    m_settingsTabBtn->setProperty("active", idx == 1);

    m_dictationsTabBtn->style()->unpolish(m_dictationsTabBtn);
    m_dictationsTabBtn->style()->polish(m_dictationsTabBtn);
    m_settingsTabBtn->style()->unpolish(m_settingsTabBtn);
    m_settingsTabBtn->style()->polish(m_settingsTabBtn);
}

void VaaniHomeWindow::refreshState()
{
    const auto engineState = m_core->engineState();
    const bool isListening = m_core->trayStatus() == VaaniCoreController::TrayStatus::Listening;
    const bool isTranscribing = m_core->dictationHudPhase() == VaaniDictationHudPhase::Transcribing;

    // Status pill
    if (engineState == VaaniCoreController::EngineState::Loading) {
        m_statusDot->setStyleSheet(QStringLiteral("background-color: #f59e0b; border-radius: 4px;"));
        m_statusLabel->setText(tr("Loading Model…"));
    } else if (engineState == VaaniCoreController::EngineState::Failed) {
        m_statusDot->setStyleSheet(QStringLiteral("background-color: #ef4444; border-radius: 4px;"));
        m_statusLabel->setText(tr("Engine Error"));
    } else if (isListening) {
        m_statusDot->setStyleSheet(QStringLiteral("background-color: #ef4444; border-radius: 4px;"));
        m_statusLabel->setText(tr("Listening…"));
    } else if (isTranscribing) {
        m_statusDot->setStyleSheet(QStringLiteral("background-color: #3b82f6; border-radius: 4px;"));
        m_statusLabel->setText(tr("Transcribing…"));
    } else {
        m_statusDot->setStyleSheet(QStringLiteral("background-color: #10b981; border-radius: 4px;"));
        m_statusLabel->setText(tr("Ready"));
    }

    // Hotkey badge
    const QString hotkeyText = m_core->settings()->pushToTalkHotkey().displayName();
    m_hotkeyBadge->setText(hotkeyText.isEmpty() ? QStringLiteral("None") : hotkeyText);

    // Model badge
    const QString modelId = m_core->settings()->selectedModelId();
    m_modelBadge->setText(modelId.isEmpty() ? QStringLiteral("Default") : modelId);

    // Languages in combo box
    const QString curLang = m_core->settings()->dictationLanguageCode();
    m_langCombo->blockSignals(true);
    m_langCombo->clear();

    const QStringList primary = m_core->settings()->primaryLanguages();
    for (const QString& code : primary) {
        m_langCombo->addItem(LanguageUtils::displayFromLanguageCode(code), code);
    }

    if (m_langCombo->findData(curLang) == -1) {
        m_langCombo->addItem(LanguageUtils::displayFromLanguageCode(curLang), curLang);
    }

    int idx = m_langCombo->findData(curLang);
    if (idx >= 0)
        m_langCombo->setCurrentIndex(idx);
    m_langCombo->blockSignals(false);

    // Mic badge
    const QString micId = m_core->settings()->microphoneEndpointId();
    m_micBadge->setText(micId.isEmpty() ? tr("Default Mic") : tr("Selected Mic"));
}

void VaaniHomeWindow::onLanguageChanged(int index)
{
    if (index < 0)
        return;
    const QString code = m_langCombo->itemData(index).toString();
    if (!code.isEmpty() && code != m_core->settings()->dictationLanguageCode()) {
        m_core->settings()->setDictationLanguageCode(code);
        m_core->settings()->save();
    }
}

void VaaniHomeWindow::onMicLevelChanged(float level)
{
    if (!isVisible() || m_stack->currentIndex() != 0)
        return;
    const int val = qBound(0, static_cast<int>(level * 100.0f), 100);
    m_micLevelBar->setValue(val);
}

void VaaniHomeWindow::onHistoryChanged()
{
    refreshHistory();
}

void VaaniHomeWindow::onSearchTextChanged(const QString& query)
{
    rebuildHistoryList(query.trimmed());
}

void VaaniHomeWindow::refreshHistory()
{
    rebuildHistoryList(m_searchEdit ? m_searchEdit->text().trimmed() : QString());
}

QString VaaniHomeWindow::formatTimestamp(const QDateTime& dt) const
{
    if (!dt.isValid())
        return QString();

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = dt.secsTo(now);
    if (secs < 60)
        return tr("Just now");
    if (secs < 3600)
        return tr("%1m ago").arg(secs / 60);
    if (dt.date() == now.date())
        return dt.toString(QStringLiteral("h:mm AP"));
    return dt.toString(QStringLiteral("MMM d, h:mm AP"));
}

void VaaniHomeWindow::rebuildHistoryList(const QString& filter)
{
    // Clear existing item cards in layout (except trailing stretch)
    QLayoutItem* item = nullptr;
    while (m_historyLayout->count() > 1) {
        item = m_historyLayout->takeAt(0);
        if (item->widget())
            delete item->widget();
        delete item;
    }

    const auto allItems = m_core->historyStore()->items();
    int visibleCount = 0;

    for (const auto& hItem : allItems) {
        if (!filter.isEmpty() && !hItem.text.contains(filter, Qt::CaseInsensitive))
            continue;

        visibleCount++;

        auto* card = new QFrame(m_historyContainer);
        card->setProperty("class", QStringLiteral("vaaniHistoryCard"));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(8);

        // Header row
        auto* topRow = new QHBoxLayout();
        topRow->setSpacing(8);

        auto* timeLabel = new QLabel(formatTimestamp(hItem.timestamp), card);
        timeLabel->setObjectName(QStringLiteral("cardTime"));
        topRow->addWidget(timeLabel);

        if (!hItem.languageCode.isEmpty()) {
            auto* langBadge = new QLabel(hItem.languageCode.toUpper(), card);
            langBadge->setObjectName(QStringLiteral("cardLangBadge"));
            topRow->addWidget(langBadge);
        }

        const int words = hItem.text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
        auto* wordLabel = new QLabel(tr("%1 words").arg(words), card);
        wordLabel->setObjectName(QStringLiteral("cardWords"));
        topRow->addWidget(wordLabel);

        topRow->addStretch();

        // Copy button with icon
        auto* copyBtn = new QPushButton(tr("Copy"), card);
        copyBtn->setObjectName(QStringLiteral("cardCopyBtn"));
        copyBtn->setIcon(QIcon(QStringLiteral(":/icons/copy.png")));
        copyBtn->setCursor(Qt::PointingHandCursor);
        const QString textToCopy = hItem.text;
        connect(copyBtn, &QPushButton::clicked, copyBtn, [copyBtn, textToCopy]() {
            QClipboard* clipboard = QApplication::clipboard();
            if (clipboard)
                clipboard->setText(textToCopy);
            copyBtn->setText(tr("✓ Copied!"));
            copyBtn->setStyleSheet(QStringLiteral("background-color: #065f46; color: #34d399;"));
            QTimer::singleShot(1500, copyBtn, [copyBtn]() {
                copyBtn->setText(tr("Copy"));
                copyBtn->setStyleSheet(QString());
            });
        });
        topRow->addWidget(copyBtn);

        // Delete button with trash icon
        auto* delBtn = new QPushButton(card);
        delBtn->setObjectName(QStringLiteral("cardDeleteBtn"));
        delBtn->setIcon(QIcon(QStringLiteral(":/icons/trash.png")));
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setToolTip(tr("Delete"));
        const QString itemId = hItem.id;
        connect(delBtn, &QPushButton::clicked, this, [this, itemId]() {
            m_core->historyStore()->removeItem(itemId);
        });
        topRow->addWidget(delBtn);

        cardLayout->addLayout(topRow);

        // Text body
        auto* textLabel = new QLabel(hItem.text, card);
        textLabel->setObjectName(QStringLiteral("cardText"));
        textLabel->setWordWrap(true);
        textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        cardLayout->addWidget(textLabel);

        m_historyLayout->insertWidget(m_historyLayout->count() - 1, card);
    }

    m_historyCountLabel->setText(QStringLiteral("(%1)").arg(visibleCount));
    m_exportBtn->setEnabled(visibleCount > 0);
    m_clearBtn->setEnabled(!allItems.isEmpty());

    if (visibleCount == 0) {
        if (!filter.isEmpty()) {
            m_emptyStateWidget->findChild<QLabel*>(QStringLiteral("emptyStateTitle"))->setText(
                tr("No matching transcriptions"));
            m_emptyStateWidget->findChild<QLabel*>(QStringLiteral("emptyStateSubtitle"))->setText(
                tr("Try another search term."));
        } else {
            m_emptyStateWidget->findChild<QLabel*>(QStringLiteral("emptyStateTitle"))->setText(
                tr("No transcriptions yet"));
            m_emptyStateWidget->findChild<QLabel*>(QStringLiteral("emptyStateSubtitle"))->setText(
                tr("Hold your hotkey in any text field to speak. Text will appear here and in your app."));
        }
        m_emptyStateWidget->show();
        m_historyContainer->hide();
    } else {
        m_emptyStateWidget->hide();
        m_historyContainer->show();
    }
}

void VaaniHomeWindow::onExportClicked()
{
    const auto allItems = m_core->historyStore()->items();
    const QString filter = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    QList<VaaniHistoryItem> exportItems;
    for (const auto& item : allItems) {
        if (!filter.isEmpty() && !item.text.contains(filter, Qt::CaseInsensitive))
            continue;
        exportItems.append(item);
    }

    if (exportItems.isEmpty())
        return;

    const QString defaultName = QStringLiteral("Vaani_Dictations_%1.txt")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmm")));
    const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QLatin1Char('/') + defaultName;

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Export Dictations"),
        defaultPath,
        tr("Text File (*.txt);;Markdown File (*.md);;All Files (*.*)"));

    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write to the selected file."));
        return;
    }

    QTextStream out(&file);
    const bool isMd = filePath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive);

    if (isMd) {
        out << QStringLiteral("# Vaani Dictations Export\n");
        out << QStringLiteral("Exported on: %1\n\n").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss AP")));
        for (const auto& it : exportItems) {
            out << QStringLiteral("### %1 [%2]\n\n").arg(it.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm AP")), it.languageCode.toUpper());
            out << it.text << QStringLiteral("\n\n---\n\n");
        }
    } else {
        out << QStringLiteral("=== Vaani Dictations Export ===\n");
        out << QStringLiteral("Exported on: %1\n\n").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss AP")));
        for (const auto& it : exportItems) {
            out << QStringLiteral("[%1 | %2]\n").arg(it.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm AP")), it.languageCode.toUpper());
            out << it.text << QStringLiteral("\n\n----------------------------------------\n\n");
        }
    }

    file.close();

    m_exportBtn->setText(tr("✓ Exported!"));
    QTimer::singleShot(1500, m_exportBtn, [this]() {
        m_exportBtn->setText(tr("Export"));
    });
}

void VaaniHomeWindow::onClearHistoryClicked()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Clear History — Vaani"));
    msgBox.setText(tr("Are you sure you want to clear all transcription history?"));
    msgBox.setInformativeText(tr("This will permanently remove all saved dictations from your history."));
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    Vaani::applySettingsStyle(&msgBox, m_core->settings()->uiTheme());

    if (msgBox.exec() == QMessageBox::Yes) {
        m_core->historyStore()->clear();
    }
}

#include "Settings/VaaniSettingsDialog.h"

#include "Core/VaaniCoreController.h"
#include "Input/VaaniHotkey.h"
#include "Models/VaaniModelCatalog.h"
#include "Models/VaaniModelDownloader.h"
#include "Settings/CaptureEndpointList.h"
#include "Settings/VaaniHotkeyCaptureField.h"
#include "Settings/VaaniSettingsStore.h"
#include "Settings/VaaniSettingsStyle.h"
#include "Utils/LanguageUtils.h"
#include "Utils/VaaniAutoStartManager.h"
#include "Utils/VaaniBrandIcon.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QVBoxLayout>

#include <climits>

namespace {

QVBoxLayout* sectionBody(QFrame* section)
{
    return qobject_cast<QVBoxLayout*>(section->layout());
}

} // namespace

VaaniSettingsDialog::VaaniSettingsDialog(VaaniCoreController* core, QWidget* parent)
    : QDialog(parent, Qt::Window)
    , m_core(core)
{
    setObjectName(QStringLiteral("VaaniSettingsRoot"));
    setWindowTitle(tr("Vaani Settings"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(500, 540);
    resize(520, 600);

    m_modelDownloader = new Vaani::VaaniModelDownloader(this);
    connect(m_modelDownloader, &Vaani::VaaniModelDownloader::statusChanged, this,
            [this](const QString& text) {
                if (m_modelStatus)
                    m_modelStatus->setText(text);
            });
    connect(m_modelDownloader, &Vaani::VaaniModelDownloader::progressChanged, this,
            [this](qint64 received, qint64 total) {
                if (!m_modelProgress || total <= 0)
                    return;
                m_modelProgress->setRange(0, static_cast<int>(qMin(total, qint64(INT_MAX))));
                m_modelProgress->setValue(static_cast<int>(qMin(received, qint64(INT_MAX))));
                m_modelProgress->setFormat(QStringLiteral("%p%"));
            });
    connect(m_modelDownloader, &Vaani::VaaniModelDownloader::failed, this,
            &VaaniSettingsDialog::onModelDownloadFailed);
    connect(m_modelDownloader, &Vaani::VaaniModelDownloader::finishedSuccess, this,
            &VaaniSettingsDialog::onModelDownloadFinished);

    buildUi();
    reloadFromStore();
    applyThemeFromStore();

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this]() { applyThemeFromStore(); });
    connect(m_core, &VaaniCoreController::engineStateChanged, this, [this]() {
        if (isVisible())
            refreshModelSection();
    });
}

void VaaniSettingsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto* header = new QHBoxLayout();
    header->setSpacing(12);
    auto* logo = new QLabel(this);
    logo->setPixmap(Vaani::brandPixmap(40));
    header->addWidget(logo);
    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("Vaani"), this);
    title->setObjectName(QStringLiteral("vaaniSettingsTitle"));
    auto* subtitle = new QLabel(
        tr("External dictation — Vaani keeps running in the tray when you close this window."),
        this);
    subtitle->setObjectName(QStringLiteral("vaaniSettingsSubtitle"));
    subtitle->setWordWrap(true);
    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);
    header->addLayout(titleCol, 1);
    root->addLayout(header);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("VaaniSettingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("VaaniSettingsContent"));
    auto* sections = new QVBoxLayout(content);
    sections->setContentsMargins(0, 0, 4, 0);
    sections->setSpacing(12);

    {
        QFrame* section = addSection(tr("Speech model"), sections);
        auto* hint = new QLabel(
            tr("Default is %1 (balanced speed and accuracy). Download once per model; switching models reloads the engine.")
                .arg(Vaani::defaultModelOffer().whisperFileName),
            this);
        hint->setProperty("class", QVariant(QStringLiteral("vaaniSettingsHint")));
        hint->setWordWrap(true);
        sectionBody(section)->addWidget(hint);
        m_modelCombo = new QComboBox(this);
        for (const Vaani::ModelOffer& offer : Vaani::modelCatalog())
            m_modelCombo->addItem(Vaani::formatModelChoiceLabel(offer), offer.id);
        connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                &VaaniSettingsDialog::onModelSelectionChanged);
        sectionBody(section)->addWidget(m_modelCombo);
        m_modelStatus = new QLabel(this);
        m_modelStatus->setWordWrap(true);
        m_modelStatus->setProperty("class", QVariant(QStringLiteral("vaaniSettingsHint")));
        sectionBody(section)->addWidget(m_modelStatus);
        m_modelProgress = new QProgressBar(this);
        m_modelProgress->setTextVisible(true);
        m_modelProgress->setFixedHeight(12);
        sectionBody(section)->addWidget(m_modelProgress);
        m_modelDownloadBtn = new QPushButton(tr("Download selected model"), this);
        m_modelDownloadBtn->setObjectName(QStringLiteral("vaaniPrimaryButton"));
        connect(m_modelDownloadBtn, &QPushButton::clicked, this, &VaaniSettingsDialog::startModelDownload);
        sectionBody(section)->addWidget(m_modelDownloadBtn, 0, Qt::AlignLeft);
    }

    {
        QFrame* section = addSection(tr("Audio"), sections);
        sectionBody(section)->addWidget(new QLabel(tr("Microphone"), this));
        m_micCombo = new QComboBox(this);
        sectionBody(section)->addWidget(m_micCombo);
    }

    {
        QFrame* section = addSection(tr("Recognition"), sections);

        sectionBody(section)->addWidget(new QLabel(tr("Active Dictation Language"), this));
        m_languageCombo = new QComboBox(this);
        const auto allLanguages = LanguageUtils::allSupportedLanguagesSorted();
        for (const auto& pair : allLanguages)
            m_languageCombo->addItem(pair.first, pair.second);
        sectionBody(section)->addWidget(m_languageCombo);

        sectionBody(section)->addWidget(new QLabel(tr("Primary Languages (quick switch in system tray menu)"), this));
        m_primaryLangList = new QListWidget(this);
        m_primaryLangList->setFixedHeight(105);
        sectionBody(section)->addWidget(m_primaryLangList);

        auto* addRow = new QHBoxLayout();
        m_addLangCombo = new QComboBox(this);
        for (const auto& pair : allLanguages)
            m_addLangCombo->addItem(pair.first, pair.second);

        m_addPrimaryBtn = new QPushButton(tr("Add to Primary"), this);
        m_addPrimaryBtn->setObjectName(QStringLiteral("vaaniSecondaryButton"));
        m_removePrimaryBtn = new QPushButton(tr("Remove"), this);
        m_removePrimaryBtn->setObjectName(QStringLiteral("vaaniSecondaryButton"));

        addRow->addWidget(m_addLangCombo, 1);
        addRow->addWidget(m_addPrimaryBtn);
        addRow->addWidget(m_removePrimaryBtn);
        sectionBody(section)->addLayout(addRow);

        connect(m_addPrimaryBtn, &QPushButton::clicked, this, [this]() {
            if (!m_addLangCombo || !m_primaryLangList)
                return;
            const QString code = m_addLangCombo->currentData().toString();
            const QString name = m_addLangCombo->currentText();
            for (int i = 0; i < m_primaryLangList->count(); ++i) {
                if (m_primaryLangList->item(i)->data(Qt::UserRole).toString() == code)
                    return;
            }
            auto* item = new QListWidgetItem(name, m_primaryLangList);
            item->setData(Qt::UserRole, code);
        });

        connect(m_removePrimaryBtn, &QPushButton::clicked, this, [this]() {
            if (!m_primaryLangList)
                return;
            if (m_primaryLangList->count() <= 1)
                return;
            QListWidgetItem* item = m_primaryLangList->currentItem();
            if (item)
                delete item;
        });
    }

    {
        QFrame* section = addSection(tr("Push-to-talk"), sections);
        sectionBody(section)->addWidget(new QLabel(tr("Hotkey"), this));
        m_pttCapture = new VaaniHotkeyCaptureField(this);
        m_pttRefusal = new QLabel(this);
        m_pttRefusal->setObjectName(QStringLiteral("vaaniRefusalLabel"));
        m_pttRefusal->hide();
        sectionBody(section)->addWidget(m_pttCapture);
        sectionBody(section)->addWidget(m_pttRefusal);

        m_doubleTapCheck = new QCheckBox(tr("Double-tap hotkey for hands-free listening"), this);
        sectionBody(section)->addWidget(m_doubleTapCheck);

        connect(m_pttCapture, &VaaniHotkeyCaptureField::captureBegan, this, [this]() {
            m_core->setPushToTalkCaptureActive(true);
            m_pttRefusal->hide();
        });
        connect(m_pttCapture, &VaaniHotkeyCaptureField::captureEnded, this, [this]() {
            m_core->setPushToTalkCaptureActive(false);
        });
        connect(m_pttCapture, &VaaniHotkeyCaptureField::hotkeyRefused, this,
                &VaaniSettingsDialog::onHotkeyRefused);
        connect(m_pttCapture, &VaaniHotkeyCaptureField::hotkeyAccepted, this,
                &VaaniSettingsDialog::onHotkeyAccepted);
        connect(m_pttCapture, &VaaniHotkeyCaptureField::escapePressed, this,
                [this]() { m_pttRefusal->hide(); });
    }

    {
        QFrame* section = addSection(tr("Injection"), sections);
        m_keystrokeFallbackCheck = new QCheckBox(tr("Type text if clipboard paste fails"), this);
        auto* hint = new QLabel(
            tr("Final phrases are pasted with Ctrl+V into the focused application."),
            this);
        hint->setProperty("class", QVariant(QStringLiteral("vaaniSettingsHint")));
        hint->setWordWrap(true);
        sectionBody(section)->addWidget(hint);
        sectionBody(section)->addWidget(m_keystrokeFallbackCheck);
        m_injectPartialsCheck = new QCheckBox(
            tr("Inject partial results (debounced; experimental)"), this);
        sectionBody(section)->addWidget(m_injectPartialsCheck);
    }

    {
        QFrame* section = addSection(tr("Feedback"), sections);
        sectionBody(section)->addWidget(new QLabel(tr("Dictation HUD"), this));
        auto* hudHint = new QLabel(
            tr("Bottom-centre pill with level animation while listening; bars while transcribing."),
            this);
        hudHint->setProperty("class", QVariant(QStringLiteral("vaaniSettingsHint")));
        hudHint->setWordWrap(true);
        sectionBody(section)->addWidget(hudHint);
        m_listeningCombo = new QComboBox(this);
        m_listeningCombo->addItem(tr("HUD pill (recommended)"), QVariant::fromValue(
            static_cast<int>(VaaniSettingsStore::ListeningIndicator::StatusPill)));
        m_listeningCombo->addItem(tr("HUD + tray highlight"), QVariant::fromValue(
            static_cast<int>(VaaniSettingsStore::ListeningIndicator::TrayIcon)));
        m_listeningCombo->addItem(tr("Off"), QVariant::fromValue(
            static_cast<int>(VaaniSettingsStore::ListeningIndicator::Off)));
        sectionBody(section)->addWidget(m_listeningCombo);

        sectionBody(section)->addWidget(new QLabel(tr("Appearance"), this));
        m_themeCombo = new QComboBox(this);
        m_themeCombo->addItem(tr("Match Windows"), static_cast<int>(Vaani::UiTheme::System));
        m_themeCombo->addItem(tr("Light"), static_cast<int>(Vaani::UiTheme::Light));
        m_themeCombo->addItem(tr("Dark"), static_cast<int>(Vaani::UiTheme::Dark));
        connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                &VaaniSettingsDialog::onThemeComboChanged);
        sectionBody(section)->addWidget(m_themeCombo);
    }

    {
        QFrame* section = addSection(tr("Startup"), sections);
        m_autoStartCheck = new QCheckBox(tr("Start Vaani when Windows signs in"), this);
        m_autoStartCheck->setEnabled(Vaani::AutoStartManager::isSupported());
        sectionBody(section)->addWidget(m_autoStartCheck);
    }

    sections->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setObjectName(QStringLiteral("vaaniSecondaryButton"));
    connect(closeBtn, &QPushButton::clicked, this, [this]() { hide(); });
    auto* saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setObjectName(QStringLiteral("vaaniPrimaryButton"));
    connect(saveBtn, &QPushButton::clicked, this, &VaaniSettingsDialog::onSave);
    buttons->addWidget(closeBtn);
    buttons->addWidget(saveBtn);
    root->addLayout(buttons);
}

QFrame* VaaniSettingsDialog::addSection(const QString& title, QVBoxLayout* parentLayout)
{
    auto* section = new QFrame(parentLayout->parentWidget());
    section->setProperty("class", QVariant(QStringLiteral("vaaniSettingsSection")));
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    auto* heading = new QLabel(title, section);
    heading->setProperty("class", QVariant(QStringLiteral("vaaniSectionTitle")));
    layout->addWidget(heading);
    parentLayout->addWidget(section);
    return section;
}

void VaaniSettingsDialog::refreshMicrophoneList()
{
    if (!m_micCombo)
        return;
    const QString current = m_micCombo->currentData().toString();
    m_micCombo->clear();
    for (const Vaani::CaptureEndpoint& ep : Vaani::listActiveCaptureEndpoints())
        m_micCombo->addItem(ep.displayName, ep.endpointId);
    const int idx = m_micCombo->findData(current);
    if (idx >= 0)
        m_micCombo->setCurrentIndex(idx);
}

void VaaniSettingsDialog::refreshModelSection()
{
    if (!m_modelCombo || !m_core)
        return;

    const QString selectedId = m_core->settings()->selectedModelId();
    {
        QSignalBlocker blocker(m_modelCombo);
        const int idx = m_modelCombo->findData(selectedId);
        if (idx >= 0)
            m_modelCombo->setCurrentIndex(idx);
        else {
            const int defIdx = m_modelCombo->findData(Vaani::defaultModelOffer().id);
            if (defIdx >= 0)
                m_modelCombo->setCurrentIndex(defIdx);
        }
    }

    const QString currentId = m_modelCombo->currentData().toString();
    const bool installed = Vaani::isModelInstalled(currentId);
  const Vaani::ModelOffer offer = Vaani::offerById(currentId);

    if (installed) {
        const bool active = m_core->settings()->selectedModelId() == currentId
            && m_core->engineState() == VaaniCoreController::EngineState::Ready;
        m_modelStatus->setObjectName(QString());
        m_modelStatus->setProperty("class", QVariant(QStringLiteral("vaaniSettingsHint")));
        m_modelStatus->setText(active
            ? tr("%1 is installed and active.").arg(offer.whisperFileName)
            : tr("%1 is installed. Save or download to apply if you changed selection.")
                  .arg(offer.whisperFileName));
        m_modelProgress->setRange(0, 1);
        m_modelProgress->setValue(1);
        m_modelProgress->setFormat(QStringLiteral("100%"));
        m_modelDownloadBtn->setEnabled(false);
        m_modelDownloadBtn->setText(tr("Download selected model"));
    } else {
        m_modelStatus->setProperty("class", QVariant(QStringLiteral("vaaniSettingsHint")));
        m_modelStatus->setText(
            tr("%1 is not installed. Internet required (Whisper + Silero VAD).")
                .arg(offer.whisperFileName));
        m_modelProgress->setRange(0, 100);
        m_modelProgress->setValue(0);
        m_modelProgress->setFormat(QStringLiteral("%p%"));
        m_modelDownloadBtn->setEnabled(true);
        m_modelDownloadBtn->setText(tr("Download selected model"));
    }
}

void VaaniSettingsDialog::onModelSelectionChanged(int index)
{
    Q_UNUSED(index);
    refreshModelSection();
}

void VaaniSettingsDialog::startModelDownload()
{
    if (!m_modelCombo || !m_core)
        return;
    const QString id = m_modelCombo->currentData().toString();
    m_core->settings()->setSelectedModelId(id);
    m_core->settings()->save();
    const Vaani::ModelOffer offer = Vaani::offerById(id);
    m_modelDownloadBtn->setEnabled(false);
    m_modelProgress->setRange(0, 0);
    m_modelDownloader->startDownload(offer);
}

void VaaniSettingsDialog::onModelDownloadFailed(const QString& message)
{
    if (m_modelStatus) {
        m_modelStatus->setObjectName(QStringLiteral("vaaniRefusalLabel"));
        m_modelStatus->setText(message);
    }
    if (m_modelDownloadBtn) {
        m_modelDownloadBtn->setEnabled(true);
        m_modelDownloadBtn->setText(tr("Retry download"));
    }
    if (m_modelProgress) {
        m_modelProgress->setRange(0, 1);
        m_modelProgress->setValue(0);
    }
}

void VaaniSettingsDialog::onModelDownloadFinished(const QString& directory)
{
    Q_UNUSED(directory);
    if (!m_core || !m_modelCombo)
        return;
    m_core->settings()->setSelectedModelId(m_modelCombo->currentData().toString());
    m_core->settings()->save();
    refreshModelSection();
    m_core->reloadSpeechEngine();
}

void VaaniSettingsDialog::reloadFromStore()
{
    VaaniSettingsStore* store = m_core->settings();
    refreshMicrophoneList();
    refreshModelSection();

    const int langIndex = m_languageCombo->findData(store->dictationLanguageCode());
    if (langIndex >= 0)
        m_languageCombo->setCurrentIndex(langIndex);

    m_primaryLangList->clear();
    for (const QString& code : store->primaryLanguages()) {
        auto* item = new QListWidgetItem(LanguageUtils::displayFromLanguageCode(code), m_primaryLangList);
        item->setData(Qt::UserRole, code);
    }

    const int micIndex = m_micCombo->findData(store->microphoneEndpointId());
    if (micIndex >= 0)
        m_micCombo->setCurrentIndex(micIndex);

    m_pttCapture->setHotkey(store->pushToTalkHotkey());
    m_doubleTapCheck->setChecked(store->doubleTapToggleEnabled());
    m_keystrokeFallbackCheck->setChecked(store->keystrokeFallbackOnPasteFailure());
    if (m_injectPartialsCheck)
        m_injectPartialsCheck->setChecked(store->injectPartialResults());
    m_autoStartCheck->setChecked(Vaani::AutoStartManager::isEnabled());

    const int listenIndex =
        m_listeningCombo->findData(static_cast<int>(store->listeningIndicator()));
    if (listenIndex >= 0)
        m_listeningCombo->setCurrentIndex(listenIndex);

    const int themeIndex = m_themeCombo->findData(static_cast<int>(store->uiTheme()));
    if (themeIndex >= 0)
        m_themeCombo->setCurrentIndex(themeIndex);

    m_pttRefusal->hide();
}

void VaaniSettingsDialog::applyThemeFromStore()
{
    Vaani::applySettingsStyle(this, m_core->settings()->uiTheme());
}

void VaaniSettingsDialog::onThemeComboChanged(int index)
{
    Q_UNUSED(index);
    const auto theme = static_cast<Vaani::UiTheme>(m_themeCombo->currentData().toInt());
    Vaani::applySettingsStyle(this, theme);
}

void VaaniSettingsDialog::onHotkeyRefused(const QString& reason)
{
    if (reason.isEmpty()) {
        m_pttRefusal->hide();
        return;
    }
    m_pttRefusal->setText(reason);
    m_pttRefusal->show();
}

void VaaniSettingsDialog::onHotkeyAccepted(const VaaniHotkey& hotkey)
{
    m_pttRefusal->hide();
    m_core->settings()->setPushToTalkHotkey(hotkey);
    m_pttCapture->setHotkey(hotkey);
}

void VaaniSettingsDialog::onSave()
{
    VaaniSettingsStore* store = m_core->settings();
    const QString previousModelId = store->selectedModelId();

    store->setDictationLanguageCode(m_languageCombo->currentData().toString());

    QStringList primaryCodes;
    for (int i = 0; i < m_primaryLangList->count(); ++i) {
        primaryCodes.append(m_primaryLangList->item(i)->data(Qt::UserRole).toString());
    }
    if (!primaryCodes.isEmpty())
        store->setPrimaryLanguages(primaryCodes);
    store->setMicrophoneEndpointId(m_micCombo->currentData().toString());
    store->setPushToTalkHotkey(m_pttCapture->hotkey());
    store->setDoubleTapToggleEnabled(m_doubleTapCheck->isChecked());
    store->setKeystrokeFallbackOnPasteFailure(m_keystrokeFallbackCheck->isChecked());
    if (m_injectPartialsCheck)
        store->setInjectPartialResults(m_injectPartialsCheck->isChecked());
    store->setListeningIndicator(static_cast<VaaniSettingsStore::ListeningIndicator>(
        m_listeningCombo->currentData().toInt()));
    store->setUiTheme(static_cast<Vaani::UiTheme>(m_themeCombo->currentData().toInt()));

    if (m_modelCombo) {
        const QString modelId = m_modelCombo->currentData().toString();
        if (Vaani::isModelInstalled(modelId))
            store->setSelectedModelId(modelId);
    }

    if (Vaani::AutoStartManager::isSupported())
        Vaani::AutoStartManager::setEnabled(m_autoStartCheck->isChecked());

    store->save();
    m_core->applySettingsToSpeechEngine();
    applyThemeFromStore();
    refreshModelSection();

    if (store->selectedModelId() != previousModelId
        && Vaani::isModelInstalled(store->selectedModelId())) {
        m_core->reloadSpeechEngine();
    }

    hide();
}

void VaaniSettingsDialog::closeEvent(QCloseEvent* event)
{
    event->ignore();
    if (m_pttCapture && m_pttCapture->isCapturing())
        m_pttCapture->cancelCapture();
    hide();
}

void VaaniSettingsDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_pttCapture)
        m_pttCapture->cancelCapture();
}
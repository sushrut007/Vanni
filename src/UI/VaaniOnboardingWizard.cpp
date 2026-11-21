#include "UI/VaaniOnboardingWizard.h"

#include "Core/VaaniCoreController.h"
#include "Input/VaaniHotkey.h"
#include "Models/VaaniModelCatalog.h"
#include "Models/VaaniModelDownloader.h"
#include "Settings/CaptureEndpointList.h"
#include "Settings/VaaniHotkeyCaptureField.h"
#include "Settings/VaaniSettingsStore.h"
#include "Settings/VaaniSettingsStyle.h"
#include "Utils/LanguageUtils.h"
#include "Utils/VaaniBrandIcon.h"

#include <QtGlobal>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <climits>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("—");
    const double mb = bytes / (1024.0 * 1024.0);
    return QStringLiteral("~%1 MB").arg(QString::number(mb, 'f', 0));
}

QFrame* makeCard(QWidget* parent, QVBoxLayout** outLayout)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("vaaniOnboardingCard"));
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(12);
    *outLayout = lay;
    return card;
}

} // namespace

VaaniOnboardingWizard::VaaniOnboardingWizard(VaaniCoreController* core, QWidget* parent)
    : QDialog(parent, Qt::Dialog)
    , m_core(core)
    , m_store(core ? core->settings() : nullptr)
{
    setObjectName(QStringLiteral("VaaniOnboardingRoot"));
    setWindowTitle(QStringLiteral("Welcome to Vaani"));
    setMinimumSize(560, 520);
    setAttribute(Qt::WA_QuitOnClose, false);

    m_downloader = new Vaani::VaaniModelDownloader(this);
    connect(m_downloader, &Vaani::VaaniModelDownloader::statusChanged, this, [this](const QString& text) {
        if (m_downloadStatus)
            m_downloadStatus->setText(text);
    });
    connect(m_downloader, &Vaani::VaaniModelDownloader::progressChanged, this,
            [this](qint64 received, qint64 total) {
                if (!m_progress || total <= 0)
                    return;
                m_progress->setRange(0, static_cast<int>(qMin(total, qint64(INT_MAX))));
                m_progress->setValue(static_cast<int>(qMin(received, qint64(INT_MAX))));
                m_progress->setFormat(QStringLiteral("%p%"));
            });
    connect(m_downloader, &Vaani::VaaniModelDownloader::failed, this, &VaaniOnboardingWizard::onDownloadFailed);
    connect(m_downloader, &Vaani::VaaniModelDownloader::finishedSuccess, this,
            &VaaniOnboardingWizard::onDownloadFinished);

    if (m_store) {
        m_modelReady = Vaani::isModelInstalled(m_store->selectedModelId());
        Vaani::applySettingsStyle(this, m_store->uiTheme());
    }

    buildPages();
    refreshModelPage();
    refreshMicrophones();
    updateNav();
}

void VaaniOnboardingWizard::buildPages()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 22);
    root->setSpacing(16);

    auto* header = new QHBoxLayout();
    auto* logo = new QLabel(this);
    logo->setPixmap(Vaani::brandPixmap(44));
    header->addWidget(logo);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("Vaani"), this);
    title->setObjectName(QStringLiteral("vaaniOnboardingTitle"));
    auto* subtitle = new QLabel(QStringLiteral("External dictation for Windows"), this);
    subtitle->setObjectName(QStringLiteral("vaaniOnboardingSubtitle"));
    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);
    header->addLayout(titleCol, 1);
    root->addLayout(header);

    m_stepIndicator = new QLabel(this);
    m_stepIndicator->setObjectName(QStringLiteral("vaaniStepIndicator"));
    root->addWidget(m_stepIndicator);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeCard(this, &lay);
        auto* pageTitle = new QLabel(QStringLiteral("Tray-first dictation"), card);
        pageTitle->setObjectName(QStringLiteral("vaaniOnboardingPageTitle"));
        lay->addWidget(pageTitle);
        lay->addWidget(new QLabel(
            QStringLiteral(
                "Vaani runs in the system tray. Hold your hotkey to dictate into any app, "
                "or double-tap for hands-free mode. Close setup anytime — Vaani keeps running."),
            card));
        lay->addStretch();
        auto* page = new QWidget(this);
        auto* wrap = new QVBoxLayout(page);
        wrap->setContentsMargins(0, 0, 0, 0);
        wrap->addWidget(card);
        m_stack->addWidget(page);
    }

    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeCard(this, &lay);
        auto* pageTitle = new QLabel(QStringLiteral("Speech models"), card);
        pageTitle->setObjectName(QStringLiteral("vaaniOnboardingPageTitle"));
        lay->addWidget(pageTitle);
        auto* hint = new QLabel(
            QStringLiteral(
                "One-time download from Hugging Face. Default: %1.")
                .arg(Vaani::defaultModelOffer().whisperFileName),
            card);
        hint->setWordWrap(true);
        hint->setObjectName(QStringLiteral("vaaniOnboardingBody"));
        lay->addWidget(hint);
        m_modelCombo = new QComboBox(card);
        for (const Vaani::ModelOffer& offer : Vaani::modelCatalog())
            m_modelCombo->addItem(Vaani::formatModelChoiceLabel(offer), offer.id);
        lay->addWidget(m_modelCombo);
        m_downloadStatus = new QLabel(card);
        m_downloadStatus->setWordWrap(true);
        m_downloadStatus->setObjectName(QStringLiteral("vaaniOnboardingBody"));
        lay->addWidget(m_downloadStatus);
        m_progress = new QProgressBar(card);
        m_progress->setTextVisible(true);
        lay->addWidget(m_progress);
        m_downloadBtn = new QPushButton(QStringLiteral("Download"), card);
        m_downloadBtn->setObjectName(QStringLiteral("vaaniPrimaryButton"));
        m_downloadBtn->setCursor(Qt::PointingHandCursor);
        connect(m_downloadBtn, &QPushButton::clicked, this, &VaaniOnboardingWizard::startDownload);
        lay->addWidget(m_downloadBtn, 0, Qt::AlignLeft);
        lay->addStretch();
        auto* page = new QWidget(this);
        auto* wrap = new QVBoxLayout(page);
        wrap->setContentsMargins(0, 0, 0, 0);
        wrap->addWidget(card);
        m_stack->addWidget(page);
    }

    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeCard(this, &lay);
        auto* pageTitle = new QLabel(QStringLiteral("Primary languages"), card);
        pageTitle->setObjectName(QStringLiteral("vaaniOnboardingPageTitle"));
        lay->addWidget(pageTitle);

        auto* hint = new QLabel(
            QStringLiteral(
                "Select the languages you speak. These will appear in your system tray context menu "
                "for quick 1-click switching."),
            card);
        hint->setWordWrap(true);
        hint->setObjectName(QStringLiteral("vaaniOnboardingBody"));
        lay->addWidget(hint);

        m_langSearch = new QLineEdit(card);
        m_langSearch->setPlaceholderText(QStringLiteral("Search languages (e.g. English, Hindi, Spanish)..."));
        m_langSearch->setClearButtonEnabled(true);
        lay->addWidget(m_langSearch);

        m_langList = new QListWidget(card);
        m_langList->setObjectName(QStringLiteral("vaaniLangList"));
        m_langList->setMinimumHeight(150);
        m_langList->setMaximumHeight(190);

        const auto allLanguages = LanguageUtils::allSupportedLanguagesSorted();
        for (const auto& pair : allLanguages) {
            auto* item = new QListWidgetItem(pair.first, m_langList);
            item->setData(Qt::UserRole, pair.second);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            if (pair.second == QLatin1String("en"))
                item->setCheckState(Qt::Checked);
            else
                item->setCheckState(Qt::Unchecked);
        }
        lay->addWidget(m_langList);

        auto* defaultRow = new QHBoxLayout();
        auto* defaultLabel = new QLabel(QStringLiteral("Default language:"), card);
        defaultLabel->setObjectName(QStringLiteral("vaaniOnboardingBody"));
        defaultRow->addWidget(defaultLabel);
        m_defaultLangCombo = new QComboBox(card);
        defaultRow->addWidget(m_defaultLangCombo, 1);
        lay->addLayout(defaultRow);

        updateDefaultLangCombo();

        connect(m_langSearch, &QLineEdit::textChanged, this, [this](const QString& query) {
            const QString q = query.trimmed().toLower();
            for (int i = 0; i < m_langList->count(); ++i) {
                QListWidgetItem* it = m_langList->item(i);
                it->setHidden(!q.isEmpty() && !it->text().toLower().contains(q));
            }
        });

        connect(m_langList, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
            updateDefaultLangCombo();
        });

        auto* page = new QWidget(this);
        auto* wrap = new QVBoxLayout(page);
        wrap->setContentsMargins(0, 0, 0, 0);
        wrap->addWidget(card);
        m_stack->addWidget(page);
    }

    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeCard(this, &lay);
        auto* pageTitle = new QLabel(QStringLiteral("Microphone"), card);
        pageTitle->setObjectName(QStringLiteral("vaaniOnboardingPageTitle"));
        lay->addWidget(pageTitle);
        lay->addWidget(new QLabel(QStringLiteral("Pick the input device Vaani should use."), card));
        m_micCombo = new QComboBox(card);
        lay->addWidget(m_micCombo);
        lay->addStretch();
        auto* page = new QWidget(this);
        auto* wrap = new QVBoxLayout(page);
        wrap->setContentsMargins(0, 0, 0, 0);
        wrap->addWidget(card);
        m_stack->addWidget(page);
    }

    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeCard(this, &lay);
        auto* pageTitle = new QLabel(QStringLiteral("Push-to-talk"), card);
        pageTitle->setObjectName(QStringLiteral("vaaniOnboardingPageTitle"));
        lay->addWidget(pageTitle);
        lay->addWidget(new QLabel(
            QStringLiteral("Choose 1–3 keys. System shortcuts such as Alt+F4 are blocked."), card));
        m_hotkeyField = new VaaniHotkeyCaptureField(card);
        lay->addWidget(m_hotkeyField);
        m_hotkeyError = new QLabel(card);
        m_hotkeyError->setObjectName(QStringLiteral("vaaniRefusalLabel"));
        m_hotkeyError->hide();
        lay->addWidget(m_hotkeyError);
        connect(m_hotkeyField, &VaaniHotkeyCaptureField::hotkeyRefused, this, [this](const QString& r) {
            m_hotkeyError->setText(r);
            m_hotkeyError->show();
        });
        connect(m_hotkeyField, &VaaniHotkeyCaptureField::hotkeyAccepted, this, [this](const VaaniHotkey&) {
            m_hotkeyError->hide();
        });
        connect(m_hotkeyField, &VaaniHotkeyCaptureField::captureBegan, this, [this]() {
            if (m_core)
                m_core->setPushToTalkCaptureActive(true);
        });
        connect(m_hotkeyField, &VaaniHotkeyCaptureField::captureEnded, this, [this]() {
            if (m_core)
                m_core->setPushToTalkCaptureActive(false);
        });
        if (m_store)
            m_hotkeyField->setHotkey(m_store->pushToTalkHotkey());
        lay->addStretch();
        auto* page = new QWidget(this);
        auto* wrap = new QVBoxLayout(page);
        wrap->setContentsMargins(0, 0, 0, 0);
        wrap->addWidget(card);
        m_stack->addWidget(page);
    }

    {
        QVBoxLayout* lay = nullptr;
        auto* card = makeCard(this, &lay);
        auto* pageTitle = new QLabel(QStringLiteral("Ready"), card);
        pageTitle->setObjectName(QStringLiteral("vaaniOnboardingPageTitle"));
        lay->addWidget(pageTitle);
        lay->addWidget(new QLabel(
            QStringLiteral(
                "Open settings anytime from the tray. Hold your hotkey, speak, release — text appears "
                "where your cursor is."),
            card));
        m_skipCheck = new QCheckBox(QStringLiteral("Don’t show this setup again"), card);
        lay->addWidget(m_skipCheck);
        lay->addStretch();
        auto* page = new QWidget(this);
        auto* wrap = new QVBoxLayout(page);
        wrap->setContentsMargins(0, 0, 0, 0);
        wrap->addWidget(card);
        m_stack->addWidget(page);
    }

    auto* nav = new QHBoxLayout();
    nav->addStretch();
    m_back = new QPushButton(QStringLiteral("Back"), this);
    m_back->setObjectName(QStringLiteral("vaaniSecondaryButton"));
    m_back->setCursor(Qt::PointingHandCursor);
    m_next = new QPushButton(QStringLiteral("Next"), this);
    m_next->setObjectName(QStringLiteral("vaaniPrimaryButton"));
    m_next->setCursor(Qt::PointingHandCursor);
    connect(m_back, &QPushButton::clicked, this, &VaaniOnboardingWizard::goBack);
    connect(m_next, &QPushButton::clicked, this, &VaaniOnboardingWizard::goNext);
    nav->addWidget(m_back);
    nav->addWidget(m_next);
    root->addLayout(nav);
}

void VaaniOnboardingWizard::refreshMicrophones()
{
    if (!m_micCombo)
        return;
    m_micCombo->clear();
    for (const Vaani::CaptureEndpoint& ep : Vaani::listActiveCaptureEndpoints()) {
        m_micCombo->addItem(ep.displayName, ep.endpointId);
    }
    if (m_store) {
        const int idx = m_micCombo->findData(m_store->microphoneEndpointId());
        if (idx >= 0)
            m_micCombo->setCurrentIndex(idx);
        else if (m_micCombo->count() > 0)
            m_micCombo->setCurrentIndex(0);
    }
}

void VaaniOnboardingWizard::refreshModelPage()
{
    if (!m_store || !m_modelCombo)
        return;
    const int idx = m_modelCombo->findData(m_store->selectedModelId());
    if (idx >= 0)
        m_modelCombo->setCurrentIndex(idx);
    else {
        const Vaani::ModelOffer def = Vaani::defaultModelOffer();
        const int defIdx = m_modelCombo->findData(def.id);
        if (defIdx >= 0)
            m_modelCombo->setCurrentIndex(defIdx);
    }

    m_modelReady = Vaani::isModelInstalled(m_modelCombo->currentData().toString());
    if (m_modelReady) {
        m_downloadStatus->setText(QStringLiteral("Models are installed and ready."));
        m_progress->setRange(0, 1);
        m_progress->setValue(1);
        m_progress->setFormat(QStringLiteral("100%"));
        if (m_downloadBtn)
            m_downloadBtn->setEnabled(false);
    } else {
        m_downloadStatus->setText(
            QStringLiteral("Requires an internet connection. Whisper and Silero VAD will be downloaded."));
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        m_progress->setFormat(QStringLiteral("%p%"));
        if (m_downloadBtn)
            m_downloadBtn->setEnabled(true);
    }
}

void VaaniOnboardingWizard::startDownload()
{
    if (!m_modelCombo || !m_store)
        return;
    const QString id = m_modelCombo->currentData().toString();
    m_store->setSelectedModelId(id);
    m_store->save();
    const Vaani::ModelOffer offer = Vaani::offerById(id);
    m_downloadBtn->setEnabled(false);
    m_progress->setRange(0, 0);
    m_downloader->startDownload(offer);
}

void VaaniOnboardingWizard::onDownloadFailed(const QString& message)
{
    m_downloadStatus->setText(message);
    m_downloadStatus->setObjectName(QStringLiteral("vaaniRefusalLabel"));
    if (m_downloadBtn) {
        m_downloadBtn->setEnabled(true);
        m_downloadBtn->setText(QStringLiteral("Retry download"));
    }
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
}

void VaaniOnboardingWizard::onDownloadFinished(const QString& directory)
{
    Q_UNUSED(directory);
    m_modelReady = true;
    m_downloadStatus->setObjectName(QStringLiteral("vaaniOnboardingBody"));
    m_downloadStatus->setText(QStringLiteral("All set — models downloaded successfully."));
    m_progress->setRange(0, 1);
    m_progress->setValue(1);
    updateNav();
}

void VaaniOnboardingWizard::updateDefaultLangCombo()
{
    if (!m_defaultLangCombo || !m_langList)
        return;

    const QString prevSelected = m_defaultLangCombo->currentData().toString();
    m_defaultLangCombo->clear();

    for (int i = 0; i < m_langList->count(); ++i) {
        QListWidgetItem* it = m_langList->item(i);
        if (it->checkState() == Qt::Checked) {
            m_defaultLangCombo->addItem(it->text(), it->data(Qt::UserRole));
        }
    }

    if (m_defaultLangCombo->count() == 0) {
        for (int i = 0; i < m_langList->count(); ++i) {
            if (m_langList->item(i)->data(Qt::UserRole).toString() == QLatin1String("en")) {
                m_langList->item(i)->setCheckState(Qt::Checked);
                m_defaultLangCombo->addItem(m_langList->item(i)->text(), QLatin1String("en"));
                break;
            }
        }
    }

    int idx = m_defaultLangCombo->findData(prevSelected);
    if (idx >= 0)
        m_defaultLangCombo->setCurrentIndex(idx);
    else if (m_defaultLangCombo->count() > 0)
        m_defaultLangCombo->setCurrentIndex(0);
}

void VaaniOnboardingWizard::saveLanguagesFromOnboarding()
{
    if (!m_store || !m_langList)
        return;

    QStringList selectedCodes;
    for (int i = 0; i < m_langList->count(); ++i) {
        QListWidgetItem* it = m_langList->item(i);
        if (it->checkState() == Qt::Checked) {
            const QString code = it->data(Qt::UserRole).toString();
            if (!code.isEmpty() && !selectedCodes.contains(code))
                selectedCodes.append(code);
        }
    }
    if (selectedCodes.isEmpty())
        selectedCodes.append(QStringLiteral("en"));

    m_store->setPrimaryLanguages(selectedCodes);

    if (m_defaultLangCombo && m_defaultLangCombo->currentIndex() >= 0) {
        const QString activeCode = m_defaultLangCombo->currentData().toString();
        if (!activeCode.isEmpty())
            m_store->setDictationLanguageCode(activeCode);
    }
}

void VaaniOnboardingWizard::goBack()
{
    if (m_page <= 0)
        return;
    --m_page;
    m_stack->setCurrentIndex(m_page);
    updateNav();
}

void VaaniOnboardingWizard::goNext()
{
    if (m_page == 1 && !m_modelReady) {
        m_downloadStatus->setObjectName(QStringLiteral("vaaniRefusalLabel"));
        m_downloadStatus->setText(QStringLiteral("Download the speech models before continuing."));
        return;
    }

    if (m_page == 2)
        saveLanguagesFromOnboarding();
    if (m_page == 3 && m_micCombo && m_store)
        m_store->setMicrophoneEndpointId(m_micCombo->currentData().toString());
    if (m_page == 4 && m_hotkeyField && m_store)
        m_store->setPushToTalkHotkey(m_hotkeyField->hotkey());

    if (m_page >= m_stack->count() - 1) {
        saveLanguagesFromOnboarding();
        if (m_store) {
            if (m_modelCombo)
                m_store->setSelectedModelId(m_modelCombo->currentData().toString());
            m_store->setOnboardingCompleted(true);
            if (m_skipCheck && m_skipCheck->isChecked())
                m_store->setOnboardingSkipped(true);
            m_store->save();
        }
        accept();
        return;
    }

    ++m_page;
    m_stack->setCurrentIndex(m_page);
    updateNav();
}

void VaaniOnboardingWizard::updateNav()
{
    if (m_stepIndicator && m_stack)
        m_stepIndicator->setText(
            QStringLiteral("Step %1 of %2")
                .arg(m_page + 1)
                .arg(m_stack->count()));
    m_back->setEnabled(m_page > 0);
    m_next->setText(m_page >= m_stack->count() - 1 ? QStringLiteral("Finish") : QStringLiteral("Next"));
}
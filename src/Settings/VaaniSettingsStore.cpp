#include "Settings/VaaniSettingsStore.h"

#include "Utils/AppSettingsPath.h"
#include "Utils/LanguageUtils.h"

#include <QSettings>

namespace {
constexpr char kLanguageGroup[] = "Language";
constexpr char kDictationLanguageKey[] = "DictationLanguage";
constexpr char kMicrophoneGroup[] = "Microphone";
constexpr char kSelectedDeviceIdKey[] = "SelectedDeviceId";
constexpr char kInjectionGroup[] = "Injection";
constexpr char kKeystrokeFallbackKey[] = "KeystrokeFallback";
constexpr char kInjectPartialsKey[] = "InjectPartials";
constexpr char kHotkeysGroup[] = "Hotkeys";
constexpr char kPushToTalkKey[] = "PushToTalk";
constexpr char kDoubleTapToggleKey[] = "DoubleTapToggle";
constexpr char kModelsGroup[] = "Models";
constexpr char kSelectedModelKey[] = "SelectedId";
constexpr char kOnboardingGroup[] = "Onboarding";
constexpr char kOnboardingCompletedKey[] = "Done";
constexpr char kOnboardingSkippedKey[] = "Skipped";
constexpr char kUiGroup[] = "UI";
constexpr char kThemeKey[] = "Theme";
constexpr char kListeningIndicatorKey[] = "ListeningIndicator";

Vaani::UiTheme themeFromStorage(const QString& name)
{
    if (name.compare(QStringLiteral("Light"), Qt::CaseInsensitive) == 0)
        return Vaani::UiTheme::Light;
    if (name.compare(QStringLiteral("Dark"), Qt::CaseInsensitive) == 0)
        return Vaani::UiTheme::Dark;
    return Vaani::UiTheme::System;
}

QString themeToStorage(Vaani::UiTheme theme)
{
    switch (theme) {
    case Vaani::UiTheme::Light:
        return QStringLiteral("Light");
    case Vaani::UiTheme::Dark:
        return QStringLiteral("Dark");
    case Vaani::UiTheme::System:
        break;
    }
    return QStringLiteral("System");
}

VaaniSettingsStore::ListeningIndicator indicatorFromStorage(const QString& name)
{
    if (name.compare(QStringLiteral("StatusPill"), Qt::CaseInsensitive) == 0)
        return VaaniSettingsStore::ListeningIndicator::StatusPill;
    if (name.compare(QStringLiteral("Off"), Qt::CaseInsensitive) == 0)
        return VaaniSettingsStore::ListeningIndicator::Off;
    return VaaniSettingsStore::ListeningIndicator::TrayIcon;
}

QString indicatorToStorage(VaaniSettingsStore::ListeningIndicator mode)
{
    switch (mode) {
    case VaaniSettingsStore::ListeningIndicator::StatusPill:
        return QStringLiteral("StatusPill");
    case VaaniSettingsStore::ListeningIndicator::Off:
        return QStringLiteral("Off");
    case VaaniSettingsStore::ListeningIndicator::TrayIcon:
        break;
    }
    return QStringLiteral("TrayIcon");
}

VaaniHotkey hotkeyFromStorage(const QString& stored)
{
    if (stored.compare(QStringLiteral("CtrlWin"), Qt::CaseInsensitive) == 0) {
        VaaniHotkey hk;
        hk.virtualKeys = { 0x11, 0x5B };
        return hk;
    }
    if (stored.compare(QStringLiteral("ShiftWin"), Qt::CaseInsensitive) == 0) {
        VaaniHotkey hk;
        hk.virtualKeys = { 0x10, 0x5B };
        return hk;
    }
    if (stored.compare(QStringLiteral("CtrlShiftWin"), Qt::CaseInsensitive) == 0) {
        VaaniHotkey hk;
        hk.virtualKeys = { 0x11, 0x10, 0x5B };
        return hk;
    }
    return VaaniHotkey::fromStorage(stored, VaaniHotkey::defaultPushToTalk());
}
} // namespace

VaaniSettingsStore::VaaniSettingsStore(QObject* parent)
    : QObject(parent)
{
}

QString VaaniSettingsStore::dictationLanguageCode() const
{
    return m_dictationLanguage;
}

void VaaniSettingsStore::setDictationLanguageCode(const QString& code)
{
    const QString normalized = code.trimmed().toLower();
    if (normalized.isEmpty() || normalized == m_dictationLanguage)
        return;
    m_dictationLanguage = normalized;
    emit dictationLanguageChanged(m_dictationLanguage);
}

QString VaaniSettingsStore::microphoneEndpointId() const
{
    return m_microphoneEndpointId;
}

void VaaniSettingsStore::setMicrophoneEndpointId(const QString& endpointId)
{
    if (endpointId == m_microphoneEndpointId)
        return;
    m_microphoneEndpointId = endpointId;
    emit microphoneEndpointChanged(m_microphoneEndpointId);
}

bool VaaniSettingsStore::keystrokeFallbackOnPasteFailure() const
{
    return m_keystrokeFallback;
}

bool VaaniSettingsStore::injectPartialResults() const
{
    return m_injectPartials;
}

void VaaniSettingsStore::setInjectPartialResults(bool enabled)
{
    if (m_injectPartials == enabled)
        return;
    m_injectPartials = enabled;
    emit injectPartialResultsChanged(enabled);
}

bool VaaniSettingsStore::doubleTapToggleEnabled() const
{
    return m_doubleTapToggle;
}

void VaaniSettingsStore::setDoubleTapToggleEnabled(bool enabled)
{
    if (m_doubleTapToggle == enabled)
        return;
    m_doubleTapToggle = enabled;
    emit doubleTapToggleChanged(enabled);
}

VaaniHotkey VaaniSettingsStore::pushToTalkHotkey() const
{
    return m_pushToTalk;
}

void VaaniSettingsStore::setPushToTalkHotkey(const VaaniHotkey& hotkey)
{
    if (m_pushToTalk.virtualKeys == hotkey.virtualKeys)
        return;
    m_pushToTalk = hotkey;
    emit pushToTalkHotkeyChanged(hotkey);
}

QString VaaniSettingsStore::selectedModelId() const
{
    return m_modelId;
}

void VaaniSettingsStore::setSelectedModelId(const QString& id)
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty() || trimmed == m_modelId)
        return;
    m_modelId = trimmed;
    emit selectedModelIdChanged(m_modelId);
}

bool VaaniSettingsStore::onboardingSkipped() const
{
    return m_onboardingSkipped;
}

bool VaaniSettingsStore::onboardingCompleted() const
{
    return m_onboardingCompleted;
}

void VaaniSettingsStore::setOnboardingCompleted(bool completed)
{
    m_onboardingCompleted = completed;
}

void VaaniSettingsStore::setOnboardingSkipped(bool skipped)
{
    if (m_onboardingSkipped == skipped)
        return;
    m_onboardingSkipped = skipped;
}

Vaani::UiTheme VaaniSettingsStore::uiTheme() const
{
    return m_uiTheme;
}

void VaaniSettingsStore::setUiTheme(Vaani::UiTheme theme)
{
    if (m_uiTheme == theme)
        return;
    m_uiTheme = theme;
    emit uiThemeChanged(theme);
}

VaaniSettingsStore::ListeningIndicator VaaniSettingsStore::listeningIndicator() const
{
    return m_listeningIndicator;
}

void VaaniSettingsStore::setListeningIndicator(ListeningIndicator mode)
{
    if (m_listeningIndicator == mode)
        return;
    m_listeningIndicator = mode;
    emit listeningIndicatorChanged(mode);
}

void VaaniSettingsStore::setKeystrokeFallbackOnPasteFailure(bool enabled)
{
    if (m_keystrokeFallback == enabled)
        return;
    m_keystrokeFallback = enabled;
    emit keystrokeFallbackChanged(enabled);
}

void VaaniSettingsStore::load()
{
    QSettings settings(Vaani::appSettingsIniPath(), QSettings::IniFormat);

    settings.beginGroup(QString::fromLatin1(kLanguageGroup));
    const QString displayLang =
        settings.value(QLatin1String(kDictationLanguageKey), QStringLiteral("English"))
            .toString();
    settings.endGroup();
    const QString code = LanguageUtils::languageCodeFromDisplay(displayLang);
    if (!code.isEmpty())
        m_dictationLanguage = code;

    settings.beginGroup(QString::fromLatin1(kMicrophoneGroup));
    m_microphoneEndpointId =
        settings.value(QLatin1String(kSelectedDeviceIdKey), QString()).toString();
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kInjectionGroup));
    m_keystrokeFallback =
        settings.value(QLatin1String(kKeystrokeFallbackKey), false).toBool();
    m_injectPartials = settings.value(QLatin1String(kInjectPartialsKey), false).toBool();
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kHotkeysGroup));
    m_pushToTalk = hotkeyFromStorage(settings.value(QLatin1String(kPushToTalkKey)).toString());
    m_doubleTapToggle = settings.value(QLatin1String(kDoubleTapToggleKey), true).toBool();
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kModelsGroup));
    m_modelId = settings.value(QLatin1String(kSelectedModelKey), QStringLiteral("base-q8"))
                    .toString();
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kOnboardingGroup));
    m_onboardingCompleted = settings.value(QLatin1String(kOnboardingCompletedKey), false).toBool();
    m_onboardingSkipped = settings.value(QLatin1String(kOnboardingSkippedKey), false).toBool();
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kUiGroup));
    m_uiTheme = themeFromStorage(settings.value(QLatin1String(kThemeKey)).toString());
    m_listeningIndicator = indicatorFromStorage(
        settings.value(QLatin1String(kListeningIndicatorKey)).toString());
    settings.endGroup();
}

void VaaniSettingsStore::save()
{
    QSettings settings(Vaani::appSettingsIniPath(), QSettings::IniFormat);

    settings.beginGroup(QString::fromLatin1(kLanguageGroup));
    settings.setValue(QLatin1String(kDictationLanguageKey),
                      LanguageUtils::displayFromLanguageCode(m_dictationLanguage));
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kMicrophoneGroup));
    settings.setValue(QLatin1String(kSelectedDeviceIdKey), m_microphoneEndpointId);
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kInjectionGroup));
    settings.setValue(QLatin1String(kKeystrokeFallbackKey), m_keystrokeFallback);
    settings.setValue(QLatin1String(kInjectPartialsKey), m_injectPartials);
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kHotkeysGroup));
    settings.setValue(QLatin1String(kPushToTalkKey), m_pushToTalk.storageString());
    settings.setValue(QLatin1String(kDoubleTapToggleKey), m_doubleTapToggle);
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kModelsGroup));
    settings.setValue(QLatin1String(kSelectedModelKey), m_modelId);
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kOnboardingGroup));
    settings.setValue(QLatin1String(kOnboardingCompletedKey), m_onboardingCompleted);
    settings.setValue(QLatin1String(kOnboardingSkippedKey), m_onboardingSkipped);
    settings.endGroup();

    settings.beginGroup(QString::fromLatin1(kUiGroup));
    settings.setValue(QLatin1String(kThemeKey), themeToStorage(m_uiTheme));
    settings.setValue(QLatin1String(kListeningIndicatorKey), indicatorToStorage(m_listeningIndicator));
    settings.endGroup();

    settings.sync();
}
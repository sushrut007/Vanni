#pragma once

#include "Input/VaaniHotkey.h"
#include "Settings/VaaniSettingsStyle.h"

#include <QObject>
#include <QString>

class VaaniSettingsStore : public QObject
{
    Q_OBJECT

public:
    enum class ListeningIndicator {
        TrayIcon,
        StatusPill,
        Off,
    };
    Q_ENUM(ListeningIndicator)

    explicit VaaniSettingsStore(QObject* parent = nullptr);

    QString dictationLanguageCode() const;
    void setDictationLanguageCode(const QString& code);

    QStringList primaryLanguages() const;
    void setPrimaryLanguages(const QStringList& codes);

    QString microphoneEndpointId() const;
    void setMicrophoneEndpointId(const QString& endpointId);

    bool keystrokeFallbackOnPasteFailure() const;
    void setKeystrokeFallbackOnPasteFailure(bool enabled);

    bool injectPartialResults() const;
    void setInjectPartialResults(bool enabled);

    bool doubleTapToggleEnabled() const;
    void setDoubleTapToggleEnabled(bool enabled);

    VaaniHotkey pushToTalkHotkey() const;
    void setPushToTalkHotkey(const VaaniHotkey& hotkey);

    QString selectedModelId() const;
    void setSelectedModelId(const QString& id);

    bool onboardingCompleted() const;
    void setOnboardingCompleted(bool completed);

    bool onboardingSkipped() const;
    void setOnboardingSkipped(bool skipped);

    Vaani::UiTheme uiTheme() const;
    void setUiTheme(Vaani::UiTheme theme);

    ListeningIndicator listeningIndicator() const;
    void setListeningIndicator(ListeningIndicator mode);

    void load();
    void save();

signals:
    void dictationLanguageChanged(const QString& code);
    void primaryLanguagesChanged(const QStringList& codes);
    void microphoneEndpointChanged(const QString& endpointId);
    void keystrokeFallbackChanged(bool enabled);
    void injectPartialResultsChanged(bool enabled);
    void pushToTalkHotkeyChanged(const VaaniHotkey& hotkey);
    void doubleTapToggleChanged(bool enabled);
    void selectedModelIdChanged(const QString& id);
    void uiThemeChanged(Vaani::UiTheme theme);
    void listeningIndicatorChanged(VaaniSettingsStore::ListeningIndicator mode);

private:
    QString m_dictationLanguage = QStringLiteral("en");
    QStringList m_primaryLanguages = { QStringLiteral("en") };
    QString m_microphoneEndpointId;
    bool m_keystrokeFallback = false;
    bool m_injectPartials = false;
    bool m_doubleTapToggle = true;
    VaaniHotkey m_pushToTalk = VaaniHotkey::defaultPushToTalk();
    QString m_modelId = QStringLiteral("base-q8");
    bool m_onboardingCompleted = false;
    bool m_onboardingSkipped = false;
    Vaani::UiTheme m_uiTheme = Vaani::UiTheme::System;
    ListeningIndicator m_listeningIndicator = ListeningIndicator::StatusPill;
};
#pragma once
#ifndef LANGUAGEUTILS_H
#define LANGUAGEUTILS_H

#include <QString>
#include <QHash>
#include <QSet>
#include <QStringLiteral>
#include <QRegularExpression>
#include <QStringList>
#include <QCollator>
#include <QLocale>
#include <QCoreApplication>
#include <algorithm>

/**
 * @brief Centralized language code-to-display-name mapping
 *
 * Consolidates the duplicate language maps previously maintained in
 * Vaani settings / onboarding into
 * a single source of truth.
 */
namespace LanguageUtils {

/**
 * @brief Get display name for a language code (broad map, 70+ languages)
 * @param code ISO 639-1/639-3 language code (e.g. "en", "fr", "jw")
 * @param fallback Value to return if code not found (default: "English")
 * @return Human-readable language name
 */
inline QString displayFromLanguageCode(const QString& code,
                                        const QString& fallback = QStringLiteral("English"))
{
    const QString c = code.trimmed().toLower();
    static const QHash<QString, QString> map = {
        {"en","English"},{"es","Spanish"},{"fr","French"},{"de","German"},
        {"it","Italian"},{"pt","Portuguese"},{"nl","Dutch"},{"sv","Swedish"},
        {"no","Norwegian"},{"da","Danish"},{"pl","Polish"},{"ja","Japanese"},
        {"ko","Korean"},{"zh","Chinese"},{"ru","Russian"},{"sq","Albanian"},
        {"am","Amharic"},{"ar","Arabic"},{"hy","Armenian"},{"as","Assamese"},
        {"az","Azerbaijani"},{"eu","Basque"},{"bn","Bengali"},{"be","Belarusian"},
        {"bs","Bosnian"},{"bg","Bulgarian"},{"my","Burmese"},{"ca","Catalan"},
        {"hr","Croatian"},{"cs","Czech"},{"et","Estonian"},{"fa","Farsi"},
        {"fi","Finnish"},{"gl","Galician"},{"ka","Georgian"},{"el","Greek"},
        {"gu","Gujarati"},{"ht","HaitianCreole"},{"he","Hebrew"},{"hi","Hindi"},
        {"hu","Hungarian"},{"is","Icelandic"},{"id","Indonesian"},
        // Session 596 review, B-17: the speech model names Javanese "jw".
        {"jw","Javanese"},{"kn","Kannada"},{"kk","Kazakh"},{"km","Khmers"},
        {"lo","Lao"},{"lv","Latvian"},{"lt","Lithuanian"},
        {"lb","Luxembourgish"},{"mk","Macedonian"},{"ms","Malay"},{"ml","Malayalam"},
        {"mt","Maltese"},{"mr","Marathi"},{"mn","Mongolian"},{"ps","Pashto"},
        {"pa","Punjabi"},{"ro","Romanian"},{"sr","Serbian"},{"sn","Shona"},
        {"sd","Sindhi"},{"si","Sinhala"},{"sk","Slovak"},{"sl","Slovenian"},
        {"so","Somali"},{"sw","Swahili"},{"tl","Tagalog"},{"ta","Tamil"},
        {"tg","Tajik"},{"te","Telugu"},{"th","Thai"},{"tr","Turkish"},
        {"tk","Turkmen"},{"uk","Ukrainian"},{"ur","Urdu"},{"uz","Uzbek"},
        {"vi","Vietnamese"},{"cy","Welsh"},{"yi","Yiddish"},{"yo","Yoruba"}
    };
    return map.value(c, fallback);
}

/**
 * @brief Subset of supported app UI languages (for splash screen, narrow matching)
 * @param code ISO 639-1 language code
 * @return Display name if code is a supported app UI language, empty string otherwise
 */
inline QString appUiLanguageName(const QString& code)
{
    const QString c = code.trimmed().toLower();
    if (c == "en") return QStringLiteral("English");
    if (c == "de") return QStringLiteral("German");
    if (c == "fr") return QStringLiteral("French");
    if (c == "es") return QStringLiteral("Spanish");
    if (c == "pt") return QStringLiteral("Portuguese");
    if (c == "it") return QStringLiteral("Italian");
    return QString();
}

/**
 * @brief Map display name back to resource-style language code (e.g., "English" -> "En")
 * @param displayName Human-readable language name
 * @return Capitalized language code for resource lookups, or "En" as fallback
 */
inline QString resourceCodeFromDisplayName(const QString& displayName)
{
    const QString d = displayName.trimmed().toLower();
    if (d == "english")    return QStringLiteral("En");
    if (d == "german")     return QStringLiteral("De");
    if (d == "french")     return QStringLiteral("Fr");
    if (d == "spanish")    return QStringLiteral("Es");
    if (d == "portuguese") return QStringLiteral("Pt");
    if (d == "italian")    return QStringLiteral("It");
    return QStringLiteral("En");
}

/**
 * @brief Shared 26-language non-Latin set used by WhisperWorker and WhisperRecognizer.
 *
 * A single authoritative constant prevents the two independent local sets from
 * drifting apart. Both post-processor bypass (WhisperWorker) and chunking
 * (WhisperRecognizer) must agree on every script; any mismatch produces either
 * corrupted output or incorrect silence-detection thresholds.
 *
 * Covers: Devanagari, CJK, Thai, Hebrew, Yiddish, Arabic script, South Indian
 * scripts, Southeast Asian scripts, Georgian, and Amharic.
 */
inline const QSet<QString>& nonLatinLanguageCodes()
{
    static const QSet<QString> s = {
        QStringLiteral("hi"), QStringLiteral("mr"), QStringLiteral("ne"),  // Devanagari
        QStringLiteral("ja"), QStringLiteral("zh"), QStringLiteral("ko"), QStringLiteral("th"), // CJK, Thai
        QStringLiteral("he"), QStringLiteral("yi"),                         // Hebrew script
        QStringLiteral("ar"), QStringLiteral("fa"), QStringLiteral("ur"), QStringLiteral("ps"), // Arabic script
        QStringLiteral("bn"), QStringLiteral("gu"), QStringLiteral("pa"),  // Bengali, Gujarati, Punjabi
        QStringLiteral("ta"), QStringLiteral("te"), QStringLiteral("kn"), QStringLiteral("ml"), // South Indian
        QStringLiteral("km"), QStringLiteral("lo"), QStringLiteral("my"), QStringLiteral("si"), // Southeast Asian
        QStringLiteral("ka"),                                                // Georgian (Mkhedruli)
        QStringLiteral("am"),                                                // Amharic (Ethiopic)
    };
    return s;
}

/**
 * @brief Map display name to ISO language code (inverse of displayFromLanguageCode).
 *
 * Item 4 / I-4 (FCR Pass 10 Feedback): Moved here from dictationprocpp.cpp where
 * it was file-static. Two files now need it (dictationprocpp.cpp and settingspage.cpp),
 * and duplicating an 87-entry map is the mistake that caused drift in nonLatinLanguageCodes.
 * Put next to displayFromLanguageCode() so mismatch between the two directions is visible.
 *
 * Brazilian Portuguese maps to "pt" (same as Portuguese) so VoiceCommandProcessor
 * loads the same localized catalogue for both display names.
 */
inline QString languageCodeFromDisplay(const QString& display)
{
    const QString d = display.trimmed().toLower();
    static const QHash<QString, QString> map = {
        {"english","en"},
        {"spanish","es"},
        {"french","fr"},
        {"german","de"},
        {"italian","it"},
        {"portuguese","pt"},
        {"brazilianportuguese","pt"}, // normalize to pt
        {"dutch","nl"},
        {"swedish","sv"},
        {"norwegian","no"},
        {"danish","da"},
        {"polish","pl"},
        {"japanese","ja"},
        {"korean","ko"},
        {"chinese","zh"},
        {"russian","ru"},
        {"albanian","sq"},
        {"amharic","am"},
        {"arabic","ar"},
        {"armenian","hy"},
        {"assamese","as"},
        {"azerbaijani","az"},
        {"basque","eu"},
        {"bengali","bn"},
        {"belarusian","be"},
        {"bosnian","bs"},
        {"bulgarian","bg"},
        {"burmese","my"},
        {"catalan","ca"},
        {"croatian","hr"},
        {"czech","cs"},
        {"estonian","et"},
        {"farsi","fa"},
        {"finnish","fi"},
        {"galician","gl"},
        {"georgian","ka"},
        {"greek","el"},
        {"gujarati","gu"},
        {"haitiancreole","ht"},
        {"hebrew","he"},
        {"hindi","hi"},
        {"hungarian","hu"},
        {"icelandic","is"},
        {"indonesian","id"},
        {"javanese","jw"},   // Session 596 review, B-17: the model's own code
        {"kannada","kn"},
        {"kazakh","kk"},
        {"khmers","km"},
        {"lao","lo"},
        {"latvian","lv"},
        {"lithuanian","lt"},
        {"luxembourgish","lb"},
        {"macedonian","mk"},
        {"malay","ms"},
        {"malayalam","ml"},
        {"maltese","mt"},
        {"marathi","mr"},
        {"mongolian","mn"},
        {"pashto","ps"},
        {"persian","fa"},
        {"punjabi","pa"},
        {"romanian","ro"},
        {"serbian","sr"},
        {"shona","sn"},
        {"sindhi","sd"},
        {"sinhala","si"},
        {"slovak","sk"},
        {"slovenian","sl"},
        {"somali","so"},
        {"swahili","sw"},
        {"tagalog","tl"},
        {"tamil","ta"},
        {"tajik","tg"},
        {"telugu","te"},
        {"thai","th"},
        {"turkish","tr"},
        {"turkmen","tk"},
        {"ukrainian","uk"},
        {"urdu","ur"},
        {"uzbek","uz"},
        {"vietnamese","vi"},
        {"welsh","cy"},
        {"yiddish","yi"},
        {"yoruba","yo"}
        // Session 596 review, B-17: Zulu, Kurdish and Hmong are withdrawn -- the
        // speech model has none of them (withdrawnDictationLanguages()).
    };
    // Session 596 review, B-17: a name first, then a code. The code test ran
    // first, so the three-letter name "Lao" was returned as the code "lao",
    // which the speech model does not know (its code is "lo").
    const auto named = map.constFind(d);
    if (named != map.cend()) return named.value();
    // If already a code (two-letter or three-letter) return as-is
    static const QRegularExpression kLangCode(QStringLiteral("^[a-z]{2,3}$"));
    if (d.size() <= 3 && kLangCode.match(d).hasMatch()) return d;
    return QStringLiteral("en");
}

/**
 * @brief The set of language codes that have a localized voice-command catalogue.
 *
 * Item 4 / F-9 (FCR Pass 10 Feedback): One canonical home for the set of language
 * codes that ship a :/Voice Commands/<code>.json resource. The note in settingspage
 * reads from this set so it and the loader can never disagree, including for
 * Brazilian Portuguese (which resolves to "pt" and therefore has a catalogue).
 *
 * Keyed by RESOLVED language code, not display name.
 */
inline const QSet<QString>& voiceCommandLanguageCodes()
{
    static const QSet<QString> codes = {
        QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("fr"),
        QStringLiteral("pt"), QStringLiteral("de"), QStringLiteral("it")
    };
    return codes;
}

/**
 * @brief Returns true if the resolved language code has a localized voice-command catalogue.
 * @param languageCode Resolved ISO code (e.g. "pt" for both Portuguese and Brazilian Portuguese)
 */
inline bool hasLocalizedVoiceCommands(const QString& languageCode)
{
    return voiceCommandLanguageCodes().contains(languageCode.trimmed().toLower());
}

/**
 * @brief Which catalogue the voice-command processor should load.
 *
 * FCR Pass 8 item D. Only six languages ship a :/Voice Commands/<code>.json
 * catalogue. On any other recognition language the processor was given a code
 * with no catalogue, so nothing matched and the spoken command was inserted
 * into the document as dictated text. VoiceCommandProcessor::setLanguage()
 * does not reject an unknown code -- it stores it and falls back to English
 * internally -- so the failure was silent.
 *
 * Commands are spoken by a person reading the interface, so the application
 * display language is the correct second choice.
 *
 * Order: the recognition language if it has a catalogue; then the display
 * language if it has one; then English. Both parameters are stable English
 * storage keys drawn from the same set, so one lookup table resolves both.
 *
 * This rule lives beside the catalogue list it depends on, so adding a seventh
 * catalogue is one edit in one place.
 *
 * @param dictationDisplayKey   Recognition language storage key (e.g. "German")
 * @param applicationDisplayKey Interface language storage key (e.g. "French")
 */
inline QString voiceCommandLanguageCode(const QString& dictationDisplayKey,
                                        const QString& applicationDisplayKey)
{
    const QString dictationCode = languageCodeFromDisplay(dictationDisplayKey);
    if (hasLocalizedVoiceCommands(dictationCode))
        return dictationCode;

    const QString appCode = languageCodeFromDisplay(applicationDisplayKey);
    if (hasLocalizedVoiceCommands(appCode))
        return appCode;

    return QStringLiteral("en");
}


/**
 * @brief The six curated primary dictation languages, in fixed display order.
 *
 * These mirror the six application UI languages and are pinned to the top of
 * the dictation language selector. The value persisted in settings is always
 * the English key below, so a stored selection survives a UI language change.
 */
inline const QStringList& primaryDictationLanguages()
{
    static const QStringList keys = {
        QStringLiteral("English"), QStringLiteral("Spanish"), QStringLiteral("French"),
        QStringLiteral("Portuguese"), QStringLiteral("German"), QStringLiteral("Italian")
    };
    return keys;
}

/**
 * @brief Every remaining selectable dictation language, de-duplicated.
 *
 * Session 386: the previous inline literal in settingspage listed Korean,
 * Norwegian, Polish, Russian and Swedish twice, so each appeared twice in the
 * dropdown. This list is the single de-duplicated source of truth.
 *
 * Session 596 review, B-17: every entry is a language the speech model can
 * recognise, under the code it knows. Zulu, Kurdish and Hmong are withdrawn
 * (the model has none of them, so choosing one gave text in another language or
 * none), and Brazilian Portuguese is not listed beside Portuguese: it was the
 * same engine language under a second name. Combined with
 * primaryDictationLanguages() the application offers 82 distinct languages --
 * "80+" wherever the figure is published.
 */
inline const QStringList& additionalDictationLanguages()
{
    static const QStringList keys = {
        QStringLiteral("Albanian"), QStringLiteral("Amharic"), QStringLiteral("Arabic"),
        QStringLiteral("Armenian"), QStringLiteral("Assamese"), QStringLiteral("Azerbaijani"),
        QStringLiteral("Basque"), QStringLiteral("Belarusian"), QStringLiteral("Bengali"),
        QStringLiteral("Bosnian"), QStringLiteral("Bulgarian"),
        QStringLiteral("Burmese"), QStringLiteral("Catalan"), QStringLiteral("Chinese"),
        QStringLiteral("Croatian"), QStringLiteral("Czech"), QStringLiteral("Danish"),
        QStringLiteral("Dutch"), QStringLiteral("Estonian"), QStringLiteral("Farsi"),
        QStringLiteral("Finnish"), QStringLiteral("Galician"), QStringLiteral("Georgian"),
        QStringLiteral("Greek"), QStringLiteral("Gujarati"), QStringLiteral("HaitianCreole"),
        QStringLiteral("Hebrew"), QStringLiteral("Hindi"),
        QStringLiteral("Hungarian"), QStringLiteral("Icelandic"), QStringLiteral("Indonesian"),
        QStringLiteral("Japanese"), QStringLiteral("Javanese"), QStringLiteral("Kannada"),
        QStringLiteral("Kazakh"), QStringLiteral("Khmers"), QStringLiteral("Korean"),
        QStringLiteral("Lao"), QStringLiteral("Latvian"),
        QStringLiteral("Lithuanian"), QStringLiteral("Luxembourgish"), QStringLiteral("Macedonian"),
        QStringLiteral("Malay"), QStringLiteral("Malayalam"), QStringLiteral("Maltese"),
        QStringLiteral("Marathi"), QStringLiteral("Mongolian"), QStringLiteral("Norwegian"),
        QStringLiteral("Pashto"), QStringLiteral("Polish"), QStringLiteral("Punjabi"),
        QStringLiteral("Romanian"), QStringLiteral("Russian"), QStringLiteral("Serbian"),
        QStringLiteral("Shona"), QStringLiteral("Sindhi"), QStringLiteral("Sinhala"),
        QStringLiteral("Slovak"), QStringLiteral("Slovenian"), QStringLiteral("Somali"),
        QStringLiteral("Swahili"), QStringLiteral("Swedish"), QStringLiteral("Tagalog"),
        QStringLiteral("Tajik"), QStringLiteral("Tamil"), QStringLiteral("Telugu"),
        QStringLiteral("Thai"), QStringLiteral("Turkish"), QStringLiteral("Turkmen"),
        QStringLiteral("Ukrainian"), QStringLiteral("Urdu"), QStringLiteral("Uzbek"),
        QStringLiteral("Vietnamese"), QStringLiteral("Welsh"), QStringLiteral("Yiddish"),
        QStringLiteral("Yoruba")
    };
    return keys;
}

/**
 * @brief ISO codes for every selectable dictation language.
 *
 * Used by SpeechRecognitionManager::isLanguageSupported. Built from the same
 * primary + additional display-key lists so the gate cannot drift from the UI.
 */
inline const QSet<QString>& supportedDictationLanguageCodes()
{
    static const QSet<QString> codes = []() {
        QSet<QString> out;
        for (const QString& key : primaryDictationLanguages())
            out.insert(languageCodeFromDisplay(key));
        for (const QString& key : additionalDictationLanguages())
            out.insert(languageCodeFromDisplay(key));
        return out;
    }();
    return codes;
}

/**
 * @brief All supported languages as (Display Name, Code) pairs, sorted alphabetically.
 */
inline QList<QPair<QString, QString>> allSupportedLanguagesSorted()
{
    QStringList allKeys = primaryDictationLanguages();
    allKeys += additionalDictationLanguages();
    QList<QPair<QString, QString>> list;
    QSet<QString> seenCodes;
    for (const QString& key : allKeys) {
        const QString code = languageCodeFromDisplay(key);
        if (!seenCodes.contains(code)) {
            seenCodes.insert(code);
            list.append({ displayFromLanguageCode(code), code });
        }
    }
    QCollator collator{QLocale()};
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const QPair<QString, QString>& a, const QPair<QString, QString>& b) {
        return collator.compare(a.first, b.first) < 0;
    });
    return list;
}

/**
 * @brief English display text for a stored language key.
 *
 * Two keys are camelCase compounds for settings round-trip compatibility and
 * must not be shown to the user in that form.
 */
inline QString languageDisplaySource(const QString& key)
{
    if (key == QLatin1String("BrazilianPortuguese"))
        return QStringLiteral("Brazilian Portuguese");
    if (key == QLatin1String("HaitianCreole"))
        return QStringLiteral("Haitian Creole");
    // S386: the storage key misspells the language. Renaming the key would need
    // a settings migration for anyone who already selected it, so the key stays
    // as-is and only the user-visible name is corrected.
    if (key == QLatin1String("Khmers"))
        return QStringLiteral("Khmer");
    return key;
}

/**
 * @brief lupdate anchor for every translatable language display name.
 *
 * localizedLanguageName() translates by runtime key, which lupdate cannot
 * follow. Listing the literals here under the "LanguageNames" context makes
 * every name extractable, so `lupdate` keeps the catalogue complete.
 *
 * @param count Receives the number of names in the returned array.
 */
inline const char* const* translatableLanguageNames(int& count)
{
    static const char* const kNames[] = {
        QT_TRANSLATE_NOOP("LanguageNames", "English"), QT_TRANSLATE_NOOP("LanguageNames", "Spanish"),
        QT_TRANSLATE_NOOP("LanguageNames", "French"), QT_TRANSLATE_NOOP("LanguageNames", "Portuguese"),
        QT_TRANSLATE_NOOP("LanguageNames", "German"), QT_TRANSLATE_NOOP("LanguageNames", "Italian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Albanian"), QT_TRANSLATE_NOOP("LanguageNames", "Amharic"),
        QT_TRANSLATE_NOOP("LanguageNames", "Arabic"), QT_TRANSLATE_NOOP("LanguageNames", "Armenian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Assamese"), QT_TRANSLATE_NOOP("LanguageNames", "Azerbaijani"),
        QT_TRANSLATE_NOOP("LanguageNames", "Basque"), QT_TRANSLATE_NOOP("LanguageNames", "Belarusian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Bengali"), QT_TRANSLATE_NOOP("LanguageNames", "Bosnian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Brazilian Portuguese"), QT_TRANSLATE_NOOP("LanguageNames", "Bulgarian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Burmese"), QT_TRANSLATE_NOOP("LanguageNames", "Catalan"),
        QT_TRANSLATE_NOOP("LanguageNames", "Chinese"), QT_TRANSLATE_NOOP("LanguageNames", "Croatian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Czech"), QT_TRANSLATE_NOOP("LanguageNames", "Danish"),
        QT_TRANSLATE_NOOP("LanguageNames", "Dutch"), QT_TRANSLATE_NOOP("LanguageNames", "Estonian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Farsi"), QT_TRANSLATE_NOOP("LanguageNames", "Finnish"),
        QT_TRANSLATE_NOOP("LanguageNames", "Galician"), QT_TRANSLATE_NOOP("LanguageNames", "Georgian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Greek"), QT_TRANSLATE_NOOP("LanguageNames", "Gujarati"),
        QT_TRANSLATE_NOOP("LanguageNames", "Haitian Creole"), QT_TRANSLATE_NOOP("LanguageNames", "Hebrew"),
        QT_TRANSLATE_NOOP("LanguageNames", "Hindi"), QT_TRANSLATE_NOOP("LanguageNames", "Hmong"),
        QT_TRANSLATE_NOOP("LanguageNames", "Hungarian"), QT_TRANSLATE_NOOP("LanguageNames", "Icelandic"),
        QT_TRANSLATE_NOOP("LanguageNames", "Indonesian"), QT_TRANSLATE_NOOP("LanguageNames", "Japanese"),
        QT_TRANSLATE_NOOP("LanguageNames", "Javanese"), QT_TRANSLATE_NOOP("LanguageNames", "Kannada"),
        QT_TRANSLATE_NOOP("LanguageNames", "Kazakh"), QT_TRANSLATE_NOOP("LanguageNames", "Khmer"),
        QT_TRANSLATE_NOOP("LanguageNames", "Korean"), QT_TRANSLATE_NOOP("LanguageNames", "Kurdish"),
        QT_TRANSLATE_NOOP("LanguageNames", "Lao"), QT_TRANSLATE_NOOP("LanguageNames", "Latvian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Lithuanian"), QT_TRANSLATE_NOOP("LanguageNames", "Luxembourgish"),
        QT_TRANSLATE_NOOP("LanguageNames", "Macedonian"), QT_TRANSLATE_NOOP("LanguageNames", "Malay"),
        QT_TRANSLATE_NOOP("LanguageNames", "Malayalam"), QT_TRANSLATE_NOOP("LanguageNames", "Maltese"),
        QT_TRANSLATE_NOOP("LanguageNames", "Marathi"), QT_TRANSLATE_NOOP("LanguageNames", "Mongolian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Norwegian"), QT_TRANSLATE_NOOP("LanguageNames", "Pashto"),
        QT_TRANSLATE_NOOP("LanguageNames", "Polish"), QT_TRANSLATE_NOOP("LanguageNames", "Punjabi"),
        QT_TRANSLATE_NOOP("LanguageNames", "Romanian"), QT_TRANSLATE_NOOP("LanguageNames", "Russian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Serbian"), QT_TRANSLATE_NOOP("LanguageNames", "Shona"),
        QT_TRANSLATE_NOOP("LanguageNames", "Sindhi"), QT_TRANSLATE_NOOP("LanguageNames", "Sinhala"),
        QT_TRANSLATE_NOOP("LanguageNames", "Slovak"), QT_TRANSLATE_NOOP("LanguageNames", "Slovenian"),
        QT_TRANSLATE_NOOP("LanguageNames", "Somali"), QT_TRANSLATE_NOOP("LanguageNames", "Swahili"),
        QT_TRANSLATE_NOOP("LanguageNames", "Swedish"), QT_TRANSLATE_NOOP("LanguageNames", "Tagalog"),
        QT_TRANSLATE_NOOP("LanguageNames", "Tajik"), QT_TRANSLATE_NOOP("LanguageNames", "Tamil"),
        QT_TRANSLATE_NOOP("LanguageNames", "Telugu"), QT_TRANSLATE_NOOP("LanguageNames", "Thai"),
        QT_TRANSLATE_NOOP("LanguageNames", "Turkish"), QT_TRANSLATE_NOOP("LanguageNames", "Turkmen"),
        QT_TRANSLATE_NOOP("LanguageNames", "Ukrainian"), QT_TRANSLATE_NOOP("LanguageNames", "Urdu"),
        QT_TRANSLATE_NOOP("LanguageNames", "Uzbek"), QT_TRANSLATE_NOOP("LanguageNames", "Vietnamese"),
        QT_TRANSLATE_NOOP("LanguageNames", "Welsh"), QT_TRANSLATE_NOOP("LanguageNames", "Yiddish"),
        QT_TRANSLATE_NOOP("LanguageNames", "Yoruba"), QT_TRANSLATE_NOOP("LanguageNames", "Zulu")
    };
    count = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));
    return kNames;
}

/**
 * @brief Translated display name for a stored language key.
 *
 * Falls back to the English name when the active catalogue has no entry, which
 * is the standard QCoreApplication::translate() behaviour.
 */
inline QString localizedLanguageName(const QString& key)
{
    const QByteArray source = languageDisplaySource(key).toUtf8();
    return QCoreApplication::translate("LanguageNames", source.constData());
}

/**
 * @brief The language's name in that language ("Deutsch"), or empty when the
 * locale database does not know it.
 *
 * Session 596 review, U-4. The first letter is raised, as a list entry starts
 * ("français" -> "Français"); scripts without case are unchanged.
 */
inline QString nativeLanguageName(const QString& key)
{
    QString code = languageCodeFromDisplay(key);
    if (code == QLatin1String("jw"))
        code = QStringLiteral("jv");   // the speech model's code; the locale database uses ISO's
    const QLocale locale(code);
    if (locale.language() == QLocale::C || locale.language() == QLocale::AnyLanguage)
        return QString();
    // The locale database names English by its default territory ("American
    // English"); the language itself is "English".
    QString native = code == QLatin1String("en") ? QStringLiteral("English") : locale.nativeLanguageName();
    if (!native.isEmpty())
        native[0] = native.at(0).toUpper();
    return native;
}

/**
 * @brief How a language is shown in a language list: its own name, then its
 * name in the interface language -- "Deutsch (Allemand)" in French.
 *
 * Session 596 review, U-4: the lists named every language only in the interface
 * language, so a customer who had switched to a language they cannot read could
 * not find their own. One name when the two are the same ("Français" in French)
 * or the locale database has no native name.
 */
inline QString selectorLanguageName(const QString& key)
{
    const QString localized = localizedLanguageName(key);
    const QString native = nativeLanguageName(key);
    if (native.isEmpty() || native.compare(localized, Qt::CaseInsensitive) == 0)
        return localized;
    return QStringLiteral("%1 (%2)").arg(native, localized);
}

/**
 * @brief Dictation languages that were offered and are withdrawn because the
 * speech model cannot recognise them (Session 596 review, B-17).
 */
inline const QStringList& withdrawnDictationLanguages()
{
    static const QStringList keys = {
        QStringLiteral("Zulu"), QStringLiteral("Kurdish"), QStringLiteral("Hmong")
    };
    return keys;
}

/**
 * @brief The dictation language to use for a stored value from an earlier
 * version (Session 596 review, B-17).
 *
 * Brazilian Portuguese becomes Portuguese -- the same engine language, so
 * nothing changes for the customer. A withdrawn language becomes English and
 * sets `withdrawn`, so the customer can be told once. Anything else is returned
 * unchanged.
 */
inline QString migratedDictationLanguage(const QString& storedValue, bool* withdrawn = nullptr)
{
    if (withdrawn)
        *withdrawn = false;
    const QString value = storedValue.trimmed();
    if (value.compare(QLatin1String("BrazilianPortuguese"), Qt::CaseInsensitive) == 0
        || value.compare(QLatin1String("Brazilian Portuguese"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("Portuguese");
    for (const QString& key : withdrawnDictationLanguages()) {
        if (value.compare(key, Qt::CaseInsensitive) == 0) {
            if (withdrawn)
                *withdrawn = true;
            return QStringLiteral("English");
        }
    }
    return storedValue;
}

/**
 * @brief Additional language keys ordered by their translated display name.
 *
 * Alphabetical order in English is meaningless once the names are translated,
 * so the list is collated in the active locale each time it is built.
 *
 * Session 596 review, U-4: by the name as the list shows it (the language's own
 * name first), so the visible order is alphabetical.
 */
inline QStringList additionalLanguagesSortedForDisplay()
{
    QStringList keys = additionalDictationLanguages();
    QCollator collator{QLocale()};
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(keys.begin(), keys.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(selectorLanguageName(a), selectorLanguageName(b)) < 0;
    });
    return keys;
}


/**
 * @brief Resolve a persisted dictation-language value to a canonical English key.
 *
 * Builds before Session 154 stored the combo's display text rather than the
 * stable key, so a settings file in the field may hold "Brazilian Portuguese"
 * (with a space) instead of "BrazilianPortuguese". Display text is translated
 * from Session 386 onwards, so QComboBox::findText() can no longer recover
 * those values in a non-English UI. Matching is done against the keys first,
 * then their English display text, case-insensitively.
 *
 * @return The canonical key, or an empty string when nothing matches.
 */
inline QString canonicalLanguageKey(const QString& storedValue)
{
    // Session 596 review, B-17: values from earlier versions first (Brazilian
    // Portuguese is Portuguese; a withdrawn language is English).
    const QString value = migratedDictationLanguage(storedValue).trimmed();
    if (value.isEmpty())
        return QString();

    QStringList all = primaryDictationLanguages();
    all += additionalDictationLanguages();

    for (const QString& key : all) {
        if (key.compare(value, Qt::CaseInsensitive) == 0)
            return key;
    }
    for (const QString& key : all) {
        if (languageDisplaySource(key).compare(value, Qt::CaseInsensitive) == 0)
            return key;
    }
    return QString();
}

} // namespace LanguageUtils

#endif // LANGUAGEUTILS_H

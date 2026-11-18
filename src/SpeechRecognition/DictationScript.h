#pragma once

#include <QChar>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include "Utils/LanguageUtils.h"

// Which script a dictated language is written in, and what to do when the speech
// model answers in another one.
//
// Session 624 Pass 9 review, SH-2 (Session 625): on the base model Hindi, Marathi
// and Nepali are sometimes written in Arabic (Urdu) script, and the English prompt
// the engine carries from slice to slice kept pulling the next slice the same way.
// For the 26 non-Latin languages the engine now (1) decodes without the rolling
// prompt, (2) gives the three Devanagari languages a one-sentence prompt in their
// own script, (3) retries once with a wider search when the text comes back in the
// wrong script, and (4) removes only the letters that are in a different non-Latin
// script, so Latin words, numbers, punctuation, joiners and line breaks all stay.
// The functions are pure and work on whole code points, so they are exercised
// without a speech model.
namespace DictationScript {

enum class Script {
    Latin, Common, Devanagari, Bengali, Gurmukhi, Gujarati, Tamil, Telugu, Kannada,
    Malayalam, Sinhala, Thai, Lao, Myanmar, Georgian, Ethiopic, Khmer, Hebrew, Arabic,
    Han, Hiragana, Katakana, Hangul, Bopomofo, Other
};

struct Range { char32_t first; char32_t last; Script script; };

// Unicode blocks, sorted by first code point for the lookup below. Everything not
// listed is Common (marks and symbols that belong to no one script) or, for a
// letter, Other (Greek, Cyrillic, and so on).
inline const QVector<Range>& scriptRanges()
{
    static const QVector<Range> r = {
        { 0x0041, 0x024F, Script::Latin },      // Basic Latin letters, Latin-1, Extended A and B
        { 0x0250, 0x02AF, Script::Latin },      // IPA
        { 0x0590, 0x05FF, Script::Hebrew },
        { 0x0600, 0x06FF, Script::Arabic },
        { 0x0750, 0x077F, Script::Arabic },
        { 0x08A0, 0x08FF, Script::Arabic },
        { 0x0900, 0x097F, Script::Devanagari },
        { 0x0980, 0x09FF, Script::Bengali },
        { 0x0A00, 0x0A7F, Script::Gurmukhi },
        { 0x0A80, 0x0AFF, Script::Gujarati },
        { 0x0B80, 0x0BFF, Script::Tamil },
        { 0x0C00, 0x0C7F, Script::Telugu },
        { 0x0C80, 0x0CFF, Script::Kannada },
        { 0x0D00, 0x0D7F, Script::Malayalam },
        { 0x0D80, 0x0DFF, Script::Sinhala },
        { 0x0E00, 0x0E7F, Script::Thai },
        { 0x0E80, 0x0EFF, Script::Lao },
        { 0x1000, 0x109F, Script::Myanmar },
        { 0x10A0, 0x10FF, Script::Georgian },
        { 0x1100, 0x11FF, Script::Hangul },
        { 0x1200, 0x139F, Script::Ethiopic },
        { 0x1780, 0x17FF, Script::Khmer },
        { 0x1E00, 0x1EFF, Script::Latin },      // Latin Extended Additional
        { 0x2C60, 0x2C7F, Script::Latin },
        { 0x2D00, 0x2D2F, Script::Georgian },
        { 0x2D80, 0x2DDF, Script::Ethiopic },
        { 0x3040, 0x309F, Script::Hiragana },
        { 0x30A0, 0x30FF, Script::Katakana },
        { 0x3100, 0x312F, Script::Bopomofo },
        { 0x3130, 0x318F, Script::Hangul },
        { 0x31F0, 0x31FF, Script::Katakana },
        { 0x3400, 0x4DBF, Script::Han },
        { 0x4E00, 0x9FFF, Script::Han },
        { 0xA720, 0xA7FF, Script::Latin },
        { 0xA8E0, 0xA8FF, Script::Devanagari },
        { 0xAC00, 0xD7AF, Script::Hangul },
        { 0xF900, 0xFAFF, Script::Han },
        { 0xFB1D, 0xFB4F, Script::Hebrew },
        { 0xFB50, 0xFDFF, Script::Arabic },
        { 0xFE70, 0xFEFF, Script::Arabic },
        { 0xFF21, 0xFF3A, Script::Latin },      // fullwidth Latin
        { 0xFF41, 0xFF5A, Script::Latin },
        { 0xFF66, 0xFF9F, Script::Katakana },   // halfwidth katakana
        { 0xFFA0, 0xFFDC, Script::Hangul },     // halfwidth hangul
        { 0x20000, 0x2FA1F, Script::Han },      // CJK extensions B to F and compatibility
        { 0x30000, 0x3134F, Script::Han },
    };
    return r;
}

inline bool isMark(QChar::Category c)
{
    return c == QChar::Mark_NonSpacing || c == QChar::Mark_SpacingCombining || c == QChar::Mark_Enclosing;
}

inline bool isLetter(QChar::Category c)
{
    return c == QChar::Letter_Uppercase || c == QChar::Letter_Lowercase || c == QChar::Letter_Titlecase
        || c == QChar::Letter_Modifier || c == QChar::Letter_Other;
}

// The script of one code point. A letter outside every listed block is Other; any
// other character outside them (punctuation, symbols, digits, joiners, combining
// marks of no listed block) is Common.
inline Script scriptOf(char32_t cp)
{
    for (const Range& r : scriptRanges()) {
        if (cp >= r.first && cp <= r.last) {
            // Punctuation, symbols and digits inside a block (the danda, Arabic comma,
            // CJK brackets, digits) belong to no one script.
            const QChar::Category c = QChar::category(cp);
            if (isLetter(c) || isMark(c))
                return r.script;
            return Script::Common;
        }
    }
    return isLetter(QChar::category(cp)) ? Script::Other : Script::Common;
}

// The scripts a language is written in. Empty for a language that is not in the
// 26-language non-Latin set (Latin-script languages are never filtered).
inline QVector<Script> scriptsForLanguage(const QString& code)
{
    static const QHash<QString, QVector<Script>> m = {
        { QStringLiteral("hi"), { Script::Devanagari } },
        { QStringLiteral("mr"), { Script::Devanagari } },
        { QStringLiteral("ne"), { Script::Devanagari } },
        { QStringLiteral("ja"), { Script::Han, Script::Hiragana, Script::Katakana } },
        { QStringLiteral("zh"), { Script::Han, Script::Bopomofo } },
        { QStringLiteral("ko"), { Script::Hangul, Script::Han } },
        { QStringLiteral("th"), { Script::Thai } },
        { QStringLiteral("he"), { Script::Hebrew } },
        { QStringLiteral("yi"), { Script::Hebrew } },
        { QStringLiteral("ar"), { Script::Arabic } },
        { QStringLiteral("fa"), { Script::Arabic } },
        { QStringLiteral("ur"), { Script::Arabic } },
        { QStringLiteral("ps"), { Script::Arabic } },
        { QStringLiteral("bn"), { Script::Bengali } },
        { QStringLiteral("gu"), { Script::Gujarati } },
        { QStringLiteral("pa"), { Script::Gurmukhi } },
        { QStringLiteral("ta"), { Script::Tamil } },
        { QStringLiteral("te"), { Script::Telugu } },
        { QStringLiteral("kn"), { Script::Kannada } },
        { QStringLiteral("ml"), { Script::Malayalam } },
        { QStringLiteral("km"), { Script::Khmer } },
        { QStringLiteral("lo"), { Script::Lao } },
        { QStringLiteral("my"), { Script::Myanmar } },
        { QStringLiteral("si"), { Script::Sinhala } },
        { QStringLiteral("ka"), { Script::Georgian } },
        { QStringLiteral("am"), { Script::Ethiopic } },
    };
    return m.value(code.toLower());
}

// A one-sentence prompt in the language's own script. It tells the model which
// script to answer in and nothing else. UTF-8 is what whisper.cpp expects; the text
// is written with \u escapes so no source encoding can change it.
inline QString initialPromptFor(const QString& code)
{
    const QString c = code.toLower();
    if (c == QLatin1String("hi")) return QStringLiteral("\u092F\u0939 \u0939\u093F\u0902\u0926\u0940 \u092E\u0947\u0902 \u0932\u093F\u0916\u093E \u0917\u092F\u093E \u0935\u093E\u0915\u094D\u092F \u0939\u0948\u0964");
    if (c == QLatin1String("mr")) return QStringLiteral("\u0939\u0947 \u092E\u0930\u093E\u0920\u0940\u092E\u0927\u094D\u092F\u0947 \u0932\u093F\u0939\u093F\u0932\u0947\u0932\u0947 \u0935\u093E\u0915\u094D\u092F \u0906\u0939\u0947.");
    if (c == QLatin1String("ne")) return QStringLiteral("\u092F\u094B \u0928\u0947\u092A\u093E\u0932\u0940\u092E\u093E \u0932\u0947\u0916\u093F\u090F\u0915\u094B \u0935\u093E\u0915\u094D\u092F \u0939\u094B\u0964");
    return QString();
}

// The non-Latin languages that deliberately get no prompt: their script is not the
// one the base model confuses, so the model alone decides. Every language in
// nonLatinLanguageCodes() has a prompt or is in this list; a test holds that.
inline const QSet<QString>& languagesWithoutPrompt()
{
    static const QSet<QString> s = {
        QStringLiteral("ja"), QStringLiteral("zh"), QStringLiteral("ko"), QStringLiteral("th"),
        QStringLiteral("he"), QStringLiteral("yi"), QStringLiteral("ar"), QStringLiteral("fa"),
        QStringLiteral("ur"), QStringLiteral("ps"), QStringLiteral("bn"), QStringLiteral("gu"),
        QStringLiteral("pa"), QStringLiteral("ta"), QStringLiteral("te"), QStringLiteral("kn"),
        QStringLiteral("ml"), QStringLiteral("km"), QStringLiteral("lo"), QStringLiteral("my"),
        QStringLiteral("si"), QStringLiteral("ka"), QStringLiteral("am"),
    };
    return s;
}

// True for a letter (or a mark that belongs to a script) that is in a non-Latin
// script other than the language's own. Latin letters, digits, punctuation,
// symbols, joiners and whitespace are never wrong.
inline bool isWrongScript(char32_t cp, const QVector<Script>& allowed)
{
    const Script s = scriptOf(cp);
    if (s == Script::Latin || s == Script::Common)
        return false;
    return !allowed.contains(s);
}

inline bool containsWrongScript(const QString& text, const QString& code)
{
    const QVector<Script> allowed = scriptsForLanguage(code);
    if (allowed.isEmpty())
        return false;
    const QVector<uint> cps = text.toUcs4();
    for (const uint cp : cps) {
        if (isLetter(QChar::category(cp)) && isWrongScript(cp, allowed))
            return true;
    }
    return false;
}

// Remove only the letters in a different non-Latin script, and the marks that were
// attached to a letter just removed (they have lost their base). Everything else
// is returned exactly as it came: punctuation, brackets and quotation marks, the
// zero-width joiner and non-joiner Indic spelling needs, digits, symbols, line
// breaks. Works on whole code points, so a character outside the basic plane is not
// split. A space left doubled by a removal is closed up.
inline QString enforce(const QString& text, const QString& code, bool* removedAny = nullptr)
{
    if (removedAny)
        *removedAny = false;
    const QVector<Script> allowed = scriptsForLanguage(code);
    if (allowed.isEmpty() || text.isEmpty())
        return text;

    const QVector<uint> cps = text.toUcs4();
    QString out;
    out.reserve(text.size());
    bool previousRemoved = false;
    bool removed = false;
    for (const uint cp : cps) {
        const QChar::Category cat = QChar::category(cp);
        const bool drop = (isLetter(cat) && isWrongScript(cp, allowed))
                       || (isMark(cat) && previousRemoved && isWrongScript(cp, allowed));
        if (drop) {
            previousRemoved = true;
            removed = true;
            continue;
        }
        // A space that would sit next to a space only because text between them was
        // removed.
        if (previousRemoved && cp == 0x20 && out.endsWith(QLatin1Char(' '))) {
            previousRemoved = false;
            continue;
        }
        previousRemoved = false;
        if (QChar::requiresSurrogates(cp)) {
            out.append(QChar(QChar::highSurrogate(cp)));
            out.append(QChar(QChar::lowSurrogate(cp)));
        } else {
            out.append(QChar(static_cast<char16_t>(cp)));
        }
    }
    if (removedAny)
        *removedAny = removed;
    return removed ? out : text;
}

// True when the text still holds a letter or a number. Used to tell "some wrong
// letters were taken out" from "nothing usable is left".
inline bool hasLetterOrNumber(const QString& text)
{
    const QVector<uint> cps = text.toUcs4();
    for (const uint cp : cps) {
        const QChar::Category c = QChar::category(cp);
        if (isLetter(c) || c == QChar::Number_DecimalDigit || c == QChar::Number_Letter || c == QChar::Number_Other)
            return true;
    }
    return false;
}

} // namespace DictationScript

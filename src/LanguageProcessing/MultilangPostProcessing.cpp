/**
 * @file MultilangPostProcessing.cpp
 * @brief Implementation of multilingual post-processing
 *
 */

#include "LanguageProcessing/MultilangPostProcessing.h"
#include "LanguageProcessing/OptimizedPostProcessor.h"
#include <QDebug>

QString applyMultilingualCommaHeuristics(const QString& input, const QString& language,
                                         bool atSentenceStart)
{
    QString result = input;

    // Cache per-language regex to avoid rebuilding complex alternation patterns on every utterance
    struct CachedPatterns {
        QRegularExpression introRegex;
        QRegularExpression connectorRegex;
    };
    static QHash<QString, CachedPatterns> s_patternCache;
    if (!s_patternCache.contains(language)) {
        CachedPatterns p;
        p.introRegex     = LanguageRulesManager::instance().createDiscourseMarkerRegex(language);
        p.connectorRegex = LanguageRulesManager::instance().createConnectorRegex(language);
        s_patternCache.insert(language, p);
    }
    const CachedPatterns& cached = s_patternCache[language];

    // Apply discourse markers -- only at a sentence start (Session 596 review, B-31).
    if (atSentenceStart) {
        const QRegularExpression& introRegex = cached.introRegex;
        result.replace(introRegex, "\\1, ");
    }

    // Apply connectors
    const QRegularExpression& connectorRegex = cached.connectorRegex;

    qsizetype searchFrom = 0;
    while (true) {
        QRegularExpressionMatch match = connectorRegex.match(result, searchFrom);
        if (!match.hasMatch()) break;

        qsizetype wordStart = match.capturedStart(1);
        if (wordStart > 0) {
            bool hasPunctNearby = false;
            for (qsizetype p = wordStart - 1; p >= 0 && p >= wordStart - 3; --p) {
                const QChar ch = result.at(p);
                if (ch == ',' || ch == ';' || ch == '.' || ch == '?' || ch == '!') {
                    hasPunctNearby = true;
                    break;
                }
            }
            if (!hasPunctNearby && !connectorIsPartOfPhrase(result, wordStart, language)) {
                // Session 579 review (found in review): the comma goes straight
                // after the previous word, before the space -- "late, but" -- not
                // in front of the connector, which wrote "late ,but".
                result.insert(match.capturedStart(0), ',');
                wordStart += 1;
            }
        }

        searchFrom = wordStart + match.capturedLength(1);
    }

    return result;
}

// ==================== MultilingualPunctuationInference ====================

MultilingualPunctuationInference::MultilingualPunctuationInference()
{
    // Patterns are created on-demand and cached
}

QString MultilingualPunctuationInference::apply(const QString& text, const QString& language,
                                                bool atSentenceStart)
{
    QString result = text.trimmed();
    if (result.isEmpty()) {
        return result;
    }

    // Apply language-specific comma heuristics
    result = applyMultilingualCommaHeuristics(result, language, atSentenceStart);

    const bool hasTerminalPunct =
        result.endsWith('.') || result.endsWith('?') || result.endsWith('!');

    if (!hasTerminalPunct) {
        // Get or create question pattern for this language
        if (!m_questionPatterns.contains(language)) {
            m_questionPatterns[language] =
                LanguageRulesManager::instance().createQuestionRegex(language);
        }

        // Conservative '?' addition
        if (m_questionPatterns[language].match(result).hasMatch()) {
            static const QRegularExpression shortTail("\\b\\w{1,2}$", QRegularExpression::UseUnicodePropertiesOption);
            if (result.length() >= 8 && !shortTail.match(result).hasMatch()) {
                result += "?";
                return result;
            }
        }

        // Get or create trailing connector pattern
        if (!m_trailingPatterns.contains(language)) {
            m_trailingPatterns[language] =
                LanguageRulesManager::instance().createTrailingConnectorRegex(language);
        }

        static const QRegularExpression trailingPunct("[,:;\\-]$");
        static const QRegularExpression shortTail("\\b\\w{1,2}$", QRegularExpression::UseUnicodePropertiesOption);

        static const QRegularExpression kWs(QStringLiteral("\\s+"));

        // S164-Q1: qsizetype-narrowing fix. QStringList::size() returns qsizetype
        // (signed 64-bit on x64); assigning to int trips MSVC C4244 under /W4 /WX.
        const int tokenCount =
            static_cast<int>(result.split(kWs, Qt::SkipEmptyParts).size());

        const bool looksComplete =
            result.length() >= 20 &&
            tokenCount >= 4 &&
            !m_trailingPatterns[language].match(result).hasMatch() &&
            !trailingPunct.match(result).hasMatch() &&
            !shortTail.match(result).hasMatch();

        if (looksComplete) {
            result += ".";
        }
    }

    return result;
}

// ==================== MultilingualHomophoneCorrector ====================

QString MultilingualHomophoneCorrector::disambiguate(const QString& text, const QString& language)
{
    const LanguageRules& rules = LanguageRulesManager::instance().getRules(language);

    // Simple context-based homophone correction
    // This is a basic implementation - can be enhanced with ML models
    QString result = text;

    for (auto it = rules.homophones.constBegin(); it != rules.homophones.constEnd(); ++it) {
        // PTZ: homophone context analysis — use it.key() and it.value() when
        // disambiguation heuristics are implemented (e.g., "there/their",
        // "your/you're", "haya/halla"). No variables declared here to avoid C4189.
        Q_UNUSED(it)
    }

    return result;
}

// ==================== MultilingualTimeNormalizer ====================

QString MultilingualTimeNormalizer::normalize(const QString& text, const QString& language)
{
    QString result = text;

    // Session 596 review, B-1: the table of word-based time phrases that ran here
    // was removed. It matched inside ordinary words in all six languages (German
    // "Deshalb" -> "Des30 minutes before", English "listen" -> "lis10 minutes")
    // and wrote English phrases into non-English text. Even with word boundaries
    // it would turn "ten to five" into "10 minutes to five" inside a German
    // document, so it has no correct output. The four rules below are anchored on
    // digits and stay: "3 45 pm" -> "3:45 pm", "720pm" -> "7:20 pm", the leading
    // zero, and "3 pm" -> "3:00 pm".
    Q_UNUSED(language);

    // Apply universal time patterns (numeric formats)
    // "3 45 PM" → "3:45 PM"
    static QRegularExpression time1(
        "\\b(\\d{1,2})\\s+(\\d{2})\\s+(am|pm)\\b",
        QRegularExpression::CaseInsensitiveOption);
    result.replace(time1, "\\1:\\2 \\3");

    // "720pm" → "7:20 PM"
    static QRegularExpression time2(
        "\\b(\\d{1,2})(\\d{2})\\s*(am|pm)\\b",
        QRegularExpression::CaseInsensitiveOption);
    result.replace(time2, "\\1:\\2 \\3");

    // Normalize leading zero: "07:20 am" → "7:20 am"
    static QRegularExpression leadingZero(
        "\\b0(\\d):(\\d{2})\\s*(am|pm)\\b",
        QRegularExpression::CaseInsensitiveOption);
    result.replace(leadingZero, "\\1:\\2 \\3");

    // "3 PM" → "3:00 PM" -- but not the minutes of a time that already has its
    // colon: "3:45 pm" stays "3:45 pm" (it used to become "3:45:00 pm").
    static QRegularExpression hourOnly(
        "(?<![:.\\d])\\b(\\d{1,2})\\s+(am|pm)\\b",
        QRegularExpression::CaseInsensitiveOption);
    result.replace(hourOnly, "\\1:00 \\2");

    return result;
}



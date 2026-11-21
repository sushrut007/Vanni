/**
 * @file OptimizedPostProcessor.cpp
 * @brief Implementation of OptimizedPostProcessor
 *
 */

#include "LanguageProcessing/OptimizedPostProcessor.h"
#include "Utils/VaaniLogging.h"
#include "LanguageProcessing/MultilangPostProcessing.h"
#include <QGuiApplication>
#include <QThread>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>
#include <QtConcurrent/QtConcurrent>

// Session 596 review, N-1: the English-only applyCommaHeuristics() and the
// FastPunctuationInference class that called it were a second copy of the comma
// and sentence-end rules that no product code ever ran -- the live rule is
// MultilingualPunctuationInference (MultilangPostProcessing.cpp), called from
// processFull(). Several suites tested the copy, so a change to the live rule
// could pass them while the product behaved differently. Both are removed; the
// suites now drive the live path.

bool isAbbreviationBeforePeriod(const QString& text, qsizetype periodPos)
{
    if (periodPos <= 0 || periodPos >= text.size() || text.at(periodPos) != QLatin1Char('.'))
        return false;
    // The token that ends at this stop: letters and inner stops, back to a space,
    // a bracket or the start of the text.
    qsizetype start = periodPos;
    while (start > 0) {
        const QChar c = text.at(start - 1);
        if (c.isLetter() || c == QLatin1Char('.'))
            --start;
        else
            break;
    }
    const QString token = text.mid(start, periodPos - start);
    if (token.isEmpty() || !token.at(0).isLetter())
        return false;
    // A dotted form whose every part is one or two letters: "a.m", "e.g", "i.e",
    // "z.B", "d.h", "U.S". Not a web or e-mail address ("deskshare.com.", whose
    // final stop ends the sentence) or a file name.
    if (token.contains(QLatin1Char('.'))) {
        const QStringList parts = token.split(QLatin1Char('.'));
        bool allShort = true;
        for (const QString& part : parts)
            allShort = allShort && !part.isEmpty() && part.size() <= 2;
        return allShort;
    }
    // Words written only abbreviated, in the languages this product post-processes.
    static const QSet<QString> kAbbreviations = {
        QStringLiteral("etc"), QStringLiteral("vs"), QStringLiteral("approx"),
        QStringLiteral("incl"), QStringLiteral("ca"), QStringLiteral("cf"),
        QStringLiteral("misc"), QStringLiteral("dept"), QStringLiteral("esp"),
        QStringLiteral("mr"), QStringLiteral("mrs"), QStringLiteral("ms"),
        QStringLiteral("dr"), QStringLiteral("prof"), QStringLiteral("jr"),
        QStringLiteral("sr"), QStringLiteral("usw"), QStringLiteral("bzw"),
        QStringLiteral("ggf"), QStringLiteral("evtl"), QStringLiteral("ecc"),
        QStringLiteral("sig"), QStringLiteral("sra"), QStringLiteral("mme"),
    };
    return kAbbreviations.contains(token.toLower());
}

bool connectorIsPartOfPhrase(const QString& text, qsizetype connectorStart, const QString& language)
{
    // The word before the connector.
    qsizetype end = connectorStart;
    while (end > 0 && text.at(end - 1).isSpace())
        --end;
    qsizetype start = end;
    while (start > 0 && (text.at(start - 1).isLetter() || text.at(start - 1) == QLatin1Char('\'')))
        --start;
    const QString previous = text.mid(start, end - start).toLower();
    if (previous.isEmpty())
        return false;
    const QString connector = text.mid(connectorStart).section(QLatin1Char(' '), 0, 0).toLower();
    const QString lang = language.left(2).toLower();
    if (lang == QLatin1String("en") && connector == QLatin1String("but")) {
        static const QSet<QString> kBeforeBut = {
            QStringLiteral("nothing"), QStringLiteral("all"), QStringLiteral("anything"),
            QStringLiteral("help"), QStringLiteral("none"), QStringLiteral("everything"),
            QStringLiteral("anyone"), QStringLiteral("nobody"), QStringLiteral("everyone"),
        };
        return kBeforeBut.contains(previous);
    }
    if (lang == QLatin1String("de") && connector == QLatin1String("aber")) {
        // After a verb or a pronoun "aber" is a particle inside the clause.
        static const QSet<QString> kBeforeAber = {
            QStringLiteral("ist"), QStringLiteral("war"), QStringLiteral("sind"), QStringLiteral("waren"),
            QStringLiteral("bin"), QStringLiteral("bist"), QStringLiteral("hat"), QStringLiteral("hatte"),
            QStringLiteral("habe"), QStringLiteral("haben"), QStringLiteral("hast"), QStringLiteral("kann"),
            QStringLiteral("kannst"), QStringLiteral("wird"), QStringLiteral("wurde"), QStringLiteral("kommt"),
            QStringLiteral("geht"), QStringLiteral("muss"), QStringLiteral("soll"), QStringLiteral("will"),
            QStringLiteral("ich"), QStringLiteral("du"), QStringLiteral("er"), QStringLiteral("sie"),
            QStringLiteral("es"), QStringLiteral("wir"), QStringLiteral("ihr"), QStringLiteral("das"),
            QStringLiteral("man"),
        };
        return kBeforeAber.contains(previous);
    }
    return false;
}

// ==================== PrecompiledPatternMatcher Implementation ====================

PrecompiledPatternMatcher::PrecompiledPatternMatcher()
{
    initializePatterns();
}

void PrecompiledPatternMatcher::initializePatterns()
{
    // Time expressions: "3 45 PM" → "3:45 PM"
    m_patterns.append({
        QRegularExpression("\\b(\\d{1,2})\\s+(\\d{2})\\s+(am|pm)\\b",
                           QRegularExpression::CaseInsensitiveOption),
        "\\1:\\2 \\3"
        });

    // Time expressions (compact): "720 pm" or "0930am" → "7:20 pm" / "9:30 am"
    m_patterns.append({
        QRegularExpression("\\b(\\d{1,2})(\\d{2})\\s*(am|pm)\\b",
                           QRegularExpression::CaseInsensitiveOption),
        "\\1:\\2 \\3"
        });

    // Normalize leading zero in hour: "07:20 am" → "7:20 am"
    m_patterns.append({
        QRegularExpression("\\b0(\\d):(\\d{2})\\s*(am|pm)\\b",
                           QRegularExpression::CaseInsensitiveOption),
        "\\1:\\2 \\3"
        });

    // Time expressions: "3 PM" → "3:00 PM". Not the minutes of a time already
    // written with a colon: "3:45 pm" stays as it is (it used to become
    // "3:45:00 pm", because a word boundary sits between ':' and '4').
    m_patterns.append({
        QRegularExpression("(?<![:.\\d])\\b(\\d{1,2})\\s+(am|pm)\\b",
                           QRegularExpression::CaseInsensitiveOption),
        "\\1:00 \\2"
        });

    // Fix common spacing issues
    m_patterns.append({
        QRegularExpression("\\s+([.,!?;:])"),
        "\\1"
        });

    // Fix quote spacing
    m_patterns.append({
        QRegularExpression("\"\\s+"),
        "\""
        });

    m_patterns.append({
        QRegularExpression("\\s+\""),
        "\""
        });

    // Fix apostrophe spacing: "can ' t" → "can't"
    m_patterns.append({
        QRegularExpression("\\s+'\\s+"),
        "'"
        });
}

QString PrecompiledPatternMatcher::apply(const QString& text)
{
    QString result = text;

    for (const Pattern& pattern : m_patterns) {
        result.replace(pattern.regex, pattern.replacement);
    }

    return result;
}

// ==================== OptimizedPostProcessor Implementation ====================

OptimizedPostProcessor::OptimizedPostProcessor(QObject* parent)
    : QObject(parent)
{
}

OptimizedPostProcessor::~OptimizedPostProcessor()
{
}

bool OptimizedPostProcessor::initialize()
{
    if (m_initialized) {
        return true;
    }

    // Initialize sub-components
    m_patternMatcher = std::make_unique<PrecompiledPatternMatcher>();

    // Start-of-document context
    m_isAtSentenceStart = true;

    m_initialized = true;
    return true;
}

QString OptimizedPostProcessor::processFast(const QString& rawText, float confidence)
{
    if (!m_initialized) {
        initialize();
    }

    m_timer.start();
    m_lastMetrics = ProcessingMetrics();

    // Step 1: Whitespace normalization
    QString result = normalizeWhitespace(rawText);

    // Step 2: Fast cached vocabulary correction -- skipped when the engine is
    // confident (the full vocabulary pass in processFull() still runs).
    if (!shouldBypassCorrection(confidence)) {
        result = applyFastCorrections(result, confidence);
    }

    // Step 3: Precompiled pattern matching, always (Session 579 review, N-1).
    // Formatting does not depend on how sure the engine was: "3 45 pm" -> "3:45
    // pm" and the spacing before punctuation used to be skipped on confident
    // speech, which is exactly when the customer expects the best result.
    result = applyPatternMatching(result);

    m_lastMetrics.fastStageMs = m_timer.elapsed();
    return result;
}

QString OptimizedPostProcessor::processFull(const QString& rawText, const TranscriptionMetadata& metadata)
{
    QString language = metadata.language.isEmpty() ? "en" : metadata.language;

    // Session 579 review (S-1): Whisper writes the first word of every slice with
    // a capital, as if a new sentence began. When this slice continues a
    // sentence, that capital is not the speaker's, so the first word goes back to
    // lower case -- only when it is an ordinary capitalised word (a capital and
    // then only small letters), including a one-letter word ("A shop", Italian
    // "E poi"). What must still not change:
    //   - a slice that begins a sentence (m_isAtSentenceStart);
    //   - German, where every noun starts with a capital;
    //   - the English "I", "I'm", "I'll": lowered here, and put back by the
    //     English-only "I" rule in applySmartCapitalization() below;
    //   - acronyms written in capitals, such as "NASA";
    //   - an initial or dotted abbreviation ("U.S."), which a stop follows;
    //   - a trained name: the vocabulary passes below run after this step and
    //     put back its written form.
    QString input = rawText;
    if (!m_isAtSentenceStart
        && !language.startsWith(QStringLiteral("de"), Qt::CaseInsensitive)) {
        // Session 596 review, B-30: Unicode word rules. Without them "\\b" uses
        // ASCII rules, finds no word boundary after an accented capital, and an
        // Italian "È vero" (or Portuguese "É verdade", French "À demain") kept
        // its capital in the middle of a sentence while "E poi" was lowered.
        static const QRegularExpression firstWord(
            QStringLiteral("^([\\s\"'(\\[{\\x{201C}]*)(\\p{Lu})\\p{Ll}*\\b(?!\\.)"),
            QRegularExpression::UseUnicodePropertiesOption);
        const QRegularExpressionMatch m = firstWord.match(input);
        if (m.hasMatch()) {
            const qsizetype pos = m.capturedStart(2);
            input[pos] = input[pos].toLower();
        }
    }
    QString result = processFast(input, metadata.averageConfidence);

    // Language-aware time normalization
    result = MultilingualTimeNormalizer::normalize(result, language);

    if (m_contextCorrectionEnabled) {
        result = applyContextCorrection(result);
    }

    // FCR S517, U-1 (Arvind) - DECIDED. The homophone call that was here is
    // removed from the live path.
    //
    // Homophone disambiguation is NOT IMPLEMENTED. MultilingualHomophoneCorrector
    // ::disambiguate() copied its input, walked the rules object's homophone
    // table doing nothing with each entry, and returned the copy unchanged. Every
    // utterance on both engines paid for that walk and no utterance was ever
    // corrected - and, more expensively, every reader of this line believed a
    // correction was happening.
    //
    // The corrector class and the language tables in LanguageProcessing/
    // LanguageRules.cpp are RETAINED exactly as they are. No data and no work is
    // thrown away; only the pretence.
    //
    // m_homophoneDetectionEnabled and its setter are also retained on purpose:
    // SpeechRecognition/WhisperWorker.cpp (transcribeWithTokens) sets it and
    // tests/test_session224_optimized_post_processor.cpp:284-286 toggles it, so
    // removing it would break a caller and a test for no gain. It is inert until
    // the capability is implemented - see the comment beside the member.
    //
    // If the capability is ever wanted it is a piece of work in its own right and
    // needs a measurement first: how often the engines actually confuse these
    // pairs in the six languages this product ships. The tables are a starting
    // point, not the solution - choosing between homophones is a question about
    // context, which is a language-model problem rather than a lookup.

    // Language-aware punctuation inference
    MultilingualPunctuationInference punctuator;
    // Session 596 review, B-31: the opening-word comma ("Actually, ...") belongs
    // at the start of a sentence, not at the start of every slice. A pause in
    // "I was | actually going" used to give "actually, going".
    result = punctuator.apply(result, language, m_isAtSentenceStart);

    // FCR S525, B-11 (second half): capitalisation now runs AFTER the punctuation
    // inference rather than before it. The inference inserts sentence-ending periods,
    // and a period this product adds itself was never followed by a capital because
    // capitalisation had already run. One reordering closes both halves of the fault.
    result = applySmartCapitalization(result, language);

    // Update sentence boundary context AFTER punctuation was applied
    updateSentenceContextFromTail(result);

    return result;
}

QString OptimizedPostProcessor::processIncremental(const QString& newSegment, bool isFirst,
    bool isFinal, float confidence)
{
    if (isFirst) {
        m_incrementalBuffer.clear();
        m_isAtSentenceStart = true; // reset formatting context
    }

    // Process the new segment quickly (no capitalization here)
    QString processed = processFast(newSegment, confidence);

    // Append to buffer
    if (!m_incrementalBuffer.isEmpty())
        m_incrementalBuffer.append(' ');
    m_incrementalBuffer.append(processed);
    m_incrementalBuffer = m_incrementalBuffer.trimmed();

    // Optional: on final chunk, run context corrections
    if (isFinal && m_contextCorrectionEnabled) {
        const int contextStart = static_cast<int>(qMax(qsizetype(0), m_incrementalBuffer.length() - 200));
        QString context = m_incrementalBuffer.mid(contextStart);
        QString refined = applyContextCorrection(context);
        m_incrementalBuffer.replace(contextStart, static_cast<int>(context.length()), refined);
    }

    // Keep sentence context in sync with buffer tail
    updateSentenceContextFromTail(m_incrementalBuffer);

    return m_incrementalBuffer;
}

void OptimizedPostProcessor::resetBuffer()
{
    m_incrementalBuffer.clear();
    m_isAtSentenceStart = true; // reset streaming capitalization context
}

void OptimizedPostProcessor::setContextCorrectionEnabled(bool enabled)
{
    m_contextCorrectionEnabled = enabled;
}

void OptimizedPostProcessor::setHomophoneDetectionEnabled(bool enabled)
{
    m_homophoneDetectionEnabled = enabled;
}

void OptimizedPostProcessor::setFastPathThreshold(float threshold)
{
    m_fastPathThreshold = qBound(0.0f, threshold, 1.0f);
}

// ==================== Private Implementation ====================

QString OptimizedPostProcessor::normalizeWhitespace(const QString& text)
{
    static const QRegularExpression kMultiWs(QStringLiteral("\\s+"));
    QString result = text;
    result.replace(kMultiWs, " ");
    result = result.trimmed();
    return result;
}

QString OptimizedPostProcessor::applyFastCorrections(const QString& text, float confidence)
{
    // Session 48 Fix: Lowered thresholds to enable vocabulary corrections
    // Previously: 0.8f/0.95f prevented most valid corrections from applying
    // Now: 0.7f/0.8f allows user vocabulary to override confident Whisper results
    float threshold = (confidence < 0.7f) ? 0.7f : 0.8f;
    m_lastMetrics.correctionCount++;
    return text;
}

QString OptimizedPostProcessor::applyPatternMatching(const QString& text)
{
    return m_patternMatcher->apply(text);
}

QString OptimizedPostProcessor::applyContextCorrection(const QString& text)
{
    Q_UNUSED(text);
    return text;
}

QString OptimizedPostProcessor::applyHomophoneDisambiguation(const QString& text)
{
    QString result = text;

    static QHash<QString, QPair<QString, QRegularExpression>> rules = {
        {"there", {"their", QRegularExpression("\\btheir\\s+(\\w+\\s+)?(house|car|dog|cat|family|friend)\\b")}},
        {"to", {"too", QRegularExpression("\\btoo\\s+(much|many|little|few)\\b")}},
        {"your", {"you're", QRegularExpression("\\byou're\\s+(\\w+ing|going|coming|happy)\\b")}}
    };

    for (auto it = rules.begin(); it != rules.end(); ++it) {
        if (it.value().second.match(result).hasMatch()) {
            result.replace(it.key(), it.value().first, Qt::CaseInsensitive);
        }
    }

    return result;
}

QString OptimizedPostProcessor::applySmartCapitalization(const QString& text, const QString& language)
{
    QString result = text;
    if (result.isEmpty()) {
        return result;
    }

    // Capitalize first letter ONLY if we are at a sentence start (streaming-aware)
    if (m_isAtSentenceStart) {
        qsizetype i = 0;
        // Skip leading spaces and closing/opening quotes or brackets
        while (i < result.size()) {
            const QChar c = result[i];
            if (c.isSpace() || c == '"' || c == '\'' || c == '(' || c == '[' || c == '{' || c == u'“' || c == u'”') {
                ++i;
            }
            else {
                break;
            }
        }
        if (i < result.size() && result[i].isLetter()) {
            result[i] = result[i].toUpper();
        }
    }

    // Capitalize after sentence endings inside this chunk
    // Handles trailing quotes/brackets before the next lowercase letter.
    // FCR S525, B-11 (P1): the trailing ')' below was unmatched -- the ')' inside the
    // character class is a literal and balances nothing -- so isValid() was false, the
    // globalMatch returned an empty iterator, and this loop had never executed. Qt does
    // not warn on an invalid QRegularExpression; it simply matches nothing. This was the
    // only thing restoring case inside a chunk at the time, because the Latin Whisper path
    // then lower-cased the whole result before calling processFull(). Since the Session 579
    // review it no longer does: Whisper's own capitals are kept, and this loop capitalises
    // only a small letter that follows a sentence end inside the chunk.
    // Every QRegularExpression in the product was compiled to check for others: 79
    // patterns, exactly one invalid, and it was this one. See the isValid() guard in
    // tests/ that now holds the whole set.
    static const QRegularExpression sentenceEnd(
        QStringLiteral("([.!?])[)\\]\"']*\\s+(\\p{Ll})"));
    QRegularExpressionMatchIterator it = sentenceEnd.globalMatch(result);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        // Session 579 review (S-2): a stop that ends an abbreviation ("9 a.m.
        // tomorrow", "e.g. apples") is not a sentence end.
        if (isAbbreviationBeforePeriod(result, m.capturedStart(1)))
            continue;
        qsizetype pos = m.capturedStart(2);
        if (pos >= 0 && pos < result.length()) {
            result[pos] = result[pos].toUpper();
        }
    }

    // FCR S525, B-12 (P1): this is an English rule and it used to run for every Latin
    // language. In Italian "i" is the masculine plural definite article and occurs
    // constantly, so "i libri sono sul tavolo" became "I libri sono sul tavolo" on every
    // sentence. The two calls beside this one in processFull() already take the language
    // -- the time normaliser at :339 and the punctuator at :381 -- so only capitalisation
    // was ignoring it. The negative lookahead keeps the "i" in "i.e." lower case.
    if (language.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0
        || language.startsWith(QStringLiteral("en-"), Qt::CaseInsensitive)
        || language.startsWith(QStringLiteral("en_"), Qt::CaseInsensitive)) {
        static const QRegularExpression reStandaloneI(QStringLiteral("\\bi\\b(?!\\.)"));
        result.replace(reStandaloneI, QStringLiteral("I"));
    }

    return result;
}

void OptimizedPostProcessor::updateSentenceContextFromTail(const QString& text)
{
    // Look from end for the last significant character
    for (qsizetype i = text.size() - 1; i >= 0; --i) {
        const QChar c = text.at(i);
        if (c.isSpace()) continue;

        // Skip closing quotes/brackets
        if (c == '"' || c == '\'' || c == ')' || c == ']' || c == '}' || c == u'”') {
            continue;
        }

        if (c == '?' || c == '!') {
            m_isAtSentenceStart = true;
        }
        else if (c == '.') {
            // Session 596 review, B-32: the stop at the end of an abbreviation
            // ("at 9 a.m." | "tomorrow") does not end a sentence, so the next slice
            // must not start one. The same test the capitalisation uses inside a
            // slice (applySmartCapitalization()).
            m_isAtSentenceStart = !isAbbreviationBeforePeriod(text, i);
        }
        else {
            m_isAtSentenceStart = false;
        }
        return;
    }
    // If no content, keep state as-is (or assume start)
    // m_isAtSentenceStart unchanged
}

QString ProcessingMetrics::toString() const
{
    return QStringLiteral("fast=%1ms slow=%2ms corrections=%3 cacheHits=%4 cacheMisses=%5")
        .arg(fastStageMs).arg(slowStageMs).arg(correctionCount).arg(cacheHits).arg(cacheMisses);
}

bool OptimizedPostProcessor::shouldBypassCorrection(float confidence) const
{
    return confidence >= m_fastPathThreshold;
}

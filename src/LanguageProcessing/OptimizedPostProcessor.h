#pragma once
#include <QObject>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QFuture>
#include <QVector>
#include <memory>

// True when the full stop at periodPos ends an abbreviation rather than a
// sentence: a dotted form of one- or two-letter parts ("a.m.", "e.g.", "i.e.",
// "z.B.", "U.S.") or a short
// word that is only ever written abbreviated ("etc.", "vs.", "approx.", "usw.",
// "Mr.", "Dr."). Session 579 review, S-2: full stops inside the text are now
// kept, so a small letter after "a.m." must not be read as a new sentence, and
// the stop at the end of "a.m." must not be removed as a sentence end.
// What must still count as a sentence end: a stop after an ordinary word
// ("clear. The"), after a number ("in 2025. Then"), and after a single letter
// ("plan A. Then"), and after a web address ("deskshare.com. Then"), because
// those are far more often sentences than initials.
bool isAbbreviationBeforePeriod(const QString& text, qsizetype periodPos);

// True when the word before a connector makes it part of a fixed phrase rather
// than the start of a clause, so no comma goes in front of it (Session 579
// review, found in review): English "nothing but", "all but", "anything but",
// "cannot help but", "can't help but"; German "aber" straight after a verb or
// pronoun, where it is a particle ("Das ist aber schoen", "Er kommt aber nicht").
// What must still take a comma: "late, but we stayed", "kalt, aber sonnig".
bool connectorIsPartOfPhrase(const QString& text, qsizetype connectorStart, const QString& language);

struct TranscriptionMetadata {
    float averageConfidence;
    int segmentCount;
    qint64 durationMs;
    QString language;
    // ctrlHeld: physical Ctrl state captured on the GUI thread at the moment
    // the audio buffer is dispatched to the worker (N-1 pattern from Session 349).
    // QGuiApplication::queryKeyboardModifiers() is unreliable on worker threads;
    // GetAsyncKeyState(VK_CONTROL) must be read on the thread with the message pump.
    bool ctrlHeld;
    TranscriptionMetadata()
        : averageConfidence(0.0f), segmentCount(0), durationMs(0), language("en"),
          ctrlHeld(false) {
    }
};

struct ProcessingMetrics {
    qint64 fastStageMs;
    qint64 slowStageMs;
    int correctionCount;
    int cacheHits;
    int cacheMisses;
    ProcessingMetrics()
        : fastStageMs(0), slowStageMs(0), correctionCount(0), cacheHits(0), cacheMisses(0) {
    }
    QString toString() const;
};

class PrecompiledPatternMatcher {
public:
    PrecompiledPatternMatcher();
    QString apply(const QString& text);
private:
    struct Pattern {
        QRegularExpression regex;
        QString replacement;
    };
    QVector<Pattern> m_patterns;
    void initializePatterns();
};

class OptimizedPostProcessor : public QObject
{
    Q_OBJECT
public:
    explicit OptimizedPostProcessor(QObject* parent = nullptr);
    ~OptimizedPostProcessor();

    bool initialize();

    QString processFast(const QString& rawText, float confidence);
    QString processFull(const QString& rawText, const TranscriptionMetadata& metadata);
    QString processIncremental(const QString& newSegment, bool isFirst, bool isFinal, float confidence);

    void resetBuffer();

    void setContextCorrectionEnabled(bool enabled);
    // FCR S517, U-1. INERT. Homophone disambiguation is not implemented, and the
    // call that read this switch has been removed from the live path - see the
    // note at its former call site in OptimizedPostProcessor.cpp. The setter and
    // the member are retained because WhisperWorker.cpp (transcribeWithTokens) sets it and
    // test_session224_optimized_post_processor.cpp toggles it; nothing reads it.
    // Do not read it as a working switch.
    void setHomophoneDetectionEnabled(bool enabled);
    void setFastPathThreshold(float threshold);

signals:

private:
    // Steps
    QString normalizeWhitespace(const QString& text);
    QString applyFastCorrections(const QString& text, float confidence);
    QString applyPatternMatching(const QString& text);
    QString applyContextCorrection(const QString& text);
    QString applyHomophoneDisambiguation(const QString& text);

    // Streaming-aware capitalization (uses internal context)
    // FCR S525, B-12: takes the language, like the time normaliser and the
    // punctuator beside it. The standalone-"i" rule is English-only.
    QString applySmartCapitalization(const QString& text, const QString& language);

    // Context helpers
    bool shouldBypassCorrection(float confidence) const;
    void updateSentenceContextFromTail(const QString& text);

private:
    bool m_initialized{ false };
    std::unique_ptr<PrecompiledPatternMatcher> m_patternMatcher;

    // Streaming context: are we at a sentence start for the next chunk?
    bool m_isAtSentenceStart{ true };

    // Settings
    bool m_contextCorrectionEnabled{ true };
    bool m_homophoneDetectionEnabled{ true };
    float m_fastPathThreshold{ 0.97f };

    // Metrics
    ProcessingMetrics m_lastMetrics;
    QElapsedTimer m_timer;

    // Incremental buffer
    QString m_incrementalBuffer;
};

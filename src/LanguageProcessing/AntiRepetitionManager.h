#pragma once

#include <QStringList>
#include <QString>

class AntiRepetitionManager {
public:
    bool isRepetitive(const QString& newText) const;

    // Bug 1 fix (Session 186 FCR): extension-aware incremental-dedup check.
    //
    // Returns true if text is genuinely new content that should be emitted.
    // Returns false if text is a repetition that should be suppressed.
    //
    // Compared to isRepetitive(), this uses two guards instead of one:
    //   1. Similarity against lastEmitted at 0.88 (vs. 0.70) — less aggressive;
    //      valid short follow-up utterances are no longer misidentified as repeats.
    //   2. Prefix ratio check — if the new text starts with the last emitted text
    //      at ≥ 0.80 coverage, the new text is a continuation/extension of the
    //      prior utterance and is always treated as genuinely new regardless of
    //      overall similarity.
    bool isGenuinelyNew(const QString& text, const QString& lastEmitted) const;

    void addSegment(const QString& text);
    void reset();

private:
    QStringList m_recentSegments;
    static constexpr int MAX_HISTORY = 4;
    static constexpr float SIMILARITY_THRESHOLD = 0.70f;
    static constexpr float GENUINE_NEW_SIMILARITY_THRESHOLD = 0.88f;
    static constexpr float GENUINE_NEW_PREFIX_RATIO = 0.80f;

    float calculateSimilarity(const QString& text1, const QString& text2) const;
};

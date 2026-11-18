#include "LanguageProcessing/AntiRepetitionManager.h"
#include <QVector>
#include <algorithm>

bool AntiRepetitionManager::isRepetitive(const QString& newText) const {
    QString normalized = newText.trimmed();
    if (normalized.isEmpty() || normalized.length() < 3) return false;

    for (const QString& segment : m_recentSegments) {
        float similarity = calculateSimilarity(normalized, segment);
        if (similarity > SIMILARITY_THRESHOLD) {
            return true;
        }
    }

    return false;
}

void AntiRepetitionManager::addSegment(const QString& text) {
    QString normalized = text.trimmed();
    if (normalized.isEmpty()) return;

    m_recentSegments.append(normalized);

    while (m_recentSegments.size() > MAX_HISTORY) {
        m_recentSegments.removeFirst();
    }
}

void AntiRepetitionManager::reset() {
    m_recentSegments.clear();
}

// Bug 1 fix (Session 186 FCR): extension-aware dedup.
//
// Two-condition guard:
//   A) Similarity between text and lastEmitted is below GENUINE_NEW_SIMILARITY_THRESHOLD (0.88).
//      If similarity >= 0.88 the text is nearly identical to what was just emitted.
//   B) The new text begins with at least GENUINE_NEW_PREFIX_RATIO (0.80) of lastEmitted.
//      If it does, the utterance is an extension/continuation — pass it through even
//      if overall similarity is high (e.g. lastEmitted = "play some" and new text =
//      "play some music" share high similarity but the new text carries new content).
//
// Text is genuinely new if:
//   - Similarity < 0.88 (clearly different), OR
//   - New text is a prefix-extension of lastEmitted (prefixCoverage >= 0.80)
bool AntiRepetitionManager::isGenuinelyNew(const QString& text, const QString& lastEmitted) const
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return false;
    // Short utterances (< 3 chars) cannot be meaningfully compared for similarity;
    // treat them as genuinely new rather than suppressing them.
    if (t.length() < 3) return true;
    if (lastEmitted.isEmpty()) return true;

    const QString tLow = t.toLower();
    const QString lLow = lastEmitted.trimmed().toLower();

    // Condition B: extension check — new text begins with the last emitted text.
    // If lastEmitted is a leading substring of text at >= 80% coverage, it is a
    // continuation, not a repeat.
    //
    // FCR S547: an extension must be LONGER than what was emitted. Identical text
    // also "starts with" the last emitted text, at 100% coverage, so without the
    // length test every exact repeat -- the case this function exists to stop --
    // was waved through as a continuation and typed into the document twice.
    if (!lLow.isEmpty()) {
        const int overlapLen = static_cast<int>(lLow.length());
        if (tLow.length() > lLow.length() && tLow.startsWith(lLow)) {
            const float prefixCoverage = static_cast<float>(overlapLen)
                / static_cast<float>(std::max(static_cast<int>(tLow.length()), overlapLen));
            if (prefixCoverage >= GENUINE_NEW_PREFIX_RATIO) {
                return true; // continuation — allow through
            }
        }
    }

    // Condition A: similarity check — compare against lastEmitted at the stricter
    // 0.88 threshold (isRepetitive() uses 0.70; this is less aggressive).
    const float similarity = calculateSimilarity(tLow, lLow);
    return similarity < GENUINE_NEW_SIMILARITY_THRESHOLD;
}

float AntiRepetitionManager::calculateSimilarity(const QString& text1, const QString& text2) const {
    if (text1.isEmpty() && text2.isEmpty()) return 1.0f;
    if (text1.isEmpty() || text2.isEmpty()) return 0.0f;

    QString s1 = text1.trimmed().toLower();
    QString s2 = text2.trimmed().toLower();

    if (s1 == s2) return 1.0f;

    const int len1 = static_cast<int>(s1.length());
    const int len2 = static_cast<int>(s2.length());

    // Use optimized edit distance calculation
    QVector<int> prev(len2 + 1);
    QVector<int> curr(len2 + 1);

    for (int j = 0; j <= len2; ++j) prev[j] = j;

    for (int i = 1; i <= len1; ++i) {
        curr[0] = i;
        for (int j = 1; j <= len2; ++j) {
            if (s1[i - 1] == s2[j - 1]) {
                curr[j] = prev[j - 1];
            }
            else {
                curr[j] = 1 + std::min({ prev[j], curr[j - 1], prev[j - 1] });
            }
        }
        prev = curr;
    }

    int maxLen = std::max(len1, len2);
    return 1.0f - static_cast<float>(curr[len2]) / maxLen;
}

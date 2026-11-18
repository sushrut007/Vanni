/**
 * @file MultilangPostProcessing.h
 * @brief Multilingual extensions for OptimizedPostProcessor
 *
 */

#pragma once

#include <QString>
#include <QRegularExpression>
#include "LanguageProcessing/LanguageRules.h"

 /**
  * @brief Language-aware comma heuristics
  */
// atSentenceStart (Session 596 review, B-31): the opening-word rule ("Actually,
// ...", "Deshalb, ...") is anchored at the start of the text and must run only
// when that text starts a sentence. A slice that continues a sentence passes
// false. The connector rule ("late, but") runs either way.
QString applyMultilingualCommaHeuristics(const QString& input, const QString& language,
                                         bool atSentenceStart = true);

/**
 * @brief Language-aware punctuation inference
 */
class MultilingualPunctuationInference
{
public:
    MultilingualPunctuationInference();

    /**
     * @brief Apply punctuation based on language rules
     * @param text Input text
     * @param language Language code (en, es, de, it, pt, fr)
     * @param atSentenceStart False when this text continues a sentence begun in an
     *        earlier slice (Session 596 review, B-31); the opening-word comma is
     *        then not applied.
     * @return Text with inferred punctuation
     */
    QString apply(const QString& text, const QString& language, bool atSentenceStart = true);

private:
    // Cached regex patterns by language
    mutable QHash<QString, QRegularExpression> m_questionPatterns;
    mutable QHash<QString, QRegularExpression> m_trailingPatterns;
};

/**
 * @brief Language-aware homophone disambiguation
 */
class MultilingualHomophoneCorrector
{
public:
    /**
     * @brief Disambiguate homophones based on context
     * @param text Input text
     * @param language Language code
     * @return Text with corrected homophones
     */
    static QString disambiguate(const QString& text, const QString& language);
};

/**
 * @brief Language-aware time expression normalizer
 */
class MultilingualTimeNormalizer
{
public:
    /**
     * @brief Normalize time expressions
     * @param text Input text
     * @param language Language code
     * @return Text with normalized time expressions
     */
    static QString normalize(const QString& text, const QString& language);
};

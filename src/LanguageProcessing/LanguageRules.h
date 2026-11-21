/**
 * @file LanguageRules.h
 * @brief Language-specific rules for text post-processing
 *
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QRegularExpression>

 /**
  * @brief Language-specific rules for punctuation, homophones, and patterns
  */
struct LanguageRules {
    // Question words for each language
    QStringList questionWords;

    // Discourse markers (words that typically need commas after them)
    QStringList discourseMarkers;

    // Connectors that need commas
    QStringList connectors;

    // Session 596 review, B-1: the word-based time table ("quarter past five",
    // "halb drei", "Mittag") was removed. It was compiled without word boundaries
    // and rewrote ordinary words ("listen" -> "lis10 minutes", "Mittagessen" ->
    // "12:00essen"), and every replacement wrote ENGLISH into the other five
    // languages. It has no correct output, so it is gone; only the digit-anchored
    // rules in MultilingualTimeNormalizer::normalize() remain.

    // Common homophones for disambiguation
    QHash<QString, QStringList> homophones;

    // Trailing connectors (incomplete sentence indicators)
    QStringList trailingConnectors;
};

/**
 * @brief Manager for language-specific rules
 */
class LanguageRulesManager {
public:
    static LanguageRulesManager& instance();

    /**
     * @brief Get rules for a specific language
     * @param language Language code (en, es, de, it, pt, fr, nl, sv, no, da, pl, ja, ko)
     * @return Language rules, defaults to English if not found
     */
    const LanguageRules& getRules(const QString& language) const;

    /**
     * @brief Get supported languages
     */
    QStringList getSupportedLanguages() const;

    /**
     * @brief Create regex for question detection
     */
    QRegularExpression createQuestionRegex(const QString& language) const;

    /**
     * @brief Create regex for discourse markers
     */
    QRegularExpression createDiscourseMarkerRegex(const QString& language) const;

    /**
     * @brief Create regex for connectors
     */
    QRegularExpression createConnectorRegex(const QString& language) const;

    /**
     * @brief Create regex for trailing connectors
     */
    QRegularExpression createTrailingConnectorRegex(const QString& language) const;

private:
    LanguageRulesManager();
    ~LanguageRulesManager() = default;
    LanguageRulesManager(const LanguageRulesManager&) = delete;
    LanguageRulesManager& operator=(const LanguageRulesManager&) = delete;

    void initializeRules();
    void initializeEnglishRules();
    void initializeSpanishRules();
    void initializeGermanRules();
    void initializeItalianRules();
    void initializePortugueseRules();
    void initializeFrenchRules();
    void initializeDutchRules();
    void initializeSwedishRules();
    void initializeNorwegianRules();
    void initializeDanishRules();
    void initializePolishRules();
    void initializeJapaneseRules();
    void initializeKoreanRules();

    QHash<QString, LanguageRules> m_rules;
    LanguageRules m_fallbackRules; // English as fallback
};

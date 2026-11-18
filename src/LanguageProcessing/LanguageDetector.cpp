#include "LanguageProcessing/LanguageDetector.h"
#include <QMutexLocker>
#include <QLocale>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

LanguageDetector::LanguageDetector(QObject* parent) : QObject(parent) {
    initializeLanguageProfiles();
}

void LanguageDetector::initializeLanguageProfiles() {
    m_languageProfiles["en"] = LanguageProfile{
        "en",
        "English",
        {0.3f, 0.5f, 0.2f},
        {"the", "and", "to", "of", "a", "in", "is", "it", "for", "with"},
        0.8f
    };

    m_languageProfiles["es"] = LanguageProfile{
        "es",
        "Spanish",
        {0.4f, 0.4f, 0.2f},
        {"el", "la", "de", "que", "y", "en", "un", "es", "se", "no"},
        0.6f
    };

    m_languageProfiles["fr"] = LanguageProfile{
        "fr",
        "French",
        {0.2f, 0.6f, 0.2f},
        {"le", "de", "et", "a", "un", "il", "etre", "et", "en", "avoir"},
        0.4f
    };

    m_languageProfiles["de"] = LanguageProfile{
        "de",
        "German",
        {0.5f, 0.3f, 0.2f},
        {"der", "die", "und", "in", "den", "von", "zu", "das", "mit", "sich"},
        0.4f
    };

    m_languageProfiles["it"] = LanguageProfile{
        "it",
        "Italian",
        {0.4f, 0.5f, 0.1f},
        {"il", "di", "che", "e", "la", "per", "una", "in", "con", "non"},
        0.3f
    };
}

QString LanguageDetector::detectLanguageFromText(const QString& text) {
    QMutexLocker locker(&m_mutex);

    QHash<QString, float> scores = analyzeTextPatterns(text);

    QString bestLanguage = "en";
    float bestScore = 0.0f;

    for (auto it = scores.begin(); it != scores.end(); ++it) {
        if (it.value() > bestScore) {
            bestScore = it.value();
            bestLanguage = it.key();
        }
    }

    m_detectedLanguage = bestLanguage;
    m_detectionConfidence = bestScore;
    m_languageScores = scores;

    return bestLanguage;
}

QString LanguageDetector::detectLanguageFromAudio(const std::vector<float>& audio) {
    QMutexLocker locker(&m_mutex);

    QHash<QString, float> scores = analyzePhoneticFeatures(audio);

    QString bestLanguage = "en";
    float bestScore = 0.0f;

    for (auto it = scores.begin(); it != scores.end(); ++it) {
        if (it.value() > bestScore) {
            bestScore = it.value();
            bestLanguage = it.key();
        }
    }

    m_detectedLanguage = bestLanguage;
    m_detectionConfidence = bestScore * 0.7f;
    m_languageScores = scores;

    return bestLanguage;
}

QString LanguageDetector::detectLanguageMultiModal(const QString& text, const std::vector<float>& audio) {
    QMutexLocker locker(&m_mutex);

    QHash<QString, float> textScores = analyzeTextPatterns(text);
    QHash<QString, float> audioScores = analyzePhoneticFeatures(audio);

    QHash<QString, float> combinedScores;

    for (const QString& lang : textScores.keys()) {
        float textScore = textScores.value(lang, 0.0f);
        float audioScore = audioScores.value(lang, 0.0f);
        combinedScores[lang] = 0.7f * textScore + 0.3f * audioScore;
    }

    QString bestLanguage = "en";
    float bestScore = 0.0f;

    for (auto it = combinedScores.begin(); it != combinedScores.end(); ++it) {
        if (it.value() > bestScore) {
            bestScore = it.value();
            bestLanguage = it.key();
        }
    }

    m_detectedLanguage = bestLanguage;
    m_detectionConfidence = bestScore;
    m_languageScores = combinedScores;

    locker.unlock();
    emit languageDetected(bestLanguage, bestScore);

    return bestLanguage;
}

QString LanguageDetector::getCurrentLanguage() const {
    QMutexLocker locker(&m_mutex);
    return m_detectedLanguage;
}

float LanguageDetector::getDetectionConfidence() const {
    QMutexLocker locker(&m_mutex);
    return m_detectionConfidence;
}

QHash<QString, float> LanguageDetector::getLanguageScores() const {
    QMutexLocker locker(&m_mutex);
    return m_languageScores;
}

QStringList LanguageDetector::getSupportedLanguages() const {
    QMutexLocker locker(&m_mutex);
    QStringList languages;
    for (auto it = m_languageProfiles.cbegin(); it != m_languageProfiles.cend(); ++it) {
        languages.append(it->first);
    }
    return languages;
}

void LanguageDetector::setLanguageOverride(const QString& language) {
    QMutexLocker locker(&m_mutex);
    m_detectedLanguage = language;
    m_detectionConfidence = 1.0f;
    locker.unlock();
    emit languageChanged(language);
}

QHash<QString, float> LanguageDetector::analyzeTextPatterns(const QString& text) const {
    QHash<QString, float> scores;

    if (text.length() < 10) {
        scores["en"] = 1.0f;
        return scores;
    }

    QString lowerText = text.toLower();
    static const QRegularExpression reWhitespace("\\s+");
    QStringList words = lowerText.split(reWhitespace, Qt::SkipEmptyParts);

    for (const auto& [langCode, profile] : m_languageProfiles) {
        float score = 0.0f;
        int wordMatches = 0;

        for (const QString& word : words) {
            for (const QString& commonWord : profile.commonWords) {
                if (word == commonWord) {
                    wordMatches++;
                    break;
                }
            }
        }

        if (!words.isEmpty()) {
            score = static_cast<float>(wordMatches) / words.size();
        }

        score *= profile.frequency;
        scores[langCode] = score;
    }

    return scores;
}

QHash<QString, float> LanguageDetector::analyzePhoneticFeatures(const std::vector<float>& audio) const {
    QHash<QString, float> scores;

    if (audio.size() < 1600) {
        scores["en"] = 1.0f;
        return scores;
    }

    std::vector<float> spectralFeatures = extractSpectralFeatures(audio);

    if (spectralFeatures.size() < 3) {
        scores["en"] = 1.0f;
        return scores;
    }

    for (const auto& [langCode, profile] : m_languageProfiles) {
        float similarity = 0.0f;

        for (size_t i = 0; i < std::min(spectralFeatures.size(), profile.phoneticFeatures.size()); ++i) {
            float diff = std::abs(spectralFeatures[i] - profile.phoneticFeatures[i]);
            similarity += 1.0f - diff;
        }

        const size_t featureCount = std::min(spectralFeatures.size(), profile.phoneticFeatures.size());
        if (featureCount == 0) {
            scores[langCode] = 0.0f;
            continue;
        }
        similarity /= static_cast<float>(featureCount);
        similarity = std::max(0.0f, std::min(1.0f, similarity));

        scores[langCode] = similarity;
    }

    return scores;
}

std::vector<float> LanguageDetector::extractSpectralFeatures(const std::vector<float>& audio) const {
    if (audio.empty()) return {};

    std::vector<float> features;

    float lowEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;

    const int windowSize = 512;
    for (size_t i = 0; i + windowSize < audio.size(); i += windowSize) {
        int zeroCrossings = 0;
        for (int j = 1; j < windowSize; ++j) {
            if ((audio[i + j] >= 0) != (audio[i + j - 1] >= 0)) {
                zeroCrossings++;
            }
        }
        float zcr = static_cast<float>(zeroCrossings) / windowSize;

        float energy = 0.0f;
        for (int j = 0; j < windowSize; ++j) {
            energy += audio[i + j] * audio[i + j];
        }
        energy = std::sqrt(energy / windowSize);

        if (zcr < 0.2f) {
            lowEnergy += energy;
        }
        else if (zcr < 0.4f) {
            midEnergy += energy;
        }
        else {
            highEnergy += energy;
        }
    }

    float totalEnergy = lowEnergy + midEnergy + highEnergy;
    if (totalEnergy > 0.0f) {
        features.push_back(lowEnergy / totalEnergy);
        features.push_back(midEnergy / totalEnergy);
        features.push_back(highEnergy / totalEnergy);
    }
    else {
        features = { 0.33f, 0.33f, 0.34f };
    }

    return features;
}
static QString normalizeToPrimaryLang(const QString& tag)
{
    // Convert "en_US" or "en-US" to "en"
    QString t = tag.toLower();
    QString norm = t;
    norm.replace('_', '-');
    const int dash = static_cast<int>(norm.indexOf('-'));
    if (dash > 0) norm = norm.left(dash);

    // Normalize Norwegian variants to "no"
    if (norm == "nb" || norm == "nn") norm = "no";
    return norm;
}

QString LanguageDetector::detectSystemLanguagePreferred()
{
    static const QSet<QString> allowed{
        "en","es","de","it","pt","fr","nl","sv","no","da","pl","ja","ko"
    };

    QStringList candidates;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    const QStringList uiLangs = QLocale().uiLanguages();
    for (const QString& tag : uiLangs) {
        candidates << tag.toLower();
    }
#endif
    const QString sysName = QLocale::system().name().toLower();
    if (!candidates.contains(sysName)) {
        candidates.prepend(sysName);
    }
    QStringList primaries;
    for (const QString& c : candidates) {
        primaries << normalizeToPrimaryLang(c);
    }
    QStringList ordered;
    for (const QString& p : primaries) {
        if (!ordered.contains(p)) ordered << p;
    }
    for (const QString& code : ordered) {
        if (allowed.contains(code)) {
            return code;
        }
    }
    switch (QLocale::system().language()) {
    case QLocale::English:       return "en";
    case QLocale::Spanish:       return "es";
    case QLocale::German:        return "de";
    case QLocale::Italian:       return "it";
    case QLocale::Portuguese:    return "pt";
    case QLocale::French:        return "fr";
    case QLocale::Dutch:         return "nl";
    case QLocale::Swedish:       return "sv";
    case QLocale::NorwegianBokmal:
    case QLocale::NorwegianNynorsk: return "no";
    case QLocale::Danish:        return "da";
    case QLocale::Polish:        return "pl";
    case QLocale::Japanese:      return "ja";
    case QLocale::Korean:        return "ko";
    default: break;
    }
    return "en";
}

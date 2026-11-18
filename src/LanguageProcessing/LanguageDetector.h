#pragma once

#include <QObject>
#include <QString>
#include <QHash>
#include <QMutex>
#include <vector>
#include <unordered_map>

class LanguageDetector : public QObject {
    Q_OBJECT

public:
    explicit LanguageDetector(QObject* parent = nullptr);

    QString detectLanguageFromText(const QString& text);
    QString detectLanguageFromAudio(const std::vector<float>& audio);
    QString detectLanguageMultiModal(const QString& text, const std::vector<float>& audio);
    QString getCurrentLanguage() const;
    float getDetectionConfidence() const;
    QHash<QString, float> getLanguageScores() const;
    QStringList getSupportedLanguages() const;
    void setLanguageOverride(const QString& language);
    static QString detectSystemLanguagePreferred();
    QString detectSystemLanguagePreferredInstance() const { return detectSystemLanguagePreferred(); }

signals:
    void languageDetected(const QString& language, float confidence);
    void languageChanged(const QString& language);

private:
    mutable QMutex m_mutex;
    QString m_detectedLanguage = "en";
    float m_detectionConfidence = 1.0f;
    QHash<QString, float> m_languageScores;

    struct LanguageProfile {
        QString code;
        QString name;
        std::vector<float> phoneticFeatures;
        std::vector<QString> commonWords;
        // Multiplies the match score, so an unset value would silently distort
        // detection rather than fail. All five profiles set it.
        float frequency = 0.0f;
    };

    std::unordered_map<QString, LanguageProfile> m_languageProfiles;

    void initializeLanguageProfiles();
    QHash<QString, float> analyzeTextPatterns(const QString& text) const;
    QHash<QString, float> analyzePhoneticFeatures(const std::vector<float>& audio) const;
    std::vector<float> extractSpectralFeatures(const std::vector<float>& audio) const;
};

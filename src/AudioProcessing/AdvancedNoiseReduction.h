#pragma once

#include <QObject>
#include <vector>
#include <complex>

class AdvancedNoiseReduction : public QObject {
    Q_OBJECT

public:
    explicit AdvancedNoiseReduction(QObject* parent = nullptr);

    void setEnabled(bool enabled);
    void setAggressiveness(float aggressiveness);
    std::vector<float> processAudio(const std::vector<float>& input);
    void resetNoiseProfile();
    bool isEnabled() const;
    float getAggressiveness() const;

private:
    bool m_enabled = true;
    float m_aggressiveness = 0.5f;
    std::vector<float> m_noiseProfile;
    int m_noiseEstimationFrames = 0;

    static constexpr int FRAME_SIZE = 512;
    static constexpr int HOP_SIZE = 256;
    static constexpr float ALPHA = 0.98f;
    static constexpr float BETA = 0.02f;
    static constexpr int NOISE_ESTIMATION_REQUIRED = 10;

    void applyHammingWindow(std::vector<float>& frame) const;
    std::vector<std::complex<float>> computeFFT(const std::vector<float>& input) const;
    std::vector<float> computeIFFT(const std::vector<std::complex<float>>& input) const;
    void updateNoiseProfile(const std::vector<std::complex<float>>& spectrum);
    std::vector<std::complex<float>> applySpectralSubtraction(const std::vector<std::complex<float>>& spectrum) const;
    std::vector<std::complex<float>> applyWienerFilter(const std::vector<std::complex<float>>& spectrum) const;
};

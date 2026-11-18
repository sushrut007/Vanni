#include "AudioProcessing/AdvancedNoiseReduction.h"
#include "AudioProcessing/FFT.h"
#include <cmath>
#include <algorithm>

AdvancedNoiseReduction::AdvancedNoiseReduction(QObject* parent) : QObject(parent) {}

void AdvancedNoiseReduction::setEnabled(bool enabled) {
    m_enabled = enabled;
}

void AdvancedNoiseReduction::setAggressiveness(float aggressiveness) {
    m_aggressiveness = std::max(0.0f, std::min(1.0f, aggressiveness));
}

std::vector<float> AdvancedNoiseReduction::processAudio(const std::vector<float>& input) {
    if (!m_enabled || input.empty()) return input;

    std::vector<float> output;
    output.reserve(input.size());

    for (size_t pos = 0; pos < input.size(); pos += HOP_SIZE) {
        size_t frameEnd = std::min(pos + FRAME_SIZE, input.size());
        std::vector<float> frame(input.begin() + pos, input.begin() + frameEnd);

        if (frame.size() < FRAME_SIZE) {
            frame.resize(FRAME_SIZE, 0.0f);
        }

        applyHammingWindow(frame);
        auto spectrum = computeFFT(frame);

        float frameEnergy = 0.0f;
        for (float sample : frame) {
            frameEnergy += sample * sample;
        }
        frameEnergy /= frame.size();

        if (frameEnergy < 0.01f) {
            updateNoiseProfile(spectrum);
        }

        auto cleaned = applySpectralSubtraction(spectrum);
        cleaned = applyWienerFilter(cleaned);

        auto cleanedFrame = computeIFFT(cleaned);

        if (pos == 0) {
            output.insert(output.end(), cleanedFrame.begin(),
                cleanedFrame.begin() + HOP_SIZE);
        }
        else {
            size_t overlapSize = std::min(static_cast<size_t>(HOP_SIZE), output.size());
            size_t outputStart = output.size() - overlapSize;

            for (size_t i = 0; i < overlapSize; ++i) {
                if (outputStart + i < output.size() && i < cleanedFrame.size()) {
                    output[outputStart + i] += cleanedFrame[i];
                }
            }

            if (HOP_SIZE < cleanedFrame.size()) {
                output.insert(output.end(),
                    cleanedFrame.begin() + HOP_SIZE,
                    cleanedFrame.end());
            }
        }
    }

    if (output.size() > input.size()) {
        output.resize(input.size());
    }

    return output;
}

void AdvancedNoiseReduction::resetNoiseProfile() {
    m_noiseProfile.clear();
    m_noiseEstimationFrames = 0;
}

bool AdvancedNoiseReduction::isEnabled() const {
    return m_enabled;
}

float AdvancedNoiseReduction::getAggressiveness() const {
    return m_aggressiveness;
}

void AdvancedNoiseReduction::applyHammingWindow(std::vector<float>& frame) const {
    if (frame.size() <= 1) return;
    const size_t N = frame.size() - 1;
    for (size_t i = 0; i < frame.size(); ++i) {
        float w = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / N);
        frame[i] *= w;
    }
}

std::vector<std::complex<float>> AdvancedNoiseReduction::computeFFT(const std::vector<float>& input) const {
    // W3 (Session 304): iterative radix-2 FFT (O(N log N)) replaces the former
    // naive DFT (O(N^2)). FRAME_SIZE is 512 (a power of two), so radix-2 always
    // applies on the live path; other sizes fall back to a direct DFT inside
    // FFT.h, keeping the output equivalent for any input length.
    return dictationpro::fftForward(input);
}

std::vector<float> AdvancedNoiseReduction::computeIFFT(const std::vector<std::complex<float>>& input) const {
    // W3 (Session 304): radix-2 inverse FFT; returns real parts scaled by 1/N,
    // matching the previous naive IDFT contract.
    return dictationpro::fftInverseReal(input);
}

void AdvancedNoiseReduction::updateNoiseProfile(const std::vector<std::complex<float>>& spectrum) {
    if (m_noiseProfile.empty()) {
        m_noiseProfile.resize(spectrum.size());
        std::fill(m_noiseProfile.begin(), m_noiseProfile.end(), 0.0f);
    }

    for (size_t i = 0; i < spectrum.size(); ++i) {
        float magnitude = std::abs(spectrum[i]);
        m_noiseProfile[i] = (m_noiseProfile[i] * m_noiseEstimationFrames + magnitude) /
            (m_noiseEstimationFrames + 1);
    }

    m_noiseEstimationFrames = std::min(m_noiseEstimationFrames + 1, 100);
}

std::vector<std::complex<float>> AdvancedNoiseReduction::applySpectralSubtraction(
    const std::vector<std::complex<float>>& spectrum) const {

    if (m_noiseProfile.empty()) return spectrum;

    std::vector<std::complex<float>> cleaned(spectrum.size());

    for (size_t i = 0; i < spectrum.size(); ++i) {
        float magnitude = std::abs(spectrum[i]);
        float phase = std::arg(spectrum[i]);

        if (i < m_noiseProfile.size()) {
            float noiseMagnitude = m_noiseProfile[i];
            float cleanMagnitude = magnitude - ALPHA * noiseMagnitude;
            cleanMagnitude = std::max(cleanMagnitude, BETA * magnitude);
            cleaned[i] = std::polar(cleanMagnitude, phase);
        }
        else {
            cleaned[i] = spectrum[i];
        }
    }

    return cleaned;
}

std::vector<std::complex<float>> AdvancedNoiseReduction::applyWienerFilter(
    const std::vector<std::complex<float>>& spectrum) const {

    if (m_noiseProfile.empty()) return spectrum;

    std::vector<std::complex<float>> filtered(spectrum.size());
    constexpr float epsilon = 1e-10f; // Prevent divide-by-zero

    for (size_t i = 0; i < spectrum.size(); ++i) {
        float signalPower = std::abs(spectrum[i]) * std::abs(spectrum[i]);
        float noisePower = 0.0f;

        if (i < m_noiseProfile.size()) {
            noisePower = m_noiseProfile[i] * m_noiseProfile[i];
        }

        float denominator = signalPower + noisePower * m_aggressiveness + epsilon;
        float wienerGain = signalPower / denominator;
        filtered[i] = spectrum[i] * wienerGain;
    }

    return filtered;
}

#include "SpeechRecognition/VoiceActivityDetector.h"
#include <cmath>
#include <algorithm>

bool VoiceActivityDetector::detectSpeech(const std::vector<float>& pcm) {
    if (pcm.empty()) return false;
    return detectSpeechInRange(pcm.data(), pcm.size());
}

bool VoiceActivityDetector::detectSpeechInRange(const float* data, size_t count) {
    if (!data || count == 0) return false;

    float energy = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        energy += data[i] * data[i];
    }
    energy = std::sqrt(energy / static_cast<float>(count));

    static constexpr float ADJUSTED_ENERGY_THRESHOLD = 0.002f;

    if (energy < ADJUSTED_ENERGY_THRESHOLD) return false;

    int zeroCrossings = 0;
    for (size_t i = 1; i < count; ++i) {
        if ((data[i] >= 0) != (data[i - 1] >= 0)) {
            zeroCrossings++;
        }
    }
    float zcr = static_cast<float>(zeroCrossings) / static_cast<float>(count);

    static constexpr float ADJUSTED_ZCR_MIN = 0.005f;
    static constexpr float ADJUSTED_ZCR_MAX = 0.85f;

    return zcr >= ADJUSTED_ZCR_MIN && zcr <= ADJUSTED_ZCR_MAX;
}

bool VoiceActivityDetector::detectSpeechInAnyWindow(const std::vector<float>& pcm,
                                                    size_t windowSamples, size_t hopSamples) {
    if (pcm.empty()) return false;
    if (windowSamples == 0 || pcm.size() <= windowSamples)
        return detectSpeechInRange(pcm.data(), pcm.size());
    const size_t hop = hopSamples == 0 ? windowSamples : hopSamples;
    for (size_t start = 0; start + windowSamples <= pcm.size(); start += hop) {
        if (detectSpeechInRange(pcm.data() + start, windowSamples))
            return true;
    }
    // The last window, aligned to the end, so the final samples are never skipped.
    return detectSpeechInRange(pcm.data() + (pcm.size() - windowSamples), windowSamples);
}

float VoiceActivityDetector::calculateConfidence(const std::vector<float>& pcm) {
    if (pcm.empty()) return 0.0f;
    if (!detectSpeech(pcm)) return 0.0f;

    // Calculate multiple features for confidence
    float energy = 0.0f;
    int zeroCrossings = 0;

    for (size_t i = 0; i < pcm.size(); ++i) {
        energy += pcm[i] * pcm[i];
        if (i > 0 && (pcm[i] >= 0) != (pcm[i - 1] >= 0)) {
            zeroCrossings++;
        }
    }

    energy = std::sqrt(energy / pcm.size());
    float zcr = static_cast<float>(zeroCrossings) / pcm.size();

    // Enhanced confidence calculation with spectral analysis
    float energyScore = std::min(energy / 0.1f, 1.0f);
    float zcrScore = 1.0f - std::abs(zcr - 0.3f) / 0.3f;

    // Add spectral continuity check
    float spectralScore = calculateSpectralContinuity(pcm);

    return (energyScore + zcrScore + spectralScore) / 3.0f;
}

float VoiceActivityDetector::calculateSpectralContinuity(const std::vector<float>& pcm) {
    if (pcm.size() < 512) return 0.5f;

    // Simple spectral continuity measure
    float continuity = 0.0f;
    const int windowSize = 256;
    const int hopSize = 128;

    // Check if pcm buffer is large enough to process
    const size_t minSize = windowSize + hopSize;
    if (pcm.size() < minSize) {
        // Not enough data for analysis, return default continuity
        return 0.5f;
    }

    for (size_t i = 0; i + windowSize + hopSize < pcm.size(); i += hopSize) {
        float energy1 = 0.0f, energy2 = 0.0f;

        for (int j = 0; j < windowSize; ++j) {
            energy1 += pcm[i + j] * pcm[i + j];
            energy2 += pcm[i + hopSize + j] * pcm[i + hopSize + j];
        }

        energy1 = std::sqrt(energy1 / windowSize);
        energy2 = std::sqrt(energy2 / windowSize);

        if (energy1 > 0.0f && energy2 > 0.0f) {
            float ratio = std::min(energy1, energy2) / std::max(energy1, energy2);
            continuity += ratio;
        }
    }

    // Safe window calculation - no underflow possible after size check
    // S232 Bug 2 fix: formula overcounted by 1 vs. the actual loop
    // (condition i+windowSize+hopSize < size, not <=). Count iterations
    // directly so denominator matches what continuity was summed over.
    int windows = 0;
    for (size_t i = 0; i + windowSize + hopSize < pcm.size(); i += hopSize) {
        ++windows;
    }
    return windows > 0 ? continuity / static_cast<float>(windows) : 0.5f;
}

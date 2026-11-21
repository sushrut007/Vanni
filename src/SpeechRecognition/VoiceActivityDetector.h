#pragma once

#include <vector>

class VoiceActivityDetector {
public:
    static bool detectSpeech(const std::vector<float>& pcm);
    // Session 596 review, B-2: true as soon as one window of windowSamples
    // (stepping by hopSamples) passes detectSpeech()'s test. A pcm shorter than
    // one window is tested whole. Same thresholds as detectSpeech().
    static bool detectSpeechInAnyWindow(const std::vector<float>& pcm,
                                        size_t windowSamples, size_t hopSamples);
    static float calculateConfidence(const std::vector<float>& pcm);

    // Stateless helper, exposed so the underflow guard on a short or silent
    // buffer can be pinned directly rather than inferred from a confidence
    // score that averages it with three other measures.
    static float calculateSpectralContinuity(const std::vector<float>& pcm);

private:
    static bool detectSpeechInRange(const float* data, size_t count);
    static constexpr float ENERGY_THRESHOLD = 0.001f;
    static constexpr float SPECTRAL_CENTROID_MIN = 200.0f;
    static constexpr float SPECTRAL_CENTROID_MAX = 4000.0f;
    static constexpr float ZERO_CROSSING_RATE_MIN = 0.1f;
    static constexpr float ZERO_CROSSING_RATE_MAX = 0.6f;

};

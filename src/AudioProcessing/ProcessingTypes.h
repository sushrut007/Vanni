#pragma once

#include <QString>

enum class Environment {
    QUIET_OFFICE,
    NOISY_OFFICE,
    HOME_QUIET,
    HOME_NOISY,
    MOBILE,
    POOR_MICROPHONE,
    PHONE_CALL,
    PRESENTATION,
    UNKNOWN
};

struct EnvironmentProfile {
    float noiseLevel = 0.0f;
    float echoDetection = 0.0f;
    float signalQuality = 0.0f;
    float microphoneQuality = 0.0f;
    bool hasBackgroundSpeech = false;
    float reverberation = 0.0f;
    Environment environment = Environment::UNKNOWN;
    float confidence = 0.0f;
};

enum class ProcessingMode {
    Fast,
    Balanced,
    Quality
};

struct ProcessingConfig {
    bool enableNoiseReduction = false;
    bool enableEchoCancellation = false;
    bool enableMultiPass = false;
    bool enableAdvancedVAD = true;
    int whisperBeamSize = 1;
    int maxLatencyMs = 300;
    ProcessingMode mode = ProcessingMode::Balanced;
    QString description = QStringLiteral("Default balanced processing");

    bool operator==(const ProcessingConfig& other) const
    {
        return enableNoiseReduction == other.enableNoiseReduction
            && enableEchoCancellation == other.enableEchoCancellation
            && enableMultiPass == other.enableMultiPass
            && enableAdvancedVAD == other.enableAdvancedVAD
            && whisperBeamSize == other.whisperBeamSize
            && maxLatencyMs == other.maxLatencyMs
            && mode == other.mode
            && description == other.description;
    }
    bool operator!=(const ProcessingConfig& other) const { return !(*this == other); }
};
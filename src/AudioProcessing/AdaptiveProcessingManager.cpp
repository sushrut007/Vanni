#include "AudioProcessing/AdaptiveProcessingManager.h"
#include <QMutexLocker>
#include <algorithm>

AdaptiveProcessingManager::AdaptiveProcessingManager(QObject* parent) : QObject(parent) {
    initializeDefaultConfigs();
}

void AdaptiveProcessingManager::initializeDefaultConfigs() {
    // Quiet Office - minimal processing for speed
    m_configs[Environment::QUIET_OFFICE] = ProcessingConfig{
        false,      // enableNoiseReduction
        false,      // enableEchoCancellation
        false,      // enableMultiPass
        true,       // enableAdvancedVAD
        1,          // whisperBeamSize
        200,        // maxLatencyMs
        ProcessingMode::Fast,
        "Optimized for speed in quiet environment"
    };

    // Noisy Office - balanced processing
    m_configs[Environment::NOISY_OFFICE] = ProcessingConfig{
        true,       // enableNoiseReduction
        false,      // enableEchoCancellation
        false,      // enableMultiPass
        true,       // enableAdvancedVAD
        3,          // whisperBeamSize
        400,        // maxLatencyMs
        ProcessingMode::Balanced,
        "Balanced processing for office environment"
    };

    // Mobile - aggressive processing for poor conditions
    m_configs[Environment::MOBILE] = ProcessingConfig{
        true,       // enableNoiseReduction
        true,       // enableEchoCancellation
        true,       // enableMultiPass
        true,       // enableAdvancedVAD
        5,          // whisperBeamSize
        800,        // maxLatencyMs
        ProcessingMode::Quality,
        "Maximum processing for mobile environment"
    };

    // Poor Microphone - focus on signal enhancement
    m_configs[Environment::POOR_MICROPHONE] = ProcessingConfig{
        true,       // enableNoiseReduction
        false,      // enableEchoCancellation
        true,       // enableMultiPass
        true,       // enableAdvancedVAD
        3,          // whisperBeamSize
        600,        // maxLatencyMs
        ProcessingMode::Quality,
        "Enhanced processing for poor microphone"
    };

    // Phone Call - echo-focused processing
    m_configs[Environment::PHONE_CALL] = ProcessingConfig{
        true,       // enableNoiseReduction
        true,       // enableEchoCancellation
        false,      // enableMultiPass
        true,       // enableAdvancedVAD
        2,          // whisperBeamSize
        500,        // maxLatencyMs
        ProcessingMode::Balanced,
        "Echo cancellation for phone calls"
    };

    // Presentation - distance-optimized processing
    m_configs[Environment::PRESENTATION] = ProcessingConfig{
        true,       // enableNoiseReduction
        true,       // enableEchoCancellation
        true,       // enableMultiPass
        true,       // enableAdvancedVAD
        4,          // whisperBeamSize
        700,        // maxLatencyMs
        ProcessingMode::Quality,
        "Optimized for presentation environments"
    };

    // Home Quiet - light processing
    m_configs[Environment::HOME_QUIET] = ProcessingConfig{
        false,      // enableNoiseReduction
        false,      // enableEchoCancellation
        false,      // enableMultiPass
        true,       // enableAdvancedVAD
        2,          // whisperBeamSize
        300,        // maxLatencyMs
        ProcessingMode::Fast,
        "Light processing for quiet home"
    };

    // Home Noisy - moderate processing
    m_configs[Environment::HOME_NOISY] = ProcessingConfig{
        true,       // enableNoiseReduction
        false,      // enableEchoCancellation
        false,      // enableMultiPass
        true,       // enableAdvancedVAD
        3,          // whisperBeamSize
        450,        // maxLatencyMs
        ProcessingMode::Balanced,
        "Moderate processing for noisy home"
    };

    // Unknown - conservative balanced approach
    m_configs[Environment::UNKNOWN] = ProcessingConfig{
        true,       // enableNoiseReduction
        false,      // enableEchoCancellation
        false,      // enableMultiPass
        true,       // enableAdvancedVAD
        2,          // whisperBeamSize
        400,        // maxLatencyMs
        ProcessingMode::Balanced,
        "Balanced processing for unknown environment"
    };

    m_currentConfig = m_configs[Environment::UNKNOWN];
}

ProcessingConfig AdaptiveProcessingManager::adjustForUserPreference(const ProcessingConfig& baseConfig) const {
    ProcessingConfig adjusted = baseConfig;

    switch (m_userPreferredMode) {
    case ProcessingMode::Fast:
        adjusted.enableMultiPass = false;
        adjusted.whisperBeamSize = std::min(adjusted.whisperBeamSize, 2);
        adjusted.maxLatencyMs = std::min(adjusted.maxLatencyMs, 300);
        adjusted.mode = ProcessingMode::Fast;
        break;

    case ProcessingMode::Quality:
        adjusted.enableMultiPass = true;
        adjusted.whisperBeamSize = std::max(adjusted.whisperBeamSize, 3);
        adjusted.maxLatencyMs = std::max(adjusted.maxLatencyMs, 500);
        adjusted.mode = ProcessingMode::Quality;
        break;

    case ProcessingMode::Balanced:
    default:
        // Keep base configuration
        break;
    }

    return adjusted;
}

void AdaptiveProcessingManager::setAdaptiveEnabled(bool enabled) {
    QMutexLocker locker(&m_mutex);
    m_adaptiveEnabled = enabled;

    if (!enabled) {
        ProcessingConfig uniformConfig = m_configs[Environment::UNKNOWN];
        uniformConfig = adjustForUserPreference(uniformConfig);
        m_currentConfig = uniformConfig;
        const ProcessingConfig changed = m_currentConfig;
        locker.unlock();
        emit configurationChanged(changed);
    }
}

void AdaptiveProcessingManager::setUserPreferredMode(ProcessingMode mode) {
    QMutexLocker locker(&m_mutex);
    m_userPreferredMode = mode;

    ProcessingConfig newConfig = m_configs[m_currentEnvironment];
    newConfig = adjustForUserPreference(newConfig);

    if (newConfig != m_currentConfig) {
        m_currentConfig = newConfig;
        const ProcessingConfig changed = m_currentConfig;
        locker.unlock();
        emit configurationChanged(changed);
    }
}

void AdaptiveProcessingManager::updateForEnvironment(Environment environment) {
    QMutexLocker locker(&m_mutex);

    if (!m_adaptiveEnabled || environment == m_currentEnvironment) {
        return;
    }

    m_currentEnvironment = environment;

    if (m_configs.find(environment) != m_configs.end()) {
        ProcessingConfig newConfig = m_configs[environment];
        newConfig = adjustForUserPreference(newConfig);

        if (newConfig != m_currentConfig) {
            m_currentConfig = newConfig;
            const ProcessingConfig changed = m_currentConfig;
            locker.unlock();
            emit configurationChanged(changed);
            emit environmentConfigChanged(environment, changed);
        }
    }
}

ProcessingConfig AdaptiveProcessingManager::getCurrentConfig() const {
    QMutexLocker locker(&m_mutex);
    return m_currentConfig;
}

ProcessingConfig AdaptiveProcessingManager::getConfigForEnvironment(Environment environment) const {
    QMutexLocker locker(&m_mutex);
    auto it = m_configs.find(environment);
    if (it != m_configs.end()) {
        return adjustForUserPreference(it->second);
    }
    return m_currentConfig;
}

void AdaptiveProcessingManager::setCustomConfig(Environment environment, const ProcessingConfig& config) {
    QMutexLocker locker(&m_mutex);
    m_configs[environment] = config;

    if (environment == m_currentEnvironment) {
        m_currentConfig = adjustForUserPreference(config);
        const ProcessingConfig changed = m_currentConfig;
        locker.unlock();
        emit configurationChanged(changed);
    }
}

bool AdaptiveProcessingManager::isAdaptiveEnabled() const {
    QMutexLocker locker(&m_mutex);
    return m_adaptiveEnabled;
}

ProcessingMode AdaptiveProcessingManager::getUserPreferredMode() const {
    QMutexLocker locker(&m_mutex);
    return m_userPreferredMode;
}

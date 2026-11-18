#pragma once

#include "AudioProcessing/ProcessingTypes.h"

#include <QMutex>
#include <QObject>
#include <unordered_map>

class AdaptiveProcessingManager : public QObject
{
    Q_OBJECT

public:
    explicit AdaptiveProcessingManager(QObject* parent = nullptr);

    void setAdaptiveEnabled(bool enabled);
    void setUserPreferredMode(ProcessingMode mode);
    void updateForEnvironment(Environment environment);
    ProcessingConfig getCurrentConfig() const;
    ProcessingConfig getConfigForEnvironment(Environment environment) const;
    void setCustomConfig(Environment environment, const ProcessingConfig& config);
    bool isAdaptiveEnabled() const;
    ProcessingMode getUserPreferredMode() const;

signals:
    void configurationChanged(const ProcessingConfig& config);
    void environmentConfigChanged(Environment environment, const ProcessingConfig& config);

private:
    mutable QMutex m_mutex;
    std::unordered_map<Environment, ProcessingConfig> m_configs;
    ProcessingConfig m_currentConfig;
    Environment m_currentEnvironment = Environment::UNKNOWN;
    bool m_adaptiveEnabled = true;
    ProcessingMode m_userPreferredMode = ProcessingMode::Balanced;

    void initializeDefaultConfigs();
    ProcessingConfig adjustForUserPreference(const ProcessingConfig& baseConfig) const;
};
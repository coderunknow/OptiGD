#pragma once

#include "../core/Types.hpp"
#include <mutex>
#include <string>

namespace optigd::config {

class ConfigManager {
public:
    static ConfigManager& instance();

    const core::OptiConfig& getConfig() const;
    void setConfig(const core::OptiConfig& config);

    void applyTierPreset(core::PerformanceTier tier);
    void setTargetFrameRate(float fps);

    // Serialization
    std::string serialize() const;
    bool deserialize(const std::string& serializedData);

    bool saveToFile(const std::string& filePath) const;
    bool loadFromFile(const std::string& filePath);

    void resetToDefaults();

private:
    ConfigManager();
    mutable std::mutex m_mutex;
    core::OptiConfig m_config;
};

} // namespace optigd::config

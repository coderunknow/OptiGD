#pragma once

#include "../core/Types.hpp"
#include <mutex>
#include <string>

namespace optigd::config {

// Holds the user's configuration (what the settings say). Runtime adjustments made by
// the adaptive governor are kept separately by OptiGDManager and never written here.
class ConfigManager {
public:
    static ConfigManager& instance();

    core::OptiConfig getConfig() const;   // copy, safe to use from any thread
    void setConfig(const core::OptiConfig& config);

    void applyTierPreset(core::PerformanceTier tier);
    void setTargetFrameRate(float fps);   // non-finite or non-positive values are ignored

    // Line-based key=value serialization of every persisted field.
    std::string serialize() const;
    // Unknown keys and malformed values are skipped; returns false if no valid key was read.
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

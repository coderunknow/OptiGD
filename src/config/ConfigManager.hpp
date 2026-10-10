#pragma once

#include "../core/Types.hpp"
#include <mutex>
#include <string>
#include <string_view>

namespace optigd::config {

struct RuntimeSettingsInput {
    std::string tierPreset{"LowEnd"};
    bool enableSpatialCulling{true};
    bool enableParticleThrottling{true};
    bool enableAdaptiveGovernor{true};
    bool enableMatrixCaching{true};
    bool enableTriggerIndexing{false};
    bool enableDiagnosticsOverlay{false};
    bool exportDiagnosticsOnExit{true};

    // Custom profile parameters (used when tierPreset == "Custom")
    float customParticleDensity{0.65f};
    uint32_t customMaxParticles{3000};
    bool customCullOffscreenParticles{true};
    float customCameraPaddingX{150.0f};
    float customCameraPaddingY{100.0f};
    float targetFrameRate{60.0f};
};

class ConfigManager {
public:
    static ConfigManager& instance();

    core::OptiConfig getConfig() const;
    void setConfig(const core::OptiConfig& config);

    void applyTierPreset(core::PerformanceTier tier);
    void setTargetFrameRate(float fps);

    // Synchronizes Geode mod settings with runtime config without stale-copy overwrite
    core::OptiConfig syncFromSettings(const RuntimeSettingsInput& input);

    static core::PerformanceTier parseTierString(std::string_view tierStr);

    // Serialization
    std::string serialize() const;
    bool deserialize(const std::string& serializedData);

    bool saveToFile(const std::string& filePath) const;
    bool loadFromFile(const std::string& filePath);

    void resetToDefaults();

private:
    ConfigManager();
    void applyTierPresetUnlocked(core::PerformanceTier tier);

    mutable std::mutex m_mutex;
    core::OptiConfig m_config;
};

} // namespace optigd::config

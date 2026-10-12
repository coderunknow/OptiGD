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

// Two-layer configuration.
//
// `m_config` is the BASE layer: what the user's Geode settings resolve to. It is
// the only layer written by `syncFromSettings()`, `deserialize()`,
// `resetToDefaults()` and `applyTierPreset()`, and the only layer persisted.
//
// `m_override` is a TRANSIENT layer used by Benchmark Mode. While it is active,
// `getConfig()` returns it and the base layer is left untouched, so:
//   * a settings listener firing mid-session (Geode calls
//     `listenForSettingChanges` handlers, and `PlayLayer::init` calls
//     `syncSettingsWithConfig()`) cannot clobber the measurement
//     configuration;
//   * clearing the override restores exactly what the user had, including any
//     change they made to their settings while the session was running;
//   * `setConfig()` — used by `OptiGDManager::endFrame()` to write back
//     adaptive-tier and safety-fallback changes — updates the ACTIVE layer, so
//     a mid-session tier change never leaks into the user's saved preferences.
//
// Without this split there was nowhere to hold a temporary configuration: the
// single `m_config` member was written by both the settings listeners and the
// per-frame adaptive write-back, so any transient state was destroyed by the
// next settings event and restoring it would have overwritten user data.
class ConfigManager {
public:
    static ConfigManager& instance();

    // Returns the override when one is active, otherwise the base config.
    core::OptiConfig getConfig() const;
    // The user's saved/effective settings, ignoring any transient override.
    core::OptiConfig getBaseConfig() const;
    // Writes to the ACTIVE layer (override when one is active).
    void setConfig(const core::OptiConfig& config);
    // Writes to the base layer explicitly, ignoring any active override.
    void setBaseConfig(const core::OptiConfig& config);

    // --- transient runtime override (Benchmark Mode) ------------------------
    bool setRuntimeOverride(const core::OptiConfig& overrideConfig, std::string reason);
    // Re-asserts the configuration of an already-armed override without changing
    // its reason. Returns false when no override is armed, so a re-apply can
    // never silently install a new one.
    bool updateRuntimeOverride(const core::OptiConfig& overrideConfig);
    // Restores the base layer. Returns false when no override was active, so a
    // caller can detect a double-restore instead of silently doing nothing.
    bool clearRuntimeOverride();
    bool hasRuntimeOverride() const;
    std::string getRuntimeOverrideReason() const;
    core::OptiConfig getRuntimeOverrideConfig() const;

    void applyTierPreset(core::PerformanceTier tier);
    void setTargetFrameRate(float fps);

    // Synchronizes Geode mod settings with the BASE runtime config without a
    // stale-copy overwrite. Never touches an active override.
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
    core::OptiConfig m_config;                 // base layer (user settings)
    core::OptiConfig m_override;               // transient layer
    bool m_overrideActive{false};
    std::string m_overrideReason;
};

} // namespace optigd::config

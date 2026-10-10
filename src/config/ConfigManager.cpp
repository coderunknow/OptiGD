#include "ConfigManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace optigd::config {

namespace {

// Persisted configuration is user-editable text and may be truncated, merged or
// written by an older OptiGD build. Every conversion is therefore guarded so a
// malformed line is skipped instead of throwing out of the loader.
bool parseInt(std::string const& text, int& out) {
    try {
        size_t consumed = 0;
        int value = std::stoi(text, &consumed);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseFloat(std::string const& text, float& out) {
    try {
        size_t consumed = 0;
        float value = std::stof(text, &consumed);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseUnsigned(std::string const& text, uint32_t& out) {
    try {
        size_t consumed = 0;
        unsigned long value = std::stoul(text, &consumed);
        if (consumed != text.size()) return false;
        out = static_cast<uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

ConfigManager::ConfigManager() {
    resetToDefaults();
}

ConfigManager& ConfigManager::instance() {
    static ConfigManager s_instance;
    return s_instance;
}

core::PerformanceTier ConfigManager::parseTierString(std::string_view tierStr) {
    if (tierStr == "LowEnd") return core::PerformanceTier::LowEnd;
    if (tierStr == "Balanced") return core::PerformanceTier::Balanced;
    if (tierStr == "HighEnd") return core::PerformanceTier::HighEnd;
    if (tierStr == "Custom") return core::PerformanceTier::Custom;
    return core::PerformanceTier::LowEnd;
}

void ConfigManager::resetToDefaults() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = core::OptiConfig{};
    applyTierPresetUnlocked(core::PerformanceTier::LowEnd);
    m_config.spatialCellSize = 128.0f;
    m_config.targetFrameRate = 60.0f;
    m_config.targetFrameTimeMs = 16.666f;
    m_config.fallbackVarianceThresholdMs = 4.0f;
    m_config.fallbackCooldownFrames = 120;
}

core::OptiConfig ConfigManager::getConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void ConfigManager::setConfig(const core::OptiConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void ConfigManager::applyTierPresetUnlocked(core::PerformanceTier tier) {
    m_config.tier = tier;
    // Single source of truth: see core::kTierDefaults in Types.hpp.
    // For PerformanceTier::Custom this is a no-op and existing custom numeric
    // values are preserved.
    core::applyTierDefaults(m_config, tier);
}

void ConfigManager::applyTierPreset(core::PerformanceTier tier) {
    std::lock_guard<std::mutex> lock(m_mutex);
    applyTierPresetUnlocked(tier);
}

void ConfigManager::setTargetFrameRate(float fps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (fps <= 0.0f || !std::isfinite(fps)) fps = 60.0f;
    m_config.targetFrameRate = fps;
    m_config.targetFrameTimeMs = 1000.0f / fps;
}

core::OptiConfig ConfigManager::syncFromSettings(const RuntimeSettingsInput& input) {
    std::lock_guard<std::mutex> lock(m_mutex);

    core::PerformanceTier tier = parseTierString(input.tierPreset);
    applyTierPresetUnlocked(tier);

    if (tier == core::PerformanceTier::Custom) {
        float density = std::isfinite(input.customParticleDensity) ? input.customParticleDensity : 0.65f;
        m_config.particleDensityScale = std::clamp(density, 0.05f, 1.0f);
        m_config.maxActiveParticles = std::clamp<uint32_t>(input.customMaxParticles, 128, 32768);
        m_config.cullOffscreenEmitters = input.customCullOffscreenParticles;
        m_config.cameraPaddingX = std::clamp(input.customCameraPaddingX, 0.0f, 2000.0f);
        m_config.cameraPaddingY = std::clamp(input.customCameraPaddingY, 0.0f, 2000.0f);
    }

    float fps = (input.targetFrameRate > 0.0f && std::isfinite(input.targetFrameRate))
                    ? std::clamp(input.targetFrameRate, 24.0f, 1000.0f)
                    : 60.0f;
    m_config.targetFrameRate = fps;
    m_config.targetFrameTimeMs = 1000.0f / fps;

    m_config.flags.enableSpatialCulling = input.enableSpatialCulling;
    m_config.flags.enableParticleThrottling = input.enableParticleThrottling;
    m_config.flags.enableAdaptiveGovernor = input.enableAdaptiveGovernor;
    m_config.flags.enableMatrixCaching = input.enableMatrixCaching;
    m_config.flags.enableTriggerIndexing = input.enableTriggerIndexing;
    m_config.flags.enableDiagnosticsOverlay = input.enableDiagnosticsOverlay;
    m_config.flags.exportDiagnosticsOnExit = input.exportDiagnosticsOnExit;

    return m_config;
}

std::string ConfigManager::serialize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "tier=" << static_cast<int>(m_config.tier) << "\n";
    ss << "targetFps=" << m_config.targetFrameRate << "\n";
    ss << "spatialCulling=" << (m_config.flags.enableSpatialCulling ? 1 : 0) << "\n";
    ss << "particleThrottling=" << (m_config.flags.enableParticleThrottling ? 1 : 0) << "\n";
    ss << "triggerIndexing=" << (m_config.flags.enableTriggerIndexing ? 1 : 0) << "\n";
    ss << "matrixCaching=" << (m_config.flags.enableMatrixCaching ? 1 : 0) << "\n";
    ss << "adaptiveGovernor=" << (m_config.flags.enableAdaptiveGovernor ? 1 : 0) << "\n";
    ss << "diagnosticsOverlay=" << (m_config.flags.enableDiagnosticsOverlay ? 1 : 0) << "\n";
    ss << "exportDiagnosticsOnExit=" << (m_config.flags.exportDiagnosticsOnExit ? 1 : 0) << "\n";
    ss << "particleDensityScale=" << m_config.particleDensityScale << "\n";
    ss << "maxActiveParticles=" << m_config.maxActiveParticles << "\n";
    ss << "cullOffscreenEmitters=" << (m_config.cullOffscreenEmitters ? 1 : 0) << "\n";
    ss << "cameraPaddingX=" << m_config.cameraPaddingX << "\n";
    ss << "cameraPaddingY=" << m_config.cameraPaddingY << "\n";
    return ss.str();
}

bool ConfigManager::deserialize(const std::string& serializedData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::istringstream ss(serializedData);
    std::string line;
    bool parsedAny = false;
    while (std::getline(ss, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        parsedAny = true;

        if (key == "tier") {
            int t = 0;
            if (parseInt(val, t) && t >= 0 && t <= 3) {
                m_config.tier = static_cast<core::PerformanceTier>(t);
            }
        } else if (key == "targetFps") {
            float fps = 0.0f;
            if (parseFloat(val, fps) && fps > 0.0f && std::isfinite(fps)) {
                m_config.targetFrameRate = fps;
                m_config.targetFrameTimeMs = 1000.0f / fps;
            }
        } else if (key == "spatialCulling") {
            m_config.flags.enableSpatialCulling = (val == "1");
        } else if (key == "particleThrottling") {
            m_config.flags.enableParticleThrottling = (val == "1");
        } else if (key == "triggerIndexing") {
            m_config.flags.enableTriggerIndexing = (val == "1");
        } else if (key == "matrixCaching") {
            m_config.flags.enableMatrixCaching = (val == "1");
        } else if (key == "adaptiveGovernor") {
            m_config.flags.enableAdaptiveGovernor = (val == "1");
        } else if (key == "diagnosticsOverlay") {
            m_config.flags.enableDiagnosticsOverlay = (val == "1");
        } else if (key == "exportDiagnosticsOnExit") {
            m_config.flags.exportDiagnosticsOnExit = (val == "1");
        } else if (key == "particleDensityScale") {
            float density = 0.0f;
            if (parseFloat(val, density) && std::isfinite(density)) {
                m_config.particleDensityScale = std::clamp(density, 0.05f, 1.0f);
            }
        } else if (key == "maxActiveParticles") {
            uint32_t cap = 0;
            if (parseUnsigned(val, cap)) {
                m_config.maxActiveParticles = cap;
            }
        } else if (key == "cullOffscreenEmitters") {
            m_config.cullOffscreenEmitters = (val == "1");
        } else if (key == "cameraPaddingX") {
            float padX = 0.0f;
            if (parseFloat(val, padX) && std::isfinite(padX)) {
                m_config.cameraPaddingX = padX;
            }
        } else if (key == "cameraPaddingY") {
            float padY = 0.0f;
            if (parseFloat(val, padY) && std::isfinite(padY)) {
                m_config.cameraPaddingY = padY;
            }
        }
    }
    return parsedAny;
}

bool ConfigManager::saveToFile(const std::string& filePath) const {
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) return false;
    ofs << serialize();
    return true;
}

bool ConfigManager::loadFromFile(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    return deserialize(content);
}

} // namespace optigd::config

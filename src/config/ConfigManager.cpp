#include "ConfigManager.hpp"
#include "Presets.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace optigd::config {

namespace {

bool parseFloat(const std::string& text, float& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno != 0 || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool parseUnsigned(const std::string& text, uint32_t& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno != 0 || v > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseInt(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno != 0 || v < 0 || v > 255) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace

ConfigManager::ConfigManager() {
    resetToDefaults();
}

ConfigManager& ConfigManager::instance() {
    static ConfigManager s_instance;
    return s_instance;
}

void ConfigManager::resetToDefaults() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = core::OptiConfig{};
    applyPresetTier(m_config, core::PerformanceTier::LowEnd);
    m_config.targetFrameRate = 60.0f;
    m_config.targetFrameTimeMs = 1000.0f / 60.0f;
}

core::OptiConfig ConfigManager::getConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void ConfigManager::setConfig(const core::OptiConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void ConfigManager::applyTierPreset(core::PerformanceTier tier) {
    std::lock_guard<std::mutex> lock(m_mutex);
    applyPresetTier(m_config, tier);
}

void ConfigManager::setTargetFrameRate(float fps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!std::isfinite(fps) || fps <= 0.0f) return;
    m_config.targetFrameRate = fps;
    m_config.targetFrameTimeMs = 1000.0f / fps;
}

std::string ConfigManager::serialize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss.precision(9);
    ss << "tier=" << static_cast<int>(m_config.tier) << "\n";
    ss << "targetFps=" << m_config.targetFrameRate << "\n";
    ss << "spatialCulling=" << (m_config.flags.enableSpatialCulling ? 1 : 0) << "\n";
    ss << "particleThrottling=" << (m_config.flags.enableParticleThrottling ? 1 : 0) << "\n";
    ss << "triggerIndexing=" << (m_config.flags.enableTriggerIndexing ? 1 : 0) << "\n";
    ss << "matrixCaching=" << (m_config.flags.enableMatrixCaching ? 1 : 0) << "\n";
    ss << "adaptiveGovernor=" << (m_config.flags.enableAdaptiveGovernor ? 1 : 0) << "\n";
    ss << "diagnosticsOverlay=" << (m_config.flags.enableDiagnosticsOverlay ? 1 : 0) << "\n";
    ss << "frameTimeRecorder=" << (m_config.flags.enableFrameTimeRecorder ? 1 : 0) << "\n";
    ss << "particleDensityScale=" << m_config.particleDensityScale << "\n";
    ss << "maxActiveParticles=" << m_config.maxActiveParticles << "\n";
    ss << "cullOffscreenEmitters=" << (m_config.cullOffscreenEmitters ? 1 : 0) << "\n";
    ss << "cameraPaddingX=" << m_config.cameraPaddingX << "\n";
    ss << "cameraPaddingY=" << m_config.cameraPaddingY << "\n";
    ss << "spatialCellSize=" << m_config.spatialCellSize << "\n";
    return ss.str();
}

bool ConfigManager::deserialize(const std::string& serializedData) {
    std::lock_guard<std::mutex> lock(m_mutex);
    core::OptiConfig cfg = m_config;
    bool anyValid = false;

    std::istringstream ss(serializedData);
    std::string line;
    while (std::getline(ss, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        int intVal = 0;
        uint32_t uintVal = 0;
        float floatVal = 0.0f;
        if (key == "tier") {
            if (parseInt(val, intVal) && intVal <= 3) {
                cfg.tier = static_cast<core::PerformanceTier>(intVal);
                anyValid = true;
            }
        } else if (key == "targetFps") {
            if (parseFloat(val, floatVal) && floatVal > 0.0f) {
                cfg.targetFrameRate = floatVal;
                cfg.targetFrameTimeMs = 1000.0f / floatVal;
                anyValid = true;
            }
        } else if (key == "spatialCulling") {
            cfg.flags.enableSpatialCulling = (val == "1");
            anyValid = true;
        } else if (key == "particleThrottling") {
            cfg.flags.enableParticleThrottling = (val == "1");
            anyValid = true;
        } else if (key == "triggerIndexing") {
            cfg.flags.enableTriggerIndexing = (val == "1");
            anyValid = true;
        } else if (key == "matrixCaching") {
            cfg.flags.enableMatrixCaching = (val == "1");
            anyValid = true;
        } else if (key == "adaptiveGovernor") {
            cfg.flags.enableAdaptiveGovernor = (val == "1");
            anyValid = true;
        } else if (key == "diagnosticsOverlay") {
            cfg.flags.enableDiagnosticsOverlay = (val == "1");
            anyValid = true;
        } else if (key == "frameTimeRecorder") {
            cfg.flags.enableFrameTimeRecorder = (val == "1");
            anyValid = true;
        } else if (key == "particleDensityScale") {
            if (parseFloat(val, floatVal)) {
                cfg.particleDensityScale = std::clamp(floatVal, 0.05f, 1.0f);
                anyValid = true;
            }
        } else if (key == "maxActiveParticles") {
            if (parseUnsigned(val, uintVal)) {
                cfg.maxActiveParticles = uintVal;
                anyValid = true;
            }
        } else if (key == "cullOffscreenEmitters") {
            cfg.cullOffscreenEmitters = (val == "1");
            anyValid = true;
        } else if (key == "cameraPaddingX") {
            if (parseFloat(val, floatVal) && floatVal >= 0.0f) {
                cfg.cameraPaddingX = floatVal;
                anyValid = true;
            }
        } else if (key == "cameraPaddingY") {
            if (parseFloat(val, floatVal) && floatVal >= 0.0f) {
                cfg.cameraPaddingY = floatVal;
                anyValid = true;
            }
        } else if (key == "spatialCellSize") {
            if (parseFloat(val, floatVal) && floatVal > 0.0f) {
                cfg.spatialCellSize = floatVal;
                anyValid = true;
            }
        }
    }
    if (anyValid) {
        m_config = cfg;
    }
    return anyValid;
}

bool ConfigManager::saveToFile(const std::string& filePath) const {
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) return false;
    ofs << serialize();
    return ofs.good();
}

bool ConfigManager::loadFromFile(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return deserialize(content);
}

} // namespace optigd::config

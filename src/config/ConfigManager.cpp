#include "ConfigManager.hpp"
#include <fstream>
#include <sstream>

namespace optigd::config {

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
    // Default tier is LowEnd (High Performance) per user selection
    m_config.tier = core::PerformanceTier::LowEnd;
    m_config.particleDensityScale = 0.5f;
    m_config.maxActiveParticles = 2048;
    m_config.cullOffscreenEmitters = true;
    m_config.spatialCellSize = 128.0f;
    m_config.cameraPaddingX = 120.0f;
    m_config.cameraPaddingY = 80.0f;
    m_config.targetFrameRate = 60.0f;
    m_config.targetFrameTimeMs = 16.666f;
    m_config.fallbackVarianceThresholdMs = 4.0f;
    m_config.fallbackCooldownFrames = 120;
}

const core::OptiConfig& ConfigManager::getConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void ConfigManager::setConfig(const core::OptiConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void ConfigManager::applyTierPreset(core::PerformanceTier tier) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.tier = tier;
    if (tier == core::PerformanceTier::LowEnd) {
        m_config.particleDensityScale = 0.5f;
        m_config.maxActiveParticles = 2048;
        m_config.cullOffscreenEmitters = true;
        m_config.cameraPaddingX = 120.0f;
        m_config.cameraPaddingY = 80.0f;
    } else if (tier == core::PerformanceTier::Balanced) {
        m_config.particleDensityScale = 0.75f;
        m_config.maxActiveParticles = 4096;
        m_config.cullOffscreenEmitters = true;
        m_config.cameraPaddingX = 150.0f;
        m_config.cameraPaddingY = 100.0f;
    } else if (tier == core::PerformanceTier::HighEnd) {
        m_config.particleDensityScale = 1.0f;
        m_config.maxActiveParticles = 8192;
        m_config.cullOffscreenEmitters = false;
        m_config.cameraPaddingX = 200.0f;
        m_config.cameraPaddingY = 150.0f;
    }
}

void ConfigManager::setTargetFrameRate(float fps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (fps <= 0.0f) fps = 60.0f;
    m_config.targetFrameRate = fps;
    m_config.targetFrameTimeMs = 1000.0f / fps;
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
    while (std::getline(ss, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "tier") {
            int t = std::stoi(val);
            m_config.tier = static_cast<core::PerformanceTier>(t);
        } else if (key == "targetFps") {
            m_config.targetFrameRate = std::stof(val);
            m_config.targetFrameTimeMs = 1000.0f / m_config.targetFrameRate;
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
        } else if (key == "particleDensityScale") {
            m_config.particleDensityScale = std::stof(val);
        } else if (key == "maxActiveParticles") {
            m_config.maxActiveParticles = std::stoul(val);
        } else if (key == "cullOffscreenEmitters") {
            m_config.cullOffscreenEmitters = (val == "1");
        } else if (key == "cameraPaddingX") {
            m_config.cameraPaddingX = std::stof(val);
        } else if (key == "cameraPaddingY") {
            m_config.cameraPaddingY = std::stof(val);
        }
    }
    return true;
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

#include "OptiGDManager.hpp"
#include "Logger.hpp"
#include "../config/ConfigManager.hpp"
#include "../compatibility/CompatibilityLayer.hpp"
#include "../diagnostics/DiagnosticsEngine.hpp"
#include "../platform/Platform.hpp"
#include "Version.hpp"

#include <algorithm>
#include <vector>

namespace optigd::core {

namespace {

// Adds the elapsed time of the enclosing scope to an accumulator.
class OverheadTimer {
public:
    explicit OverheadTimer(double& accumulatorUs)
        : m_accumulator(accumulatorUs), m_start(platform::Platform::getTimestampNs()) {}
    ~OverheadTimer() {
        m_accumulator += static_cast<double>(platform::Platform::getTimestampNs() - m_start) / 1000.0;
    }

private:
    double& m_accumulator;
    uint64_t m_start;
};

} // namespace

OptiGDManager::OptiGDManager()
    : m_culler(128.0f),
      m_particleGovernor(0.5f, 2048),
      m_triggerOptimizer(800.0f) {}

OptiGDManager::~OptiGDManager() {
    shutdown();
}

OptiGDManager& OptiGDManager::instance() {
    static OptiGDManager s_instance;
    return s_instance;
}

OptiConfig OptiGDManager::getUserConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_userConfig;
}

OptiConfig OptiGDManager::getEffectiveConfig() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_effectiveConfig;
}

void OptiGDManager::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;

    OPTIGD_LOG_INFO(std::string("Initializing OptiGD v") + kOptiGDVersion + " engine...");
    const auto sys = platform::Platform::getSystemInfo();
    OPTIGD_LOG_INFO("Platform: " + sys.osName + ", cores: " + std::to_string(sys.logicalCores) +
                    ", RAM: " + std::to_string(sys.totalRamMb) + " MB, compiler: " + sys.compiler);

    compat::CompatibilityLayer::instance().initialize(optigd::kGeometryDashVersion, kOptiGDGeodeVersion);

    m_userConfig = config::ConfigManager::instance().getConfig();
    m_culler.setCellSize(m_userConfig.spatialCellSize);
    m_adaptiveGovernor.reset(m_userConfig);
    m_adaptiveGovernor.setDynamicAdaptationEnabled(m_userConfig.flags.enableAdaptiveGovernor);
    m_effectiveConfig = m_userConfig;
    applyEffectiveConfigLocked(m_effectiveConfig);

    m_initialized = true;
    OPTIGD_LOG_INFO(std::string("OptiGD initialized. Profile: ") + tierToString(m_userConfig.tier));
}

void OptiGDManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    OPTIGD_LOG_INFO("Shutting down OptiGD engine...");
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    m_frameArena.reset();
    m_levelActive = false;
    m_initialized = false;
}

void OptiGDManager::onSettingsChanged() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userConfig = config::ConfigManager::instance().getConfig();
    m_culler.setCellSize(m_userConfig.spatialCellSize);
    m_adaptiveGovernor.reset(m_userConfig);
    m_adaptiveGovernor.setDynamicAdaptationEnabled(m_userConfig.flags.enableAdaptiveGovernor);
    m_fallbackManager.reset();
    m_effectiveConfig = m_userConfig;
    applyEffectiveConfigLocked(m_effectiveConfig);
    OPTIGD_LOG_INFO(std::string("Settings applied. Profile: ") + tierToString(m_userConfig.tier));
}

void OptiGDManager::onLevelLoaded() {
    std::lock_guard<std::mutex> lock(m_mutex);
    OPTIGD_LOG_INFO("Level loaded: resetting per-level optimization state.");
    m_levelActive = true;
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.prepareLevel();
    m_fallbackManager.reset();
    m_hasLastBegin = false; // the next frame has no valid interval
}

void OptiGDManager::onLevelExited() {
    std::lock_guard<std::mutex> lock(m_mutex);
    OPTIGD_LOG_INFO("Level exited: clearing level resources.");
    m_levelActive = false;
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    m_hasLastBegin = false;
}

void OptiGDManager::applyEffectiveConfigLocked(const OptiConfig& effective) {
    m_culler.setPadding(effective.cameraPaddingX, effective.cameraPaddingY);
    m_particleGovernor.setDensityScale(effective.particleDensityScale);
    m_particleGovernor.setGlobalParticleLimit(effective.maxActiveParticles);
    m_particleGovernor.setCullOffscreen(effective.cullOffscreenEmitters);
    m_adaptiveGovernor.setTargetFrameRate(effective.targetFrameRate);
    m_effectiveConfig = effective;
}

void OptiGDManager::beginFrame(uint64_t frameIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    OverheadTimer timer(m_overheadAccumUs);

    const uint64_t now = platform::Platform::getTimestampNs();
    m_currentStats = FrameStats{};
    m_currentStats.frameIndex = frameIndex;
    m_currentStats.frameIntervalUs = m_hasLastBegin
        ? static_cast<double>(now - m_lastBeginUs) / 1000.0
        : 0.0;
    m_lastBeginUs = now;
    m_hasLastBegin = true;
    m_frameOpen = true;

    m_frameArena.reset();
    m_arenaAllocBase = m_frameArena.getAllocationCount();
    m_overheadAccumUs = 0.0;
}

void OptiGDManager::markGameUpdateBegin() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gameUpdateStartUs = platform::Platform::getTimestampNs();
}

void OptiGDManager::markGameUpdateEnd() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_gameUpdateStartUs == 0) return;
    const uint64_t now = platform::Platform::getTimestampNs();
    m_currentStats.gameUpdateUs = static_cast<double>(now - m_gameUpdateStartUs) / 1000.0;
    m_gameUpdateStartUs = 0;
}

void OptiGDManager::recordCullingShadow(const Rect2D& cameraRect,
                                        const std::function<void(optimization::VisibilityCuller&)>& refresh) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_frameOpen) return;
    OverheadTimer timer(m_overheadAccumUs);

    const uint64_t start = platform::Platform::getTimestampNs();
    if (refresh) {
        refresh(m_culler);
    }
    std::vector<void*> visible;
    m_culler.cullAgainstCamera(cameraRect, visible);
    m_currentStats.cullingShadowUs = static_cast<double>(platform::Platform::getTimestampNs() - start) / 1000.0;
    m_currentStats.totalObjects = static_cast<uint32_t>(m_culler.getTotalObjectCount());
    m_currentStats.shadowVisibleObjects = static_cast<uint32_t>(m_culler.getLastVisibleCount());
    m_currentStats.shadowCulledObjects = static_cast<uint32_t>(m_culler.getLastCulledCount());
}

void OptiGDManager::endFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_frameOpen) return;
    m_frameOpen = false;
    // Overhead is measured up to the point the stats are recorded (this scope excluded).
    OverheadTimer timer(m_overheadAccumUs);

    const auto& user = m_userConfig;
    OptiConfig effective = user;
    if (user.flags.enableAdaptiveGovernor) {
        m_adaptiveGovernor.recordFrameInterval(m_currentStats.frameIntervalUs);
        m_adaptiveGovernor.update(user);
        effective = m_adaptiveGovernor.effectiveConfig(user);
        m_fallbackManager.update(m_currentStats.frameIndex, m_adaptiveGovernor.getRecentStats(), user);
        m_fallbackManager.apply(effective);
    }
    applyEffectiveConfigLocked(effective);

    m_currentStats.arenaAllocations = m_frameArena.getAllocationCount() - m_arenaAllocBase;
    m_currentStats.optigdOverheadUs = m_overheadAccumUs;

    auto& diag = diagnostics::DiagnosticsEngine::instance();
    diag.setTargetFrameRate(m_adaptiveGovernor.getTargetFrameRate());
    diag.recordFrame(m_currentStats);
}

} // namespace optigd::core

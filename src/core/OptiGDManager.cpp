#include "OptiGDManager.hpp"
#include "Logger.hpp"
#include "../platform/Platform.hpp"
#include <chrono>

namespace optigd::core {

OptiGDManager::OptiGDManager()
    : m_frameArena(1024 * 1024),
      m_adaptiveGovernor(config::ConfigManager::instance().getConfig()),
      m_fallbackManager(4.0f, 120) {}

OptiGDManager::~OptiGDManager() {
    shutdown();
}

OptiGDManager& OptiGDManager::instance() {
    static OptiGDManager s_instance;
    return s_instance;
}

void OptiGDManager::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return;

    OPTIGD_LOG_INFO("Initializing OptiGD v1.0.0 engine...");

    auto sys = platform::Platform::getSystemInfo();
    OPTIGD_LOG_INFO("Platform: " + sys.osName + ", Cores: " +
                    std::to_string(sys.logicalCores) + ", RAM: " +
                    std::to_string(sys.totalRamMb) + " MB, Compiler: " + sys.compiler);

    compat::CompatibilityLayer::instance().initialize("2.2081", "v4.0.0");

    const auto& config = config::ConfigManager::instance().getConfig();
    m_culler.setCellSize(config.spatialCellSize);
    m_culler.setPadding(config.cameraPaddingX, config.cameraPaddingY);

    m_particleGovernor.setDensityScale(config.particleDensityScale);
    m_particleGovernor.setGlobalParticleLimit(config.maxActiveParticles);
    m_particleGovernor.setCullOffscreen(config.cullOffscreenEmitters);

    m_adaptiveGovernor.setTargetFrameRate(config.targetFrameRate);

    m_initialized = true;
    OPTIGD_LOG_INFO("OptiGD initialized successfully with default profile: " +
                    std::string(tierToString(config.tier)));
}

void OptiGDManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    OPTIGD_LOG_INFO("Shutting down OptiGD engine...");
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    m_frameArena.reset();
    m_initialized = false;
}

void OptiGDManager::onLevelLoaded() {
    std::lock_guard<std::mutex> lock(m_mutex);
    OPTIGD_LOG_INFO("Level loaded. Resetting optimization caches...");
    m_levelActive = true;
    m_frameArena.reset();
    m_triggerOptimizer.prepareLevel();
    m_fallbackManager.reset();
}

void OptiGDManager::onLevelExited() {
    std::lock_guard<std::mutex> lock(m_mutex);
    OPTIGD_LOG_INFO("Level exited. Clearing level resources...");
    m_levelActive = false;
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    m_frameArena.reset();
}

void OptiGDManager::beginFrame(uint64_t frameIndex) {
    m_frameStartTimeUs = platform::Platform::getTimestampUs();

    // Reset per-frame memory arena in O(1)
    m_frameArena.reset();

    m_currentStats = FrameStats{};
    m_currentStats.frameIndex = frameIndex;
}

void OptiGDManager::endFrame() {
    uint64_t frameEndTimeUs = platform::Platform::getTimestampUs();
    m_currentStats.totalFrameTimeUs = static_cast<double>(frameEndTimeUs - m_frameStartTimeUs);

    m_currentStats.arenaAllocatedBytes = m_frameArena.getUsedBytes();
    m_currentStats.heapAllocationsAvoided = m_frameArena.getAllocationCount();

    // Update adaptive governor and fallback manager
    auto config = config::ConfigManager::instance().getConfig();
    m_adaptiveGovernor.recordFrameTime(m_currentStats.totalFrameTimeUs);
    m_adaptiveGovernor.update(config);

    m_currentStats.governorOverheadUs = m_adaptiveGovernor.getLastGovernorOverheadUs();

    m_fallbackManager.checkAndApplyFallbacks(
        m_currentStats.frameIndex,
        m_adaptiveGovernor.getRecentStats(),
        config
    );

    diagnostics::DiagnosticsEngine::instance().recordFrame(m_currentStats);
}

} // namespace optigd::core

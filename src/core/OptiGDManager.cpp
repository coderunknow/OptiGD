#include "OptiGDManager.hpp"
#include "Logger.hpp"

namespace optigd::core {

OptiGDManager::OptiGDManager()
    : m_activeConfig(config::ConfigManager::instance().getConfig()),
      m_frameArena(2 * 1024 * 1024), // 2 MB per-frame arena
      m_culler(m_activeConfig.spatialCellSize),
      m_particleGovernor(m_activeConfig.particleDensityScale, m_activeConfig.maxActiveParticles),
      m_triggerOptimizer(800.0f),
      m_governor(m_activeConfig),
      m_fallbackManager(m_activeConfig.fallbackVarianceThresholdMs, m_activeConfig.fallbackCooldownFrames) {}

OptiGDManager& OptiGDManager::instance() {
    static OptiGDManager s_instance;
    return s_instance;
}

void OptiGDManager::initialize(std::string_view gdVersion, std::string_view geodeVersion) {
    if (m_initialized) return;

    OPTIGD_LOG_INFO(std::string("Initializing OptiGD v") + std::string(kOptiGDVersion) +
                    " Performance Engine...");
    compat::CompatibilityLayer::instance().initialize(gdVersion, geodeVersion);
    reloadConfig();
    diagnostics::DiagnosticsEngine::instance().reset();
    m_initialized = true;
    OPTIGD_LOG_INFO("OptiGD initialized successfully. Active Tier: " +
                    std::string(tierToString(m_activeConfig.tier)));
}

void OptiGDManager::shutdown() {
    if (!m_initialized) return;
    OPTIGD_LOG_INFO("Shutting down OptiGD...");
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    optimization::MatrixCache::instance().clear();
    profiling::Profiler::instance().reset();
    m_initialized = false;
}

void OptiGDManager::applyConfigToSubsystems() {
    m_culler.setCellSize(m_activeConfig.spatialCellSize);
    m_culler.setPadding(m_activeConfig.cameraPaddingX, m_activeConfig.cameraPaddingY);
    m_particleGovernor.setDensityScale(m_activeConfig.particleDensityScale);
    m_particleGovernor.setGlobalParticleLimit(m_activeConfig.maxActiveParticles);
    m_particleGovernor.setCullOffscreen(m_activeConfig.cullOffscreenEmitters);
    m_governor.setTargetFrameRate(m_activeConfig.targetFrameRate);
    m_governor.setDynamicAdaptationEnabled(m_activeConfig.flags.enableAdaptiveGovernor);
    m_governor.setHysteresisFrames(m_activeConfig.fallbackCooldownFrames);
    m_fallbackManager.setVarianceThreshold(m_activeConfig.fallbackVarianceThresholdMs);
    m_fallbackManager.setCooldownFrames(m_activeConfig.fallbackCooldownFrames);
    ui::DiagnosticsOverlay::instance().setEnabled(m_activeConfig.flags.enableDiagnosticsOverlay);
}

void OptiGDManager::reloadConfig() {
    m_activeConfig = config::ConfigManager::instance().getConfig();
    m_governor.forceTier(m_activeConfig.tier);
    applyConfigToSubsystems();
}

void OptiGDManager::onLevelLoad() {
    OPTIGD_LOG_INFO("OptiGD preparing level acceleration structures...");
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    optimization::MatrixCache::instance().clear();
    profiling::Profiler::instance().reset();
    m_frameArena.reset();
}

void OptiGDManager::onLevelReset() {
    m_triggerOptimizer.resetTriggerStates();
    m_frameArena.reset();
}

void OptiGDManager::onLevelExit() {
    OPTIGD_LOG_INFO("OptiGD cleaning up level acceleration structures...");
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    optimization::MatrixCache::instance().clear();
    m_frameArena.reset();
}

void OptiGDManager::beginFrame() {
    m_frameStartTime = std::chrono::steady_clock::now();
    m_frameArena.reset();
    m_currentStats = FrameStats{};
    m_externalFrameDeltaUs = 0.0;
    m_frameOptigdOverheadUs = 0.0;
    m_frameHeapAllocationsAvoided = 0;
    m_frameStartMatrixHits = optimization::MatrixCache::instance().getHits();
    m_frameStartMatrixMisses = optimization::MatrixCache::instance().getMisses();
    m_frameCounter++;
}

void OptiGDManager::endFrame() {
    auto endTime = std::chrono::steady_clock::now();
    double updateUs = std::chrono::duration<double, std::micro>(endTime - m_frameStartTime).count();

    m_currentStats.updateCpuTimeUs = updateUs;
    m_currentStats.totalFrameTimeUs = (m_externalFrameDeltaUs > 0.0) ? m_externalFrameDeltaUs : updateUs;

    m_currentStats.totalObjects = static_cast<uint32_t>(m_culler.getTotalObjectCount());
    m_currentStats.visibleObjects = static_cast<uint32_t>(m_culler.getLastVisibleCount());
    m_currentStats.culledObjects = static_cast<uint32_t>(m_culler.getLastCulledCount());
    m_currentStats.newlyVisibleObjects = static_cast<uint32_t>(m_culler.getLastNewlyVisibleCount());
    m_currentStats.newlyCulledObjects = static_cast<uint32_t>(m_culler.getLastNewlyCulledCount());

    m_currentStats.activeParticles = m_particleGovernor.getTotalActiveParticles();
    m_currentStats.culledParticles = m_particleGovernor.getLastAvoidedParticleCount();
    if (m_currentStats.culledParticles == 0) {
        m_currentStats.culledParticles = m_particleGovernor.getLastCulledEmitterCount() * 32;
    }

    m_currentStats.totalTriggers = static_cast<uint32_t>(m_triggerOptimizer.getTotalTriggers());
    m_currentStats.evaluatedTriggers = static_cast<uint32_t>(m_triggerOptimizer.getLastEvaluatedCount());
    m_currentStats.executedTriggers = static_cast<uint32_t>(m_triggerOptimizer.getLastExecutedCount());

    uint64_t curHits = optimization::MatrixCache::instance().getHits();
    uint64_t curMisses = optimization::MatrixCache::instance().getMisses();
    m_currentStats.matrixCacheHits = static_cast<uint32_t>(
        curHits >= m_frameStartMatrixHits ? (curHits - m_frameStartMatrixHits) : curHits);
    m_currentStats.matrixCacheMisses = static_cast<uint32_t>(
        curMisses >= m_frameStartMatrixMisses ? (curMisses - m_frameStartMatrixMisses) : curMisses);

    // Record frame time and run AdaptiveGovernor using the frame arena for zero-heap percentile sorting
    m_governor.recordFrameTime(m_currentStats.totalFrameTimeUs);
    OptiConfig prevConfig = m_activeConfig;
    m_governor.update(m_activeConfig, &m_frameArena);
    m_currentStats.governorTimeUs = m_governor.getLastGovernorOverheadUs();

    // Check fallbacks and propagate any config changes back to ConfigManager and subsystems
    bool fallbackChanged = m_fallbackManager.checkAndApplyFeatureFallbacks(
        m_frameCounter, m_currentStats, m_governor.getRecentStats(), m_activeConfig);

    bool tierChanged = (m_activeConfig.tier != prevConfig.tier) ||
                       (m_activeConfig.particleDensityScale != prevConfig.particleDensityScale) ||
                       (m_activeConfig.maxActiveParticles != prevConfig.maxActiveParticles) ||
                       (m_activeConfig.cullOffscreenEmitters != prevConfig.cullOffscreenEmitters) ||
                       (m_activeConfig.cameraPaddingX != prevConfig.cameraPaddingX) ||
                       (m_activeConfig.cameraPaddingY != prevConfig.cameraPaddingY) ||
                       fallbackChanged;

    if (tierChanged) {
        config::ConfigManager::instance().setConfig(m_activeConfig);
        applyConfigToSubsystems();
    }

    m_currentStats.arenaUsedBytes = m_frameArena.getUsedBytes();
    m_currentStats.arenaPeakBytes = m_frameArena.getPeakUsage();
    m_currentStats.arenaAllocations = static_cast<uint32_t>(m_frameArena.getAllocationCount());
    m_currentStats.heapAllocationsAvoided =
        m_frameHeapAllocationsAvoided + static_cast<uint32_t>(m_frameArena.getAllocationCount());
    m_currentStats.optigdOverheadUs = m_frameOptigdOverheadUs + m_currentStats.governorTimeUs;

    if (profiling::Profiler::instance().isEnabled()) {
        profiling::Profiler::instance().record("FrameUpdateCpu", m_currentStats.updateCpuTimeUs);
        profiling::Profiler::instance().record("OptiGDOverhead", m_currentStats.optigdOverheadUs);
        if (m_currentStats.cullingTimeUs > 0.0) {
            profiling::Profiler::instance().record("VisibilityCuller", m_currentStats.cullingTimeUs);
        }
        if (m_currentStats.particleTimeUs > 0.0) {
            profiling::Profiler::instance().record("ParticleGovernor", m_currentStats.particleTimeUs);
        }
        if (m_currentStats.triggerTimeUs > 0.0) {
            profiling::Profiler::instance().record("TriggerOptimizer", m_currentStats.triggerTimeUs);
        }
        if (m_currentStats.governorTimeUs > 0.0) {
            profiling::Profiler::instance().record("AdaptiveGovernor", m_currentStats.governorTimeUs);
        }
    }

    diagnostics::DiagnosticsEngine::instance().recordFrameStats(m_currentStats);
}

} // namespace optigd::core

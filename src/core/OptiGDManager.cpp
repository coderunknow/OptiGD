#include "OptiGDManager.hpp"
#include "Logger.hpp"
#include "GeodeVersion.hpp"
#include "../benchmark/BenchmarkController.hpp"
#include "../platform/Platform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

    // Startup safety net: the mod.json `version` tag must satisfy Geode's
    // VersionInfo grammar or the loader refuses to load the mod and reports
    // only "unexpected type string". Catch that class of mistake loudly here.
    if (!core::geodever::isValidGeodeVersionString(core::kOptiGDVersionTag)) {
        auto parsed = core::geodever::parseVersionString(core::kOptiGDVersionTag);
        OPTIGD_LOG_ERROR(std::string("OptiGD version tag '") + core::kOptiGDVersionTag +
                         "' is not a valid Geode version string: " + parsed.error +
                         ". Geode will fail to load this mod; fix mod.json / Types.hpp.");
    }

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
    // Restores the user's saved configuration if a benchmark session left a
    // transient override installed. Deliberately on the shutdown path so an
    // unload can never strand the mod in a forced-passthrough configuration.
    benchmark::BenchmarkController::instance().restoreConfiguration();
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

    // beta.3 fix (defect B): the telemetry capture is level-scoped, so it starts
    // here. In beta.2 `DiagnosticsEngine::reset()` ran only from `initialize()`
    // at mod load, which meant `optigd_diagnostics.json` aggregated every level
    // and every attempt played since the mod was loaded and still called itself
    // a level report.
    diagnostics::DiagnosticsEngine::instance().reset();
    m_governor.clearHistory();
    clearOutOfWindowAccounting();
    m_frameOpen = false;
    m_lastInterFrameIntervalUs = 0.0;
    m_lastFrameWasStall = false;
}

void OptiGDManager::onLevelReset() {
    m_triggerOptimizer.resetTriggerStates();
    m_frameArena.reset();
    diagnostics::DiagnosticsEngine::instance().noteLevelReset();
    // Benchmark Mode stops (or warns) on an attempt restart; see the
    // `benchmark-stop-on-death` setting. Notified here rather than from the hook
    // so every caller of `onLevelReset()` is covered.
    benchmark::BenchmarkController::instance().onLevelReset();
}

void OptiGDManager::onLevelExit() {
    OPTIGD_LOG_INFO("OptiGD cleaning up level acceleration structures...");
    // Restores the transient benchmark configuration and finalizes any session
    // still open. Runs *before* the subsystems are torn down so the persisted
    // record describes the configuration that was actually in force.
    benchmark::BenchmarkController::instance().onLevelExit();
    m_culler.clear();
    m_particleGovernor.clear();
    m_triggerOptimizer.clear();
    optimization::MatrixCache::instance().clear();
    m_frameArena.reset();
}

void OptiGDManager::attributeSubsystemTime(double& statSlotUs, double us) {
    if (!std::isfinite(us) || us <= 0.0) return;
    if (!m_frameOpen) {
        // Recorded outside a beginFrame()/endFrame() window (CCParticleSystem
        // updates are not confined to GJBaseGameLayer::update). Counted and
        // reported separately rather than being charged to an unrelated frame.
        ++m_outOfWindowSubsystemEvents;
        m_outOfWindowSubsystemUs += us;
        return;
    }
    statSlotUs += us;
    recordSubsystemOverheadUs(us);
}

void OptiGDManager::beginFrame() {
    m_frameStartTime = std::chrono::steady_clock::now();
    m_frameStartThreadCpuUs = platform::Platform::getCurrentThreadCpuTimeUs();
    m_frameOpen = true;
    m_frameArena.reset();
    m_currentStats = FrameStats{};
    m_externalFrameDeltaUs = 0.0;
    m_framePaused = false;
    m_frameOptigdOverheadUs = 0.0;
    m_frameHeapAllocationsAvoided = 0;
    m_frameStartMatrixHits = optimization::MatrixCache::instance().getHits();
    m_frameStartMatrixMisses = optimization::MatrixCache::instance().getMisses();
    m_frameCounter++;
}

void OptiGDManager::endFrame() {
    auto endTime = std::chrono::steady_clock::now();
    double updateUs = std::chrono::duration<double, std::micro>(endTime - m_frameStartTime).count();

    // `updateCpuTimeUs` is a WALL-CLOCK span of the hooked update() call, not
    // thread CPU time. beta.3 adds the real thread CPU time next to it so a
    // preempted update can be told apart from a slow one; the report states both
    // and says which is which.
    m_currentStats.updateCpuTimeUs = updateUs;
    const uint64_t endThreadCpuUs = platform::Platform::getCurrentThreadCpuTimeUs();
    m_currentStats.updateThreadCpuUs =
        (endThreadCpuUs > m_frameStartThreadCpuUs)
            ? static_cast<double>(endThreadCpuUs - m_frameStartThreadCpuUs)
            : 0.0;

    m_currentStats.totalFrameTimeUs = (m_externalFrameDeltaUs > 0.0) ? m_externalFrameDeltaUs : updateUs;

    // Stall classification, using the single shared threshold in Types.hpp.
    const double intervalUs = m_currentStats.totalFrameTimeUs;
    m_lastInterFrameIntervalUs = intervalUs;
    m_lastFrameWasStall = std::isfinite(intervalUs) && intervalUs > kStallThresholdUs;
    m_currentStats.gapFrame = m_lastFrameWasStall || m_framePaused;
    m_currentStats.frameCounted =
        !m_currentStats.gapFrame && std::isfinite(intervalUs) && intervalUs > 0.0;
    m_framePaused = false;

    m_currentStats.totalObjects = static_cast<uint32_t>(m_culler.getTotalObjectCount());
    m_currentStats.visibleObjects = static_cast<uint32_t>(m_culler.getLastVisibleCount());
    m_currentStats.culledObjects = static_cast<uint32_t>(m_culler.getLastCulledCount());
    m_currentStats.newlyVisibleObjects = static_cast<uint32_t>(m_culler.getLastNewlyVisibleCount());
    m_currentStats.newlyCulledObjects = static_cast<uint32_t>(m_culler.getLastNewlyCulledCount());

    m_currentStats.activeParticles = m_particleGovernor.getTotalActiveParticles();
    // beta.3 fix (defect D): report only what was measured. beta.2 substituted
    // `getLastCulledEmitterCount() * 32` whenever the measured avoided-particle
    // count was 0, which put a fabricated estimate into the same field as a real
    // measurement and inflated `total_particles_avoided` in every report. Zero
    // is the honest value when no particles were avoided.
    m_currentStats.culledParticles = m_particleGovernor.getLastAvoidedParticleCount();

    // TriggerOptimizer only ever *measures* the lookahead window; it never
    // gates or replaces native Geometry Dash trigger execution. Report its
    // counters as zero unless the indexing feature is actually enabled so the
    // diagnostics report cannot imply triggers were skipped when none were.
    if (m_activeConfig.flags.enableTriggerIndexing) {
        m_currentStats.totalTriggers = static_cast<uint32_t>(m_triggerOptimizer.getTotalTriggers());
        m_currentStats.evaluatedTriggers = static_cast<uint32_t>(m_triggerOptimizer.getLastEvaluatedCount());
        m_currentStats.executedTriggers = static_cast<uint32_t>(m_triggerOptimizer.getLastExecutedCount());
    } else {
        m_currentStats.totalTriggers = 0;
        m_currentStats.evaluatedTriggers = 0;
        m_currentStats.executedTriggers = 0;
    }

    uint64_t curHits = optimization::MatrixCache::instance().getHits();
    uint64_t curMisses = optimization::MatrixCache::instance().getMisses();
    // beta.3 fix (defect E): FrameStats.matrixCacheHits/Misses are uint64_t but
    // beta.2 computed the per-frame delta as uint64_t and then truncated it with
    // `static_cast<uint32_t>`, so a large delta wrapped to a small number
    // instead of being reported. No cast is needed here at all.
    m_currentStats.matrixCacheHits =
        (curHits >= m_frameStartMatrixHits) ? (curHits - m_frameStartMatrixHits) : curHits;
    m_currentStats.matrixCacheMisses =
        (curMisses >= m_frameStartMatrixMisses) ? (curMisses - m_frameStartMatrixMisses) : curMisses;

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
        // Writes the ACTIVE configuration layer. While a benchmark session owns
        // that layer (ConfigManager's transient override) the write stays inside
        // the session and the user's saved settings are untouched; see
        // `ConfigManager::setConfig`.
        config::ConfigManager::instance().setConfig(m_activeConfig);
        applyConfigToSubsystems();
        if (fallbackChanged) {
            benchmark::BenchmarkController::instance().onFallbackEvent();
        }
        if (m_activeConfig.tier != prevConfig.tier) {
            benchmark::BenchmarkController::instance().onTierTransition();
        }
    }

    m_currentStats.arenaUsedBytes = m_frameArena.getUsedBytes();
    m_currentStats.arenaPeakBytes = m_frameArena.getPeakUsage();
    m_currentStats.arenaAllocations = static_cast<uint32_t>(m_frameArena.getAllocationCount());
    m_currentStats.heapAllocationsAvoided =
        m_frameHeapAllocationsAvoided + static_cast<uint32_t>(m_frameArena.getAllocationCount());
    m_currentStats.optigdOverheadUs = m_frameOptigdOverheadUs + m_currentStats.governorTimeUs;

    if (profiling::Profiler::instance().isEnabled()) {
        profiling::Profiler::instance().recordById(profiling::MetricId::FrameUpdateCpu,
                                                   m_currentStats.updateCpuTimeUs);
        profiling::Profiler::instance().recordById(profiling::MetricId::OptiGDOverhead,
                                                   m_currentStats.optigdOverheadUs);
        if (m_currentStats.cullingTimeUs > 0.0) {
            profiling::Profiler::instance().recordById(profiling::MetricId::VisibilityCuller,
                                                       m_currentStats.cullingTimeUs);
        }
        if (m_currentStats.particleTimeUs > 0.0) {
            profiling::Profiler::instance().recordById(profiling::MetricId::ParticleGovernor,
                                                       m_currentStats.particleTimeUs);
        }
        if (m_currentStats.triggerTimeUs > 0.0) {
            profiling::Profiler::instance().recordById(profiling::MetricId::TriggerOptimizer,
                                                       m_currentStats.triggerTimeUs);
        }
        if (m_currentStats.governorTimeUs > 0.0) {
            profiling::Profiler::instance().recordById(profiling::MetricId::AdaptiveGovernor,
                                                       m_currentStats.governorTimeUs);
        }
    }

    diagnostics::DiagnosticsEngine::instance().recordFrameStats(m_currentStats);

    // Benchmark Mode sees the finished frame last, so every subsystem has
    // already contributed its own timing. The controller classifies the interval
    // (counted / excluded stall / rejected) against the same shared threshold.
    benchmark::BenchmarkController::instance().onFrame(m_currentStats);

    m_frameOpen = false;
}

} // namespace optigd::core

#include "BenchmarkController.hpp"

#include "../compatibility/CompatibilityLayer.hpp"
#include "../core/Logger.hpp"
#include "../platform/Platform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace optigd::benchmark {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

uint64_t steadyNowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint64_t systemNowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

bool nearlyEqual(float a, float b) {
    return std::fabs(static_cast<double>(a) - static_cast<double>(b)) < 1e-4;
}

// Compares everything that can change runtime behaviour. Used to verify that an
// override actually reached the engine, so a session cannot report a
// configuration that was never applied.
bool sameEffectiveConfig(const core::OptiConfig& a, const core::OptiConfig& b) {
    if (a.tier != b.tier) return false;
    if (a.flags.enableSpatialCulling != b.flags.enableSpatialCulling) return false;
    if (a.flags.enableParticleThrottling != b.flags.enableParticleThrottling) return false;
    if (a.flags.enableTriggerIndexing != b.flags.enableTriggerIndexing) return false;
    if (a.flags.enableMatrixCaching != b.flags.enableMatrixCaching) return false;
    if (a.flags.enableAdaptiveGovernor != b.flags.enableAdaptiveGovernor) return false;
    if (a.flags.enableMemoryArena != b.flags.enableMemoryArena) return false;
    if (a.flags.enableDiagnosticsOverlay != b.flags.enableDiagnosticsOverlay) return false;
    if (a.flags.exportDiagnosticsOnExit != b.flags.exportDiagnosticsOnExit) return false;
    if (!nearlyEqual(a.particleDensityScale, b.particleDensityScale)) return false;
    if (a.maxActiveParticles != b.maxActiveParticles) return false;
    if (a.cullOffscreenEmitters != b.cullOffscreenEmitters) return false;
    if (!nearlyEqual(a.cameraPaddingX, b.cameraPaddingX)) return false;
    if (!nearlyEqual(a.cameraPaddingY, b.cameraPaddingY)) return false;
    if (!nearlyEqual(a.targetFrameRate, b.targetFrameRate)) return false;
    if (!nearlyEqual(a.spatialCellSize, b.spatialCellSize)) return false;
    return true;
}

BenchmarkEnvironment defaultEnvironment() {
    BenchmarkEnvironment env;
    env.optigdVersion = core::kOptiGDVersion;
    auto& compat = compat::CompatibilityLayer::instance();
    env.gdVersion = std::string(compat.getGDVersion());
    env.geodeVersion = std::string(compat.getGeodeVersion());
    auto info = platform::Platform::getSystemInfo();
    env.osName = info.osName;
    env.architecture = std::string(platform::Platform::getArchitectureName());
    env.compiler = info.compiler;
    env.logicalCores = info.logicalCores;
    env.totalRamMb = info.totalRamMb;
    env.refreshHint = "unknown";
    return env;
}

} // namespace

core::OptiConfig makeBaselineConfig(const core::OptiConfig& userConfig) {
    core::OptiConfig baseline = userConfig;
    // Every optimization feature is put into its passthrough behaviour. The
    // hooks themselves stay installed: this is an internal control, not vanilla
    // Geometry Dash, and the session says so.
    baseline.flags.enableSpatialCulling = false;
    baseline.flags.enableParticleThrottling = false;
    baseline.flags.enableTriggerIndexing = false;
    baseline.flags.enableMatrixCaching = false;
    baseline.flags.enableAdaptiveGovernor = false;
    // The diagnostics HUD and the level-exit exporter are deliberately left at
    // the user's values: Benchmark Mode needs them, and they are not
    // optimizations.
    return baseline;
}

std::vector<std::string> describePassthroughFeatures(const core::OptiConfig& userConfig,
                                                     const core::OptiConfig& baselineConfig) {
    std::vector<std::string> out;
    if (userConfig.flags.enableSpatialCulling && !baselineConfig.flags.enableSpatialCulling) {
        out.emplace_back("spatial_culling");
    }
    if (userConfig.flags.enableParticleThrottling && !baselineConfig.flags.enableParticleThrottling) {
        out.emplace_back("particle_governor");
    }
    if (userConfig.flags.enableMatrixCaching && !baselineConfig.flags.enableMatrixCaching) {
        out.emplace_back("matrix_cache");
    }
    if (userConfig.flags.enableAdaptiveGovernor && !baselineConfig.flags.enableAdaptiveGovernor) {
        out.emplace_back("adaptive_governor");
    }
    if (userConfig.flags.enableTriggerIndexing && !baselineConfig.flags.enableTriggerIndexing) {
        out.emplace_back("trigger_indexer");
    }
    return out;
}

BenchmarkRuntimeBridge makeDefaultRuntimeBridge() {
    BenchmarkRuntimeBridge bridge;
    bridge.readActiveConfig = [] { return config::ConfigManager::instance().getConfig(); };
    bridge.installOverride = [](const core::OptiConfig& cfg, const std::string& reason) {
        return config::ConfigManager::instance().setRuntimeOverride(cfg, reason);
    };
    bridge.updateOverride = [](const core::OptiConfig& cfg) {
        return config::ConfigManager::instance().updateRuntimeOverride(cfg);
    };
    bridge.clearOverride = [] { return config::ConfigManager::instance().clearRuntimeOverride(); };
    bridge.monotonicMs = &steadyNowMs;
    bridge.epochMs = &systemNowMs;
    bridge.levelIdentity = [] { return LevelIdentity{}; };
    bridge.environment = &defaultEnvironment;
    return bridge;
}

// ---------------------------------------------------------------------------
// BenchmarkController
// ---------------------------------------------------------------------------

BenchmarkController& BenchmarkController::instance() {
    static BenchmarkController s_instance;
    return s_instance;
}

BenchmarkController::BenchmarkController()
    : m_bridge(makeDefaultRuntimeBridge()),
      m_window(std::make_unique<diagnostics::SampleWindow>()) {}

void BenchmarkController::setPolicy(const BenchmarkPolicy& policy) {
    m_policy = policy;
    if (!(m_policy.targetDurationMs > 0.0)) m_policy.targetDurationMs = 60000.0;
    if (!(m_policy.stallThresholdMs > 0.0)) m_policy.stallThresholdMs = 250.0;
    if (!(m_policy.hitchThresholdMs > 0.0)) m_policy.hitchThresholdMs = 1000.0 / 30.0;
    if (!(m_policy.maxWallClockMs >= m_policy.targetDurationMs)) {
        m_policy.maxWallClockMs = recommendedWallClockCeilingMs(m_policy.targetDurationMs);
    }
}

void BenchmarkController::setRuntimeBridge(const BenchmarkRuntimeBridge& bridge) {
    BenchmarkRuntimeBridge defaults = makeDefaultRuntimeBridge();
    m_bridge = bridge;
    // Any slot the caller left empty falls back to the standalone default, so a
    // partially wired bridge cannot crash on an empty std::function.
    if (!m_bridge.readActiveConfig) m_bridge.readActiveConfig = defaults.readActiveConfig;
    if (!m_bridge.installOverride) m_bridge.installOverride = defaults.installOverride;
    if (!m_bridge.clearOverride) m_bridge.clearOverride = defaults.clearOverride;
    if (!m_bridge.monotonicMs) m_bridge.monotonicMs = defaults.monotonicMs;
    if (!m_bridge.epochMs) m_bridge.epochMs = defaults.epochMs;
    if (!m_bridge.levelIdentity) m_bridge.levelIdentity = defaults.levelIdentity;
    if (!m_bridge.environment) m_bridge.environment = defaults.environment;
}

void BenchmarkController::setStore(std::shared_ptr<BenchmarkStore> store) {
    m_store = std::move(store);
}

void BenchmarkController::setEnvironment(const BenchmarkEnvironment& environment) {
    m_environmentOverride = environment;
    m_hasEnvironmentOverride = true;
}

void BenchmarkController::setLevelIdentity(const LevelIdentity& identity) {
    m_levelIdentityOverride = identity;
    m_hasLevelIdentityOverride = true;
}

uint64_t BenchmarkController::monotonicNowMs() const {
    return m_bridge.monotonicMs ? m_bridge.monotonicMs() : steadyNowMs();
}

uint64_t BenchmarkController::epochNowMs() const {
    return m_bridge.epochMs ? m_bridge.epochMs() : systemNowMs();
}

BenchmarkEnvironment BenchmarkController::resolveEnvironment() const {
    BenchmarkEnvironment env = m_bridge.environment ? m_bridge.environment() : defaultEnvironment();
    if (m_hasEnvironmentOverride) {
        // Prefer values the host actually knows (live Geode/GD versions) and
        // fill anything it left blank from the resolved defaults.
        if (!m_environmentOverride.optigdVersion.empty()) env.optigdVersion = m_environmentOverride.optigdVersion;
        if (!m_environmentOverride.gdVersion.empty()) env.gdVersion = m_environmentOverride.gdVersion;
        if (!m_environmentOverride.geodeVersion.empty()) env.geodeVersion = m_environmentOverride.geodeVersion;
        if (!m_environmentOverride.osName.empty()) env.osName = m_environmentOverride.osName;
        if (!m_environmentOverride.architecture.empty()) env.architecture = m_environmentOverride.architecture;
        if (!m_environmentOverride.compiler.empty()) env.compiler = m_environmentOverride.compiler;
        if (m_environmentOverride.logicalCores > 0) env.logicalCores = m_environmentOverride.logicalCores;
        if (m_environmentOverride.totalRamMb > 0) env.totalRamMb = m_environmentOverride.totalRamMb;
        if (!m_environmentOverride.refreshHint.empty()) env.refreshHint = m_environmentOverride.refreshHint;
    }
    if (env.optigdVersion.empty()) env.optigdVersion = core::kOptiGDVersion;
    return env;
}

LevelIdentity BenchmarkController::resolveLevelIdentity() const {
    if (m_hasLevelIdentityOverride) return m_levelIdentityOverride;
    return m_bridge.levelIdentity ? m_bridge.levelIdentity() : LevelIdentity{};
}

size_t BenchmarkController::ringCapacityForPolicy() const {
    double seconds = m_policy.targetDurationMs / 1000.0;
    double capacity = seconds * m_policy.maxTrackedFps;
    if (!std::isfinite(capacity) || capacity <= 0.0) capacity = 3600.0;
    size_t result = static_cast<size_t>(capacity);
    result = std::max(result, m_policy.minRingCapacity);
    result = std::min(result, m_policy.maxRingCapacity);
    return result;
}

bool BenchmarkController::isRunning() const {
    return m_session != nullptr && m_session->state == BenchmarkState::Running;
}

BenchmarkState BenchmarkController::getState() const {
    return m_session ? m_session->state : BenchmarkState::Idle;
}

std::string BenchmarkController::previewNextSessionId(BenchmarkMode mode) const {
    uint32_t counter = m_sessionCounter;
    uint64_t epoch = epochNowMs();
    if (m_store) return m_store->allocateSessionId(mode, epoch, counter);
    return generateSessionId(mode, epoch, counter);
}

// ---------------------------------------------------------------------------
// Configuration override
// ---------------------------------------------------------------------------

void BenchmarkController::installOverrideFor(BenchmarkMode mode, const core::OptiConfig& userConfig) {
    m_requestedOverrideConfig = (mode == BenchmarkMode::Baseline)
                                    ? makeBaselineConfig(userConfig)
                                    : userConfig; // freeze the user's own configuration
    const std::string reason = std::string("OptiGD Benchmark Mode (") + modeId(mode) + " session)";

    m_overrideActive = m_bridge.installOverride(m_requestedOverrideConfig, reason);
    if (!m_overrideActive) {
        // Something had already armed an override. Take it over so this capture
        // measures what it says it measures, but record that it happened: two
        // overlapping sessions would otherwise be invisible in the record.
        if (m_bridge.updateOverride) {
            m_overrideActive = m_bridge.updateOverride(m_requestedOverrideConfig);
        }
        if (m_overrideActive) {
            m_overrideReapplyCount++;
            OPTIGD_LOG_WARN("Benchmark Mode found a configuration override already armed and "
                            "took it over; the previous owner's configuration was replaced");
        }
    }
    verifyOverride();
}

void BenchmarkController::verifyOverride() {
    if (!m_overrideActive || !m_session) return;
    core::OptiConfig actual = m_bridge.readActiveConfig();
    if (sameEffectiveConfig(actual, m_requestedOverrideConfig)) {
        m_session->configOverrideApplied = true;
        return;
    }
    // Something outside Benchmark Mode rewrote the active configuration (a Geode
    // settings listener is the realistic cause). Re-apply and record it, so the
    // report cannot claim a configuration that was not in force.
    ++m_overrideReapplyCount;
    m_session->addWarning(warning::kConfigOverrideLost);
    // Re-assert through the update path: this session already owns the override,
    // so arming a second one would be refused (correctly) and the capture would
    // be left measuring the wrong configuration.
    if (m_bridge.updateOverride) {
        m_overrideActive = m_bridge.updateOverride(m_requestedOverrideConfig) || m_overrideActive;
    } else {
        m_bridge.installOverride(m_requestedOverrideConfig, "OptiGD Benchmark Mode re-apply");
    }
    core::OptiConfig retried = m_bridge.readActiveConfig();
    m_session->configOverrideApplied = sameEffectiveConfig(retried, m_requestedOverrideConfig);
    if (!m_session->configOverrideApplied) {
        OPTIGD_LOG_ERROR("Benchmark Mode could not apply its session configuration; "
                         "the capture will be marked as not applying the intended config");
    }
}

void BenchmarkController::clearOverrideInternal() {
    if (!m_overrideActive) return;
    m_bridge.clearOverride();
    m_overrideActive = false;
    m_requestedOverrideConfig = core::OptiConfig{};
}

void BenchmarkController::restoreConfiguration() {
    if (isRunning()) {
        finalize(BenchmarkState::Incomplete, warning::kLevelExit,
                 "OptiGD restored its configuration while a capture was still running");
        return;
    }
    clearOverrideInternal();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

BenchmarkActionResult BenchmarkController::startSession(BenchmarkMode mode) {
    BenchmarkActionResult result;

    if (isRunning()) {
        result.ok = false;
        result.state = BenchmarkState::Running;
        result.sessionId = m_session ? m_session->id : std::string();
        result.errorCode = "already_running";
        result.message = std::string("A ") + modeId(m_session->mode) +
                         " session is already running. Cancel it or wait for it to finish.";
        return result;
    }
    if (m_session && m_session->state == BenchmarkState::Finalizing) {
        result.ok = false;
        result.state = BenchmarkState::Finalizing;
        result.errorCode = "busy";
        result.message = "The previous session is still being written. Try again in a moment.";
        return result;
    }

    // Archive the previous terminal session so the UI can still show it, then
    // start from a clean slate. Nothing is deleted from disk.
    if (m_session) {
        m_lastSession = std::move(m_session);
        m_session.reset();
    }

    m_sessionCounter++;
    uint64_t epoch = epochNowMs();
    uint32_t counter = m_sessionCounter;
    std::string id = m_store ? m_store->allocateSessionId(mode, epoch, counter)
                             : generateSessionId(mode, epoch, counter);
    m_sessionCounter = counter;

    auto session = std::make_unique<BenchmarkSession>();
    session->id = std::move(id);
    session->mode = mode;
    session->state = BenchmarkState::Running;
    session->targetDurationMs = m_policy.targetDurationMs;
    session->environment = resolveEnvironment();
    session->evidenceClassification =
        m_liveGameRuntime ? evidence::kMeasuredInGame : evidence::kSimulated;
    session->evidenceClassificationNote =
        m_liveGameRuntime ? evidence::kMeasuredInGameNote : evidence::kSimulatedNote;
    session->level = resolveLevelIdentity();
    if (!session->level.hasAnyIdentity()) {
        session->addWarning(warning::kLevelUnknown);
    }
    session->startedEpochMs = epoch;
    session->startedUtc = formatUtcIso8601(epoch);

    m_userConfigAtStart = m_bridge.readActiveConfig();
    m_overrideReapplyCount = 0;
    m_pauseObserved = false;
    installOverrideFor(mode, m_userConfigAtStart);

    // Record the configuration that is *actually* in force after installation,
    // not the one that was requested.
    session->config = m_bridge.readActiveConfig();
    session->featuresDisabledForBaseline =
        (mode == BenchmarkMode::Baseline)
            ? describePassthroughFeatures(m_userConfigAtStart, session->config)
            : std::vector<std::string>{};

    diagnostics::SampleWindowConfig windowConfig;
    windowConfig.ringCapacity = ringCapacityForPolicy();
    windowConfig.histogramMaxMs = std::max(m_policy.stallThresholdMs, 250.0);
    windowConfig.histogramBins = 2500;
    windowConfig.hitchThresholdMs = m_policy.hitchThresholdMs;
    windowConfig.stallThresholdMs = m_policy.stallThresholdMs;
    m_window = std::make_unique<diagnostics::SampleWindow>();
    m_window->configure(windowConfig);

    m_startMonotonicMs = monotonicNowMs();
    m_lastMonotonicMs = m_startMonotonicMs;

    OPTIGD_LOG_INFO(std::string("Benchmark Mode: started ") + modeId(mode) + " session '" +
                    session->id + "' targeting " +
                    std::to_string(static_cast<int>(m_policy.targetDurationMs / 1000.0)) + "s of active gameplay");

    result.ok = true;
    result.state = BenchmarkState::Running;
    result.sessionId = session->id;
    result.message = std::string("Measuring ") + modeDisplayName(mode) + ". Play normally for " +
                     std::to_string(static_cast<int>(m_policy.targetDurationMs / 1000.0)) +
                     " seconds; the session finalizes automatically.";
    m_session = std::move(session);
    return result;
}

BenchmarkActionResult BenchmarkController::cancelSession() {
    BenchmarkActionResult result;
    if (!isRunning()) {
        result.ok = false;
        result.state = getState();
        result.errorCode = "not_running";
        result.message = "No benchmark session is running.";
        return result;
    }
    std::string id = m_session->id;
    finalize(BenchmarkState::Cancelled, warning::kCancelledByUser,
             "Cancelled by the user before the target duration elapsed.");
    result.ok = true;
    result.state = m_session ? m_session->state : BenchmarkState::Cancelled;
    result.sessionId = id;
    result.message = "Session cancelled and saved as incomplete evidence.";
    return result;
}

void BenchmarkController::onFrame(const core::FrameStats& stats) {
    if (!isRunning() || !m_window || !m_session) return;

    m_session->framesObserved++;
    const double intervalUs = stats.totalFrameTimeUs;

    if (!std::isfinite(intervalUs) || intervalUs <= 0.0) {
        // push() rejects it and counts the rejection; nothing is invented.
        m_window->push(stats);
    } else if (m_window->isStallIntervalUs(intervalUs)) {
        // A stall: pause, application switch, level load or scheduler
        // pre-emption. Excluded from the captured window and counted, so the
        // session states how much non-gameplay time it spanned.
        m_session->gapEvents++;
        m_window->recordExcluded(intervalUs);
    } else if (m_pauseObserved || !stats.frameCounted) {
        // The game loop is running but the level is not being played. This
        // cannot be detected from the interval alone: with the pause menu open
        // `GJBaseGameLayer::update` still runs at the display rate, so every
        // frame looks like ordinary gameplay. Only the explicit pause signal
        // (or the engine's own not-counted flag) identifies it.
        m_session->pausedMs += intervalUs / 1000.0;
        m_window->recordExcluded(intervalUs);
    } else {
        m_window->push(stats);
    }

    m_lastMonotonicMs = monotonicNowMs();

    // Re-verify the configuration periodically rather than every frame: the
    // comparison is cheap but there is no reason to run it 1000 times a second.
    if ((m_session->framesObserved & 0xFu) == 0u) {
        verifyOverride();
    }

    const double capturedMs = m_window->getTotalFrameTimeMs();
    if (capturedMs >= m_policy.targetDurationMs) {
        finalize(BenchmarkState::Completed, {}, "Target duration reached.");
        return;
    }

    const double wallMs = static_cast<double>(m_lastMonotonicMs - m_startMonotonicMs);
    if (wallMs > m_policy.maxWallClockMs) {
        finalize(BenchmarkState::Incomplete, warning::kWallClockDrift,
                 "The capture exceeded its wall-clock ceiling; most of the elapsed "
                 "time was not active gameplay.");
    }
}

void BenchmarkController::onLevelReset() {
    if (!isRunning() || !m_session) return;
    m_session->resetEvents++;
    if (m_policy.stopOnLevelReset) {
        finalize(BenchmarkState::Incomplete, warning::kLevelReset,
                 "The attempt restarted during the capture, so the measured level "
                 "segment is no longer contiguous.");
        return;
    }
    m_session->addWarning(warning::kLevelReset);
    m_session->addWarning(warning::kSegmentNotContiguous);
}

void BenchmarkController::onLevelExit() {
    // Always restore, even when no session is running: leaving an override
    // installed after the level is gone would disable the user's optimizations.
    if (isRunning() && m_session) {
        finalize(BenchmarkState::Incomplete, warning::kLevelExit,
                 "The level was exited before the capture reached its target duration.");
        return;
    }
    clearOverrideInternal();
}

void BenchmarkController::onPauseObserved() {
    if (m_pauseObserved) return;
    m_pauseObserved = true;
    if (m_session && isRunning()) m_session->pauseEvents++;
}

void BenchmarkController::onResumeObserved() {
    m_pauseObserved = false;
}

void BenchmarkController::onTierTransition() {
    if (m_session && isRunning()) m_session->tierTransitions++;
}

void BenchmarkController::onFallbackEvent() {
    if (m_session && isRunning()) m_session->fallbackEvents++;
}

void BenchmarkController::finalize(BenchmarkState terminalState, std::string_view warningCode,
                                   const std::string& message) {
    if (!m_session) return;
    if (isTerminalState(m_session->state)) return;   // never finalize twice

    m_session->state = BenchmarkState::Finalizing;
    if (!warningCode.empty()) m_session->addWarning(warningCode);

    const uint64_t endEpoch = epochNowMs();
    const uint64_t endMonotonic = monotonicNowMs();
    m_session->endedEpochMs = endEpoch;
    m_session->endedUtc = formatUtcIso8601(endEpoch);
    m_session->wallDurationMs =
        static_cast<double>(endMonotonic) - static_cast<double>(m_startMonotonicMs);

    diagnostics::WindowStatistics stats;
    if (m_window) {
        stats = m_window->computeStatistics(diagnostics::StatPrecision::Exact);
    }
    m_session->stats = stats;
    m_session->capturedDurationMs = stats.totalFrameTimeMs;
    m_session->framesCounted = stats.samples;
    m_session->framesExcluded = stats.excludedSamples;
    m_session->framesRejected = stats.rejectedSamples;
    m_session->excludedGapMs = stats.excludedTimeMs;
    m_session->config = m_bridge.readActiveConfig();

    if (!m_policy.stopOnLevelReset && m_session->resetEvents > 0) {
        m_session->addWarning(warning::kSegmentNotContiguous);
    }

    // Decide the terminal state from the data, not from the caller's request.
    BenchmarkState finalState = terminalState;
    const bool hasEnoughData = stats.samples > 0 &&
                               stats.totalFrameTimeMs >= m_policy.minValidCapturedMs;
    if (!hasEnoughData) {
        // Too little window to be a measurement. One exception: an explicit user
        // cancellation keeps saying `cancelled`, because that states a fact about
        // what happened which `invalid` would hide. Nothing is claimed either way -
        // `isComparable()` is false for both states, and the record still carries
        // `no_samples_recorded` / `capture_shorter_than_target`.
        finalState = (terminalState == BenchmarkState::Cancelled)
                         ? BenchmarkState::Cancelled
                         : BenchmarkState::Invalid;
    } else if (terminalState == BenchmarkState::Completed &&
               stats.totalFrameTimeMs < m_policy.targetDurationMs * 0.98) {
        // Cannot happen through the normal timeout path, but a session must
        // never be labelled completed when its window is short.
        finalState = BenchmarkState::Incomplete;
    }
    m_session->state = finalState;

    m_session->refreshDerivedWarnings();

    // Restore the user's configuration before anything that can fail.
    clearOverrideInternal();

    if (m_store) {
        SaveResult saved = m_store->save(*m_session);
        if (saved.ok) {
            m_session->sourcePath = saved.path.string();
            if (!saved.wrote) {
                OPTIGD_LOG_WARN("Benchmark Mode: session '" + m_session->id +
                                "' was already stored; the existing file was kept");
            }
            std::string indexError;
            if (!m_store->rebuildIndex(indexError)) {
                OPTIGD_LOG_WARN("Benchmark Mode: could not refresh the session index: " + indexError);
            }
        } else {
            m_session->addWarning(warning::kPersistFailed);
            OPTIGD_LOG_ERROR("Benchmark Mode: could not save session '" + m_session->id +
                             "': " + saved.error);
            // Retry once with a distinct id rather than lose the evidence.
            uint32_t counter = ++m_sessionCounter;
            std::string retryId = m_store->allocateSessionId(m_session->mode, endEpoch, counter);
            m_sessionCounter = counter;
            if (retryId != m_session->id) {
                m_session->id = retryId;
                SaveResult retried = m_store->save(*m_session);
                if (retried.ok) {
                    m_session->sourcePath = retried.path.string();
                    OPTIGD_LOG_INFO("Benchmark Mode: session saved under the retry id '" + retryId + "'");
                } else {
                    OPTIGD_LOG_ERROR("Benchmark Mode: retry save also failed: " + retried.error);
                }
            }
        }
    } else {
        m_session->addWarning(warning::kPersistFailed);
        m_session->sourcePath.clear();
    }

    m_window.reset();

    OPTIGD_LOG_INFO("Benchmark Mode: session '" + m_session->id + "' finished as " +
                    stateId(m_session->state) + " (" +
                    std::to_string(static_cast<int>(m_session->capturedDurationMs / 1000.0)) +
                    "s captured, " + std::to_string(m_session->framesCounted) + " frames). " + message);
}

// ---------------------------------------------------------------------------
// Observation
// ---------------------------------------------------------------------------

diagnostics::WindowStatistics BenchmarkController::getLiveStatistics(
    diagnostics::StatPrecision precision) const {
    if (!m_window) return diagnostics::WindowStatistics{};
    return m_window->computeStatistics(precision);
}

BenchmarkSnapshot BenchmarkController::getSnapshot() const {
    BenchmarkSnapshot snap;
    snap.configOverrideActive = m_overrideActive;

    core::OptiConfig active = m_bridge.readActiveConfig();
    snap.tierName = core::tierToString(active.tier);
    snap.spatialCullingEnabled = active.flags.enableSpatialCulling;
    snap.particleThrottlingEnabled = active.flags.enableParticleThrottling;
    snap.matrixCachingEnabled = active.flags.enableMatrixCaching;
    snap.adaptiveGovernorEnabled = active.flags.enableAdaptiveGovernor;
    snap.triggerIndexingEnabled = active.flags.enableTriggerIndexing;

    if (!m_session) {
        snap.state = BenchmarkState::Idle;
        snap.isRunning = false;
        snap.hasTerminalSession = false;
        snap.targetDurationMs = m_policy.targetDurationMs;
        snap.remainingDurationMs = m_policy.targetDurationMs;
        return snap;
    }

    snap.state = m_session->state;
    snap.mode = m_session->mode;
    snap.sessionId = m_session->id;
    snap.targetDurationMs = m_session->targetDurationMs;
    snap.framesObserved = m_session->framesObserved;
    snap.framesCounted = m_session->framesCounted;
    snap.framesExcluded = m_session->framesExcluded;
    snap.pauseEvents = m_session->pauseEvents;
    snap.resetEvents = m_session->resetEvents;
    snap.gapEvents = m_session->gapEvents;
    snap.warnings = m_session->warnings;
    snap.persistPath = m_session->sourcePath;
    snap.persisted = !m_session->sourcePath.empty();
    snap.hasTerminalSession = isTerminalState(m_session->state);
    snap.isRunning = isRunning();

    if (isRunning() && m_window) {
        snap.capturedDurationMs = m_window->getTotalFrameTimeMs();
        snap.framesCounted = m_window->getSampleCount();
        snap.framesExcluded = m_window->getExcludedCount();
        snap.wallDurationMs = static_cast<double>(monotonicNowMs()) -
                              static_cast<double>(m_startMonotonicMs);
        snap.windowBytes = m_window->getOwnedBytes();
    } else {
        snap.capturedDurationMs = m_session->capturedDurationMs;
        snap.wallDurationMs = m_session->wallDurationMs;
    }

    snap.remainingDurationMs = std::max(0.0, snap.targetDurationMs - snap.capturedDurationMs);
    snap.progress01 = (snap.targetDurationMs > 0.0)
                          ? std::clamp(snap.capturedDurationMs / snap.targetDurationMs, 0.0, 1.0)
                          : 0.0;
    return snap;
}

} // namespace optigd::benchmark

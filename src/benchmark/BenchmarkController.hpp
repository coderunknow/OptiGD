#pragma once

// Benchmark Mode lifecycle: the state machine, the transient configuration
// override, and the measurement window for one capture.
//
// This is platform-agnostic on purpose. Everything that decides *whether a
// session is valid* lives here and is unit-testable in the standalone build;
// `src/geode/BenchmarkUI.cpp` only renders the values this class produces and
// forwards the events the engine gives it. That split is also what keeps the
// Geode-specific surface small enough to be reviewed.
//
// Lifecycle guarantees implemented here:
//   * a session owns its window. Counters, samples and percentiles cannot be
//     contaminated by a previous level or a previous session, because the
//     window is created when the session starts and destroyed with it;
//   * the transient configuration is installed through `ConfigManager`'s
//     override layer and cleared on EVERY exit path — normal completion,
//     cancellation, level reset, level exit, a hard wall-clock ceiling and a
//     thrown/failed persistence step alike;
//   * the override is *verified* after installation and re-verified every frame.
//     If a Geode settings listener or anything else rewrote the active
//     configuration mid-session, the controller re-applies it and records
//     `config_override_reapplied`, so a report can never claim a configuration
//     that was not actually in force;
//   * duplicate starts, starts while finalizing, and invalid transitions are
//     rejected with a machine-readable code and a message the UI can show.

#include "../config/ConfigManager.hpp"
#include "../diagnostics/SampleWindow.hpp"
#include "BenchmarkSession.hpp"
#include "BenchmarkStore.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace optigd::benchmark {

struct BenchmarkPolicy {
    // The required 60 s default. Configurable so longer captures are possible;
    // the value actually used is recorded in every session.
    double targetDurationMs{60000.0};
    // Inter-frame intervals above this are stalls (pause / app switch / load),
    // not gameplay. Matches the 250 ms guard the beta.2 frame hook already used.
    double stallThresholdMs{250.0};
    // A capture shorter than this has no usable statistics at all.
    double minValidCapturedMs{500.0};
    // Death / attempt restart behaviour. Default `true` per the user decision:
    // a restart means the measured level segment is no longer contiguous, so the
    // session is finalized as `incomplete` instead of quietly mixing sections.
    bool stopOnLevelReset{true};
    // Hard ceiling so a session cannot stay open forever while the game is
    // paused or backgrounded. Finalizes as `incomplete`.
    double maxWallClockMs{900000.0};
    // Ring sizing: capacity = clamp(targetDurationMs * maxTrackedFps / 1000).
    double maxTrackedFps{1000.0};
    size_t minRingCapacity{1024};
    size_t maxRingCapacity{120000};
    // Frames slower than this are counted as hitches and reported separately.
    double hitchThresholdMs{1000.0 / 30.0};
};

// Everything the UI needs to draw one frame of state. Cheap to build: no
// statistics are computed here, the caller asks for those separately.
struct BenchmarkSnapshot {
    BenchmarkState state{BenchmarkState::Idle};
    BenchmarkMode mode{BenchmarkMode::Baseline};
    std::string sessionId;

    double targetDurationMs{0.0};
    double capturedDurationMs{0.0};
    double remainingDurationMs{0.0};
    double wallDurationMs{0.0};
    double progress01{0.0};

    uint64_t framesObserved{0};
    uint64_t framesCounted{0};
    uint64_t framesExcluded{0};
    uint32_t pauseEvents{0};
    uint32_t resetEvents{0};
    uint32_t gapEvents{0};

    bool configOverrideActive{false};
    std::string tierName;
    bool spatialCullingEnabled{false};
    bool particleThrottlingEnabled{false};
    bool matrixCachingEnabled{false};
    bool adaptiveGovernorEnabled{false};
    bool triggerIndexingEnabled{false};

    bool persisted{false};
    std::string persistPath;
    std::string lastMessage;
    std::string lastErrorCode;
    std::vector<std::string> warnings;

    bool hasTerminalSession{false};   // a finished session is available to review
    bool isRunning{false};
    size_t windowBytes{0};
};

// Injected runtime services. The Geode bridge supplies real implementations;
// `makeDefaultRuntimeBridge()` supplies standalone ones backed by
// `ConfigManager` and `std::chrono` so the controller can be tested without a
// game process.
struct BenchmarkRuntimeBridge {
    std::function<core::OptiConfig()> readActiveConfig;
    // Arms a NEW transient override. Must return false when one is already
    // armed, so an overlapping session is detected rather than silently
    // replacing the configuration another capture needs restored.
    std::function<bool(const core::OptiConfig&, const std::string&)> installOverride;
    // Re-asserts the configuration of an override this session already owns,
    // without re-arming. Used when something else rewrote the active
    // configuration mid-capture. Must return false when nothing is armed.
    std::function<bool(const core::OptiConfig&)> updateOverride;
    std::function<bool()> clearOverride;
    std::function<uint64_t()> monotonicMs;
    std::function<uint64_t()> epochMs;
    std::function<LevelIdentity()> levelIdentity;
    std::function<BenchmarkEnvironment()> environment;
};

BenchmarkRuntimeBridge makeDefaultRuntimeBridge();

// Builds the baseline (passthrough) configuration from the user's active one.
// Only the optimization feature flags are forced off; every numeric parameter
// and the tier are preserved so the record shows exactly what changed.
core::OptiConfig makeBaselineConfig(const core::OptiConfig& userConfig);
// Names of the features that were enabled in `userConfig` and are disabled in
// `baselineConfig`. Derived from the two configs, not from a hardcoded list, so
// it cannot claim a change that did not happen.
std::vector<std::string> describePassthroughFeatures(const core::OptiConfig& userConfig,
                                                     const core::OptiConfig& baselineConfig);

class BenchmarkController {
public:
    static BenchmarkController& instance();

    // Wall-clock slack granted to a capture, as a multiple of its target ACTIVE
    // duration. The ceiling is a runaway guard for a session left open while the
    // game is paused or backgrounded - it is NOT a time budget. Real play spends
    // wall time on pauses, deaths, restarts, loading stalls and app switches that
    // the active window deliberately excludes, so the slack has to be generous or
    // an ordinary play session would be finalized as `incomplete` for nothing
    // worse than having paused once.
    static constexpr double kWallClockCeilingFactor = 15.0;

    // The ceiling recommended for a target active duration. Callers may keep a
    // wider one; nothing should silently narrow it (see `setPolicy`).
    static double recommendedWallClockCeilingMs(double targetDurationMs) {
        return targetDurationMs * kWallClockCeilingFactor;
    }

    // --- configuration ------------------------------------------------------
    void setPolicy(const BenchmarkPolicy& policy);
    const BenchmarkPolicy& getPolicy() const { return m_policy; }
    void setRuntimeBridge(const BenchmarkRuntimeBridge& bridge);
    void setStore(std::shared_ptr<BenchmarkStore> store);
    const std::shared_ptr<BenchmarkStore>& getStore() const { return m_store; }
    // Environment values the bridge cannot supply (versions are known to the
    // Geode layer). Falls back to the standalone defaults.
    void setEnvironment(const BenchmarkEnvironment& environment);
    void setLevelIdentity(const LevelIdentity& identity);

    // --- lifecycle ----------------------------------------------------------
    BenchmarkActionResult startSession(BenchmarkMode mode);
    BenchmarkActionResult cancelSession();

    // Called once per rendered frame with the frame statistics the engine just
    // produced. No-op when no session is running.
    void onFrame(const core::FrameStats& stats);

    void onLevelReset();
    void onLevelExit();
    void onPauseObserved();
    void onResumeObserved();
    void onTierTransition();
    void onFallbackEvent();

    // Forces restoration of the transient configuration. Called by the mod on
    // shutdown and by tests; safe to call when nothing is active.
    void restoreConfiguration();

    // --- observation --------------------------------------------------------
    BenchmarkSnapshot getSnapshot() const;
    // Live statistics for the HUD. `Fast` is the default because it is O(bins)
    // rather than O(n log n); the panel labels those values as approximate.
    diagnostics::WindowStatistics getLiveStatistics(
        diagnostics::StatPrecision precision = diagnostics::StatPrecision::Fast) const;

    bool isRunning() const;
    bool hasSession() const { return m_session != nullptr; }
    const BenchmarkSession* getSession() const { return m_session.get(); }
    const BenchmarkSession* getLastSession() const { return m_lastSession.get(); }
    BenchmarkState getState() const;

    // True when the transient override is installed right now.
    bool isOverrideActive() const { return m_overrideActive; }
    uint32_t getOverrideReapplyCount() const { return m_overrideReapplyCount; }

    // Tells the controller whether it is running inside a live Geometry Dash
    // process. Only the Geode integration (`installBenchmarkRuntimeBridge`) may
    // set this to true; the standalone binaries leave it false so every record
    // they write is stamped [SIMULATED] instead of claiming an in-game capture.
    void setLiveGameRuntime(bool live) { m_liveGameRuntime = live; }
    bool isLiveGameRuntime() const { return m_liveGameRuntime; }

    // Session id that will be used by the next `startSession()`. Exposed so the
    // HUD can show the id of a running capture.
    std::string previewNextSessionId(BenchmarkMode mode) const;

private:
    BenchmarkController();
    BenchmarkController(const BenchmarkController&) = delete;
    BenchmarkController& operator=(const BenchmarkController&) = delete;

    size_t ringCapacityForPolicy() const;
    uint64_t monotonicNowMs() const;
    uint64_t epochNowMs() const;
    BenchmarkEnvironment resolveEnvironment() const;
    LevelIdentity resolveLevelIdentity() const;

    void installOverrideFor(BenchmarkMode mode, const core::OptiConfig& userConfig);
    void clearOverrideInternal();
    void verifyOverride();
    void finalize(BenchmarkState terminalState, std::string_view warningCode,
                  const std::string& message);

    BenchmarkPolicy m_policy;
    BenchmarkRuntimeBridge m_bridge;
    std::shared_ptr<BenchmarkStore> m_store;
    BenchmarkEnvironment m_environmentOverride;
    bool m_hasEnvironmentOverride{false};
    LevelIdentity m_levelIdentityOverride;
    bool m_hasLevelIdentityOverride{false};

    std::unique_ptr<BenchmarkSession> m_session;      // current or last terminal
    std::unique_ptr<BenchmarkSession> m_lastSession;  // previous terminal session
    std::unique_ptr<diagnostics::SampleWindow> m_window;

    bool m_overrideActive{false};
    uint32_t m_overrideReapplyCount{0};
    bool m_liveGameRuntime{false};
    core::OptiConfig m_requestedOverrideConfig;
    core::OptiConfig m_userConfigAtStart;

    uint64_t m_startMonotonicMs{0};
    uint64_t m_lastMonotonicMs{0};
    bool m_pauseObserved{false};
    mutable uint32_t m_sessionCounter{0};
};

} // namespace optigd::benchmark

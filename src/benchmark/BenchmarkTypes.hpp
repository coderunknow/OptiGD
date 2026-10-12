#pragma once

// Shared vocabulary for Benchmark Mode.
//
// These types are deliberately free of any Geode / Cocos2d dependency so the
// whole benchmark lifecycle can be unit-tested in the standalone build and so
// the in-game UI layer stays a thin renderer over values it did not compute.

#include "../core/Types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace optigd::benchmark {

// ---------------------------------------------------------------------------
// Mode: which OptiGD configuration the session measures.
// ---------------------------------------------------------------------------
enum class BenchmarkMode : uint8_t {
    Baseline = 0,   // OptiGD installed, optimization features in passthrough
    Optimized = 1,  // the user's selected OptiGD configuration is active
};

const char* modeId(BenchmarkMode mode);          // "baseline" / "optimized"
const char* modeDisplayName(BenchmarkMode mode); // human readable, for the HUD
bool parseMode(std::string_view text, BenchmarkMode& out);

// ---------------------------------------------------------------------------
// State: the lifecycle the UI has to be able to display.
//
// The request asks for "idle, running, completing, completed, cancelled or
// invalid". Those map onto the values below; `Incomplete` is the additional
// state needed to express "a capture that ended early" without either calling
// it completed or throwing the partial data away.
// ---------------------------------------------------------------------------
enum class BenchmarkState : uint8_t {
    Idle = 0,       // nothing running, no session loaded
    Running,        // measuring
    Finalizing,     // "completing": statistics + persistence in progress
    Completed,      // reached the configured duration with usable samples
    Cancelled,      // stopped by the user before the duration elapsed
    Incomplete,     // ended early (level exit, reset, interruption)
    Invalid,        // terminal, but produced no usable samples at all
};

const char* stateId(BenchmarkState state);          // machine readable
const char* stateDisplayName(BenchmarkState state); // HUD / panel wording
bool isTerminalState(BenchmarkState state);
bool isFailureState(BenchmarkState state);
// A session may only be used as one side of an A/B comparison when it actually
// measured what it claims to have measured.
bool isComparableState(BenchmarkState state);
bool parseState(std::string_view text, BenchmarkState& out);

// ---------------------------------------------------------------------------
// Evidence classification.
//
// Every artifact OptiGD writes states where its numbers came from, in a fixed
// vocabulary the Python validators and the docs can check. The classification is
// stamped by the code that produced the record; it is never inferred from the
// data, and it can only ever be downgraded on read (see BenchmarkSession).
// ---------------------------------------------------------------------------
namespace evidence {
// Captured by OptiGD's own instrumentation inside a live Geometry Dash process.
// Still an INTERNAL measurement: it says nothing about GPU work, swap/present
// timing, or externally observed frame delivery.
inline constexpr const char* kMeasuredInGame = "[MEASURED-IN-GAME]";
inline constexpr const char* kMeasuredInGameNote =
    "This record was produced by OptiGD's own instrumentation inside a live "
    "Geometry Dash process. It is an internal measurement, NOT an external "
    "presented-frame capture. See measurement_definitions.";
// Produced by a standalone binary: real OptiGD code, but a synthetic clock and
// no game process. Structural evidence about the format and the invariants -
// never a measurement of anything.
inline constexpr const char* kSimulated = "[SIMULATED]";
inline constexpr const char* kSimulatedNote =
    "This record was produced by a standalone OptiGD binary (unit tests / "
    "schema tools) driving the real session code with a synthetic clock and no "
    "Geometry Dash process. It is structural evidence about the record format "
    "and its invariants. It contains no measured FPS result and must never be "
    "quoted as one.";
// Internal telemetry that is not a benchmark session (diagnostics report).
inline constexpr const char* kInternalTelemetry = "[INTERNAL-TELEMETRY]";
} // namespace evidence

// ---------------------------------------------------------------------------
// Integrity warning codes.
//
// Fixed strings (not free text) so tests, the Python validators and the
// in-game panel can all agree on what a warning means. Each one is also
// rendered with a human sentence by `warningDescription()`.
// ---------------------------------------------------------------------------
namespace warning {
inline constexpr const char* kLevelReset          = "level_reset";
inline constexpr const char* kLevelExit           = "level_exit";
inline constexpr const char* kCancelledByUser     = "cancelled_by_user";
inline constexpr const char* kPaused              = "paused";
inline constexpr const char* kLongGap             = "long_gap";
inline constexpr const char* kTierChanged         = "tier_changed_during_capture";
inline constexpr const char* kFallbackTriggered   = "safety_fallback_triggered";
inline constexpr const char* kConfigOverrideLost  = "config_override_reapplied";
inline constexpr const char* kShortCapture        = "capture_shorter_than_target";
inline constexpr const char* kNoSamples           = "no_samples_recorded";
inline constexpr const char* kRingOverflow        = "sample_ring_overflow";
inline constexpr const char* kHistogramSaturated  = "histogram_saturated";
inline constexpr const char* kApproxPercentiles   = "percentiles_approximated";
inline constexpr const char* kLevelUnknown        = "level_identity_unavailable";
inline constexpr const char* kSegmentNotContiguous = "segment_not_contiguous";
inline constexpr const char* kWallClockDrift      = "wall_clock_exceeds_captured_window";
inline constexpr const char* kRejectedSamples     = "rejected_samples";
inline constexpr const char* kDurationMismatch    = "target_duration_mismatch";
inline constexpr const char* kPersistFailed       = "persist_failed";
} // namespace warning

std::string warningDescription(std::string_view code);

// ---------------------------------------------------------------------------
// Level identity.
//
// Best effort only. Geometry Dash does not always expose a stable level id
// (local/created levels use an id that is only meaningful locally, and some
// contexts have no `GJGameLevel` at all), so every field carries an explicit
// "known" flag and the report states when identity could not be established.
// Nothing here is invented to make two sessions look comparable.
// ---------------------------------------------------------------------------
struct LevelIdentity {
    std::string name;
    std::string creator;
    long long levelId{0};
    bool levelIdKnown{false};
    int attempt{0};
    bool attemptKnown{false};
    bool practiceMode{false};
    bool practiceModeKnown{false};
    // Where the values came from, e.g. "PlayLayer::m_level" or "unavailable".
    std::string source{"unavailable"};

    bool hasAnyIdentity() const { return !name.empty() || levelIdKnown; }
    // Two identities are "the same level" only when both are known and agree.
    // An unknown identity never counts as a match: that would be claiming
    // equivalence that was not established.
    static bool matches(const LevelIdentity& a, const LevelIdentity& b, std::string& reason);
};

// ---------------------------------------------------------------------------
// Environment / version metadata recorded with every session.
// ---------------------------------------------------------------------------
struct BenchmarkEnvironment {
    std::string optigdVersion;
    std::string gdVersion;
    std::string geodeVersion;
    std::string osName;
    std::string architecture;
    std::string compiler;
    uint32_t logicalCores{0};
    uint64_t totalRamMb{0};
    // Geometry Dash's configured frame-rate limit / vsync state is not reliably
    // readable from a mod, so it is recorded only when the caller can supply it
    // and is reported as unknown otherwise. Never guessed.
    std::string refreshHint{"unknown"};

    std::string describe() const;
};

// ---------------------------------------------------------------------------
// Result of a lifecycle request. `ok == false` always carries a reason that the
// UI can show, so a rejected start is never silent.
// ---------------------------------------------------------------------------
struct BenchmarkActionResult {
    bool ok{false};
    BenchmarkState state{BenchmarkState::Idle};
    std::string sessionId;
    std::string message;   // human readable
    std::string errorCode; // machine readable, e.g. "already_running"
};

} // namespace optigd::benchmark

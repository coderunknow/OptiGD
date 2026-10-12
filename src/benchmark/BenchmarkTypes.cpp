#include "BenchmarkTypes.hpp"

#include <sstream>

namespace optigd::benchmark {

const char* modeId(BenchmarkMode mode) {
    switch (mode) {
        case BenchmarkMode::Baseline: return "baseline";
        case BenchmarkMode::Optimized: return "optimized";
        default: return "unknown";
    }
}

const char* modeDisplayName(BenchmarkMode mode) {
    switch (mode) {
        case BenchmarkMode::Baseline:
            return "BASELINE (OptiGD installed, optimizations in passthrough)";
        case BenchmarkMode::Optimized:
            return "OPTIMIZED (your OptiGD configuration active)";
        default:
            return "UNKNOWN";
    }
}

bool parseMode(std::string_view text, BenchmarkMode& out) {
    if (text == "baseline" || text == "Baseline" || text == "BASELINE") {
        out = BenchmarkMode::Baseline;
        return true;
    }
    if (text == "optimized" || text == "optimised" || text == "Optimized" ||
        text == "OPTIMIZED" || text == "optigd") {
        out = BenchmarkMode::Optimized;
        return true;
    }
    return false;
}

const char* stateId(BenchmarkState state) {
    switch (state) {
        case BenchmarkState::Idle: return "idle";
        case BenchmarkState::Running: return "running";
        case BenchmarkState::Finalizing: return "finalizing";
        case BenchmarkState::Completed: return "completed";
        case BenchmarkState::Cancelled: return "cancelled";
        case BenchmarkState::Incomplete: return "incomplete";
        case BenchmarkState::Invalid: return "invalid";
        default: return "unknown";
    }
}

const char* stateDisplayName(BenchmarkState state) {
    switch (state) {
        case BenchmarkState::Idle: return "Idle";
        case BenchmarkState::Running: return "RUNNING";
        case BenchmarkState::Finalizing: return "Finalizing...";
        case BenchmarkState::Completed: return "COMPLETED";
        case BenchmarkState::Cancelled: return "CANCELLED";
        case BenchmarkState::Incomplete: return "INCOMPLETE";
        case BenchmarkState::Invalid: return "INVALID";
        default: return "Unknown";
    }
}

bool isTerminalState(BenchmarkState state) {
    switch (state) {
        case BenchmarkState::Completed:
        case BenchmarkState::Cancelled:
        case BenchmarkState::Incomplete:
        case BenchmarkState::Invalid:
            return true;
        default:
            return false;
    }
}

bool isFailureState(BenchmarkState state) {
    switch (state) {
        case BenchmarkState::Cancelled:
        case BenchmarkState::Incomplete:
        case BenchmarkState::Invalid:
            return true;
        default:
            return false;
    }
}

bool isComparableState(BenchmarkState state) {
    // Only a capture that ran for its intended window may be used as one side
    // of an A/B comparison. A cancelled, interrupted or empty session is
    // retained as evidence but must never be presented as a measurement.
    return state == BenchmarkState::Completed;
}

bool parseState(std::string_view text, BenchmarkState& out) {
    if (text == "idle") { out = BenchmarkState::Idle; return true; }
    if (text == "running") { out = BenchmarkState::Running; return true; }
    if (text == "finalizing") { out = BenchmarkState::Finalizing; return true; }
    if (text == "completed") { out = BenchmarkState::Completed; return true; }
    if (text == "cancelled") { out = BenchmarkState::Cancelled; return true; }
    if (text == "incomplete") { out = BenchmarkState::Incomplete; return true; }
    if (text == "invalid") { out = BenchmarkState::Invalid; return true; }
    return false;
}

std::string warningDescription(std::string_view code) {
    if (code == warning::kLevelReset)
        return "The attempt was restarted during the capture, so the measured level segment is not contiguous.";
    if (code == warning::kLevelExit)
        return "The level was exited before the capture reached its target duration.";
    if (code == warning::kCancelledByUser)
        return "The capture was stopped by the user before the target duration elapsed.";
    if (code == warning::kPaused)
        return "The game was paused during the capture. Paused time is excluded from the measurement window.";
    if (code == warning::kLongGap)
        return "One or more inter-frame intervals exceeded the gap threshold (application switch, loading or a stall). Those intervals are excluded from the window and counted separately.";
    if (code == warning::kTierChanged)
        return "The adaptive governor changed the performance tier while measuring, so the capture does not describe one fixed configuration.";
    if (code == warning::kFallbackTriggered)
        return "A safety fallback fired while measuring, so part of the capture ran with reduced optimization settings.";
    if (code == warning::kConfigOverrideLost)
        return "Something outside Benchmark Mode rewrote the active configuration; the session configuration was re-applied. Check the recorded configuration snapshot.";
    if (code == warning::kShortCapture)
        return "The captured window is shorter than the target duration.";
    if (code == warning::kNoSamples)
        return "No frame samples were recorded, so no statistic in this session is meaningful.";
    if (code == warning::kRingOverflow)
        return "More samples arrived than the exact-sample ring can retain. Percentiles are computed from the fixed-bin histogram instead.";
    if (code == warning::kHistogramSaturated)
        return "At least one frame time exceeded the histogram range, so the tail percentile is reported as a lower bound.";
    if (code == warning::kApproxPercentiles)
        return "Percentiles are histogram approximations (bin-width resolution), not exact order statistics.";
    if (code == warning::kLevelUnknown)
        return "The level identity could not be established, so this session cannot be proven to cover the same level as another.";
    if (code == warning::kSegmentNotContiguous)
        return "The capture continued across an attempt restart, so it mixes different level sections.";
    if (code == warning::kWallClockDrift)
        return "Wall-clock time substantially exceeds the captured gameplay window (pauses, stalls or an application switch during the capture).";
    if (code == warning::kRejectedSamples)
        return "Some frame observations were rejected as non-finite or non-positive and are not part of any statistic.";
    if (code == warning::kDurationMismatch)
        return "The configured target duration differs between the two sessions being compared.";
    if (code == warning::kPersistFailed)
        return "The session could not be written to the local save directory, so this result exists only in memory and will be lost when the game closes.";
    return std::string("Unrecognized integrity warning: ") + std::string(code);
}

bool LevelIdentity::matches(const LevelIdentity& a, const LevelIdentity& b, std::string& reason) {
    if (!a.hasAnyIdentity() || !b.hasAnyIdentity()) {
        reason = "Level identity is unavailable for at least one session; equivalence cannot be established.";
        return false;
    }
    if (a.levelIdKnown && b.levelIdKnown) {
        if (a.levelId != b.levelId) {
            std::ostringstream ss;
            ss << "Different level ids (" << a.levelId << " vs " << b.levelId << ").";
            reason = ss.str();
            return false;
        }
        reason = "Same level id.";
        return true;
    }
    // Only names are known: compare them, but say so explicitly.
    if (a.name != b.name) {
        reason = "Different level names ('" + a.name + "' vs '" + b.name + "').";
        return false;
    }
    reason = "Same level name (no stable level id available for either session).";
    return true;
}

std::string BenchmarkEnvironment::describe() const {
    std::ostringstream ss;
    ss << "OptiGD " << optigdVersion << " | GD " << gdVersion << " | Geode " << geodeVersion
       << " | " << osName << " " << architecture << " | " << logicalCores << " cores";
    return ss.str();
}

} // namespace optigd::benchmark

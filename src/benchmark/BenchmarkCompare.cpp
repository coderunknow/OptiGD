#include "BenchmarkCompare.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace optigd::benchmark {

const char* verdictName(ComparisonVerdict verdict) {
    switch (verdict) {
        case ComparisonVerdict::Improved: return "improved";
        case ComparisonVerdict::Worsened: return "worsened";
        case ComparisonVerdict::Unchanged: return "unchanged";
        case ComparisonVerdict::Unavailable: return "unavailable";
        default: return "unknown";
    }
}

const MetricComparison* ComparisonResult::find(WindowMetricId id) const {
    for (const auto& m : metrics) {
        if (m.metric == id) return &m;
    }
    return nullptr;
}

namespace {

const char* unitFor(WindowMetricId id) {
    switch (id) {
        case WindowMetricId::AvgFps: return "fps";
        case WindowMetricId::MeanFrameTimeMs:
        case WindowMetricId::P50FrameTimeMs:
        case WindowMetricId::P95FrameTimeMs:
        case WindowMetricId::P99FrameTimeMs:
        case WindowMetricId::MaxFrameTimeMs:
        case WindowMetricId::StdDevFrameTimeMs:
        case WindowMetricId::AvgUpdateCpuMs:
        case WindowMetricId::AvgUpdateThreadCpuMs:
        case WindowMetricId::CapturedDurationMs:
            return "ms";
        case WindowMetricId::AvgOverheadUs:
            return "us";
        default:
            return "count";
    }
}

void addIssue(std::vector<CompatibilityIssue>& issues, const std::string& code,
              const std::string& message, bool blocking) {
    for (const auto& existing : issues) {
        if (existing.code == code) return;
    }
    issues.push_back(CompatibilityIssue{code, message, blocking});
}

std::string formatValue(double v, const char* unit) {
    std::ostringstream ss;
    ss.precision(3);
    ss << std::fixed << v;
    if (unit && unit[0] != '\0' && std::string(unit) != "count") ss << ' ' << unit;
    return ss.str();
}

std::string formatDelta(const MetricComparison& m) {
    std::ostringstream ss;
    ss.precision(3);
    ss << std::fixed << (m.absoluteDelta >= 0.0 ? "+" : "") << m.absoluteDelta;
    if (m.unit != "count") ss << ' ' << m.unit;
    if (m.relativeAvailable) {
        ss << " (" << (m.relativePct >= 0.0 ? "+" : "") << m.relativePct << "%)";
    } else if (!m.note.empty()) {
        ss << " [" << m.note << "]";
    }
    return ss.str();
}

} // namespace

ComparisonResult compareSessions(const BenchmarkSession& baseline,
                                 const BenchmarkSession& optimized,
                                 double noiseThresholdPct) {
    ComparisonResult result;
    result.noiseThresholdPct = std::isfinite(noiseThresholdPct) && noiseThresholdPct >= 0.0
                                   ? noiseThresholdPct : 2.0;
    result.significanceClaimed = false;
    result.significanceNote =
        "No statistical significance is claimed. This is one pair of captures: it "
        "cannot separate an optimization effect from run-to-run variability. Repeat "
        "both sides and compare the spread before concluding anything.";

    result.baselineId = baseline.id;
    result.optimizedId = optimized.id;
    result.baselineMode = modeId(baseline.mode);
    result.optimizedMode = modeId(optimized.mode);

    auto& issues = result.issues;

    // --- blocking checks ---------------------------------------------------
    if (baseline.stats.samples == 0 || optimized.stats.samples == 0) {
        addIssue(issues, "no_samples",
                 "At least one session recorded no frame samples, so there is nothing to compare.",
                 true);
    }
    if (!isComparableState(baseline.state) || !isComparableState(optimized.state)) {
        std::ostringstream ss;
        ss << "Only a session that completed its full target duration may be compared. "
           << "Baseline state: " << stateId(baseline.state)
           << ", optimized state: " << stateId(optimized.state) << ".";
        addIssue(issues, "session_not_completed", ss.str(), true);
    }
    if (baseline.mode == optimized.mode) {
        addIssue(issues, "same_mode",
                 std::string("Both sessions are '") + modeId(baseline.mode) +
                 "'. An A/B comparison needs one baseline and one optimized capture.",
                 true);
    }
    if (!baseline.environment.osName.empty() && !optimized.environment.osName.empty() &&
        (baseline.environment.osName != optimized.environment.osName ||
         baseline.environment.architecture != optimized.environment.architecture)) {
        addIssue(issues, "platform_mismatch",
                 "The two sessions ran on different platforms/architectures (" +
                 baseline.environment.osName + "/" + baseline.environment.architecture + " vs " +
                 optimized.environment.osName + "/" + optimized.environment.architecture + ").",
                 true);
    }

    std::string levelReason;
    bool levelMatches = LevelIdentity::matches(baseline.level, optimized.level, levelReason);
    if (!levelMatches) {
        const bool bothUnknown = !baseline.level.hasAnyIdentity() && !optimized.level.hasAnyIdentity();
        addIssue(issues, bothUnknown ? "level_identity_unavailable" : "level_mismatch",
                 levelReason, /*blocking=*/!bothUnknown);
    }

    // Duration equivalence: the window must cover materially the same amount of
    // gameplay, otherwise the two captures describe different amounts of level.
    const double longerDuration = std::max(baseline.capturedDurationMs, optimized.capturedDurationMs);
    if (longerDuration > 0.0) {
        const double relative =
            std::fabs(baseline.capturedDurationMs - optimized.capturedDurationMs) / longerDuration;
        if (relative > 0.25) {
            std::ostringstream ss;
            ss.precision(1);
            ss << std::fixed;
            ss << "The captured gameplay windows differ by " << (relative * 100.0)
               << "% (" << baseline.capturedDurationMs << " ms vs "
               << optimized.capturedDurationMs << " ms), which is too large to treat "
               << "as the same measurement.";
            addIssue(issues, "duration_mismatch", ss.str(), true);
        } else if (relative > 0.10) {
            std::ostringstream ss;
            ss.precision(1);
            ss << std::fixed;
            ss << "The captured gameplay windows differ by " << (relative * 100.0)
               << "%. The comparison is still produced, but the two captures do not "
               << "cover exactly the same amount of gameplay.";
            addIssue(issues, "duration_varies", ss.str(), false);
        }
    }

    // --- non-blocking context ---------------------------------------------
    if (std::fabs(baseline.targetDurationMs - optimized.targetDurationMs) > 1.0) {
        addIssue(issues, "target_duration_mismatch",
                 "The configured target durations differ (" +
                 std::to_string(static_cast<int>(baseline.targetDurationMs / 1000.0)) + "s vs " +
                 std::to_string(static_cast<int>(optimized.targetDurationMs / 1000.0)) + "s).",
                 false);
    }
    if (baseline.environment.optigdVersion != optimized.environment.optigdVersion ||
        baseline.environment.gdVersion != optimized.environment.gdVersion ||
        baseline.environment.geodeVersion != optimized.environment.geodeVersion) {
        addIssue(issues, "version_mismatch",
                 "Runtime versions differ (OptiGD " + baseline.environment.optigdVersion + "/" +
                 optimized.environment.optigdVersion + ", GD " + baseline.environment.gdVersion +
                 "/" + optimized.environment.gdVersion + ", Geode " +
                 baseline.environment.geodeVersion + "/" + optimized.environment.geodeVersion + ").",
                 false);
    }
    if (baseline.level.practiceModeKnown && optimized.level.practiceModeKnown &&
        baseline.level.practiceMode != optimized.level.practiceMode) {
        addIssue(issues, "practice_mode_mismatch",
                 "One capture ran in practice mode and the other did not.", false);
    }
    if (baseline.level.attemptKnown && optimized.level.attemptKnown &&
        baseline.level.attempt != optimized.level.attempt) {
        addIssue(issues, "attempt_differs",
                 "The captures come from different attempt numbers (" +
                 std::to_string(baseline.level.attempt) + " vs " +
                 std::to_string(optimized.level.attempt) +
                 "). Different attempts reach different level sections.", false);
    }
    if (baseline.startedEpochMs > 0 && optimized.startedEpochMs > 0) {
        const uint64_t bStart = baseline.startedEpochMs;
        const uint64_t bEnd = baseline.endedEpochMs > 0 ? baseline.endedEpochMs : bStart;
        const uint64_t oStart = optimized.startedEpochMs;
        const uint64_t oEnd = optimized.endedEpochMs > 0 ? optimized.endedEpochMs : oStart;
        if (bStart <= oEnd && oStart <= bEnd) {
            addIssue(issues, "overlapping_captures",
                     "The two capture windows overlap in time, so they cannot both "
                     "describe independent runs.", true);
        }
    }

    auto addIntegrityIssues = [&](const BenchmarkSession& s, const char* side) {
        for (const auto& code : s.warnings) {
            if (code == warning::kTierChanged || code == warning::kFallbackTriggered ||
                code == warning::kSegmentNotContiguous || code == warning::kConfigOverrideLost ||
                code == warning::kApproxPercentiles || code == warning::kHistogramSaturated) {
                addIssue(issues, std::string("integrity_") + side,
                         std::string("The ") + side + " session carries the integrity warning '" +
                         code + "': " + warningDescription(code), false);
            }
        }
    };
    addIntegrityIssues(baseline, "baseline");
    addIntegrityIssues(optimized, "optimized");

    for (const auto& issue : issues) {
        if (issue.blocking) {
            result.hasBlockingIssue = true;
            break;
        }
    }
    result.comparisonMeaningful = !result.hasBlockingIssue &&
                                  baseline.stats.samples > 0 && optimized.stats.samples > 0;

    // --- per-metric comparison --------------------------------------------
    const bool valuesUsable = baseline.stats.samples > 0 && optimized.stats.samples > 0;
    for (size_t i = 0; i < static_cast<size_t>(WindowMetricId::Count); ++i) {
        const WindowMetricId id = static_cast<WindowMetricId>(i);
        MetricComparison m;
        m.metric = id;
        m.name = metricIdName(id);
        m.direction = metricDirection(id);
        m.unit = unitFor(id);
        m.baselineValue = baseline.metricValue(id);
        m.optimizedValue = optimized.metricValue(id);

        const bool finite = std::isfinite(m.baselineValue) && std::isfinite(m.optimizedValue);
        m.available = valuesUsable && finite;
        if (!m.available) {
            m.verdict = ComparisonVerdict::Unavailable;
            m.note = valuesUsable ? "non-finite value" : "no samples";
            result.metrics.push_back(std::move(m));
            continue;
        }

        m.absoluteDelta = m.optimizedValue - m.baselineValue;
        if (std::fabs(m.baselineValue) > 1e-12) {
            m.relativePct = (m.absoluteDelta / std::fabs(m.baselineValue)) * 100.0;
            m.relativeAvailable = std::isfinite(m.relativePct);
        } else {
            m.relativeAvailable = false;
            if (std::fabs(m.optimizedValue) <= 1e-12) {
                m.relativePct = 0.0;
                m.note = "both values are zero";
            } else {
                m.relativePct = 0.0;
                m.note = "baseline is zero; relative change is undefined";
            }
        }

        if (m.direction == MetricDirection::Informational) {
            m.verdict = ComparisonVerdict::Unchanged;
            if (m.note.empty()) {
                m.note = "informational only: depends on level content, not on optimization";
            }
            result.metrics.push_back(std::move(m));
            continue;
        }

        const double magnitude = m.relativeAvailable ? std::fabs(m.relativePct)
                                                     : std::fabs(m.absoluteDelta);
        const bool withinNoise = m.relativeAvailable
                                     ? (std::fabs(m.relativePct) <= result.noiseThresholdPct)
                                     : (std::fabs(m.absoluteDelta) <= 1e-9);
        (void)magnitude;

        if (withinNoise) {
            m.verdict = ComparisonVerdict::Unchanged;
            if (m.note.empty()) {
                std::ostringstream ss;
                ss.precision(2);
                ss << std::fixed;
                ss << "within the +/-" << result.noiseThresholdPct << "% noise threshold";
                m.note = ss.str();
            }
        } else {
            const bool wentUp = m.absoluteDelta > 0.0;
            const bool better = (m.direction == MetricDirection::HigherIsBetter) ? wentUp : !wentUp;
            m.verdict = better ? ComparisonVerdict::Improved : ComparisonVerdict::Worsened;
        }
        result.metrics.push_back(std::move(m));
    }

    for (const auto& m : result.metrics) {
        if (m.direction == MetricDirection::Informational) continue;
        if (!m.available) { ++result.unavailableCount; continue; }
        switch (m.verdict) {
            case ComparisonVerdict::Improved: ++result.improvedCount; break;
            case ComparisonVerdict::Worsened: ++result.worsenedCount; break;
            case ComparisonVerdict::Unchanged: ++result.unchangedCount; break;
            default: ++result.unavailableCount; break;
        }
    }

    // --- headline & summary -------------------------------------------------
    std::ostringstream head;
    head.precision(2);
    head << std::fixed;
    if (!result.comparisonMeaningful) {
        std::string firstBlocking = "the two sessions are not comparable";
        for (const auto& issue : issues) {
            if (issue.blocking) { firstBlocking = issue.message; break; }
        }
        head << "NOT COMPARABLE - " << firstBlocking;
    } else {
        const MetricComparison* fps = result.find(WindowMetricId::AvgFps);
        const MetricComparison* mean = result.find(WindowMetricId::MeanFrameTimeMs);
        const MetricComparison* p99 = result.find(WindowMetricId::P99FrameTimeMs);
        head << result.improvedCount << " of "
             << (result.improvedCount + result.worsenedCount + result.unchangedCount)
             << " directional metrics improved, " << result.worsenedCount << " worsened, "
             << result.unchangedCount << " effectively unchanged.";
        if (fps && fps->available && fps->relativeAvailable) {
            head << " Avg FPS " << fps->baselineValue << " -> " << fps->optimizedValue
                 << " (" << (fps->relativePct >= 0 ? "+" : "") << fps->relativePct << "%).";
        }
        if (mean && mean->available && mean->relativeAvailable) {
            head << " Mean frame time " << mean->baselineValue << " -> " << mean->optimizedValue
                 << " ms (" << (mean->relativePct >= 0 ? "+" : "") << mean->relativePct << "%).";
        }
        if (p99 && p99->available && p99->relativeAvailable) {
            head << " P99 " << p99->baselineValue << " -> " << p99->optimizedValue << " ms.";
        }
    }
    result.headline = head.str();

    std::ostringstream sum;
    sum << "Baseline session : " << baseline.id << " (" << modeId(baseline.mode) << ", "
        << stateId(baseline.state) << ", "
        << static_cast<int>(baseline.capturedDurationMs / 1000.0) << "s, "
        << baseline.stats.samples << " frames)\n";
    sum << "Optimized session: " << optimized.id << " (" << modeId(optimized.mode) << ", "
        << stateId(optimized.state) << ", "
        << static_cast<int>(optimized.capturedDurationMs / 1000.0) << "s, "
        << optimized.stats.samples << " frames)\n";
    if (issues.empty()) {
        sum << "Compatibility    : no material differences detected.\n";
    } else {
        for (const auto& issue : issues) {
            sum << (issue.blocking ? "[BLOCKING] " : "[warning]  ") << issue.code << ": "
                << issue.message << "\n";
        }
    }
    sum << result.significanceNote;
    result.summary = sum.str();

    return result;
}

std::string formatComparisonText(const ComparisonResult& result) {
    std::ostringstream ss;
    ss << "== A/B comparison ==\n";
    ss << result.headline << "\n\n";
    ss << "baseline : " << result.baselineId << " [" << result.baselineMode << "]\n";
    ss << "optimized: " << result.optimizedId << " [" << result.optimizedMode << "]\n";
    ss << "noise threshold: +/-" << result.noiseThresholdPct << "%\n\n";

    for (const auto& m : result.metrics) {
        ss << "- " << m.name;
        if (!m.available) {
            ss << ": unavailable (" << m.note << ")\n";
            continue;
        }
        ss << ": " << formatValue(m.baselineValue, m.unit.c_str()) << " -> "
           << formatValue(m.optimizedValue, m.unit.c_str()) << "  " << formatDelta(m)
           << "  [" << verdictName(m.verdict) << "]";
        if (!m.note.empty()) ss << "  " << m.note;
        ss << "\n";
    }

    if (!result.issues.empty()) {
        ss << "\nCompatibility:\n";
        for (const auto& issue : result.issues) {
            ss << (issue.blocking ? "  [BLOCKING] " : "  [warning]  ") << issue.code << ": "
               << issue.message << "\n";
        }
    }
    ss << "\n" << result.significanceNote << "\n";
    return ss.str();
}

} // namespace optigd::benchmark

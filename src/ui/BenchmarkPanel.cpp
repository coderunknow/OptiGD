#include "BenchmarkPanel.hpp"

#include "../benchmark/BenchmarkCompare.hpp"
#include "../core/Types.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace optigd::ui {

namespace {

std::string fixed(double value, int precision) {
    if (!std::isfinite(value)) return "n/a";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    return ss.str();
}

std::string signedFixed(double value, int precision) {
    if (!std::isfinite(value)) return "n/a";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision);
    if (value > 0.0) ss << '+';
    ss << value;
    return ss.str();
}

std::string countString(uint64_t value) {
    std::ostringstream ss;
    ss << value;
    return ss.str();
}

// A percentile printed with a leading '~' when it did not come from every
// retained sample. The marker is the whole point: an approximate value must not
// be readable as an exact one.
std::string percentile(double value, diagnostics::PercentileMethod method) {
    std::string prefix = (method == diagnostics::PercentileMethod::Exact) ? "" : "~";
    return prefix + fixed(value, 2);
}

std::string onOff(bool value) { return value ? "ON" : "OFF"; }

} // namespace

std::string BenchmarkPanel::formatStateLine(const benchmark::BenchmarkSnapshot& snapshot) {
    std::ostringstream ss;
    ss << "State: " << benchmark::stateDisplayName(snapshot.state);
    if (snapshot.isRunning) {
        ss << "  [" << benchmark::modeId(snapshot.mode) << "]  "
           << fixed(snapshot.remainingDurationMs / 1000.0, 1) << "s left of "
           << fixed(snapshot.targetDurationMs / 1000.0, 0) << "s";
    } else if (snapshot.hasTerminalSession && !snapshot.sessionId.empty()) {
        ss << "  [" << benchmark::modeId(snapshot.mode) << "]  session " << snapshot.sessionId;
    }
    return ss.str();
}

std::string BenchmarkPanel::formatHud(const benchmark::BenchmarkSnapshot& snapshot,
                                      const diagnostics::WindowStatistics& live) {
    std::ostringstream ss;
    if (!snapshot.isRunning) {
        if (!snapshot.hasTerminalSession) return std::string();
        ss << "[OptiGD BENCH] " << benchmark::modeId(snapshot.mode) << " "
           << benchmark::stateDisplayName(snapshot.state) << "\n";
        ss << "captured " << fixed(snapshot.capturedDurationMs / 1000.0, 1) << "s / "
           << fixed(snapshot.targetDurationMs / 1000.0, 0) << "s   frames "
           << countString(snapshot.framesCounted);
        if (!snapshot.persisted && !snapshot.persistPath.empty()) {
            ss << "   NOT SAVED";
        } else if (snapshot.persisted) {
            ss << "   saved";
        }
        return ss.str();
    }

    const double secondsLeft = std::max(0.0, snapshot.remainingDurationMs / 1000.0);
    ss << "[OptiGD BENCH] " << benchmark::modeId(snapshot.mode) << "  RUNNING  "
       << fixed(secondsLeft, 1) << "s left\n";
    ss << "FPS " << fixed(live.fpsFromMeanFrameTime, 1)
       << "  mean " << fixed(live.meanFrameTimeMs, 2) << "ms"
       << "  P50 " << percentile(live.p50FrameTimeMs, live.percentileMethod)
       << "  P99 " << percentile(live.p99FrameTimeMs, live.percentileMethod) << "ms\n";
    ss << "frames " << countString(snapshot.framesCounted)
       << "  window " << fixed(snapshot.capturedDurationMs / 1000.0, 1) << "s"
       << "  excl " << countString(snapshot.framesExcluded)
       << "  overhead " << fixed(live.metrics[static_cast<size_t>(
              diagnostics::WindowMetric::OptigdOverheadUs)].mean(), 1) << "us\n";
    ss << "cfg cull " << onOff(snapshot.spatialCullingEnabled)
       << " part " << onOff(snapshot.particleThrottlingEnabled)
       << " matrix " << onOff(snapshot.matrixCachingEnabled)
       << " gov " << onOff(snapshot.adaptiveGovernorEnabled);
    if (!snapshot.warnings.empty()) {
        ss << "  !" << snapshot.warnings.size() << " warn";
    }
    return ss.str();
}

std::string BenchmarkPanel::formatMain(const benchmark::BenchmarkSnapshot& snapshot,
                                       const diagnostics::WindowStatistics& live,
                                       const std::string& statusMessage) {
    std::ostringstream ss;
    ss << "OptiGD Benchmark Mode  v" << core::kOptiGDVersion << "\n";
    ss << std::string(52, '-') << "\n";
    ss << formatStateLine(snapshot) << "\n\n";

    if (snapshot.isRunning) {
        ss << "Captured : " << fixed(snapshot.capturedDurationMs / 1000.0, 2) << " s of "
           << fixed(snapshot.targetDurationMs / 1000.0, 0) << " s active gameplay\n";
        ss << "Wall     : " << fixed(snapshot.wallDurationMs / 1000.0, 2) << " s\n";
        ss << "Frames   : " << countString(snapshot.framesCounted) << " counted, "
           << countString(snapshot.framesExcluded) << " excluded (stalls/pauses), "
           << countString(snapshot.framesObserved) << " observed\n";
        ss << "Progress : " << fixed(snapshot.progress01 * 100.0, 1) << " %\n";
        ss << "Live FPS : " << fixed(live.fpsFromMeanFrameTime, 2)
           << " (internal inter-frame rate, not a GPU present rate)\n";
        ss << "Mean     : " << fixed(live.meanFrameTimeMs, 3) << " ms\n";
        ss << "P50/P95/P99: " << percentile(live.p50FrameTimeMs, live.percentileMethod) << " / "
           << percentile(live.p95FrameTimeMs, live.percentileMethod) << " / "
           << percentile(live.p99FrameTimeMs, live.percentileMethod) << " ms"
           << (live.percentileMethod == diagnostics::PercentileMethod::Exact ? "" : "  (~ = live estimate; the saved report is exact)")
           << "\n";
        ss << "Update CPU (wall): " << fixed(live.metrics[static_cast<size_t>(
               diagnostics::WindowMetric::UpdateCpuUs)].mean() / 1000.0, 3) << " ms\n";
        ss << "OptiGD overhead  : " << fixed(live.metrics[static_cast<size_t>(
               diagnostics::WindowMetric::OptigdOverheadUs)].mean(), 2) << " us/frame\n";
        ss << "Config in force  : cull " << onOff(snapshot.spatialCullingEnabled)
           << ", particles " << onOff(snapshot.particleThrottlingEnabled)
           << ", matrix " << onOff(snapshot.matrixCachingEnabled)
           << ", governor " << onOff(snapshot.adaptiveGovernorEnabled)
           << ", trigger index " << onOff(snapshot.triggerIndexingEnabled) << "\n";
        ss << "Tier             : " << snapshot.tierName << "\n";
        ss << "Window memory    : " << countString(snapshot.windowBytes / 1024) << " KB (bounded)\n";
        ss << "Transient config override active: " << onOff(snapshot.configOverrideActive)
           << " (your saved settings are untouched)\n";
    } else {
        ss << "Nothing is being measured right now.\n\n";
        ss << "Start Baseline   - 60 s with OptiGD's optimization features in\n"
              "                   passthrough. OptiGD stays installed.\n";
        ss << "Start Optimized  - 60 s with your own OptiGD configuration.\n";
        ss << "Results          - browse and reopen every saved session.\n";
        ss << "Compare          - A/B the selected baseline against the selected\n"
              "                   optimized session.\n";
        ss << "Protocol         - how to run a pair that can actually be compared.\n";
        ss << "External (Win)   - how to cross-check with CapFrameX.\n\n";
        if (snapshot.hasTerminalSession) {
            ss << "Last session : " << snapshot.sessionId << "\n";
            ss << "  mode " << benchmark::modeId(snapshot.mode)
               << ", status " << benchmark::stateDisplayName(snapshot.state)
               << ", captured " << fixed(snapshot.capturedDurationMs / 1000.0, 2) << " s, "
               << countString(snapshot.framesCounted) << " frames\n";
            if (snapshot.persisted) {
                ss << "  saved to " << snapshot.persistPath << "\n";
            } else {
                ss << "  NOT SAVED - the result exists only in memory\n";
            }
        }
    }

    if (!snapshot.warnings.empty()) {
        ss << "\nIntegrity warnings:\n";
        for (const auto& code : snapshot.warnings) {
            ss << "  - " << code << ": " << benchmark::warningDescription(code) << "\n";
        }
    }

    if (!statusMessage.empty()) {
        ss << "\n" << statusMessage << "\n";
    }

    ss << "\n" << std::string(52, '-') << "\n";
    ss << "A BASELINE session is an INTERNAL CONTROL, not vanilla Geometry Dash:\n";
    ss << "OptiGD remains installed and its hooks and instrumentation still run.\n";
    ss << "All values are OptiGD's own inter-frame telemetry measured at\n";
    ss << "GJBaseGameLayer::update(); they say nothing about GPU execution or\n";
    ss << "presentation timing. Benchmark Mode never alters player physics,\n";
    ss << "collision, or native trigger execution.\n";
    return ss.str();
}

std::string BenchmarkPanel::formatResults(const std::vector<benchmark::SessionSummary>& sessions,
                                          size_t cursor,
                                          const std::string& selectedBaselineId,
                                          const std::string& selectedOptimizedId,
                                          const std::string& storeRoot) {
    std::ostringstream ss;
    ss << "Saved benchmark sessions (" << sessions.size() << ")\n";
    ss << "Directory: " << (storeRoot.empty() ? std::string("<not configured>") : storeRoot) << "\n";
    ss << std::string(52, '-') << "\n";
    if (sessions.empty()) {
        ss << "No sessions saved yet. Start a baseline capture from the main view.\n";
        return ss.str();
    }

    const size_t shown = sessions.size();
    for (size_t i = 0; i < shown; ++i) {
        const auto& s = sessions[i];
        ss << (i == cursor ? "> " : "  ") << (i + 1) << ". " << s.id << "\n";
        if (!s.readable) {
            ss << "       UNREADABLE: " << s.readError << "\n";
            continue;
        }
        ss << "       " << benchmark::modeId(s.mode) << " / "
           << benchmark::stateDisplayName(s.state) << " / "
           << fixed(s.capturedDurationMs / 1000.0, 1) << "s / "
           << countString(s.samples) << " frames / "
           << fixed(s.avgFps, 2) << " fps\n";
        ss << "       mean " << fixed(s.meanFrameTimeMs, 2) << " ms, P99 "
           << fixed(s.p99FrameTimeMs, 2) << " ms";
        if (!s.levelName.empty()) ss << ", level '" << s.levelName << "'";
        ss << "\n";
        std::vector<std::string> tags;
        if (s.id == selectedBaselineId) tags.emplace_back("BASELINE for comparison");
        if (s.id == selectedOptimizedId) tags.emplace_back("OPTIMIZED for comparison");
        if (!s.comparable) tags.emplace_back("not comparable");
        if (!tags.empty()) {
            ss << "       ";
            for (size_t t = 0; t < tags.size(); ++t) {
                if (t) ss << ", ";
                ss << tags[t];
            }
            ss << "\n";
        }
    }
    ss << std::string(52, '-') << "\n";
    ss << "Use Prev/Next to move the cursor, then assign it as the\nbaseline or optimized side of a comparison.\n";
    return ss.str();
}

std::string BenchmarkPanel::formatSessionDetail(const benchmark::BenchmarkSession& s) {
    std::ostringstream ss;
    ss << "Session " << s.id << "\n";
    ss << std::string(52, '-') << "\n";
    ss << "Mode       : " << benchmark::modeDisplayName(s.mode) << "\n";
    ss << "Status     : " << benchmark::stateDisplayName(s.state)
       << (s.isComparable() ? " (usable in a comparison)" : " (NOT usable in a comparison)") << "\n";
    ss << "Started    : " << (s.startedUtc.empty() ? std::string("unknown") : s.startedUtc) << "\n";
    ss << "Ended      : " << (s.endedUtc.empty() ? std::string("unknown") : s.endedUtc) << "\n";
    ss << "Captured   : " << fixed(s.capturedDurationMs / 1000.0, 3) << " s of "
       << fixed(s.targetDurationMs / 1000.0, 0) << " s target (active gameplay)\n";
    ss << "Wall clock : " << fixed(s.wallDurationMs / 1000.0, 3) << " s\n";
    ss << "Frames     : " << countString(s.framesCounted) << " counted, "
       << countString(s.framesExcluded) << " excluded, "
       << countString(s.framesRejected) << " rejected, "
       << countString(s.framesObserved) << " observed\n";
    ss << "Hitches    : " << countString(s.stats.hitchFrames) << " frames > "
       << fixed(s.stats.hitchThresholdMs, 1) << " ms ("
       << fixed(s.stats.hitchTotalMs, 1) << " ms total)\n\n";

    ss << "Frame time : mean " << fixed(s.meanFrameTimeMs(), 3)
       << " ms, min " << fixed(s.minFrameTimeMs(), 3)
       << ", max " << fixed(s.maxFrameTimeMs(), 3)
       << ", stddev " << fixed(s.stdDevFrameTimeMs(), 3) << "\n";
    ss << "Percentiles: P50 " << fixed(s.p50FrameTimeMs(), 3)
       << " ms, P95 " << fixed(s.p95FrameTimeMs(), 3)
       << " ms, P99 " << fixed(s.p99FrameTimeMs(), 3) << " ms\n";
    ss << "Method     : " << diagnostics::percentileMethodName(s.stats.percentileMethod)
       << " over " << countString(s.stats.samples) << " samples";
    if (s.stats.ringOverflow) {
        ss << " (ring capacity " << countString(s.stats.ringCapacity) << " exceeded)";
    }
    ss << "\n";
    ss << "FPS        : " << fixed(s.avgFps(), 2)
       << " (1000 / mean inter-frame interval; internal, not a present rate)\n\n";

    using WM = diagnostics::WindowMetric;
    ss << "CPU        : update wall " << fixed(s.avgUpdateCpuMs(), 3)
       << " ms, update thread " << fixed(s.avgUpdateThreadCpuMs(), 3) << " ms\n";
    ss << "OptiGD cost: " << fixed(s.avgOverheadUs(), 2) << " us/frame"
       << " (cull " << fixed(s.stats.metrics[static_cast<size_t>(WM::CullingUs)].mean(), 2)
       << ", particle " << fixed(s.stats.metrics[static_cast<size_t>(WM::ParticleUs)].mean(), 2)
       << ", matrix " << fixed(s.stats.metrics[static_cast<size_t>(WM::MatrixUs)].mean(), 2)
       << ", trigger " << fixed(s.stats.metrics[static_cast<size_t>(WM::TriggerUs)].mean(), 2)
       << ", governor " << fixed(s.stats.metrics[static_cast<size_t>(WM::GovernorUs)].mean(), 2)
       << " us)\n\n";

    ss << "Counters   : objects culled " << countString(static_cast<uint64_t>(
           s.metricValue(benchmark::WindowMetricId::ObjectsCulled)))
       << ", particles avoided " << countString(static_cast<uint64_t>(
           s.metricValue(benchmark::WindowMetricId::ParticlesAvoided))) << "\n";
    ss << "             matrix hits " << countString(static_cast<uint64_t>(
           s.metricValue(benchmark::WindowMetricId::MatrixCacheHits)))
       << ", heap allocations avoided " << countString(static_cast<uint64_t>(
           s.metricValue(benchmark::WindowMetricId::HeapAllocationsAvoided))) << "\n";
    ss << "             peak arena " << countString(static_cast<uint64_t>(
           s.stats.metrics[static_cast<size_t>(WM::ArenaPeakBytes)].aggregate(WM::ArenaPeakBytes)))
       << " bytes\n\n";

    ss << "Config     : tier " << core::tierToString(s.config.tier)
       << ", cull " << onOff(s.config.flags.enableSpatialCulling)
       << ", particles " << onOff(s.config.flags.enableParticleThrottling)
       << ", matrix " << onOff(s.config.flags.enableMatrixCaching) << "\n";
    ss << "             governor " << onOff(s.config.flags.enableAdaptiveGovernor)
       << ", trigger index " << onOff(s.config.flags.enableTriggerIndexing)
       << ", density " << fixed(s.config.particleDensityScale, 2)
       << ", cap " << s.config.maxActiveParticles << "\n";
    ss << "             transient override applied: " << onOff(s.configOverrideApplied) << "\n";
    if (!s.featuresDisabledForBaseline.empty()) {
        ss << "             forced to passthrough:";
        for (const auto& f : s.featuresDisabledForBaseline) ss << " " << f;
        ss << "\n";
    }
    ss << "\n";

    ss << "Level      : " << (s.level.name.empty() ? std::string("<unknown>") : s.level.name);
    if (!s.level.creator.empty()) ss << " by " << s.level.creator;
    ss << "\n";
    ss << "             id " << (s.level.levelIdKnown ? std::to_string(s.level.levelId)
                                                     : std::string("<unknown>"))
       << ", attempt " << (s.level.attemptKnown ? std::to_string(s.level.attempt)
                                                : std::string("<unknown>"))
       << ", practice " << (s.level.practiceModeKnown ? onOff(s.level.practiceMode)
                                                      : std::string("<unknown>"))
       << ", source " << s.level.source << "\n\n";

    ss << "Runtime    : " << s.environment.describe() << "\n\n";

    ss << "Integrity  : pauses " << s.pauseEvents << " (" << fixed(s.pausedMs, 1)
       << " ms excluded), resets " << s.resetEvents
       << ", gaps " << s.gapEvents << ", tier changes " << s.tierTransitions
       << ", fallbacks " << s.fallbackEvents << "\n";
    ss << "Excluded   : " << fixed(s.excludedGapMs / 1000.0, 2) << " s of non-gameplay time ("
       << countString(s.framesExcluded) << " frames)\n";
    if (s.warnings.empty()) {
        ss << "             no integrity warnings\n";
    } else {
        for (const auto& code : s.warnings) {
            ss << "             - " << code << ": " << benchmark::warningDescription(code) << "\n";
        }
    }
    if (!s.sourcePath.empty()) {
        ss << "\nFile       : " << s.sourcePath << "\n";
    }
    ss << "\nThis record is OptiGD's INTERNAL telemetry. It is not an external\n";
    ss << "presented-frame measurement and does not by itself prove a gain.\n";
    return ss.str();
}

std::string BenchmarkPanel::formatComparison(const benchmark::ComparisonResult& c) {
    std::ostringstream ss;
    ss << "A/B comparison\n";
    ss << std::string(52, '-') << "\n";
    ss << "baseline : " << c.baselineId << " [" << c.baselineMode << "]\n";
    ss << "optimized: " << c.optimizedId << " [" << c.optimizedMode << "]\n";
    ss << "noise threshold: +/-" << fixed(c.noiseThresholdPct, 1) << " %\n\n";
    ss << c.headline << "\n\n";

    ss << std::left << std::setw(26) << "metric"
       << std::right << std::setw(11) << "baseline"
       << std::setw(11) << "optimized"
       << std::setw(16) << "delta"
       << "  verdict\n";
    for (const auto& m : c.metrics) {
        if (!m.available) {
            ss << std::left << std::setw(26) << m.name << "unavailable (" << m.note << ")\n";
            continue;
        }
        std::ostringstream delta;
        delta << signedFixed(m.absoluteDelta, 2);
        if (m.relativeAvailable) delta << " (" << signedFixed(m.relativePct, 1) << "%)";
        else delta << " (n/a)";

        ss << std::left << std::setw(26) << m.name
           << std::right << std::setw(11) << fixed(m.baselineValue, 2)
           << std::setw(11) << fixed(m.optimizedValue, 2)
           << std::setw(16) << delta.str()
           << "  " << benchmark::verdictName(m.verdict);
        if (m.direction == benchmark::MetricDirection::Informational) ss << " (info)";
        if (!m.note.empty()) ss << "  " << m.note;
        ss << "\n";
    }

    if (!c.issues.empty()) {
        ss << "\nCompatibility:\n";
        for (const auto& issue : c.issues) {
            ss << (issue.blocking ? "  [BLOCKING] " : "  [warning]  ") << issue.code << ": "
               << issue.message << "\n";
        }
    } else {
        ss << "\nCompatibility: no material differences detected between the two captures.\n";
    }

    ss << "\n" << c.significanceNote << "\n";
    ss << "\nLower frame time and CPU cost are better; higher FPS is better.\n";
    ss << "Both values come from OptiGD's internal inter-frame telemetry.\n";
    return ss.str();
}

std::string BenchmarkPanel::formatProtocol() {
    std::ostringstream ss;
    ss << "Repeated-run protocol (do this, or the numbers mean nothing)\n";
    ss << std::string(52, '-') << "\n";
    ss << "1. Pick ONE level and ONE segment. A level you can survive for the\n"
          "   whole 60 s without dying, or practice mode from a fixed\n"
          "   checkpoint. Record which one you used.\n";
    ss << "2. Hold the environment constant for every capture:\n"
          "     - same resolution, window mode and display\n"
          "     - same GD graphics/quality settings\n"
          "     - same FPS limiter / VSync state (and note it)\n"
          "     - no other mods enabled except OptiGD\n"
          "     - no background workload, no recording software unless it is\n"
          "       the external tool you are deliberately measuring with\n"
          "     - same thermal state: let the machine idle a minute between\n"
          "       captures so a hot GPU/CPU does not become the variable\n";
    ss << "3. Start each capture at the SAME point in the level (0% after a\n"
          "   full restart, or the same practice checkpoint). Restart the\n"
          "   attempt before every capture.\n";
    ss << "4. Play normally. Do not chase a good number and do not idle:\n"
          "   Benchmark Mode measures what the game does while you play it.\n";
    ss << "5. Run BASELINE, restart the attempt, run OPTIMIZED. That is one\n"
          "   pair. A single pair is an observation, not a result.\n";
    ss << "6. Repeat for at least 3-5 pairs, alternating the order if you can\n"
          "   (baseline-first vs optimized-first), so drift over time cannot\n"
          "   masquerade as an optimization effect.\n";
    ss << "7. Compare pairs with the Compare view. Look at the SPREAD across\n"
          "   pairs, not at the best one. If the spread is larger than the\n"
          "   difference, you have not measured anything.\n";
    ss << "8. Sessions whose status is not COMPLETED, or that carry integrity\n"
          "   warnings such as level_reset, tier_changed_during_capture or\n"
          "   safety_fallback_triggered, must not be used as evidence.\n\n";
    ss << "Confounders that invalidate a pair:\n";
    ss << "  - different level sections (a death, a different start point)\n";
    ss << "  - a resolution / graphics / FPS-limit change between captures\n";
    ss << "  - an adaptive tier change or safety fallback during a capture\n";
    ss << "  - background load, streaming, antivirus or driver updates\n";
    ss << "  - thermal throttling on one side only\n";
    ss << "  - comparing sessions captured on different platforms or builds\n\n";
    ss << "The BASELINE side is an internal control: OptiGD is still installed\n";
    ss << "and still instrumenting the game. It is not vanilla Geometry Dash,\n";
    ss << "so this A/B measures OptiGD's optimization features, not the cost of\n";
    ss << "installing OptiGD. For that, use the external procedure.\n";
    return ss.str();
}

std::string BenchmarkPanel::formatExternalProcedure() {
    std::ostringstream ss;
    ss << "External check on Windows x64 (CapFrameX)\n";
    ss << std::string(52, '-') << "\n";
    ss << "Why: OptiGD measures inter-frame intervals inside the game thread.\n";
    ss << "CapFrameX measures the frames the GPU actually presented. Only the\n";
    ss << "second one can say whether installing OptiGD helps your machine.\n\n";
    ss << "A. Capture WITHOUT OptiGD (external vanilla baseline)\n";
    ss << "   1. Move coderunknow.optigd.geode out of\n"
          "      <GD folder>\\geode\\mods (or disable it in the Geode menu)\n"
          "      and restart Geometry Dash.\n";
    ss << "   2. Start CapFrameX, choose the Geometry Dash process, and use a\n"
          "      fixed hotkey for capture start/stop.\n";
    ss << "   3. Enter the level, start the capture at 0%, play exactly 60 s,\n"
          "      stop the capture, exit the level.\n";
    ss << "   4. Save the run as vanilla_run1.csv (CapFrameX: Analysis ->\n"
          "      Export -> CSV).\n\n";
    ss << "B. Capture WITH OptiGD\n";
    ss << "   5. Put the .geode back, restart GD, enable OptiGD, set the\n"
          "      profile you intend to ship.\n";
    ss << "   6. Repeat step 3 on the SAME level and the SAME segment.\n";
    ss << "   7. Save as optigd_run1.csv.\n\n";
    ss << "C. Keep conditions identical\n";
    ss << "   - same resolution, window mode, display and refresh rate\n";
    ss << "   - same GD FPS limiter / VSync setting (record what it was)\n";
    ss << "   - same other mods (ideally none), same background load\n";
    ss << "   - same capture length and the same start point\n\n";
    ss << "D. Repeat and compare\n";
    ss << "   - at least 3 captures per side, alternating sides\n";
    ss << "   - compare mean/median/P95/P99 frametime and the 1% low FPS\n";
    ss << "   - compare the frame-time DISTRIBUTIONS, not just the averages:\n";
    ss << "     two runs with the same mean can feel completely different\n";
    ss << "   - tools/compare_capframex.py renders both CSV sets side by side\n\n";
    ss << "E. Keep the evidence together\n";
    ss << "   - store the CSVs next to the exported OptiGD session JSON\n";
    ss << "   - label which is which; never merge them into one table without\n";
    ss << "     saying where each column came from\n\n";
    ss << "What CapFrameX CANNOT see: OptiGD's per-subsystem CPU cost, its\n";
    ss << "culling/particle counters, or its internal baseline. Those only\n";
    ss << "exist in OptiGD's own telemetry. What OptiGD CANNOT see: presented\n";
    ss << "frames, GPU queue time, or driver-level pacing. Report both, labelled.\n\n";
    ss << "Android: no equivalent external frame-time capture tool is available\n";
    ss << "on Android 32/64 in practice, so Android results are internal\n";
    ss << "telemetry only and must be reported as such.\n";
    return ss.str();
}

namespace {

// Hard-wraps one logical line to `maxColumns`, preferring to break at a space.
// A word longer than the column limit is broken mid-word rather than dropped, so
// a long path or session id still appears in full.
void wrapLine(const std::string& line, size_t maxColumns, std::vector<std::string>& out) {
    if (maxColumns == 0) {
        out.push_back(line);
        return;
    }
    if (line.size() <= maxColumns) {
        out.push_back(line);
        return;
    }
    size_t start = 0;
    while (start < line.size()) {
        size_t width = std::min(maxColumns, line.size() - start);
        size_t breakAt = start + width;
        if (start + width < line.size()) {
            // Prefer the last space inside the window; fall back to a hard cut.
            size_t lastSpace = line.rfind(' ', start + width);
            if (lastSpace != std::string::npos && lastSpace > start) {
                breakAt = lastSpace;
            }
        }
        out.push_back(line.substr(start, breakAt - start));
        start = breakAt;
        while (start < line.size() && line[start] == ' ' && breakAt != start + width) {
            // Only skip the single space we broke on.
            ++start;
            break;
        }
    }
}

} // namespace

std::vector<std::string> BenchmarkPanel::paginate(const std::string& text,
                                                  size_t maxLines,
                                                  size_t maxColumns) {
    if (maxLines == 0) maxLines = 1;

    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        const std::string logical = (nl == std::string::npos) ? text.substr(start)
                                                              : text.substr(start, nl - start);
        // A trailing carriage return would show up as a stray glyph on Windows.
        std::string trimmed = logical;
        while (!trimmed.empty() && (trimmed.back() == '\r')) trimmed.pop_back();
        wrapLine(trimmed, maxColumns, lines);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (lines.empty()) lines.emplace_back();

    std::vector<std::string> pages;
    for (size_t i = 0; i < lines.size(); i += maxLines) {
        std::string page;
        for (size_t j = i; j < std::min(lines.size(), i + maxLines); ++j) {
            if (j > i) page += '\n';
            page += lines[j];
        }
        pages.push_back(std::move(page));
    }
    if (pages.empty()) pages.emplace_back();
    return pages;
}

std::string BenchmarkPanel::formatPageIndicator(size_t pageIndex, size_t pageCount) {
    if (pageCount == 0) pageCount = 1;
    std::ostringstream ss;
    ss << "page " << (pageIndex + 1) << "/" << pageCount;
    return ss.str();
}

} // namespace optigd::ui

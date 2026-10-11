#include "BenchmarkSession.hpp"
#include "../core/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace optigd::benchmark {

// ---------------------------------------------------------------------------
// Time formatting
// ---------------------------------------------------------------------------

std::string formatUtcIso8601(uint64_t epochMs) {
    // Reject values that cannot be represented: a wrong timestamp is worse than
    // an obviously missing one.
    if (epochMs == 0) return std::string();
    constexpr uint64_t kMaxReasonableMs = 4102444800000ULL; // 2100-01-01
    if (epochMs > kMaxReasonableMs) return std::string();

    std::time_t seconds = static_cast<std::time_t>(epochMs / 1000ULL);
    unsigned int millis = static_cast<unsigned int>(epochMs % 1000ULL);

    std::tm tmUtc{};
#if defined(_WIN32)
    if (gmtime_s(&tmUtc, &seconds) != 0) return std::string();
#else
    if (gmtime_r(&seconds, &tmUtc) == nullptr) return std::string();
#endif

    char buf[80];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02u.%03uZ",
                  tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                  tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec, millis);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// OptiConfig <-> JSON
// ---------------------------------------------------------------------------

core::json::Value configToJson(const core::OptiConfig& config) {
    core::json::Value root = core::json::Value::object();
    root.set("tier", core::json::Value::integer(static_cast<long long>(config.tier)));
    root.set("active_tier", core::json::Value::string(core::tierToString(config.tier)));

    root.set("spatial_culling_enabled", core::json::Value::boolean(config.flags.enableSpatialCulling));
    root.set("particle_throttling_enabled", core::json::Value::boolean(config.flags.enableParticleThrottling));
    root.set("trigger_indexing_enabled", core::json::Value::boolean(config.flags.enableTriggerIndexing));
    root.set("matrix_caching_enabled", core::json::Value::boolean(config.flags.enableMatrixCaching));
    root.set("adaptive_governor_enabled", core::json::Value::boolean(config.flags.enableAdaptiveGovernor));
    root.set("memory_arena_enabled", core::json::Value::boolean(config.flags.enableMemoryArena));
    root.set("diagnostics_overlay_enabled", core::json::Value::boolean(config.flags.enableDiagnosticsOverlay));
    root.set("export_diagnostics_on_exit", core::json::Value::boolean(config.flags.exportDiagnosticsOnExit));

    root.set("target_fps", core::json::Value::number(static_cast<double>(config.targetFrameRate)));
    root.set("target_frame_time_ms", core::json::Value::number(static_cast<double>(config.targetFrameTimeMs)));
    root.set("particle_density_scale", core::json::Value::number(static_cast<double>(config.particleDensityScale)));
    root.set("max_active_particles", core::json::Value::integer(static_cast<long long>(config.maxActiveParticles)));
    root.set("cull_offscreen_emitters", core::json::Value::boolean(config.cullOffscreenEmitters));
    root.set("camera_padding_x", core::json::Value::number(static_cast<double>(config.cameraPaddingX)));
    root.set("camera_padding_y", core::json::Value::number(static_cast<double>(config.cameraPaddingY)));
    root.set("spatial_cell_size", core::json::Value::number(static_cast<double>(config.spatialCellSize)));
    root.set("sort_triggers_by_position", core::json::Value::boolean(config.sortTriggersByPosition));
    root.set("trigger_lookahead_distance", core::json::Value::number(static_cast<double>(config.triggerLookaheadDistance)));
    return root;
}

bool configFromJson(const core::json::Value& root, core::OptiConfig& out,
                    const core::OptiConfig& fallback) {
    if (!root.isObject()) return false;
    out = fallback;

    long long tier = root.getInt("tier", static_cast<long long>(fallback.tier));
    if (tier >= 0 && tier <= 3) out.tier = static_cast<core::PerformanceTier>(tier);

    auto readBool = [&](const char* key, bool& dst) {
        const core::json::Value* v = root.find(key);
        if (v && v->isBool()) dst = v->asBool();
    };
    auto readDouble = [&](const char* key, float& dst, float lo, float hi) {
        const core::json::Value* v = root.find(key);
        if (!v || !v->isNumber()) return;
        double d = v->asDouble();
        if (!std::isfinite(d)) return;
        dst = static_cast<float>(std::clamp(d, static_cast<double>(lo), static_cast<double>(hi)));
    };

    readBool("spatial_culling_enabled", out.flags.enableSpatialCulling);
    readBool("particle_throttling_enabled", out.flags.enableParticleThrottling);
    readBool("trigger_indexing_enabled", out.flags.enableTriggerIndexing);
    readBool("matrix_caching_enabled", out.flags.enableMatrixCaching);
    readBool("adaptive_governor_enabled", out.flags.enableAdaptiveGovernor);
    readBool("memory_arena_enabled", out.flags.enableMemoryArena);
    readBool("diagnostics_overlay_enabled", out.flags.enableDiagnosticsOverlay);
    readBool("export_diagnostics_on_exit", out.flags.exportDiagnosticsOnExit);
    readBool("cull_offscreen_emitters", out.cullOffscreenEmitters);
    readBool("sort_triggers_by_position", out.sortTriggersByPosition);

    readDouble("target_fps", out.targetFrameRate, 1.0f, 4000.0f);
    readDouble("target_frame_time_ms", out.targetFrameTimeMs, 0.05f, 10000.0f);
    readDouble("particle_density_scale", out.particleDensityScale, 0.0f, 4.0f);
    readDouble("camera_padding_x", out.cameraPaddingX, -100000.0f, 100000.0f);
    readDouble("camera_padding_y", out.cameraPaddingY, -100000.0f, 100000.0f);
    readDouble("spatial_cell_size", out.spatialCellSize, 1.0f, 100000.0f);
    readDouble("trigger_lookahead_distance", out.triggerLookaheadDistance, 0.0f, 1000000.0f);

    long long maxParticles = root.getInt("max_active_particles",
                                         static_cast<long long>(fallback.maxActiveParticles));
    if (maxParticles >= 0 && maxParticles <= 100000000LL) {
        out.maxActiveParticles = static_cast<uint32_t>(maxParticles);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Metric descriptors
// ---------------------------------------------------------------------------

namespace {

struct MetricDescriptor {
    const char* name;
    MetricDirection direction;
};

constexpr MetricDescriptor kMetricDescriptors[] = {
    {"avg_fps",                    MetricDirection::HigherIsBetter},
    {"mean_frame_time_ms",         MetricDirection::LowerIsBetter},
    {"p50_frame_time_ms",          MetricDirection::LowerIsBetter},
    {"p95_frame_time_ms",          MetricDirection::LowerIsBetter},
    {"p99_frame_time_ms",          MetricDirection::LowerIsBetter},
    {"max_frame_time_ms",          MetricDirection::LowerIsBetter},
    {"stddev_frame_time_ms",       MetricDirection::LowerIsBetter},
    {"avg_update_cpu_time_ms",     MetricDirection::LowerIsBetter},
    {"avg_update_thread_cpu_ms",   MetricDirection::LowerIsBetter},
    {"avg_optigd_overhead_us",     MetricDirection::LowerIsBetter},
    {"captured_duration_ms",       MetricDirection::Informational},
    {"frames_counted",             MetricDirection::Informational},
    {"objects_culled",             MetricDirection::Informational},
    {"particles_avoided",          MetricDirection::Informational},
    {"matrix_cache_hits",          MetricDirection::Informational},
    {"heap_allocations_avoided",   MetricDirection::Informational},
};

constexpr size_t kMetricDescriptorCount =
    sizeof(kMetricDescriptors) / sizeof(kMetricDescriptors[0]);

static_assert(kMetricDescriptorCount == static_cast<size_t>(WindowMetricId::Count),
              "kMetricDescriptors must have exactly one entry per WindowMetricId");

} // namespace

const char* metricIdName(WindowMetricId id) {
    size_t index = static_cast<size_t>(id);
    if (index >= kMetricDescriptorCount) return "unknown";
    return kMetricDescriptors[index].name;
}

MetricDirection metricDirection(WindowMetricId id) {
    size_t index = static_cast<size_t>(id);
    if (index >= kMetricDescriptorCount) return MetricDirection::Informational;
    return kMetricDescriptors[index].direction;
}

// ---------------------------------------------------------------------------
// BenchmarkSession derived accessors
// ---------------------------------------------------------------------------

double BenchmarkSession::avgUpdateCpuMs() const {
    return stats.metrics[static_cast<size_t>(diagnostics::WindowMetric::UpdateCpuUs)].mean() / 1000.0;
}

double BenchmarkSession::avgUpdateThreadCpuMs() const {
    return stats.metrics[static_cast<size_t>(diagnostics::WindowMetric::UpdateThreadCpuUs)].mean() / 1000.0;
}

double BenchmarkSession::avgOverheadUs() const {
    return stats.metrics[static_cast<size_t>(diagnostics::WindowMetric::OptigdOverheadUs)].mean();
}

double BenchmarkSession::metricValue(WindowMetricId id) const {
    using WM = diagnostics::WindowMetric;
    const auto& m = stats.metrics;
    switch (id) {
        case WindowMetricId::AvgFps: return stats.fpsFromMeanFrameTime;
        case WindowMetricId::MeanFrameTimeMs: return stats.meanFrameTimeMs;
        case WindowMetricId::P50FrameTimeMs: return stats.p50FrameTimeMs;
        case WindowMetricId::P95FrameTimeMs: return stats.p95FrameTimeMs;
        case WindowMetricId::P99FrameTimeMs: return stats.p99FrameTimeMs;
        case WindowMetricId::MaxFrameTimeMs: return stats.maxFrameTimeMs;
        case WindowMetricId::StdDevFrameTimeMs: return stats.stdDevFrameTimeMs;
        case WindowMetricId::AvgUpdateCpuMs: return avgUpdateCpuMs();
        case WindowMetricId::AvgUpdateThreadCpuMs: return avgUpdateThreadCpuMs();
        case WindowMetricId::AvgOverheadUs: return avgOverheadUs();
        case WindowMetricId::CapturedDurationMs: return capturedDurationMs;
        case WindowMetricId::FramesCounted: return static_cast<double>(framesCounted);
        case WindowMetricId::ObjectsCulled:
            return m[static_cast<size_t>(WM::CulledObjects)].aggregate(WM::CulledObjects);
        case WindowMetricId::ParticlesAvoided:
            return m[static_cast<size_t>(WM::ParticlesAvoided)].aggregate(WM::ParticlesAvoided);
        case WindowMetricId::MatrixCacheHits:
            return m[static_cast<size_t>(WM::MatrixCacheHits)].aggregate(WM::MatrixCacheHits);
        case WindowMetricId::HeapAllocationsAvoided:
            return m[static_cast<size_t>(WM::HeapAllocationsAvoided)].aggregate(WM::HeapAllocationsAvoided);
        default: return 0.0;
    }
}

std::string BenchmarkSession::statusId() const {
    return std::string(stateId(state));
}

bool BenchmarkSession::isComparable() const {
    return isComparableState(state) && stats.samples > 0 && !id.empty();
}

void BenchmarkSession::addWarning(std::string_view code) {
    if (code.empty()) return;
    if (hasWarning(code)) return;
    warnings.emplace_back(code);
}

bool BenchmarkSession::hasWarning(std::string_view code) const {
    for (const auto& w : warnings) {
        if (w == code) return true;
    }
    return false;
}

void BenchmarkSession::refreshDerivedWarnings() {
    if (stats.samples == 0) {
        addWarning(warning::kNoSamples);
    }
    if (stats.rejectedSamples > 0) {
        framesRejected = stats.rejectedSamples;
        addWarning(warning::kRejectedSamples);
    }
    framesCounted = stats.samples;
    framesExcluded = stats.excludedSamples;
    excludedGapMs = stats.excludedTimeMs;

    if (stats.ringOverflow) {
        addWarning(warning::kRingOverflow);
    }
    if (stats.histogramSaturated) {
        addWarning(warning::kHistogramSaturated);
    }
    if (stats.percentileMethod == diagnostics::PercentileMethod::HistogramApproximate) {
        addWarning(warning::kApproxPercentiles);
    }
    if (targetDurationMs > 0.0 && capturedDurationMs < targetDurationMs * 0.98) {
        addWarning(warning::kShortCapture);
    }
    if (wallDurationMs > capturedDurationMs * 1.25 + 1000.0) {
        addWarning(warning::kWallClockDrift);
    }
    if (pauseEvents > 0 || pausedMs > 0.0) {
        addWarning(warning::kPaused);
    }
    if (gapEvents > 0) {
        addWarning(warning::kLongGap);
    }
    if (tierTransitions > 0) {
        addWarning(warning::kTierChanged);
    }
    if (fallbackEvents > 0) {
        addWarning(warning::kFallbackTriggered);
    }
    if (!level.hasAnyIdentity()) {
        addWarning(warning::kLevelUnknown);
    }

    // A terminal state that claims success without data is a contradiction the
    // record must not be allowed to carry.
    if (state == BenchmarkState::Completed && stats.samples == 0) {
        state = BenchmarkState::Invalid;
    }
    if (state == BenchmarkState::Finalizing) {
        // Never persist a transient state.
        state = (stats.samples == 0) ? BenchmarkState::Invalid : BenchmarkState::Incomplete;
    }
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {

using core::json::Value;

Value warningsToJson(const std::vector<std::string>& warnings) {
    Value arr = Value::array();
    for (const auto& code : warnings) {
        Value item = Value::object();
        item.set("code", Value::string(code));
        item.set("description", Value::string(warningDescription(code)));
        arr.push(std::move(item));
    }
    return arr;
}

Value stringsToJson(const std::vector<std::string>& items) {
    Value arr = Value::array();
    for (const auto& s : items) arr.push(Value::string(s));
    return arr;
}

} // namespace

Value BenchmarkSession::toJson() const {
    Value root = Value::object();
    root.set("report_kind", Value::string(kReportKind));
    root.set("schema_version", Value::integer(kSchemaVersion));
    root.set("optigd_version", Value::string(environment.optigdVersion));
    root.set("gd_version", Value::string(environment.gdVersion));
    root.set("geode_version", Value::string(environment.geodeVersion));
    // Written from the record, not hardcoded: the same serializer is used by the
    // in-game capture and by the standalone schema tools, and a tool that stamps
    // "[MEASURED-IN-GAME]" onto a synthetic run would be inventing evidence.
    root.set("evidence_classification", Value::string(evidenceClassification));
    root.set("evidence_classification_note", Value::string(evidenceClassificationNote));

    // --- session -----------------------------------------------------------
    Value session = Value::object();
    session.set("id", Value::string(id));
    session.set("mode", Value::string(modeId(mode)));
    session.set("mode_display", Value::string(modeDisplayName(mode)));
    session.set("state", Value::string(stateId(state)));
    session.set("status", Value::string(statusId()));
    session.set("comparable", Value::boolean(isComparable()));
    session.set("started_utc", Value::string(startedUtc));
    session.set("ended_utc", Value::string(endedUtc));
    session.set("started_epoch_ms", Value::integer(static_cast<long long>(startedEpochMs)));
    session.set("ended_epoch_ms", Value::integer(static_cast<long long>(endedEpochMs)));
    session.set("target_duration_ms", Value::number(targetDurationMs));
    session.set("captured_duration_ms", Value::number(capturedDurationMs));
    session.set("wall_duration_ms", Value::number(wallDurationMs));
    session.set("frames_observed", Value::integer(static_cast<long long>(framesObserved)));
    session.set("frames_counted", Value::integer(static_cast<long long>(framesCounted)));
    session.set("frames_excluded", Value::integer(static_cast<long long>(framesExcluded)));
    session.set("frames_rejected", Value::integer(static_cast<long long>(framesRejected)));
    root.set("session", std::move(session));

    // --- platform ----------------------------------------------------------
    Value platform = Value::object();
    platform.set("os", Value::string(environment.osName));
    platform.set("architecture", Value::string(environment.architecture));
    platform.set("compiler", Value::string(environment.compiler));
    platform.set("cores", Value::integer(static_cast<long long>(environment.logicalCores)));
    platform.set("total_ram_mb", Value::integer(static_cast<long long>(environment.totalRamMb)));
    platform.set("refresh_hint", Value::string(environment.refreshHint));
    root.set("platform", std::move(platform));

    // --- level -------------------------------------------------------------
    Value levelJson = Value::object();
    levelJson.set("name", Value::string(level.name));
    levelJson.set("creator", Value::string(level.creator));
    levelJson.set("level_id", Value::integer(level.levelId));
    levelJson.set("level_id_known", Value::boolean(level.levelIdKnown));
    levelJson.set("attempt", Value::integer(level.attempt));
    levelJson.set("attempt_known", Value::boolean(level.attemptKnown));
    levelJson.set("practice_mode", Value::boolean(level.practiceMode));
    levelJson.set("practice_mode_known", Value::boolean(level.practiceModeKnown));
    levelJson.set("source", Value::string(level.source));
    levelJson.set("identity_available", Value::boolean(level.hasAnyIdentity()));
    root.set("level", std::move(levelJson));

    // --- configuration -----------------------------------------------------
    Value configJson = configToJson(config);
    configJson.set("override_applied", Value::boolean(configOverrideApplied));
    configJson.set("features_disabled_for_baseline", stringsToJson(featuresDisabledForBaseline));
    root.set("configuration", std::move(configJson));

    // --- telemetry ---------------------------------------------------------
    using WM = diagnostics::WindowMetric;
    const auto& m = stats.metrics;
    Value tel = Value::object();
    tel.set("samples", Value::integer(static_cast<long long>(stats.samples)));
    tel.set("total_window_ms", Value::number(stats.totalFrameTimeMs));
    tel.set("avg_fps", Value::number(stats.fpsFromMeanFrameTime));
    tel.set("fps_from_window", Value::number(stats.fpsFromWindow));
    tel.set("instant_max_fps_from_min_frame",
            Value::number(stats.minFrameTimeMs > 0.0 ? 1000.0 / stats.minFrameTimeMs : 0.0));
    tel.set("instant_min_fps_from_max_frame",
            Value::number(stats.maxFrameTimeMs > 0.0 ? 1000.0 / stats.maxFrameTimeMs : 0.0));
    tel.set("avg_frame_time_ms", Value::number(stats.meanFrameTimeMs));
    tel.set("median_frame_time_ms", Value::number(stats.p50FrameTimeMs));
    tel.set("p95_frame_time_ms", Value::number(stats.p95FrameTimeMs));
    tel.set("p99_frame_time_ms", Value::number(stats.p99FrameTimeMs));
    tel.set("min_frame_time_ms", Value::number(stats.minFrameTimeMs));
    tel.set("max_frame_time_ms", Value::number(stats.maxFrameTimeMs));
    tel.set("stddev_frame_time_ms", Value::number(stats.stdDevFrameTimeMs));
    tel.set("avg_update_cpu_time_ms", Value::number(avgUpdateCpuMs()));
    tel.set("avg_update_thread_cpu_time_ms", Value::number(avgUpdateThreadCpuMs()));
    tel.set("avg_optigd_overhead_us", Value::number(avgOverheadUs()));
    tel.set("avg_culling_us", Value::number(m[static_cast<size_t>(WM::CullingUs)].mean()));
    tel.set("avg_particle_us", Value::number(m[static_cast<size_t>(WM::ParticleUs)].mean()));
    tel.set("avg_trigger_us", Value::number(m[static_cast<size_t>(WM::TriggerUs)].mean()));
    tel.set("avg_matrix_us", Value::number(m[static_cast<size_t>(WM::MatrixUs)].mean()));
    tel.set("avg_governor_us", Value::number(m[static_cast<size_t>(WM::GovernorUs)].mean()));
    tel.set("total_objects_culled", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::CulledObjects)].aggregate(WM::CulledObjects)))));
    tel.set("total_particles_avoided", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::ParticlesAvoided)].aggregate(WM::ParticlesAvoided)))));
    tel.set("total_triggers_skipped", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::TriggersSkipped)].aggregate(WM::TriggersSkipped)))));
    tel.set("total_matrix_cache_hits", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::MatrixCacheHits)].aggregate(WM::MatrixCacheHits)))));
    tel.set("total_matrix_cache_misses", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::MatrixCacheMisses)].aggregate(WM::MatrixCacheMisses)))));
    tel.set("peak_arena_bytes", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::ArenaPeakBytes)].aggregate(WM::ArenaPeakBytes)))));
    tel.set("total_arena_allocations", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::ArenaAllocations)].aggregate(WM::ArenaAllocations)))));
    tel.set("total_heap_allocations_avoided", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::HeapAllocationsAvoided)].aggregate(WM::HeapAllocationsAvoided)))));
    tel.set("peak_total_objects", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::TotalObjects)].aggregate(WM::TotalObjects)))));
    tel.set("peak_active_particles", Value::integer(static_cast<long long>(
        std::llround(m[static_cast<size_t>(WM::ActiveParticles)].aggregate(WM::ActiveParticles)))));

    Value sampling = Value::object();
    sampling.set("percentile_method", Value::string(diagnostics::percentileMethodName(stats.percentileMethod)));
    sampling.set("sample_ring_capacity", Value::integer(static_cast<long long>(stats.ringCapacity)));
    sampling.set("sample_ring_used", Value::integer(static_cast<long long>(stats.ringUsed)));
    sampling.set("sample_ring_overflow", Value::boolean(stats.ringOverflow));
    sampling.set("overflow_samples", Value::integer(static_cast<long long>(stats.overflowSamples)));
    sampling.set("histogram_saturated", Value::boolean(stats.histogramSaturated));
    sampling.set("histogram_max_ms", Value::number(stats.histogramMaxMs));
    sampling.set("histogram_bin_width_ms", Value::number(stats.histogramBinWidthMs));
    sampling.set("percentile_definition", Value::string(
        "P50 = median (mean of the two central values for an even sample count); "
        "P95/P99 = nearest rank, index ceil(p*n)-1 clamped to [0, n-1], over the "
        "samples retained in this session's own window. When the ring overflows, "
        "percentiles come from a fixed-bin histogram and are reported as the bin "
        "upper bound (an over-estimate of at most one bin width)."));
    tel.set("sampling", std::move(sampling));
    root.set("telemetry", std::move(tel));

    // --- integrity ---------------------------------------------------------
    Value integrity = Value::object();
    integrity.set("pause_events", Value::integer(pauseEvents));
    integrity.set("reset_events", Value::integer(resetEvents));
    integrity.set("gap_events", Value::integer(gapEvents));
    integrity.set("tier_transitions", Value::integer(tierTransitions));
    integrity.set("fallback_events", Value::integer(fallbackEvents));
    integrity.set("excluded_samples", Value::integer(static_cast<long long>(stats.excludedSamples)));
    integrity.set("excluded_gap_ms", Value::number(excludedGapMs));
    integrity.set("paused_ms", Value::number(pausedMs));
    integrity.set("config_override_applied", Value::boolean(configOverrideApplied));
    integrity.set("warnings", warningsToJson(warnings));
    root.set("integrity", std::move(integrity));

    // --- definitions & limitations ----------------------------------------
    Value defs = Value::object();
    defs.set("avg_fps", Value::string(
        "1000 / mean inter-frame interval measured between consecutive "
        "GJBaseGameLayer::update() calls on the game thread. This is OptiGD's "
        "internal observation of game-loop pacing. It is NOT a GPU "
        "presentation timestamp and does not prove what the display showed."));
    defs.set("captured_duration_ms", Value::string(
        "Sum of the accepted inter-frame intervals: the amount of ACTIVE "
        "gameplay the window actually covers. Pauses, application switches and "
        "intervals above the gap threshold are excluded and counted in "
        "integrity.excluded_samples."));
    defs.set("wall_duration_ms", Value::string(
        "Monotonic clock time from session start to session end, including any "
        "paused or stalled time. Compare with captured_duration_ms to see how "
        "much of the session was actually gameplay."));
    defs.set("avg_update_cpu_time_ms", Value::string(
        "Mean WALL-clock duration of the hooked GJBaseGameLayer::update() call. "
        "It is not thread CPU time; if the game thread is preempted this value "
        "includes time the thread was not running."));
    defs.set("avg_update_thread_cpu_time_ms", Value::string(
        "Mean THREAD CPU time for the same call (CLOCK_THREAD_CPUTIME_ID on "
        "POSIX, GetThreadTimes on Windows). 0 when the platform cannot supply "
        "it. The difference from avg_update_cpu_time_ms is scheduling, not work."));
    defs.set("avg_optigd_overhead_us", Value::string(
        "Sum of the per-subsystem CPU spans OptiGD instruments itself "
        "(culling, particle governor, trigger lookahead, matrix cache, adaptive "
        "governor). It excludes hook dispatch cost and any code path OptiGD "
        "does not wrap, so it is a LOWER BOUND on OptiGD's cost, not a total."));
    defs.set("baseline_mode", Value::string(
        "OptiGD remains installed and its hooks and instrumentation still run; "
        "only the optimization features are switched to their passthrough "
        "behaviour. This is an internal control, NOT vanilla Geometry Dash."));
    root.set("measurement_definitions", std::move(defs));

    Value limitations = Value::array();
    limitations.push(Value::string(
        "Internal telemetry only: no claim is made about GPU execution, swap/present timing, or externally observed stutter."));
    limitations.push(Value::string(
        "A baseline session still includes the installed mod and its instrumentation overhead."));
    limitations.push(Value::string(
        "Native trigger execution, player physics and collision semantics are never modified by Benchmark Mode."));
    limitations.push(Value::string(
        "No statistical significance is claimed: a single pair of captures cannot separate an optimization effect from run-to-run variability."));
    root.set("limitations", std::move(limitations));

    return root;
}

bool BenchmarkSession::fromJson(const Value& root, BenchmarkSession& out, std::string& error) {
    if (!root.isObject()) {
        error = "session root must be a JSON object";
        return false;
    }
    const std::string kind = root.getString("report_kind");
    if (kind != kReportKind) {
        error = "report_kind is '" + kind + "', expected '" + kReportKind + "'";
        return false;
    }
    long long schema = root.getInt("schema_version", -1);
    if (schema < 1) {
        error = "missing or unsupported schema_version";
        return false;
    }
    if (schema > kSchemaVersion) {
        // A newer file than this build understands. Refuse rather than
        // silently reinterpret fields.
        error = "schema_version " + std::to_string(schema) + " is newer than this build supports (" +
                std::to_string(kSchemaVersion) + ")";
        return false;
    }

    out = BenchmarkSession{};
    out.loadedFromDisk = true;

    out.environment.optigdVersion = root.getString("optigd_version");
    out.environment.gdVersion = root.getString("gd_version");
    out.environment.geodeVersion = root.getString("geode_version");

    // Provenance is read, never trusted: an unrecognized or missing tag is
    // downgraded to [SIMULATED]. Loading a file may reduce what a record claims,
    // it may never increase it.
    {
        const std::string tag = root.getString("evidence_classification");
        const std::string note = root.getString("evidence_classification_note");
        if (tag == evidence::kMeasuredInGame || tag == evidence::kSimulated) {
            out.evidenceClassification = tag;
            out.evidenceClassificationNote =
                note.empty()
                    ? std::string(tag == evidence::kMeasuredInGame ? evidence::kMeasuredInGameNote
                                                                   : evidence::kSimulatedNote)
                    : note;
        } else if (!tag.empty()) {
            OPTIGD_LOG_WARN("Benchmark session '" + out.id +
                            "' carries an unrecognized evidence_classification '" + tag +
                            "'; it is reported as [SIMULATED]");
        }
    }

    if (const Value* s = root.getObject("session")) {
        out.id = s->getString("id");
        BenchmarkMode mode = BenchmarkMode::Baseline;
        if (!parseMode(s->getString("mode"), mode)) {
            error = "session.mode is not a recognized benchmark mode";
            return false;
        }
        out.mode = mode;
        BenchmarkState state = BenchmarkState::Invalid;
        if (!parseState(s->getString("state"), state)) {
            error = "session.state is not a recognized benchmark state";
            return false;
        }
        out.state = state;
        out.startedUtc = s->getString("started_utc");
        out.endedUtc = s->getString("ended_utc");
        out.startedEpochMs = s->getInt("started_epoch_ms", 0) >= 0
                                 ? static_cast<uint64_t>(s->getInt("started_epoch_ms", 0)) : 0;
        out.endedEpochMs = s->getInt("ended_epoch_ms", 0) >= 0
                               ? static_cast<uint64_t>(s->getInt("ended_epoch_ms", 0)) : 0;
        out.targetDurationMs = s->getDouble("target_duration_ms", 60000.0);
        out.capturedDurationMs = s->getDouble("captured_duration_ms", 0.0);
        out.wallDurationMs = s->getDouble("wall_duration_ms", 0.0);
        out.framesObserved = static_cast<uint64_t>(std::max<long long>(0, s->getInt("frames_observed", 0)));
        out.framesCounted = static_cast<uint64_t>(std::max<long long>(0, s->getInt("frames_counted", 0)));
        out.framesExcluded = static_cast<uint64_t>(std::max<long long>(0, s->getInt("frames_excluded", 0)));
        out.framesRejected = static_cast<uint64_t>(std::max<long long>(0, s->getInt("frames_rejected", 0)));
    } else {
        error = "missing 'session' object";
        return false;
    }

    if (const Value* p = root.getObject("platform")) {
        out.environment.osName = p->getString("os");
        out.environment.architecture = p->getString("architecture");
        out.environment.compiler = p->getString("compiler");
        out.environment.logicalCores = static_cast<uint32_t>(std::max<long long>(0, p->getInt("cores", 0)));
        out.environment.totalRamMb = static_cast<uint64_t>(std::max<long long>(0, p->getInt("total_ram_mb", 0)));
        out.environment.refreshHint = p->getString("refresh_hint", "unknown");
    }

    if (const Value* l = root.getObject("level")) {
        out.level.name = l->getString("name");
        out.level.creator = l->getString("creator");
        out.level.levelId = l->getInt("level_id", 0);
        out.level.levelIdKnown = l->getBool("level_id_known", false);
        out.level.attempt = static_cast<int>(l->getInt("attempt", 0));
        out.level.attemptKnown = l->getBool("attempt_known", false);
        out.level.practiceMode = l->getBool("practice_mode", false);
        out.level.practiceModeKnown = l->getBool("practice_mode_known", false);
        out.level.source = l->getString("source", "loaded");
    }

    if (const Value* c = root.getObject("configuration")) {
        std::string cfgError;
        if (!configFromJson(*c, out.config)) {
            cfgError = "configuration is not an object";
        }
        (void)cfgError;
        out.configOverrideApplied = c->getBool("override_applied", false);
        if (const Value* feats = c->getArray("features_disabled_for_baseline")) {
            for (const auto& f : feats->asArray()) {
                if (f.isString()) out.featuresDisabledForBaseline.push_back(f.asString());
            }
        }
    }

    if (const Value* t = root.getObject("telemetry")) {
        using WM = diagnostics::WindowMetric;
        auto& m = out.stats.metrics;
        out.stats.samples = static_cast<uint64_t>(std::max<long long>(0, t->getInt("samples", 0)));
        out.stats.totalFrameTimeMs = t->getDouble("total_window_ms", 0.0);
        out.stats.meanFrameTimeMs = t->getDouble("avg_frame_time_ms", 0.0);
        out.stats.p50FrameTimeMs = t->getDouble("median_frame_time_ms", 0.0);
        out.stats.p95FrameTimeMs = t->getDouble("p95_frame_time_ms", 0.0);
        out.stats.p99FrameTimeMs = t->getDouble("p99_frame_time_ms", 0.0);
        out.stats.minFrameTimeMs = t->getDouble("min_frame_time_ms", 0.0);
        out.stats.maxFrameTimeMs = t->getDouble("max_frame_time_ms", 0.0);
        out.stats.stdDevFrameTimeMs = t->getDouble("stddev_frame_time_ms", 0.0);
        out.stats.varianceFrameTimeMs = out.stats.stdDevFrameTimeMs * out.stats.stdDevFrameTimeMs;
        out.stats.fpsFromMeanFrameTime = t->getDouble("avg_fps", 0.0);
        out.stats.fpsFromWindow = t->getDouble("fps_from_window", 0.0);

        auto setMean = [&](WM which, double meanUs, uint64_t count) {
            auto& acc = m[static_cast<size_t>(which)];
            acc.reset();
            acc.count = count;
            acc.sum = meanUs * static_cast<double>(count);
            acc.min = count > 0 ? meanUs : acc.min;
            acc.max = count > 0 ? meanUs : acc.max;
        };
        const uint64_t n = out.stats.samples;
        setMean(WM::UpdateCpuUs, t->getDouble("avg_update_cpu_time_ms", 0.0) * 1000.0, n);
        setMean(WM::UpdateThreadCpuUs, t->getDouble("avg_update_thread_cpu_time_ms", 0.0) * 1000.0, n);
        setMean(WM::OptigdOverheadUs, t->getDouble("avg_optigd_overhead_us", 0.0), n);
        setMean(WM::CullingUs, t->getDouble("avg_culling_us", 0.0), n);
        setMean(WM::ParticleUs, t->getDouble("avg_particle_us", 0.0), n);
        setMean(WM::TriggerUs, t->getDouble("avg_trigger_us", 0.0), n);
        setMean(WM::MatrixUs, t->getDouble("avg_matrix_us", 0.0), n);
        setMean(WM::GovernorUs, t->getDouble("avg_governor_us", 0.0), n);

        auto setSum = [&](WM which, double total) {
            auto& acc = m[static_cast<size_t>(which)];
            acc.reset();
            acc.count = n;
            acc.sum = total;
            acc.min = 0.0;
            acc.max = total;
        };
        setSum(WM::CulledObjects, t->getDouble("total_objects_culled", 0.0));
        setSum(WM::ParticlesAvoided, t->getDouble("total_particles_avoided", 0.0));
        setSum(WM::TriggersSkipped, t->getDouble("total_triggers_skipped", 0.0));
        setSum(WM::MatrixCacheHits, t->getDouble("total_matrix_cache_hits", 0.0));
        setSum(WM::MatrixCacheMisses, t->getDouble("total_matrix_cache_misses", 0.0));
        setSum(WM::ArenaAllocations, t->getDouble("total_arena_allocations", 0.0));
        setSum(WM::HeapAllocationsAvoided, t->getDouble("total_heap_allocations_avoided", 0.0));

        auto setMax = [&](WM which, double peak) {
            auto& acc = m[static_cast<size_t>(which)];
            acc.reset();
            acc.count = n;
            acc.sum = peak;
            acc.min = peak;
            acc.max = peak;
        };
        setMax(WM::ArenaPeakBytes, t->getDouble("peak_arena_bytes", 0.0));
        setMax(WM::TotalObjects, t->getDouble("peak_total_objects", 0.0));
        setMax(WM::ActiveParticles, t->getDouble("peak_active_particles", 0.0));

        if (const Value* s = t->getObject("sampling")) {
            const std::string method = s->getString("percentile_method", "exact");
            if (method == "exact") out.stats.percentileMethod = diagnostics::PercentileMethod::Exact;
            else if (method == "histogram-approximate") out.stats.percentileMethod = diagnostics::PercentileMethod::HistogramApproximate;
            else out.stats.percentileMethod = diagnostics::PercentileMethod::None;
            out.stats.ringCapacity = static_cast<size_t>(std::max<long long>(0, s->getInt("sample_ring_capacity", 0)));
            out.stats.ringUsed = static_cast<size_t>(std::max<long long>(0, s->getInt("sample_ring_used", 0)));
            out.stats.ringOverflow = s->getBool("sample_ring_overflow", false);
            out.stats.overflowSamples = static_cast<uint64_t>(std::max<long long>(0, s->getInt("overflow_samples", 0)));
            out.stats.histogramSaturated = s->getBool("histogram_saturated", false);
            out.stats.histogramMaxMs = s->getDouble("histogram_max_ms", 250.0);
            out.stats.histogramBinWidthMs = s->getDouble("histogram_bin_width_ms", 0.1);
        }
        if (out.stats.samples > 0 &&
            out.stats.percentileMethod == diagnostics::PercentileMethod::None) {
            // A record with samples but no declared method cannot be trusted to
            // have exact percentiles; assume the weaker claim.
            out.stats.percentileMethod = out.stats.ringOverflow
                                             ? diagnostics::PercentileMethod::HistogramApproximate
                                             : diagnostics::PercentileMethod::Exact;
        }
    } else {
        error = "missing 'telemetry' object";
        return false;
    }

    if (const Value* ig = root.getObject("integrity")) {
        out.pauseEvents = static_cast<uint32_t>(std::max<long long>(0, ig->getInt("pause_events", 0)));
        out.resetEvents = static_cast<uint32_t>(std::max<long long>(0, ig->getInt("reset_events", 0)));
        out.gapEvents = static_cast<uint32_t>(std::max<long long>(0, ig->getInt("gap_events", 0)));
        out.tierTransitions = static_cast<uint32_t>(std::max<long long>(0, ig->getInt("tier_transitions", 0)));
        out.fallbackEvents = static_cast<uint32_t>(std::max<long long>(0, ig->getInt("fallback_events", 0)));
        out.stats.excludedSamples = static_cast<uint64_t>(std::max<long long>(0, ig->getInt("excluded_samples", 0)));
        out.excludedGapMs = ig->getDouble("excluded_gap_ms", 0.0);
        out.pausedMs = ig->getDouble("paused_ms", 0.0);
        out.stats.excludedTimeMs = out.excludedGapMs;
        out.configOverrideApplied = ig->getBool("config_override_applied", out.configOverrideApplied);
        if (const Value* w = ig->getArray("warnings")) {
            for (const auto& item : w->asArray()) {
                if (item.isString()) {
                    out.warnings.push_back(item.asString());
                } else if (const Value* code = item.find("code")) {
                    if (code->isString()) out.warnings.push_back(code->asString());
                }
            }
        }
    }

    if (out.id.empty()) {
        error = "session.id is missing";
        return false;
    }

    // A loaded record must never be able to claim more than its own data
    // supports. Re-deriving the counters and re-running the derived-warning pass
    // means a hand-edited or truncated file cannot present itself as `completed`
    // with no samples, and any warning its numbers imply is restored. Warnings
    // already in the file are preserved: this pass only adds, never removes.
    out.refreshDerivedWarnings();
    return true;
}

std::string BenchmarkSession::serialize() const {
    bool sawNonFinite = false;
    std::string text = core::json::serialize(toJson(), 2, &sawNonFinite);
    if (sawNonFinite) {
        // A non-finite number would have been emitted as `null`, which the
        // validator rejects. Surface it in the document itself instead of
        // shipping a silently wrong value.
        OPTIGD_LOG_WARN("BenchmarkSession::serialize replaced a non-finite statistic with null");
    }
    return text;
}

bool BenchmarkSession::parse(std::string_view text, BenchmarkSession& out, std::string& error) {
    core::json::ParseResult parsed = core::json::parse(text);
    if (!parsed.ok) {
        error = "JSON parse error at offset " + std::to_string(parsed.errorOffset) + ": " + parsed.error;
        return false;
    }
    return fromJson(parsed.value, out, error);
}

} // namespace optigd::benchmark

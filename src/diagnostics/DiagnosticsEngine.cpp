#include "DiagnosticsEngine.hpp"

#include "../compatibility/CompatibilityLayer.hpp"
#include "../core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace optigd::diagnostics {

namespace {

// Escapes every interpolated string. A report is written by hand-rolled
// concatenation, so an unescaped quote, backslash or control character (a
// fallback reason, a platform name, a level name) would produce JSON that no
// consumer can parse.
std::string jsonEscape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += kHex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

// A NaN or an infinity streamed with `std::fixed` emits `nan` / `inf`, which is
// not JSON. Every number goes through here; a sanitised value is also flagged in
// the report so the substitution is never silent.
class NumberWriter {
public:
    std::string write(double value) {
        if (!std::isfinite(value)) {
            m_sanitized = true;
            return "0";
        }
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(m_precision) << value;
        return ss.str();
    }
    std::string write(uint64_t value) {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }
    std::string write(long long value) {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }
    void setPrecision(int precision) { m_precision = precision; }
    bool sanitized() const { return m_sanitized; }

private:
    int m_precision{3};
    bool m_sanitized{false};
};

uint64_t steadyNowUs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint64_t systemNowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string utcIso8601(uint64_t epochMs) {
    if (epochMs == 0) return std::string();
    std::time_t seconds = static_cast<std::time_t>(epochMs / 1000ULL);
    std::tm tmUtc{};
#if defined(_WIN32)
    if (gmtime_s(&tmUtc, &seconds) != 0) return std::string();
#else
    if (gmtime_r(&seconds, &tmUtc) == nullptr) return std::string();
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                  tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);
    return std::string(buf);
}

// Writes `contents` to `path` without ever leaving a truncated file in place of
// a good one: temporary file, verify, rename.
bool writeAtomicFile(const std::string& path, const std::string& contents, std::string& error) {
    const std::string tempPath = path + ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "could not open '" + tempPath + "' for writing";
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out.good()) {
            error = "write failed for '" + tempPath + "' (disk full or I/O error)";
            out.close();
            std::remove(tempPath.c_str());
            return false;
        }
        out.close();
    }
    {
        std::ifstream verify(tempPath, std::ios::binary);
        if (!verify.is_open()) {
            error = "could not re-open '" + tempPath + "' to verify the write";
            std::remove(tempPath.c_str());
            return false;
        }
        std::string roundTrip((std::istreambuf_iterator<char>(verify)),
                              std::istreambuf_iterator<char>());
        if (roundTrip != contents) {
            error = "verification read of '" + tempPath + "' did not match what was written";
            std::remove(tempPath.c_str());
            return false;
        }
    }
    // std::rename replaces an existing file atomically on both POSIX and the
    // MSVC runtime.
    if (std::rename(tempPath.c_str(), path.c_str()) != 0) {
        error = "could not rename '" + tempPath + "' to '" + path + "'";
        std::remove(tempPath.c_str());
        return false;
    }
    return true;
}

} // namespace

DiagnosticsEngine::DiagnosticsEngine() {
    setHistoryCapacity(kDefaultHistoryCapacity);
}

DiagnosticsEngine& DiagnosticsEngine::instance() {
    static DiagnosticsEngine s_instance;
    return s_instance;
}

void DiagnosticsEngine::setHistoryCapacity(size_t frames) {
    m_historyCapacity = frames;
    SampleWindowConfig config;
    config.ringCapacity = frames;
    config.histogramMaxMs = 250.0;
    config.histogramBins = 2500;
    config.hitchThresholdMs = 1000.0 / 30.0;
    config.stallThresholdMs = 250.0;
    m_window.configure(config);
}

size_t DiagnosticsEngine::getHistoryCapacity() const {
    return m_historyCapacity;
}

void DiagnosticsEngine::reset() {
    m_framesOffered = 0;
    m_pausedFrames = 0;
    m_levelResets = 0;
    m_captureStartedEpochMs = systemNowMs();
    m_captureStartedMonotonicUs = steadyNowUs();
    // reset() keeps the configured capacity and its allocations, so starting a
    // new level does not re-allocate the window.
    m_window.reset();
}

double DiagnosticsEngine::getCaptureWallDurationMs() const {
    if (m_captureStartedMonotonicUs == 0) return 0.0;
    const uint64_t nowUs = steadyNowUs();
    if (nowUs <= m_captureStartedMonotonicUs) return 0.0;
    return static_cast<double>(nowUs - m_captureStartedMonotonicUs) / 1000.0;
}

void DiagnosticsEngine::recordFrameStats(const core::FrameStats& stats) {
    ++m_framesOffered;

    const bool isStall = stats.gapFrame && stats.totalFrameTimeUs > core::kStallThresholdUs;
    if (!stats.frameCounted && !isStall) {
        // The engine flagged this frame as not part of active gameplay (the
        // pause menu was open, or the level was still intro-ing). Its interval
        // is indistinguishable from a normal frame, so including it would let a
        // level report describe time the player spent paused. The excluded time
        // is still accounted for, and `paused_frames` says how much.
        ++m_pausedFrames;
        m_window.recordExcluded(stats.totalFrameTimeUs);
        return;
    }

    // Everything else is recorded, including stalls: a level-wide report must
    // not hide a multi-second freeze. The stall is counted separately
    // (`stall_frames`) so a reader can see it. Benchmark Mode applies a stricter
    // policy to its own window and excludes stalls from the captured duration.
    m_window.push(stats);
}

WindowStatistics DiagnosticsEngine::getWindowStatistics(StatPrecision precision) const {
    return m_window.computeStatistics(precision);
}

DiagnosticsSummary DiagnosticsEngine::computeSummary(const core::OptiConfig& currentConfig,
                                                     uint32_t fallbackCount,
                                                     uint32_t tierTransitions) const {
    DiagnosticsSummary sum{};
    WindowStatistics w = m_window.computeStatistics(StatPrecision::Exact);

    sum.activeTier = core::tierToString(currentConfig.tier);
    sum.fallbackEventsCount = fallbackCount;
    sum.tierTransitionsCount = tierTransitions;
    sum.levelResets = m_levelResets;

    // Frames offered since the capture started. `sampleCount` below is the number
    // of frames the statistics actually came from; beta.2 reported the first and
    // computed from the second without saying so.
    sum.totalFrames = m_framesOffered;

    sum.sampleCount = w.samples;
    sum.sampleWindowMs = w.totalFrameTimeMs;
    sum.historyCapacity = w.ringCapacity;
    sum.historyTruncated = w.ringOverflow;
    sum.percentileMethod = w.percentileMethod;
    sum.rejectedSamples = w.rejectedSamples;
    sum.excludedSamples = w.excludedSamples;
    sum.excludedTimeMs = w.excludedTimeMs;
    sum.hitchFrames = w.hitchFrames;
    sum.hitchTotalMs = w.hitchTotalMs;
    sum.stallFrames = w.stallFrames;
    sum.pausedFrames = m_pausedFrames;
    sum.histogramMaxMs = w.histogramMaxMs;
    sum.histogramBinWidthMs = w.histogramBinWidthMs;
    sum.windowOwnedBytes = m_window.getOwnedBytes();

    sum.avgFrameTimeMs = w.meanFrameTimeMs;
    sum.medianFrameTimeMs = w.p50FrameTimeMs;
    sum.p95FrameTimeMs = w.p95FrameTimeMs;
    sum.p99FrameTimeMs = w.p99FrameTimeMs;
    sum.minFrameTimeMs = w.minFrameTimeMs;
    sum.maxFrameTimeMs = w.maxFrameTimeMs;
    sum.stdDevFrameTimeMs = w.stdDevFrameTimeMs;
    sum.avgFps = w.fpsFromMeanFrameTime;
    sum.fpsFromWindow = w.fpsFromWindow;
    // `minFps`/`maxFps` are instantaneous rates derived from the single worst and
    // best interval. They are not a measured minimum/maximum presentation rate.
    sum.maxFps = (w.minFrameTimeMs > 0.0) ? (1000.0 / w.minFrameTimeMs) : 0.0;
    sum.minFps = (w.maxFrameTimeMs > 0.0) ? (1000.0 / w.maxFrameTimeMs) : 0.0;

    using WM = WindowMetric;
    const auto& m = w.metrics;
    sum.avgUpdateCpuTimeMs = m[static_cast<size_t>(WM::UpdateCpuUs)].mean() / 1000.0;
    sum.avgUpdateThreadCpuMs = m[static_cast<size_t>(WM::UpdateThreadCpuUs)].mean() / 1000.0;
    sum.threadCpuTimeAvailable =
        m[static_cast<size_t>(WM::UpdateThreadCpuUs)].safeMax() > 0.0;
    sum.avgOptigdOverheadUs = m[static_cast<size_t>(WM::OptigdOverheadUs)].mean();
    sum.avgCullingUs = m[static_cast<size_t>(WM::CullingUs)].mean();
    sum.avgParticleUs = m[static_cast<size_t>(WM::ParticleUs)].mean();
    sum.avgTriggerUs = m[static_cast<size_t>(WM::TriggerUs)].mean();
    sum.avgMatrixUs = m[static_cast<size_t>(WM::MatrixUs)].mean();
    sum.avgGovernorUs = m[static_cast<size_t>(WM::GovernorUs)].mean();

    sum.totalObjectsCulled = static_cast<uint64_t>(
        m[static_cast<size_t>(WM::CulledObjects)].aggregate(WM::CulledObjects));
    sum.totalParticlesAvoided = static_cast<uint64_t>(
        m[static_cast<size_t>(WM::ParticlesAvoided)].aggregate(WM::ParticlesAvoided));
    sum.totalTriggersSkipped = static_cast<uint64_t>(
        m[static_cast<size_t>(WM::TriggersSkipped)].aggregate(WM::TriggersSkipped));
    sum.totalMatrixCacheHits = static_cast<uint64_t>(
        m[static_cast<size_t>(WM::MatrixCacheHits)].aggregate(WM::MatrixCacheHits));
    sum.totalMatrixCacheMisses = static_cast<uint64_t>(
        m[static_cast<size_t>(WM::MatrixCacheMisses)].aggregate(WM::MatrixCacheMisses));
    sum.peakArenaBytes = static_cast<size_t>(
        m[static_cast<size_t>(WM::ArenaPeakBytes)].aggregate(WM::ArenaPeakBytes));
    sum.totalArenaAllocations = static_cast<uint64_t>(
        m[static_cast<size_t>(WM::ArenaAllocations)].aggregate(WM::ArenaAllocations));
    sum.totalHeapAllocationsAvoided = static_cast<uint64_t>(
        m[static_cast<size_t>(WM::HeapAllocationsAvoided)].aggregate(WM::HeapAllocationsAvoided));

    sum.captureWallDurationMs = getCaptureWallDurationMs();
    sum.captureActiveDurationMs = w.totalFrameTimeMs;

    return sum;
}

std::string DiagnosticsEngine::generateJsonReport(
    const core::OptiConfig& config,
    const std::vector<adaptive::FallbackEvent>& fallbacks,
    uint32_t tierTransitions) const {

    auto sum = computeSummary(config, static_cast<uint32_t>(fallbacks.size()), tierTransitions);
    auto hw = platform::Platform::getSystemInfo();
    auto& compat = compat::CompatibilityLayer::instance();

    NumberWriter num;
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"report_schema_version\": " << kDiagnosticsReportSchemaV2 << ",\n";
    ss << "  \"report_kind\": \"optigd_level_diagnostics\",\n";
    ss << "  \"optigd_version\": \"" << jsonEscape(core::kOptiGDVersion) << "\",\n";
    ss << "  \"gd_version\": \"" << jsonEscape(compat.getGDVersion()) << "\",\n";
    ss << "  \"geode_version\": \"" << jsonEscape(compat.getGeodeVersion()) << "\",\n";
    ss << "  \"evidence_classification\": \"[INTERNAL-TELEMETRY]\",\n";
    ss << "  \"platform\": {\n";
    ss << "    \"os\": \"" << jsonEscape(hw.osName) << "\",\n";
    ss << "    \"architecture\": \"" << jsonEscape(hw.architecture) << "\",\n";
    ss << "    \"compiler\": \"" << jsonEscape(hw.compiler) << "\",\n";
    ss << "    \"cores\": " << hw.logicalCores << ",\n";
    ss << "    \"total_ram_mb\": " << hw.totalRamMb << "\n";
    ss << "  },\n";

    ss << "  \"capture\": {\n";
    ss << "    \"started_utc\": \"" << jsonEscape(utcIso8601(m_captureStartedEpochMs)) << "\",\n";
    ss << "    \"started_epoch_ms\": " << m_captureStartedEpochMs << ",\n";
    ss << "    \"wall_duration_ms\": " << num.write(sum.captureWallDurationMs) << ",\n";
    ss << "    \"active_duration_ms\": " << num.write(sum.captureActiveDurationMs) << ",\n";
    ss << "    \"level_resets\": " << sum.levelResets << ",\n";
    ss << "    \"scope\": \"per level load; reset() runs on every PlayLayer init\"\n";
    ss << "  },\n";

    ss << "  \"configuration\": {\n";
    ss << "    \"active_tier\": \"" << jsonEscape(sum.activeTier) << "\",\n";
    ss << "    \"target_fps\": " << num.write(static_cast<double>(config.targetFrameRate)) << ",\n";
    ss << "    \"particle_density_scale\": " << num.write(static_cast<double>(config.particleDensityScale)) << ",\n";
    ss << "    \"max_active_particles\": " << config.maxActiveParticles << ",\n";
    ss << "    \"cull_offscreen_emitters\": " << (config.cullOffscreenEmitters ? "true" : "false") << ",\n";
    ss << "    \"spatial_culling_enabled\": " << (config.flags.enableSpatialCulling ? "true" : "false") << ",\n";
    ss << "    \"particle_throttling_enabled\": " << (config.flags.enableParticleThrottling ? "true" : "false") << ",\n";
    ss << "    \"trigger_indexing_enabled\": " << (config.flags.enableTriggerIndexing ? "true" : "false") << ",\n";
    ss << "    \"matrix_caching_enabled\": " << (config.flags.enableMatrixCaching ? "true" : "false") << ",\n";
    ss << "    \"adaptive_governor_enabled\": " << (config.flags.enableAdaptiveGovernor ? "true" : "false") << "\n";
    ss << "  },\n";

    ss << "  \"telemetry\": {\n";
    ss << "    \"total_frames\": " << sum.totalFrames << ",\n";
    ss << "    \"sample_count\": " << sum.sampleCount << ",\n";
    ss << "    \"sample_window_ms\": " << num.write(sum.sampleWindowMs) << ",\n";
    ss << "    \"history_capacity\": " << sum.historyCapacity << ",\n";
    ss << "    \"history_truncated\": " << (sum.historyTruncated ? "true" : "false") << ",\n";
    ss << "    \"percentile_method\": \"" << percentileMethodName(sum.percentileMethod) << "\",\n";
    ss << "    \"rejected_samples\": " << sum.rejectedSamples << ",\n";
    ss << "    \"excluded_samples\": " << sum.excludedSamples << ",\n";
    ss << "    \"excluded_time_ms\": " << num.write(sum.excludedTimeMs) << ",\n";
    ss << "    \"avg_fps\": " << num.write(sum.avgFps) << ",\n";
    ss << "    \"fps_from_window\": " << num.write(sum.fpsFromWindow) << ",\n";
    ss << "    \"min_fps\": " << num.write(sum.minFps) << ",\n";
    ss << "    \"max_fps\": " << num.write(sum.maxFps) << ",\n";
    ss << "    \"avg_frame_time_ms\": " << num.write(sum.avgFrameTimeMs) << ",\n";
    ss << "    \"median_frame_time_ms\": " << num.write(sum.medianFrameTimeMs) << ",\n";
    ss << "    \"p95_frame_time_ms\": " << num.write(sum.p95FrameTimeMs) << ",\n";
    ss << "    \"p99_frame_time_ms\": " << num.write(sum.p99FrameTimeMs) << ",\n";
    ss << "    \"min_frame_time_ms\": " << num.write(sum.minFrameTimeMs) << ",\n";
    ss << "    \"max_frame_time_ms\": " << num.write(sum.maxFrameTimeMs) << ",\n";
    ss << "    \"stddev_frame_time_ms\": " << num.write(sum.stdDevFrameTimeMs) << ",\n";
    ss << "    \"hitch_frames\": " << sum.hitchFrames << ",\n";
    ss << "    \"hitch_total_ms\": " << num.write(sum.hitchTotalMs) << ",\n";
    ss << "    \"stall_frames\": " << sum.stallFrames << ",\n";
    ss << "    \"paused_frames\": " << sum.pausedFrames << ",\n";
    ss << "    \"avg_update_cpu_time_ms\": " << num.write(sum.avgUpdateCpuTimeMs) << ",\n";
    ss << "    \"avg_update_thread_cpu_time_ms\": " << num.write(sum.avgUpdateThreadCpuMs) << ",\n";
    ss << "    \"thread_cpu_time_available\": " << (sum.threadCpuTimeAvailable ? "true" : "false") << ",\n";
    ss << "    \"avg_optigd_overhead_us\": " << num.write(sum.avgOptigdOverheadUs) << ",\n";
    ss << "    \"avg_culling_us\": " << num.write(sum.avgCullingUs) << ",\n";
    ss << "    \"avg_particle_us\": " << num.write(sum.avgParticleUs) << ",\n";
    ss << "    \"avg_trigger_us\": " << num.write(sum.avgTriggerUs) << ",\n";
    ss << "    \"avg_matrix_us\": " << num.write(sum.avgMatrixUs) << ",\n";
    ss << "    \"avg_governor_us\": " << num.write(sum.avgGovernorUs) << ",\n";
    ss << "    \"total_objects_culled\": " << sum.totalObjectsCulled << ",\n";
    ss << "    \"total_particles_avoided\": " << sum.totalParticlesAvoided << ",\n";
    ss << "    \"total_triggers_skipped\": " << sum.totalTriggersSkipped << ",\n";
    ss << "    \"total_matrix_cache_hits\": " << sum.totalMatrixCacheHits << ",\n";
    ss << "    \"total_matrix_cache_misses\": " << sum.totalMatrixCacheMisses << ",\n";
    ss << "    \"peak_arena_bytes\": " << sum.peakArenaBytes << ",\n";
    ss << "    \"total_arena_allocations\": " << sum.totalArenaAllocations << ",\n";
    ss << "    \"total_heap_allocations_avoided\": " << sum.totalHeapAllocationsAvoided << ",\n";
    ss << "    \"window_owned_bytes\": " << sum.windowOwnedBytes << ",\n";
    ss << "    \"tier_transitions_count\": " << sum.tierTransitionsCount << "\n";
    ss << "  },\n";

    ss << "  \"notes\": {\n";
    ss << "    \"native_trigger_execution_modified\": false,\n";
    ss << "    \"total_triggers_skipped_meaning\": "
       << "\"Triggers outside the TriggerOptimizer lookahead window (telemetry only); "
       << "native Geometry Dash trigger execution is never gated or replaced.\",\n";
    ss << "    \"avg_fps_meaning\": "
       << "\"1000 / mean inter-frame interval measured between consecutive "
       << "GJBaseGameLayer::update() calls on the game thread. Internal game-loop "
       << "pacing only: not a GPU presentation timestamp.\",\n";
    ss << "    \"min_max_fps_meaning\": "
       << "\"Instantaneous rates derived from the single shortest and longest "
       << "interval in the capture, not a measured presentation range.\",\n";
    ss << "    \"total_frames_meaning\": "
       << "\"Every frame offered to the engine since the capture started.\",\n";
    ss << "    \"sample_count_meaning\": "
       << "\"The frames the statistics above were computed from. When this is "
       << "smaller than total_frames the difference is reported in "
       << "rejected_samples and excluded_samples.\",\n";
    ss << "    \"percentile_definition\": "
       << "\"P50 is the median (mean of the two central values for an even sample "
       << "count). P95 and P99 use the nearest-rank index ceil(p*n)-1 clamped to "
       << "[0, n-1]. When percentile_method is histogram-approximate the value is "
       << "the upper bound of the bin containing the target rank, so it "
       << "over-estimates by at most histogram_bin_width_ms.\",\n";
    ss << "    \"stall_frames_meaning\": "
       << "\"Counted frames whose interval exceeded 250 ms (a pause, an "
       << "application switch, a load or a scheduler stall). They are kept in the "
       << "distribution so a level report cannot hide a freeze.\",\n";
    ss << "    \"paused_frames_meaning\": "
       << "\"Frames the engine flagged as not part of active gameplay (pause menu "
       << "open, level intro). They are excluded from the frame-time distribution "
       << "but their duration is included in excluded_time_ms.\",\n";
    ss << "    \"avg_update_cpu_time_ms_meaning\": "
       << "\"Wall-clock duration of the hooked update() call, not thread CPU time. "
       << "Compare with avg_update_thread_cpu_time_ms to separate slow work from "
       << "time off-core.\",\n";
    ss << "    \"avg_optigd_overhead_us_meaning\": "
       << "\"Sum of the subsystem spans OptiGD instruments itself. It excludes hook "
       << "dispatch and uninstrumented paths, so it is a lower bound on OptiGD's "
       << "cost and must never be read as total frame time.\",\n";
    ss << "    \"non_finite_values_sanitized\": " << (num.sanitized() ? "true" : "false") << "\n";
    ss << "  },\n";

    ss << "  \"fallbacks\": [\n";
    for (size_t i = 0; i < fallbacks.size(); ++i) {
        const auto& fb = fallbacks[i];
        ss << "    {\n";
        ss << "      \"frame\": " << fb.frameIndex << ",\n";
        ss << "      \"feature\": \"" << jsonEscape(fb.feature) << "\",\n";
        ss << "      \"reason\": \"" << jsonEscape(fb.reason) << "\",\n";
        ss << "      \"action\": \"" << jsonEscape(fb.actionTaken) << "\"\n";
        ss << "    }" << (i + 1 < fallbacks.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

bool DiagnosticsEngine::exportJsonReportToFile(
    const std::string& filePath,
    const core::OptiConfig& config,
    const std::vector<adaptive::FallbackEvent>& fallbacks,
    uint32_t tierTransitions) const {

    const std::string contents = generateJsonReport(config, fallbacks, tierTransitions);
    std::string error;
    if (!writeAtomicFile(filePath, contents, error)) {
        // beta.2 opened the destination directly with std::ofstream (truncating
        // it) and returned true without checking the stream, so a failed export
        // destroyed the previous report and still reported success.
        OPTIGD_LOG_ERROR("OptiGD diagnostics export failed: " + error);
        return false;
    }
    return true;
}

} // namespace optigd::diagnostics

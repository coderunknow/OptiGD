#include "DiagnosticsEngine.hpp"
#include "../compatibility/CompatibilityLayer.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace optigd::diagnostics {

namespace {

// Escapes a string so it can be embedded in a JSON string literal. Without this
// any value containing a quote, backslash or control character (for example a
// fallback reason or a platform/compiler name) would produce an unparseable
// report.
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

} // namespace

DiagnosticsEngine& DiagnosticsEngine::instance() {
    static DiagnosticsEngine s_instance;
    return s_instance;
}

void DiagnosticsEngine::reset() {
    m_history.clear();
    m_cumulativeFrames = 0;
    m_cumulativeObjectsCulled = 0;
    m_cumulativeParticlesAvoided = 0;
    m_cumulativeTriggersSkipped = 0;
    m_cumulativeMatrixHits = 0;
    m_cumulativeMatrixMisses = 0;
    m_peakArenaBytes = 0;
    m_cumulativeArenaAllocations = 0;
    m_cumulativeHeapAvoided = 0;
}

void DiagnosticsEngine::recordFrameStats(const core::FrameStats& stats) {
    m_history.push_back(stats);
    if (m_history.size() > m_maxHistoryFrames) {
        m_history.pop_front();
    }

    m_cumulativeFrames++;
    m_cumulativeObjectsCulled += stats.culledObjects;
    m_cumulativeParticlesAvoided += stats.culledParticles;
    if (stats.totalTriggers >= stats.evaluatedTriggers) {
        m_cumulativeTriggersSkipped += (stats.totalTriggers - stats.evaluatedTriggers);
    }
    m_cumulativeMatrixHits += stats.matrixCacheHits;
    m_cumulativeMatrixMisses += stats.matrixCacheMisses;
    m_peakArenaBytes = std::max(m_peakArenaBytes, stats.arenaPeakBytes);
    m_cumulativeArenaAllocations += stats.arenaAllocations;
    m_cumulativeHeapAvoided += stats.heapAllocationsAvoided;
}

DiagnosticsSummary DiagnosticsEngine::computeSummary(const core::OptiConfig& currentConfig,
                                                     uint32_t fallbackCount,
                                                     uint32_t tierTransitions) const {
    DiagnosticsSummary sum{};
    sum.totalFrames = m_cumulativeFrames;
    sum.totalObjectsCulled = m_cumulativeObjectsCulled;
    sum.totalParticlesAvoided = m_cumulativeParticlesAvoided;
    sum.totalTriggersSkipped = m_cumulativeTriggersSkipped;
    sum.totalMatrixCacheHits = m_cumulativeMatrixHits;
    sum.totalMatrixCacheMisses = m_cumulativeMatrixMisses;
    sum.peakArenaBytes = m_peakArenaBytes;
    sum.totalArenaAllocations = m_cumulativeArenaAllocations;
    sum.totalHeapAllocationsAvoided = m_cumulativeHeapAvoided;
    sum.fallbackEventsCount = fallbackCount;
    sum.tierTransitionsCount = tierTransitions;
    sum.activeTier = core::tierToString(currentConfig.tier);

    if (m_history.empty()) return sum;

    std::vector<double> frameTimesMs;
    frameTimesMs.reserve(m_history.size());

    double totalMs = 0.0;
    double totalUpdateCpuMs = 0.0;
    double totalOverheadUs = 0.0;
    double minMs = 1e9;
    double maxMs = 0.0;

    for (const auto& f : m_history) {
        double ms = f.totalFrameTimeUs / 1000.0;
        frameTimesMs.push_back(ms);
        totalMs += ms;
        totalUpdateCpuMs += (f.updateCpuTimeUs / 1000.0);
        totalOverheadUs += f.optigdOverheadUs;
        if (ms > 0.0) {
            minMs = std::min(minMs, ms);
        }
        maxMs = std::max(maxMs, ms);
    }

    size_t n = frameTimesMs.size();
    sum.avgFrameTimeMs = totalMs / static_cast<double>(n);
    sum.avgUpdateCpuTimeMs = totalUpdateCpuMs / static_cast<double>(n);
    sum.avgOptigdOverheadUs = totalOverheadUs / static_cast<double>(n);
    sum.avgFps = sum.avgFrameTimeMs > 0.0 ? (1000.0 / sum.avgFrameTimeMs) : 0.0;
    sum.maxFps = (minMs > 0.0 && minMs < 1e8) ? (1000.0 / minMs) : 0.0;
    sum.minFps = maxMs > 0.0 ? (1000.0 / maxMs) : 0.0;

    std::sort(frameTimesMs.begin(), frameTimesMs.end());
    size_t idx50 = n / 2;
    size_t idx95 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.95));
    size_t idx99 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.99));
    if (idx95 >= n) idx95 = n - 1;
    if (idx99 >= n) idx99 = n - 1;

    sum.medianFrameTimeMs = (n % 2 == 0 && n >= 2)
                                ? 0.5 * (frameTimesMs[idx50 - 1] + frameTimesMs[idx50])
                                : frameTimesMs[idx50];
    sum.p95FrameTimeMs = frameTimesMs[idx95];
    sum.p99FrameTimeMs = frameTimesMs[idx99];

    return sum;
}

std::string DiagnosticsEngine::generateJsonReport(
    const core::OptiConfig& config,
    const std::vector<adaptive::FallbackEvent>& fallbacks,
    uint32_t tierTransitions) const {

    auto sum = computeSummary(config, static_cast<uint32_t>(fallbacks.size()), tierTransitions);
    auto hw = platform::Platform::getSystemInfo();
    auto& compat = compat::CompatibilityLayer::instance();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "{\n";
    ss << "  \"optigd_version\": \"" << jsonEscape(core::kOptiGDVersion) << "\",\n";
    ss << "  \"gd_version\": \"" << jsonEscape(compat.getGDVersion()) << "\",\n";
    ss << "  \"geode_version\": \"" << jsonEscape(compat.getGeodeVersion()) << "\",\n";
    ss << "  \"platform\": {\n";
    ss << "    \"os\": \"" << jsonEscape(hw.osName) << "\",\n";
    ss << "    \"compiler\": \"" << jsonEscape(hw.compiler) << "\",\n";
    ss << "    \"cores\": " << hw.logicalCores << ",\n";
    ss << "    \"total_ram_mb\": " << hw.totalRamMb << "\n";
    ss << "  },\n";
    ss << "  \"configuration\": {\n";
    ss << "    \"active_tier\": \"" << jsonEscape(sum.activeTier) << "\",\n";
    ss << "    \"target_fps\": " << config.targetFrameRate << ",\n";
    ss << "    \"particle_density_scale\": " << config.particleDensityScale << ",\n";
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
    ss << "    \"avg_fps\": " << sum.avgFps << ",\n";
    ss << "    \"min_fps\": " << sum.minFps << ",\n";
    ss << "    \"max_fps\": " << sum.maxFps << ",\n";
    ss << "    \"avg_frame_time_ms\": " << sum.avgFrameTimeMs << ",\n";
    ss << "    \"median_frame_time_ms\": " << sum.medianFrameTimeMs << ",\n";
    ss << "    \"p95_frame_time_ms\": " << sum.p95FrameTimeMs << ",\n";
    ss << "    \"p99_frame_time_ms\": " << sum.p99FrameTimeMs << ",\n";
    ss << "    \"avg_update_cpu_time_ms\": " << sum.avgUpdateCpuTimeMs << ",\n";
    ss << "    \"avg_optigd_overhead_us\": " << sum.avgOptigdOverheadUs << ",\n";
    ss << "    \"total_objects_culled\": " << sum.totalObjectsCulled << ",\n";
    ss << "    \"total_particles_avoided\": " << sum.totalParticlesAvoided << ",\n";
    ss << "    \"total_triggers_skipped\": " << sum.totalTriggersSkipped << ",\n";
    ss << "    \"total_matrix_cache_hits\": " << sum.totalMatrixCacheHits << ",\n";
    ss << "    \"total_matrix_cache_misses\": " << sum.totalMatrixCacheMisses << ",\n";
    ss << "    \"peak_arena_bytes\": " << sum.peakArenaBytes << ",\n";
    ss << "    \"total_arena_allocations\": " << sum.totalArenaAllocations << ",\n";
    ss << "    \"total_heap_allocations_avoided\": " << sum.totalHeapAllocationsAvoided << ",\n";
    ss << "    \"tier_transitions_count\": " << sum.tierTransitionsCount << "\n";
    ss << "  },\n";
    ss << "  \"notes\": {\n";
    ss << "    \"native_trigger_execution_modified\": false,\n";
    ss << "    \"total_triggers_skipped_meaning\": "
       << "\"Triggers outside the TriggerOptimizer lookahead window (telemetry only); "
       << "native Geometry Dash trigger execution is never gated or replaced.\"\n";
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

    std::ofstream ofs(filePath);
    if (!ofs.is_open()) return false;
    ofs << generateJsonReport(config, fallbacks, tierTransitions);
    return true;
}

} // namespace optigd::diagnostics

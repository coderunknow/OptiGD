#include "DiagnosticsEngine.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <cmath>
#include <algorithm>

namespace optigd::diagnostics {

DiagnosticsEngine& DiagnosticsEngine::instance() {
    static DiagnosticsEngine s_instance;
    return s_instance;
}

void DiagnosticsEngine::recordFrame(const core::FrameStats& stats) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_totalFrames++;
    m_totalObjectsProcessed += stats.totalObjects;
    m_totalObjectsCulled += stats.culledObjects;
    m_totalHeapAllocsAvoided += stats.heapAllocationsAvoided;

    // Detect spike if frame time > 33.3ms (dropped frame at 60fps)
    if (stats.totalFrameTimeUs > 33333.0) {
        m_spikeCount++;
    }

    m_frameHistory.push_back(stats);
    if (m_frameHistory.size() > m_maxHistory) {
        m_frameHistory.pop_front();
    }
}

void DiagnosticsEngine::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameHistory.clear();
    m_totalFrames = 0;
    m_spikeCount = 0;
    m_totalObjectsProcessed = 0;
    m_totalObjectsCulled = 0;
    m_totalHeapAllocsAvoided = 0;
}

DiagnosticsSummary DiagnosticsEngine::generateSummary(const adaptive::AdaptiveGovernor& governor,
                                                    const adaptive::FallbackManager& fallbackMgr) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    DiagnosticsSummary summary;
    summary.totalFrames = m_totalFrames;
    summary.totalSpikes = m_spikeCount;
    summary.totalObjectsSeen = m_totalObjectsProcessed;
    summary.totalObjectsCulled = m_totalObjectsCulled;
    summary.totalHeapAllocsAvoided = m_totalHeapAllocsAvoided;
    summary.activeTier = governor.getCurrentTier();
    summary.fallbacksTriggered = fallbackMgr.getFallbackCount();

    if (m_totalObjectsProcessed > 0) {
        summary.averageCullingRatio = static_cast<double>(m_totalObjectsCulled) /
                                      static_cast<double>(m_totalObjectsProcessed);
    }

    if (m_frameHistory.empty()) {
        return summary;
    }

    double totalTimeMs = 0.0;
    double minMs = 1e9;
    double maxMs = 0.0;
    std::vector<double> times;
    times.reserve(m_frameHistory.size());

    for (const auto& f : m_frameHistory) {
        double ms = f.totalFrameTimeUs / 1000.0;
        times.push_back(ms);
        totalTimeMs += ms;
        minMs = std::min(minMs, ms);
        maxMs = std::max(maxMs, ms);
    }

    size_t n = times.size();
    summary.meanFrameTimeMs = totalTimeMs / static_cast<double>(n);
    summary.averageFps = summary.meanFrameTimeMs > 0.0 ? (1000.0 / summary.meanFrameTimeMs) : 0.0;
    summary.minFps = maxMs > 0.0 ? (1000.0 / maxMs) : 0.0;
    summary.maxFps = minMs > 0.0 ? (1000.0 / minMs) : 0.0;

    double sqDiffSum = 0.0;
    for (double ms : times) {
        double d = ms - summary.meanFrameTimeMs;
        sqDiffSum += d * d;
    }
    summary.frameTimeStdDevMs = std::sqrt(sqDiffSum / static_cast<double>(n));

    std::sort(times.begin(), times.end());
    size_t idx95 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.95));
    size_t idx99 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.99));
    if (idx95 >= n) idx95 = n - 1;
    if (idx99 >= n) idx99 = n - 1;

    summary.p95FrameTimeMs = times[idx95];
    summary.p99FrameTimeMs = times[idx99];

    return summary;
}

std::string DiagnosticsEngine::exportReportJson(const adaptive::AdaptiveGovernor& governor,
                                               const adaptive::FallbackManager& fallbackMgr) const {
    auto summary = generateSummary(governor, fallbackMgr);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "{\n";
    ss << "  \"mod\": \"OptiGD\",\n";
    ss << "  \"version\": \"1.0.0\",\n";
    ss << "  \"tier\": \"" << core::tierToString(summary.activeTier) << "\",\n";
    ss << "  \"totalFrames\": " << summary.totalFrames << ",\n";
    ss << "  \"fps\": {\n";
    ss << "    \"average\": " << summary.averageFps << ",\n";
    ss << "    \"min\": " << summary.minFps << ",\n";
    ss << "    \"max\": " << summary.maxFps << "\n";
    ss << "  },\n";
    ss << "  \"frameTimeMs\": {\n";
    ss << "    \"mean\": " << summary.meanFrameTimeMs << ",\n";
    ss << "    \"p95\": " << summary.p95FrameTimeMs << ",\n";
    ss << "    \"p99\": " << summary.p99FrameTimeMs << ",\n";
    ss << "    \"stdDev\": " << summary.frameTimeStdDevMs << "\n";
    ss << "  },\n";
    ss << "  \"culling\": {\n";
    ss << "    \"totalSeen\": " << summary.totalObjectsSeen << ",\n";
    ss << "    \"totalCulled\": " << summary.totalObjectsCulled << ",\n";
    ss << "    \"cullRatioPercent\": " << (summary.averageCullingRatio * 100.0) << "\n";
    ss << "  },\n";
    ss << "  \"allocationsAvoided\": " << summary.totalHeapAllocsAvoided << ",\n";
    ss << "  \"spikesDetected\": " << summary.totalSpikes << ",\n";
    ss << "  \"fallbacksTriggered\": " << summary.fallbacksTriggered << "\n";
    ss << "}\n";
    return ss.str();
}

bool DiagnosticsEngine::saveReportToFile(const std::string& path,
                                        const adaptive::AdaptiveGovernor& governor,
                                        const adaptive::FallbackManager& fallbackMgr) const {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << exportReportJson(governor, fallbackMgr);
    return true;
}

} // namespace optigd::diagnostics

#include "DiagnosticsEngine.hpp"
#include "../core/Version.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

namespace optigd::diagnostics {

DiagnosticsEngine& DiagnosticsEngine::instance() {
    static DiagnosticsEngine s_instance;
    return s_instance;
}

void DiagnosticsEngine::recordFrame(const core::FrameStats& stats) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_totalFrames++;
    m_totalShadowSeen += stats.totalObjects;
    m_totalShadowCulled += stats.shadowCulledObjects;
    m_totalArenaAllocations += stats.arenaAllocations;
    if (stats.frameIntervalUs > m_spikeThresholdUs) {
        m_spikeCount++;
    }

    m_frameHistory.push_back(stats);
    if (m_frameHistory.size() > m_maxHistory) {
        m_frameHistory.pop_front();
    }
}

void DiagnosticsEngine::setTargetFrameRate(float fps) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!(fps > 0.0f) || !std::isfinite(fps)) {
        return; // keep the previous threshold for invalid input
    }
    m_spikeThresholdUs = 2.0e6 / static_cast<double>(fps);
}

double DiagnosticsEngine::spikeThresholdUs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_spikeThresholdUs;
}

void DiagnosticsEngine::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameHistory.clear();
    m_totalFrames = 0;
    m_spikeCount = 0;
    m_totalShadowSeen = 0;
    m_totalShadowCulled = 0;
    m_totalArenaAllocations = 0;
}

std::deque<core::FrameStats> DiagnosticsEngine::getRecentFrames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_frameHistory;
}

DiagnosticsSummary DiagnosticsEngine::generateSummary(const adaptive::AdaptiveGovernor& governor,
                                                      const adaptive::FallbackManager& fallbackMgr) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    DiagnosticsSummary summary;
    summary.totalFrames = m_totalFrames;
    summary.totalSpikes = m_spikeCount;
    summary.totalShadowObjectsSeen = m_totalShadowSeen;
    summary.totalShadowObjectsCulled = m_totalShadowCulled;
    summary.totalArenaAllocations = m_totalArenaAllocations;
    summary.shadowCullRatio = m_totalShadowSeen > 0
        ? static_cast<double>(m_totalShadowCulled) / static_cast<double>(m_totalShadowSeen)
        : 0.0;
    summary.effectiveTier = governor.getCurrentTier();
    summary.fallbackEvents = static_cast<uint32_t>(fallbackMgr.getFallbackHistory().size());

    std::vector<double> intervalsMs;
    double gameUpdateSumMs = 0.0;
    double overheadSumUs = 0.0;
    for (const auto& f : m_frameHistory) {
        gameUpdateSumMs += f.gameUpdateUs / 1000.0;
        overheadSumUs += f.optigdOverheadUs;
        if (f.frameIntervalUs > 0.0) {
            intervalsMs.push_back(f.frameIntervalUs / 1000.0);
        }
    }
    if (!m_frameHistory.empty()) {
        const double n = static_cast<double>(m_frameHistory.size());
        summary.meanGameUpdateMs = gameUpdateSumMs / n;
        summary.meanOptigdOverheadUs = overheadSumUs / n;
    }

    summary.measuredIntervals = intervalsMs.size();
    if (intervalsMs.empty()) {
        return summary;
    }

    const size_t n = intervalsMs.size();
    const double sum = std::accumulate(intervalsMs.begin(), intervalsMs.end(), 0.0);
    summary.meanFrameIntervalMs = sum / static_cast<double>(n);
    summary.averageFps = 1000.0 / summary.meanFrameIntervalMs;
    summary.minFps = 1000.0 / *std::max_element(intervalsMs.begin(), intervalsMs.end());
    summary.maxFps = 1000.0 / *std::min_element(intervalsMs.begin(), intervalsMs.end());

    double sqDiffSum = 0.0;
    for (double ms : intervalsMs) {
        const double d = ms - summary.meanFrameIntervalMs;
        sqDiffSum += d * d;
    }
    summary.frameIntervalStdDevMs = std::sqrt(sqDiffSum / static_cast<double>(n));

    std::sort(intervalsMs.begin(), intervalsMs.end());
    const auto rank = [n](double p) {
        size_t r = static_cast<size_t>(std::ceil(p * static_cast<double>(n)));
        if (r == 0) r = 1;
        return std::min(r, n) - 1;
    };
    summary.p95FrameIntervalMs = intervalsMs[rank(0.95)];
    summary.p99FrameIntervalMs = intervalsMs[rank(0.99)];
    return summary;
}

std::string DiagnosticsEngine::exportReportJson(const adaptive::AdaptiveGovernor& governor,
                                                const adaptive::FallbackManager& fallbackMgr) const {
    const auto s = generateSummary(governor, fallbackMgr);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "{\n";
    ss << "  \"mod\": \"OptiGD\",\n";
    ss << "  \"version\": \"" << optigd::kOptiGDVersion << "\",\n";
    ss << "  \"effectiveTier\": \"" << core::tierToString(s.effectiveTier) << "\",\n";
    ss << "  \"totalFrames\": " << s.totalFrames << ",\n";
    ss << "  \"measuredIntervals\": " << s.measuredIntervals << ",\n";
    ss << "  \"fps\": { \"average\": " << s.averageFps << ", \"min\": " << s.minFps
       << ", \"max\": " << s.maxFps << " },\n";
    ss << "  \"frameIntervalMs\": { \"mean\": " << s.meanFrameIntervalMs
       << ", \"p95\": " << s.p95FrameIntervalMs << ", \"p99\": " << s.p99FrameIntervalMs
       << ", \"stdDev\": " << s.frameIntervalStdDevMs << " },\n";
    ss << "  \"gameUpdateMsMean\": " << s.meanGameUpdateMs << ",\n";
    ss << "  \"optigdOverheadUsMean\": " << s.meanOptigdOverheadUs << ",\n";
    ss << "  \"shadowCulling\": { \"objectsSeen\": " << s.totalShadowObjectsSeen
       << ", \"objectsWouldCull\": " << s.totalShadowObjectsCulled
       << ", \"ratio\": " << s.shadowCullRatio << " },\n";
    ss << "  \"arenaAllocations\": " << s.totalArenaAllocations << ",\n";
    ss << "  \"spikes\": " << s.totalSpikes << ",\n";
    ss << "  \"fallbackEvents\": " << s.fallbackEvents << "\n";
    ss << "}\n";
    return ss.str();
}

bool DiagnosticsEngine::saveReportToFile(const std::string& path,
                                         const adaptive::AdaptiveGovernor& governor,
                                         const adaptive::FallbackManager& fallbackMgr) const {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << exportReportJson(governor, fallbackMgr);
    return ofs.good();
}

} // namespace optigd::diagnostics

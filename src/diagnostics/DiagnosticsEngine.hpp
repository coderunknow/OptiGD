#pragma once

#include "../core/Types.hpp"
#include "../adaptive/FallbackManager.hpp"
#include "../platform/Platform.hpp"
#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <cstddef>

namespace optigd::diagnostics {

struct DiagnosticsSummary {
    double avgFps{0.0};
    double minFps{0.0};
    double maxFps{0.0};
    double avgFrameTimeMs{0.0};
    double medianFrameTimeMs{0.0};
    double p95FrameTimeMs{0.0};
    double p99FrameTimeMs{0.0};
    double avgUpdateCpuTimeMs{0.0};
    double avgOptigdOverheadUs{0.0};
    uint64_t totalFrames{0};
    uint64_t totalObjectsCulled{0};
    uint64_t totalParticlesAvoided{0};
    uint64_t totalTriggersSkipped{0};
    uint64_t totalMatrixCacheHits{0};
    uint64_t totalMatrixCacheMisses{0};
    size_t peakArenaBytes{0};
    uint64_t totalArenaAllocations{0};
    uint64_t totalHeapAllocationsAvoided{0};
    uint32_t fallbackEventsCount{0};
    uint32_t tierTransitionsCount{0};
    std::string activeTier;
};

class DiagnosticsEngine {
public:
    static DiagnosticsEngine& instance();

    void reset();
    void recordFrameStats(const core::FrameStats& stats);

    DiagnosticsSummary computeSummary(const core::OptiConfig& currentConfig,
                                      uint32_t fallbackCount,
                                      uint32_t tierTransitions) const;

    std::string generateJsonReport(const core::OptiConfig& config,
                                   const std::vector<adaptive::FallbackEvent>& fallbacks,
                                   uint32_t tierTransitions = 0) const;

    bool exportJsonReportToFile(const std::string& filePath,
                                const core::OptiConfig& config,
                                const std::vector<adaptive::FallbackEvent>& fallbacks,
                                uint32_t tierTransitions = 0) const;

    const std::deque<core::FrameStats>& getFrameHistory() const { return m_history; }

private:
    DiagnosticsEngine() = default;
    std::deque<core::FrameStats> m_history;
    size_t m_maxHistoryFrames{3600}; // 60 seconds at 60fps

    uint64_t m_cumulativeFrames{0};
    uint64_t m_cumulativeObjectsCulled{0};
    uint64_t m_cumulativeParticlesAvoided{0};
    uint64_t m_cumulativeTriggersSkipped{0};
    uint64_t m_cumulativeMatrixHits{0};
    uint64_t m_cumulativeMatrixMisses{0};
    size_t m_peakArenaBytes{0};
    uint64_t m_cumulativeArenaAllocations{0};
    uint64_t m_cumulativeHeapAvoided{0};
};

} // namespace optigd::diagnostics

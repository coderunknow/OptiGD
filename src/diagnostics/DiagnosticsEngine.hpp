#pragma once

#include "../core/Types.hpp"
#include "../adaptive/AdaptiveGovernor.hpp"
#include "../adaptive/FallbackManager.hpp"
#include <vector>
#include <deque>
#include <string>
#include <mutex>
#include <cstdint>
#include <cstddef>

namespace optigd::diagnostics {

struct DiagnosticsSummary {
    uint64_t totalFrames{0};
    double averageFps{0.0};
    double minFps{0.0};
    double maxFps{0.0};
    double meanFrameTimeMs{0.0};
    double p95FrameTimeMs{0.0};
    double p99FrameTimeMs{0.0};
    double frameTimeStdDevMs{0.0};

    uint64_t totalSpikes{0};
    uint64_t totalObjectsSeen{0};
    uint64_t totalObjectsCulled{0};
    double averageCullingRatio{0.0};

    uint64_t totalParticlesAvoided{0};
    uint64_t totalHeapAllocsAvoided{0};

    core::PerformanceTier activeTier{core::PerformanceTier::LowEnd};
    uint32_t fallbacksTriggered{0};
};

class DiagnosticsEngine {
public:
    static DiagnosticsEngine& instance();

    void recordFrame(const core::FrameStats& stats);
    void reset();

    DiagnosticsSummary generateSummary(const adaptive::AdaptiveGovernor& governor,
                                      const adaptive::FallbackManager& fallbackMgr) const;

    std::string exportReportJson(const adaptive::AdaptiveGovernor& governor,
                                 const adaptive::FallbackManager& fallbackMgr) const;

    bool saveReportToFile(const std::string& path,
                          const adaptive::AdaptiveGovernor& governor,
                          const adaptive::FallbackManager& fallbackMgr) const;

    const std::deque<core::FrameStats>& getRecentFrames() const { return m_frameHistory; }
    size_t getSpikeCount() const { return m_spikeCount; }

private:
    DiagnosticsEngine() = default;
    mutable std::mutex m_mutex;
    std::deque<core::FrameStats> m_frameHistory;
    size_t m_maxHistory{600}; // 10 seconds at 60fps

    uint64_t m_totalFrames{0};
    uint64_t m_spikeCount{0};
    uint64_t m_totalObjectsProcessed{0};
    uint64_t m_totalObjectsCulled{0};
    uint64_t m_totalHeapAllocsAvoided{0};
};

} // namespace optigd::diagnostics

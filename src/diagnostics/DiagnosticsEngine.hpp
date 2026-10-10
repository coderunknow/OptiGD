#pragma once

#include "../core/Types.hpp"
#include "../adaptive/AdaptiveGovernor.hpp"
#include "../adaptive/FallbackManager.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace optigd::diagnostics {

// Summary over the recent window (frame intervals only; zero intervals are excluded).
struct DiagnosticsSummary {
    uint64_t totalFrames{0};
    uint64_t measuredIntervals{0};   // frames with a valid frame interval
    double averageFps{0.0};
    double minFps{0.0};
    double maxFps{0.0};
    double meanFrameIntervalMs{0.0};
    double p95FrameIntervalMs{0.0};
    double p99FrameIntervalMs{0.0};
    double frameIntervalStdDevMs{0.0};

    double meanGameUpdateMs{0.0};
    double meanOptigdOverheadUs{0.0};

    uint64_t totalSpikes{0};         // frame intervals above kSpikeThresholdUs (lifetime)
    uint64_t totalShadowObjectsSeen{0};
    uint64_t totalShadowObjectsCulled{0};
    double shadowCullRatio{0.0};     // shadow measurement only; nothing is hidden
    uint64_t totalArenaAllocations{0};

    core::PerformanceTier effectiveTier{core::PerformanceTier::LowEnd};
    uint32_t fallbackEvents{0};
};

class DiagnosticsEngine {
public:
    // A frame interval counts as a spike when it exceeds two frame budgets at the target FPS.
    // Default target 60 FPS gives ~33333 us. Call setTargetFrameRate() to change it.
    static constexpr float kDefaultTargetFps = 60.0f;

    void setTargetFrameRate(float fps);
    double spikeThresholdUs() const;

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

    std::deque<core::FrameStats> getRecentFrames() const;

private:
    DiagnosticsEngine() = default;
    mutable std::mutex m_mutex;
    std::deque<core::FrameStats> m_frameHistory;
    size_t m_maxHistory{600}; // 10 seconds at 60 fps

    uint64_t m_totalFrames{0};
    uint64_t m_spikeCount{0};
    double m_spikeThresholdUs{2.0e6 / 60.0};
    uint64_t m_totalShadowSeen{0};
    uint64_t m_totalShadowCulled{0};
    uint64_t m_totalArenaAllocations{0};
};

} // namespace optigd::diagnostics

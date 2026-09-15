#pragma once

#include "../core/Types.hpp"
#include <deque>
#include <vector>
#include <cstdint>

namespace optigd::adaptive {

struct WindowStats {
    double meanMs{0.0};
    double minMs{0.0};
    double maxMs{0.0};
    double varianceMs{0.0};
    double stdDevMs{0.0};
    double p95Ms{0.0};
    double p99Ms{0.0};
    double fps{0.0};
};

class AdaptiveGovernor {
public:
    explicit AdaptiveGovernor(const core::OptiConfig& initialConfig);

    void setTargetFrameRate(float fps);
    float getTargetFrameRate() const { return m_targetFps; }

    void setHysteresisFrames(uint32_t frames) { m_hysteresisCooldown = frames; }
    uint32_t getHysteresisFrames() const { return m_hysteresisCooldown; }

    void recordFrameTime(double frameTimeUs);
    void update(core::OptiConfig& currentConfig);

    const WindowStats& getRecentStats() const { return m_stats; }
    core::PerformanceTier getCurrentTier() const { return m_currentTier; }
    void forceTier(core::PerformanceTier tier);

    uint64_t getTotalFrames() const { return m_totalFrames; }
    uint32_t getTierTransitions() const { return m_tierTransitions; }
    double getLastGovernorOverheadUs() const { return m_lastOverheadUs; }

    bool isDynamicAdaptationEnabled() const { return m_dynamicEnabled; }
    void setDynamicAdaptationEnabled(bool enabled) { m_dynamicEnabled = enabled; }

private:
    float m_targetFps{60.0f};
    double m_targetFrameBudgetMs{16.666};
    core::PerformanceTier m_currentTier{core::PerformanceTier::LowEnd};
    bool m_dynamicEnabled{true};

    std::deque<double> m_frameTimesMs;
    size_t m_windowSize{120}; // 2 seconds at 60fps

    uint32_t m_cooldownRemaining{0};
    uint32_t m_hysteresisCooldown{120};
    uint64_t m_totalFrames{0};
    uint32_t m_tierTransitions{0};

    WindowStats m_stats{};
    double m_lastOverheadUs{0.0};

    void calculateWindowStats();
    void evaluateTierAdjustment(core::OptiConfig& config);
};

} // namespace optigd::adaptive

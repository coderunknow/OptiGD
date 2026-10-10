#pragma once

#include "../core/Types.hpp"
#include <cstdint>
#include <deque>

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
    size_t sampleCount{0};
};

// Chooses an EFFECTIVE performance tier from measured frame intervals.
//
// The user's configuration is never modified. The governor reads the user config and
// produces an effective config (effectiveConfig()) where only the tuning values of the
// current effective tier are replaced by the preset table; flags are untouched.
//
// Inputs are full frame intervals (core::FrameStats::frameIntervalUs), not the cost of
// one callback. Samples with a non-positive interval are rejected.
//
// Custom is never changed automatically. Downgrades need sustained budget pressure;
// upgrades need sustained headroom; both are followed by a cooldown.
class AdaptiveGovernor {
public:
    explicit AdaptiveGovernor(const core::OptiConfig& userConfig);

    void setTargetFrameRate(float fps);
    float getTargetFrameRate() const { return m_targetFps; }

    void setCooldownFrames(uint32_t frames) { m_cooldownFrames = frames; }
    uint32_t getCooldownFrames() const { return m_cooldownFrames; }

    // Clears the sample window and the effective tier; keeps settings.
    void reset(const core::OptiConfig& userConfig);

    void recordFrameInterval(double frameIntervalUs);
    // Recomputes stats and possibly changes the effective tier.
    void update(const core::OptiConfig& userConfig);

    // Returns the user config with the effective tier's tuning applied.
    core::OptiConfig effectiveConfig(const core::OptiConfig& userConfig) const;

    const WindowStats& getRecentStats() const { return m_stats; }
    core::PerformanceTier getCurrentTier() const { return m_currentTier; }
    void forceTier(core::PerformanceTier tier);

    uint64_t getTotalFrames() const { return m_totalFrames; }
    uint32_t getTierTransitions() const { return m_tierTransitions; }
    double getLastGovernorOverheadUs() const { return m_lastOverheadUs; }

    bool isDynamicAdaptationEnabled() const { return m_dynamicEnabled; }
    void setDynamicAdaptationEnabled(bool enabled) { m_dynamicEnabled = enabled; }

    static constexpr size_t kWindowSize = 120;   // 2 s at 60 fps
    static constexpr size_t kMinSamples = 30;    // no decisions before this many samples

private:
    float m_targetFps{60.0f};
    double m_targetFrameBudgetMs{1000.0 / 60.0};
    core::PerformanceTier m_currentTier{core::PerformanceTier::LowEnd};
    bool m_dynamicEnabled{true};

    std::deque<double> m_frameIntervalsMs;

    uint32_t m_cooldownRemaining{0};
    uint32_t m_cooldownFrames{120};
    uint64_t m_totalFrames{0};
    uint32_t m_tierTransitions{0};

    WindowStats m_stats{};
    double m_lastOverheadUs{0.0};

    void calculateWindowStats();
    void evaluateTierAdjustment();
};

} // namespace optigd::adaptive

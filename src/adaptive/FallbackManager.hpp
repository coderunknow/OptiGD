#pragma once

#include "../core/Types.hpp"
#include "AdaptiveGovernor.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace optigd::adaptive {

struct FallbackEvent {
    uint64_t frameIndex{0};
    int level{0};            // level after the event
    std::string reason;
    std::string actionTaken;
};

// Reversible safety fallbacks for frame-time jitter.
//
// Levels (0 = none):
//   1: particle density capped to 25 % and camera padding +50 px
//   2: additionally, active particles capped to 1024
// Escalation: jitter (stdDev above threshold while FPS is below 80 % of target) raises the
// level by one, at most once per cooldown. Recovery: after the cooldown, stable frames
// (stdDev below half the threshold and FPS at least 95 % of target) lower the level by one.
// The fallback never changes the user's config: apply() only tightens the effective config.
class FallbackManager {
public:
    static constexpr int kMaxLevel = 2;

    explicit FallbackManager(float varianceThresholdMs = 4.0f, uint32_t cooldownFrames = 120);

    void setVarianceThreshold(float thresholdMs);
    float getVarianceThreshold() const { return m_varianceThresholdMs; }

    void setCooldownFrames(uint32_t frames) { m_cooldownFrames = frames; }
    uint32_t getCooldownFrames() const { return m_cooldownFrames; }

    // When true, no fallback is applied or escalated. Levels already applied are cleared.
    void setUserOverride(bool userOverride);
    bool isUserOverride() const { return m_userOverride; }

    // Updates the level from the latest window stats.
    // Returns true if the level changed on this call.
    bool update(uint64_t currentFrame, const WindowStats& stats, const core::OptiConfig& userConfig);

    // Applies the current level's tightening to an effective config.
    void apply(core::OptiConfig& effectiveConfig) const;

    void reset();

    int getLevel() const { return m_level; }
    bool isFallbackActive() const { return m_level > 0; }
    const std::vector<FallbackEvent>& getFallbackHistory() const { return m_history; }

private:
    float m_varianceThresholdMs{4.0f};
    uint32_t m_cooldownFrames{120};
    uint32_t m_cooldownRemaining{0};
    bool m_userOverride{false};
    int m_level{0};
    std::vector<FallbackEvent> m_history;
};

} // namespace optigd::adaptive

#pragma once

#include "../core/Types.hpp"
#include "AdaptiveGovernor.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace optigd::adaptive {

struct FallbackEvent {
    uint64_t frameIndex{0};
    std::string feature;
    std::string reason;
    std::string actionTaken;
};

class FallbackManager {
public:
    explicit FallbackManager(float varianceThresholdMs = 4.0f, uint32_t cooldownFrames = 120);

    void setVarianceThreshold(float thresholdMs) { m_varianceThresholdMs = thresholdMs; }
    float getVarianceThreshold() const { return m_varianceThresholdMs; }

    void setCooldownFrames(uint32_t frames) { m_cooldownFrames = frames; }
    uint32_t getCooldownFrames() const { return m_cooldownFrames; }

    void setRecoveryStableFrames(uint32_t frames) { m_requiredRecoveryFrames = std::max<uint32_t>(1, frames); }
    uint32_t getRecoveryStableFrames() const { return m_requiredRecoveryFrames; }

    void setUserOverride(bool userOverride) { m_userOverride = userOverride; }
    bool isUserOverride() const { return m_userOverride; }

    // Evaluates current stats for regressions and applies fallbacks if necessary
    bool checkAndApplyFallbacks(uint64_t currentFrame, const WindowStats& stats, core::OptiConfig& config);

    // Evaluates per-subsystem CPU budgets as well as frame-time variance
    bool checkAndApplyFeatureFallbacks(uint64_t currentFrame,
                                       const core::FrameStats& frameStats,
                                       const WindowStats& stats,
                                       core::OptiConfig& config);

    void reset();

    const std::vector<FallbackEvent>& getFallbackHistory() const { return m_history; }
    uint32_t getFallbackCount() const { return static_cast<uint32_t>(m_history.size()); }
    uint32_t getRecoveryCount() const { return m_recoveryCount; }
    bool isFallbackActive() const { return m_fallbackActive; }

private:
    float m_varianceThresholdMs{4.0f};
    uint32_t m_cooldownFrames{120};
    uint32_t m_cooldownRemaining{0};
    uint32_t m_requiredRecoveryFrames{1};
    uint32_t m_stableRecoveryCounter{0};
    uint32_t m_recoveryCount{0};
    bool m_userOverride{false};
    bool m_fallbackActive{false};
    bool m_hasSavedPreFallback{false};

    core::OptiConfig m_preFallbackConfig{};
    std::vector<FallbackEvent> m_history;
};

} // namespace optigd::adaptive

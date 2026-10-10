#include "FallbackManager.hpp"
#include "../core/Logger.hpp"

#include <algorithm>
#include <cmath>

namespace optigd::adaptive {

FallbackManager::FallbackManager(float varianceThresholdMs, uint32_t cooldownFrames)
    : m_varianceThresholdMs(std::isfinite(varianceThresholdMs) && varianceThresholdMs > 0.0f ? varianceThresholdMs : 4.0f),
      m_cooldownFrames(cooldownFrames) {}

void FallbackManager::setVarianceThreshold(float thresholdMs) {
    if (std::isfinite(thresholdMs) && thresholdMs > 0.0f) {
        m_varianceThresholdMs = thresholdMs;
    }
}

void FallbackManager::setUserOverride(bool userOverride) {
    m_userOverride = userOverride;
    if (userOverride && m_level > 0) {
        m_level = 0;
        m_cooldownRemaining = 0;
    }
}

bool FallbackManager::update(uint64_t currentFrame, const WindowStats& stats, const core::OptiConfig& userConfig) {
    if (m_userOverride || stats.sampleCount == 0) {
        return false;
    }
    if (m_cooldownRemaining > 0) {
        m_cooldownRemaining--;
        return false;
    }

    const double target = static_cast<double>(userConfig.targetFrameRate);
    const bool jitter = stats.stdDevMs > m_varianceThresholdMs && stats.fps < target * 0.8;
    const bool stable = stats.stdDevMs < m_varianceThresholdMs * 0.5 && stats.fps >= target * 0.95;

    if (jitter && m_level < kMaxLevel) {
        m_level++;
        m_cooldownRemaining = m_cooldownFrames;
        const std::string reason = "Frame-time jitter " + std::to_string(stats.stdDevMs) +
                                   " ms above threshold " + std::to_string(m_varianceThresholdMs) + " ms";
        const std::string action = (m_level == 1)
            ? "Level 1: particle density capped to 25% and camera padding +50 px"
            : "Level 2: active particles capped to 1024 (plus level 1)";
        m_history.push_back({currentFrame, m_level, reason, action});
        OPTIGD_LOG_WARN("FallbackManager: " + reason + ". " + action);
        return true;
    }

    if (stable && m_level > 0) {
        m_level--;
        m_cooldownRemaining = m_cooldownFrames;
        const std::string action = "Recovered to level " + std::to_string(m_level);
        m_history.push_back({currentFrame, m_level, "Frame times stable", action});
        OPTIGD_LOG_INFO("FallbackManager: " + action);
        return true;
    }
    return false;
}

void FallbackManager::apply(core::OptiConfig& effectiveConfig) const {
    if (m_level >= 1) {
        effectiveConfig.particleDensityScale = std::min(effectiveConfig.particleDensityScale, 0.25f);
        effectiveConfig.cameraPaddingX += 50.0f;
        effectiveConfig.cameraPaddingY += 50.0f;
    }
    if (m_level >= 2) {
        effectiveConfig.maxActiveParticles = std::min<uint32_t>(effectiveConfig.maxActiveParticles, 1024u);
    }
}

void FallbackManager::reset() {
    m_cooldownRemaining = 0;
    m_level = 0;
    m_history.clear();
}

} // namespace optigd::adaptive

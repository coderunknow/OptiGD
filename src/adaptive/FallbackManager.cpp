#include "FallbackManager.hpp"
#include "../core/Logger.hpp"

namespace optigd::adaptive {

FallbackManager::FallbackManager(float varianceThresholdMs, uint32_t cooldownFrames)
    : m_varianceThresholdMs(varianceThresholdMs), m_cooldownFrames(cooldownFrames) {}

bool FallbackManager::checkAndApplyFallbacks(uint64_t currentFrame, const WindowStats& stats, core::OptiConfig& config) {
    if (m_userOverride) {
        return false; // User manually overrides fallback logic
    }

    if (m_cooldownRemaining > 0) {
        m_cooldownRemaining--;
        return false;
    }

    // Check for severe frame-time variance/jitter regression
    if (stats.stdDevMs > m_varianceThresholdMs && stats.fps < (config.targetFrameRate * 0.8f)) {
        m_fallbackActive = true;
        m_cooldownRemaining = m_cooldownFrames;

        std::string reason = "Frame-time jitter (" + std::to_string(stats.stdDevMs) +
                             "ms) exceeded variance threshold (" +
                             std::to_string(m_varianceThresholdMs) + "ms)";
        std::string action;

        // Downgrade aggressiveness safely
        if (config.particleDensityScale > 0.25f) {
            config.particleDensityScale = 0.25f;
            action = "Reduced particle density to 25% and expanded camera culling padding to prevent boundary thrashing";
            config.cameraPaddingX += 50.0f;
            config.cameraPaddingY += 50.0f;
        } else {
            // Further fallback
            config.maxActiveParticles = 1024;
            action = "Capped active particles to 1024 and enabled strict boundary culling";
        }

        FallbackEvent evt{currentFrame, reason, action};
        m_history.push_back(evt);

        OPTIGD_LOG_WARN(std::string("FallbackManager: ") + reason + ". Action taken: " + action);
        return true;
    }

    // If performance is stable and variance is very low, clear fallback state after cooldown
    if (m_fallbackActive && stats.stdDevMs < (m_varianceThresholdMs * 0.5f) && stats.fps >= (config.targetFrameRate * 0.95f)) {
        m_fallbackActive = false;
        OPTIGD_LOG_INFO("FallbackManager: Frame-time variance stabilized below threshold. Returning to standard governor control.");
    }

    return false;
}

void FallbackManager::reset() {
    m_cooldownRemaining = 0;
    m_fallbackActive = false;
    m_history.clear();
}

} // namespace optigd::adaptive

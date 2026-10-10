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
    if (stats.stdDevMs > m_varianceThresholdMs && stats.fps > 0.0 && stats.fps < (config.targetFrameRate * 0.8f)) {
        if (!m_hasSavedPreFallback) {
            m_preFallbackConfig = config;
            m_hasSavedPreFallback = true;
        }
        m_fallbackActive = true;
        m_stableRecoveryCounter = 0;
        m_cooldownRemaining = m_cooldownFrames;

        std::string feature = "particle_governor";
        std::string reason = "Frame-time jitter (" + std::to_string(stats.stdDevMs) +
                             "ms) exceeded variance threshold (" +
                             std::to_string(m_varianceThresholdMs) + "ms)";
        std::string action;

        // Downgrade aggressiveness safely
        if (config.particleDensityScale > 0.25f) {
            config.particleDensityScale = 0.25f;
            config.cameraPaddingX += 50.0f;
            config.cameraPaddingY += 50.0f;
            action = "Reduced particle density to 25% and expanded camera culling padding to prevent boundary thrashing";
        } else {
            config.maxActiveParticles = 1024;
            feature = "spatial_culling";
            action = "Capped active particles to 1024 and enabled strict boundary culling";
        }

        FallbackEvent evt{currentFrame, feature, reason, action};
        m_history.push_back(evt);

        OPTIGD_LOG_WARN("FallbackManager [" + feature + "]: " + reason + ". Action taken: " + action);
        return true;
    }

    // Safe recovery path: when variance stabilizes below 50% of threshold and FPS recovers
    if (m_fallbackActive && stats.stdDevMs < (m_varianceThresholdMs * 0.5f) &&
        stats.fps >= (config.targetFrameRate * 0.95f)) {
        m_stableRecoveryCounter++;
        if (m_stableRecoveryCounter >= m_requiredRecoveryFrames) {
            m_fallbackActive = false;
            m_stableRecoveryCounter = 0;
            m_recoveryCount++;
            if (m_hasSavedPreFallback) {
                config.particleDensityScale = m_preFallbackConfig.particleDensityScale;
                config.maxActiveParticles = m_preFallbackConfig.maxActiveParticles;
                config.cameraPaddingX = m_preFallbackConfig.cameraPaddingX;
                config.cameraPaddingY = m_preFallbackConfig.cameraPaddingY;
                config.flags.enableMatrixCaching = m_preFallbackConfig.flags.enableMatrixCaching;
                config.flags.enableTriggerIndexing = m_preFallbackConfig.flags.enableTriggerIndexing;
                m_hasSavedPreFallback = false;
            }
            m_cooldownRemaining = m_cooldownFrames;
            OPTIGD_LOG_INFO("FallbackManager: Frame-time variance stabilized below threshold. Restored pre-fallback feature settings.");
        }
    } else if (m_fallbackActive) {
        m_stableRecoveryCounter = 0;
    }

    return false;
}

bool FallbackManager::checkAndApplyFeatureFallbacks(uint64_t currentFrame,
                                                    const core::FrameStats& frameStats,
                                                    const WindowStats& stats,
                                                    core::OptiConfig& config) {
    if (m_userOverride) {
        return false;
    }

    // Check if an individual optimization feature's CPU cost spiked excessively (> 1500 us)
    constexpr double kFeatureOverrunBudgetUs = 1500.0;
    if (m_cooldownRemaining == 0) {
        if (config.flags.enableMatrixCaching && frameStats.matrixTimeUs > kFeatureOverrunBudgetUs) {
            if (!m_hasSavedPreFallback) {
                m_preFallbackConfig = config;
                m_hasSavedPreFallback = true;
            }
            config.flags.enableMatrixCaching = false;
            m_fallbackActive = true;
            m_stableRecoveryCounter = 0;
            m_cooldownRemaining = m_cooldownFrames;
            std::string reason = "MatrixCache CPU time (" + std::to_string(frameStats.matrixTimeUs) +
                                 " us) exceeded safety budget";
            std::string action = "Disabled MatrixCache for dynamic transform-heavy scene";
            m_history.push_back({currentFrame, "matrix_cache", reason, action});
            OPTIGD_LOG_WARN("FallbackManager [matrix_cache]: " + reason + ". Action taken: " + action);
            return true;
        }
        if (config.flags.enableTriggerIndexing && frameStats.triggerTimeUs > kFeatureOverrunBudgetUs) {
            if (!m_hasSavedPreFallback) {
                m_preFallbackConfig = config;
                m_hasSavedPreFallback = true;
            }
            config.flags.enableTriggerIndexing = false;
            m_fallbackActive = true;
            m_stableRecoveryCounter = 0;
            m_cooldownRemaining = m_cooldownFrames;
            std::string reason = "TriggerOptimizer CPU time (" + std::to_string(frameStats.triggerTimeUs) +
                                 " us) exceeded safety budget";
            std::string action = "Disabled TriggerOptimizer lookahead indexing";
            m_history.push_back({currentFrame, "trigger_indexer", reason, action});
            OPTIGD_LOG_WARN("FallbackManager [trigger_indexer]: " + reason + ". Action taken: " + action);
            return true;
        }
    }

    return checkAndApplyFallbacks(currentFrame, stats, config);
}

void FallbackManager::reset() {
    m_cooldownRemaining = 0;
    m_stableRecoveryCounter = 0;
    m_recoveryCount = 0;
    m_fallbackActive = false;
    m_hasSavedPreFallback = false;
    m_history.clear();
}

} // namespace optigd::adaptive

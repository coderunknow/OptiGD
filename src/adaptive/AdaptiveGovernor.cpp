#include "AdaptiveGovernor.hpp"
#include "../core/Logger.hpp"
#include <numeric>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace optigd::adaptive {

AdaptiveGovernor::AdaptiveGovernor(const core::OptiConfig& initialConfig)
    : m_targetFps(initialConfig.targetFrameRate),
      m_targetFrameBudgetMs(initialConfig.targetFrameTimeMs),
      m_currentTier(initialConfig.tier),
      m_hysteresisCooldown(initialConfig.fallbackCooldownFrames) {}

void AdaptiveGovernor::setTargetFrameRate(float fps) {
    if (fps <= 0.0f) fps = 60.0f;
    m_targetFps = fps;
    m_targetFrameBudgetMs = 1000.0 / static_cast<double>(fps);
}

void AdaptiveGovernor::forceTier(core::PerformanceTier tier) {
    m_currentTier = tier;
    m_cooldownRemaining = m_hysteresisCooldown;
    OPTIGD_LOG_INFO(std::string("Manually set performance tier to: ") + core::tierToString(tier));
}

void AdaptiveGovernor::recordFrameTime(double frameTimeUs) {
    double frameTimeMs = frameTimeUs / 1000.0;
    m_frameTimesMs.push_back(frameTimeMs);
    if (m_frameTimesMs.size() > m_windowSize) {
        m_frameTimesMs.pop_front();
    }
    m_totalFrames++;
}

void AdaptiveGovernor::calculateWindowStats() {
    if (m_frameTimesMs.empty()) return;

    size_t n = m_frameTimesMs.size();
    double sum = std::accumulate(m_frameTimesMs.begin(), m_frameTimesMs.end(), 0.0);
    m_stats.meanMs = sum / static_cast<double>(n);
    m_stats.fps = m_stats.meanMs > 0.0 ? (1000.0 / m_stats.meanMs) : 0.0;

    double sqDiffSum = 0.0;
    m_stats.minMs = 1e9;
    m_stats.maxMs = 0.0;

    for (double val : m_frameTimesMs) {
        m_stats.minMs = std::min(m_stats.minMs, val);
        m_stats.maxMs = std::max(m_stats.maxMs, val);
        double diff = val - m_stats.meanMs;
        sqDiffSum += diff * diff;
    }

    m_stats.varianceMs = sqDiffSum / static_cast<double>(n);
    m_stats.stdDevMs = std::sqrt(m_stats.varianceMs);

    // Compute P95 and P99 percentiles
    std::vector<double> sortedTimes(m_frameTimesMs.begin(), m_frameTimesMs.end());
    std::sort(sortedTimes.begin(), sortedTimes.end());

    size_t idx95 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.95));
    size_t idx99 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.99));
    if (idx95 >= n) idx95 = n - 1;
    if (idx99 >= n) idx99 = n - 1;

    m_stats.p95Ms = sortedTimes[idx95];
    m_stats.p99Ms = sortedTimes[idx99];
}

void AdaptiveGovernor::update(core::OptiConfig& currentConfig) {
    auto startTime = std::chrono::steady_clock::now();

    calculateWindowStats();

    if (m_cooldownRemaining > 0) {
        m_cooldownRemaining--;
    } else if (m_dynamicEnabled && m_frameTimesMs.size() >= 30) {
        evaluateTierAdjustment(currentConfig);
    }

    auto endTime = std::chrono::steady_clock::now();
    m_lastOverheadUs = std::chrono::duration<double, std::micro>(endTime - startTime).count();
}

void AdaptiveGovernor::evaluateTierAdjustment(core::OptiConfig& config) {
    if (m_currentTier == core::PerformanceTier::Custom) {
        return; // Custom mode does not auto-scale
    }

    // Check if frame-time budget is exceeded (P95 > 105% budget or mean > budget)
    bool underBudgetPressure = (m_stats.p95Ms > m_targetFrameBudgetMs * 1.05) ||
                               (m_stats.meanMs > m_targetFrameBudgetMs);

    // Check if ample headroom exists (P99 < 75% budget and mean < 70% budget)
    bool ampleHeadroom = (m_stats.p99Ms < m_targetFrameBudgetMs * 0.75) &&
                         (m_stats.meanMs < m_targetFrameBudgetMs * 0.70);

    core::PerformanceTier newTier = m_currentTier;

    if (underBudgetPressure) {
        // Need to downgrade visual load to preserve frame stability
        if (m_currentTier == core::PerformanceTier::HighEnd) {
            newTier = core::PerformanceTier::Balanced;
        } else if (m_currentTier == core::PerformanceTier::Balanced) {
            newTier = core::PerformanceTier::LowEnd;
        }
    } else if (ampleHeadroom) {
        // Safe to upgrade fidelity without dropping frames
        if (m_currentTier == core::PerformanceTier::LowEnd) {
            newTier = core::PerformanceTier::Balanced;
        } else if (m_currentTier == core::PerformanceTier::Balanced) {
            newTier = core::PerformanceTier::HighEnd;
        }
    }

    if (newTier != m_currentTier) {
        OPTIGD_LOG_INFO(std::string("AdaptiveGovernor transitioned from ") +
                        core::tierToString(m_currentTier) + " to " +
                        core::tierToString(newTier) +
                        " (mean: " + std::to_string(m_stats.meanMs) + "ms, p95: " +
                        std::to_string(m_stats.p95Ms) + "ms, budget: " +
                        std::to_string(m_targetFrameBudgetMs) + "ms)");

        m_currentTier = newTier;
        config.tier = newTier;
        m_cooldownRemaining = m_hysteresisCooldown; // Reset hysteresis cooldown
        m_tierTransitions++;

        // Apply tier defaults
        if (newTier == core::PerformanceTier::LowEnd) {
            config.particleDensityScale = 0.5f;
            config.maxActiveParticles = 2048;
            config.cullOffscreenEmitters = true;
            config.cameraPaddingX = 120.0f;
            config.cameraPaddingY = 80.0f;
        } else if (newTier == core::PerformanceTier::Balanced) {
            config.particleDensityScale = 0.75f;
            config.maxActiveParticles = 4096;
            config.cullOffscreenEmitters = true;
            config.cameraPaddingX = 150.0f;
            config.cameraPaddingY = 100.0f;
        } else if (newTier == core::PerformanceTier::HighEnd) {
            config.particleDensityScale = 1.0f;
            config.maxActiveParticles = 8192;
            config.cullOffscreenEmitters = false;
            config.cameraPaddingX = 200.0f;
            config.cameraPaddingY = 150.0f;
        }
    }
}

} // namespace optigd::adaptive

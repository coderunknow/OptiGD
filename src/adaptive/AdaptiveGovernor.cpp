#include "AdaptiveGovernor.hpp"
#include "../core/Logger.hpp"
#include <numeric>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace optigd::adaptive {

AdaptiveGovernor::AdaptiveGovernor(const core::OptiConfig& initialConfig)
    : m_targetFps(initialConfig.targetFrameRate > 0.0f ? initialConfig.targetFrameRate : 60.0f),
      m_targetFrameBudgetMs(1000.0 / static_cast<double>(m_targetFps)),
      m_currentTier(initialConfig.tier),
      m_dynamicEnabled(initialConfig.flags.enableAdaptiveGovernor),
      m_hysteresisCooldown(initialConfig.fallbackCooldownFrames) {
    m_scratchSortBuffer.reserve(m_windowSize);
}

void AdaptiveGovernor::setTargetFrameRate(float fps) {
    if (fps <= 0.0f || !std::isfinite(fps)) fps = 60.0f;
    m_targetFps = fps;
    m_targetFrameBudgetMs = 1000.0 / static_cast<double>(fps);
}

void AdaptiveGovernor::forceTier(core::PerformanceTier tier) {
    m_currentTier = tier;
    m_cooldownRemaining = m_hysteresisCooldown;
    OPTIGD_LOG_INFO(std::string("Manually set performance tier to: ") + core::tierToString(tier));
}

void AdaptiveGovernor::clearHistory() {
    m_frameTimesMs.clear();
    m_cooldownRemaining = 0;
    m_stats = WindowStats{};
}

void AdaptiveGovernor::recordFrameTime(double frameTimeUs) {
    if (!std::isfinite(frameTimeUs) || frameTimeUs <= 0.0) {
        return;
    }
    double frameTimeMs = frameTimeUs / 1000.0;
    m_frameTimesMs.push_back(frameTimeMs);
    if (m_frameTimesMs.size() > m_windowSize) {
        m_frameTimesMs.pop_front();
    }
    m_totalFrames++;
}

void AdaptiveGovernor::calculateWindowStats(core::MemoryArena* arena) {
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

    // Use MemoryArena scratch buffer when provided to avoid per-frame heap allocation
    double* sortedPtr = nullptr;
    if (arena != nullptr) {
        sortedPtr = arena->allocateArray<double>(n);
    }
    if (sortedPtr != nullptr) {
        size_t idx = 0;
        for (double val : m_frameTimesMs) {
            sortedPtr[idx++] = val;
        }
        std::sort(sortedPtr, sortedPtr + n);
    } else {
        m_scratchSortBuffer.assign(m_frameTimesMs.begin(), m_frameTimesMs.end());
        std::sort(m_scratchSortBuffer.begin(), m_scratchSortBuffer.end());
        sortedPtr = m_scratchSortBuffer.data();
    }

    size_t idx50 = n / 2;
    size_t idx95 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.95));
    size_t idx99 = static_cast<size_t>(std::floor(static_cast<double>(n) * 0.99));
    if (idx95 >= n) idx95 = n - 1;
    if (idx99 >= n) idx99 = n - 1;

    m_stats.medianMs = (n % 2 == 0 && n >= 2)
                           ? 0.5 * (sortedPtr[idx50 - 1] + sortedPtr[idx50])
                           : sortedPtr[idx50];
    m_stats.p95Ms = sortedPtr[idx95];
    m_stats.p99Ms = sortedPtr[idx99];
}

void AdaptiveGovernor::update(core::OptiConfig& currentConfig, core::MemoryArena* arena) {
    auto startTime = std::chrono::steady_clock::now();

    if (currentConfig.targetFrameRate > 0.0f && currentConfig.targetFrameRate != m_targetFps) {
        setTargetFrameRate(currentConfig.targetFrameRate);
    }
    m_dynamicEnabled = currentConfig.flags.enableAdaptiveGovernor;
    if (currentConfig.tier == core::PerformanceTier::Custom) {
        m_currentTier = core::PerformanceTier::Custom;
    }

    calculateWindowStats(arena);

    if (m_cooldownRemaining > 0) {
        m_cooldownRemaining--;
    } else if (m_dynamicEnabled && m_frameTimesMs.size() >= 30) {
        evaluateTierAdjustment(currentConfig);
    }

    auto endTime = std::chrono::steady_clock::now();
    m_lastOverheadUs = std::chrono::duration<double, std::micro>(endTime - startTime).count();
}

void AdaptiveGovernor::evaluateTierAdjustment(core::OptiConfig& config) {
    if (m_currentTier == core::PerformanceTier::Custom ||
        config.tier == core::PerformanceTier::Custom) {
        m_currentTier = core::PerformanceTier::Custom;
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

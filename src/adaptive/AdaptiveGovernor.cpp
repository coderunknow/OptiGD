#include "AdaptiveGovernor.hpp"
#include "../core/Logger.hpp"
#include "../diagnostics/SampleWindow.hpp"
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

    // Percentiles come from the one shared implementation. This governor used to
    // index the sorted buffer at floor(n * p) while the diagnostics window and
    // Benchmark Mode use nearest-rank (rank = ceil(n * p), index = rank - 1), so
    // the same frames could produce two different "p95" numbers and the governor
    // throttled on a value no report could reproduce. The two agree whenever
    // n * p is fractional, and disagree by exactly one sample when n * p is an
    // integer - which is the common case for round windows (n = 20 or 60 or 120
    // with p = 0.95), where floor() reported the NEXT slower frame: for 20 frames
    // the "p95" was the slowest frame in the window. Both paths now call into
    // diagnostics::SampleWindow, where the definition is documented and tested.
    m_stats.medianMs = diagnostics::SampleWindow::medianFromSorted(sortedPtr, n);
    m_stats.p95Ms = diagnostics::SampleWindow::percentileFromSorted(sortedPtr, n, 0.95);
    m_stats.p99Ms = diagnostics::SampleWindow::percentileFromSorted(sortedPtr, n, 0.99);
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

        // Apply tier defaults from the shared table (core::kTierDefaults) so an
        // automatic transition produces exactly the same settings as selecting
        // the tier by hand.
        core::applyTierDefaults(config, newTier);
    }
}

} // namespace optigd::adaptive

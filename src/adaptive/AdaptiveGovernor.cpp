#include "AdaptiveGovernor.hpp"
#include "../config/Presets.hpp"
#include "../core/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

namespace optigd::adaptive {

namespace {

// Maps the three ordered tiers to indices so that "one step" is well defined.
int tierRank(core::PerformanceTier tier) {
    switch (tier) {
        case core::PerformanceTier::LowEnd: return 0;
        case core::PerformanceTier::Balanced: return 1;
        case core::PerformanceTier::HighEnd: return 2;
        case core::PerformanceTier::Custom: return -1;
    }
    return -1;
}

core::PerformanceTier tierFromRank(int rank) {
    if (rank <= 0) return core::PerformanceTier::LowEnd;
    if (rank == 1) return core::PerformanceTier::Balanced;
    return core::PerformanceTier::HighEnd;
}

} // namespace

AdaptiveGovernor::AdaptiveGovernor(const core::OptiConfig& userConfig) {
    reset(userConfig);
}

void AdaptiveGovernor::reset(const core::OptiConfig& userConfig) {
    setTargetFrameRate(userConfig.targetFrameRate);
    m_currentTier = userConfig.tier;
    m_frameIntervalsMs.clear();
    m_cooldownRemaining = 0;
    m_stats = WindowStats{};
    m_tierTransitions = 0;
}

void AdaptiveGovernor::setTargetFrameRate(float fps) {
    if (!std::isfinite(fps) || fps <= 0.0f) fps = 60.0f;
    m_targetFps = fps;
    m_targetFrameBudgetMs = 1000.0 / static_cast<double>(fps);
}

void AdaptiveGovernor::forceTier(core::PerformanceTier tier) {
    m_currentTier = tier;
    m_cooldownRemaining = m_cooldownFrames;
    OPTIGD_LOG_INFO(std::string("Adaptive governor: effective tier set to ") + core::tierToString(tier));
}

void AdaptiveGovernor::recordFrameInterval(double frameIntervalUs) {
    m_totalFrames++;
    if (!std::isfinite(frameIntervalUs) || frameIntervalUs <= 0.0) {
        return; // discontinuity (first frame, pause): not a sample
    }
    m_frameIntervalsMs.push_back(frameIntervalUs / 1000.0);
    if (m_frameIntervalsMs.size() > kWindowSize) {
        m_frameIntervalsMs.pop_front();
    }
}

void AdaptiveGovernor::calculateWindowStats() {
    const size_t n = m_frameIntervalsMs.size();
    m_stats.sampleCount = n;
    if (n == 0) return;

    const double sum = std::accumulate(m_frameIntervalsMs.begin(), m_frameIntervalsMs.end(), 0.0);
    m_stats.meanMs = sum / static_cast<double>(n);
    m_stats.fps = m_stats.meanMs > 0.0 ? (1000.0 / m_stats.meanMs) : 0.0;

    double sqDiffSum = 0.0;
    m_stats.minMs = m_frameIntervalsMs.front();
    m_stats.maxMs = m_frameIntervalsMs.front();
    for (double val : m_frameIntervalsMs) {
        m_stats.minMs = std::min(m_stats.minMs, val);
        m_stats.maxMs = std::max(m_stats.maxMs, val);
        const double diff = val - m_stats.meanMs;
        sqDiffSum += diff * diff;
    }
    m_stats.varianceMs = sqDiffSum / static_cast<double>(n);
    m_stats.stdDevMs = std::sqrt(m_stats.varianceMs);

    std::vector<double> sorted(m_frameIntervalsMs.begin(), m_frameIntervalsMs.end());
    std::sort(sorted.begin(), sorted.end());
    // Nearest-rank percentile: index = ceil(p * n) - 1.
    const auto rank = [n](double p) {
        size_t r = static_cast<size_t>(std::ceil(p * static_cast<double>(n)));
        if (r == 0) r = 1;
        return std::min(r, n) - 1;
    };
    m_stats.p95Ms = sorted[rank(0.95)];
    m_stats.p99Ms = sorted[rank(0.99)];
}

void AdaptiveGovernor::update(const core::OptiConfig& userConfig) {
    const auto startTime = std::chrono::steady_clock::now();

    calculateWindowStats();

    if (userConfig.tier == core::PerformanceTier::Custom) {
        m_currentTier = core::PerformanceTier::Custom;
    }

    if (m_cooldownRemaining > 0) {
        m_cooldownRemaining--;
    } else if (m_dynamicEnabled && m_frameIntervalsMs.size() >= kMinSamples) {
        evaluateTierAdjustment();
    }

    const auto endTime = std::chrono::steady_clock::now();
    m_lastOverheadUs = std::chrono::duration<double, std::micro>(endTime - startTime).count();
}

void AdaptiveGovernor::evaluateTierAdjustment() {
    if (m_currentTier == core::PerformanceTier::Custom) {
        return; // Custom never auto-scales
    }

    const bool underBudgetPressure = (m_stats.p95Ms > m_targetFrameBudgetMs * 1.05) ||
                                     (m_stats.meanMs > m_targetFrameBudgetMs);
    const bool ampleHeadroom = (m_stats.p99Ms < m_targetFrameBudgetMs * 0.75) &&
                               (m_stats.meanMs < m_targetFrameBudgetMs * 0.70);

    const int rank = tierRank(m_currentTier);
    int newRank = rank;
    if (underBudgetPressure) {
        newRank = std::max(0, rank - 1);       // one step toward LowEnd
    } else if (ampleHeadroom) {
        newRank = std::min(2, rank + 1);       // one step toward HighEnd
    }

    if (newRank != rank) {
        const core::PerformanceTier newTier = tierFromRank(newRank);
        OPTIGD_LOG_INFO(std::string("Adaptive governor: ") + core::tierToString(m_currentTier) + " -> " +
                        core::tierToString(newTier) +
                        " (mean " + std::to_string(m_stats.meanMs) + " ms, p95 " +
                        std::to_string(m_stats.p95Ms) + " ms, budget " +
                        std::to_string(m_targetFrameBudgetMs) + " ms)");
        m_currentTier = newTier;
        m_cooldownRemaining = m_cooldownFrames;
        m_tierTransitions++;
    }
}

core::OptiConfig AdaptiveGovernor::effectiveConfig(const core::OptiConfig& userConfig) const {
    core::OptiConfig out = userConfig;
    if (m_currentTier == core::PerformanceTier::Custom || userConfig.tier == core::PerformanceTier::Custom) {
        return out; // user tuning is authoritative for Custom
    }
    if (auto tuning = config::presetTuning(m_currentTier)) {
        config::applyTuning(out, m_currentTier, *tuning);
    }
    return out;
}

} // namespace optigd::adaptive

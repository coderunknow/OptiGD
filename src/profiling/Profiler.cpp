#include "Profiler.hpp"
#include <algorithm>

namespace optigd::profiling {

Profiler& Profiler::instance() {
    static Profiler s_instance;
    return s_instance;
}

namespace {

struct KnownMetricName {
    MetricId id;
    std::string_view name;
};

// Keep in sync with MetricId. Linear scan over a handful of entries is cheaper
// than hashing a std::string key for every sample.
constexpr KnownMetricName kKnownMetrics[] = {
    {MetricId::VisibilityCuller,  "VisibilityCuller"},
    {MetricId::ParticleGovernor,  "ParticleGovernor"},
    {MetricId::TriggerOptimizer,  "TriggerOptimizer"},
    {MetricId::MatrixCache,       "MatrixCache"},
    {MetricId::AdaptiveGovernor,  "AdaptiveGovernor"},
    {MetricId::FrameUpdateCpu,    "FrameUpdateCpu"},
    {MetricId::OptiGDOverhead,    "OptiGDOverhead"},
};

} // namespace

bool Profiler::lookupKnownMetric(std::string_view name, MetricId& outId) {
    for (const auto& entry : kKnownMetrics) {
        if (entry.name == name) {
            outId = entry.id;
            return true;
        }
    }
    return false;
}

void Profiler::recordById(MetricId id, double durationUs) {
    if (!m_enabled) return;

    const size_t index = static_cast<size_t>(id);
    if (index >= m_fastMetrics.size()) return;

    ProfileMetric& metric = m_fastMetrics[index];
    if (metric.callCount == 0) {
        metric.minTimeUs = durationUs;
        metric.maxTimeUs = durationUs;
    } else {
        metric.minTimeUs = std::min(metric.minTimeUs, durationUs);
        metric.maxTimeUs = std::max(metric.maxTimeUs, durationUs);
    }
    metric.callCount++;
    metric.totalTimeUs += durationUs;
}

ProfileMetric Profiler::getById(MetricId id) const {
    const size_t index = static_cast<size_t>(id);
    if (index >= m_fastMetrics.size()) {
        return ProfileMetric{std::string("invalid"), 0, 0.0, 0.0, 0.0};
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    ProfileMetric metric = m_fastMetrics[index];
    metric.name = kKnownMetrics[index].name;
    return metric;
}

void Profiler::record(std::string_view name, double durationUs) {
    if (!m_enabled) return;

    MetricId id{};
    if (lookupKnownMetric(name, id)) {
        recordById(id, durationUs);
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_metrics.find(std::string(name));
    if (it == m_metrics.end()) {
        ProfileMetric metric;
        metric.name = std::string(name);
        metric.callCount = 1;
        metric.totalTimeUs = durationUs;
        metric.minTimeUs = durationUs;
        metric.maxTimeUs = durationUs;
        m_metrics.emplace(std::string(name), metric);
    } else {
        auto& metric = it->second;
        metric.callCount++;
        metric.totalTimeUs += durationUs;
        metric.minTimeUs = std::min(metric.minTimeUs, durationUs);
        metric.maxTimeUs = std::max(metric.maxTimeUs, durationUs);
    }
}

void Profiler::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.clear();
    for (auto& metric : m_fastMetrics) {
        metric = ProfileMetric{};
    }
}

std::vector<ProfileMetric> Profiler::getSnapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ProfileMetric> result;
    result.reserve(m_metrics.size() + m_fastMetrics.size());
    for (size_t i = 0; i < m_fastMetrics.size(); ++i) {
        if (m_fastMetrics[i].callCount == 0) continue;
        ProfileMetric metric = m_fastMetrics[i];
        metric.name = kKnownMetrics[i].name;
        result.push_back(metric);
    }
    for (const auto& [_, metric] : m_metrics) {
        result.push_back(metric);
    }
    return result;
}

ProfileMetric Profiler::getMetric(std::string_view name) const {
    MetricId id{};
    if (lookupKnownMetric(name, id)) {
        return getById(id);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_metrics.find(std::string(name));
    if (it != m_metrics.end()) {
        return it->second;
    }
    return ProfileMetric{std::string(name), 0, 0.0, 0.0, 0.0};
}

} // namespace optigd::profiling

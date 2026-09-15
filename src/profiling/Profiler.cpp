#include "Profiler.hpp"
#include <algorithm>

namespace optigd::profiling {

Profiler& Profiler::instance() {
    static Profiler s_instance;
    return s_instance;
}

void Profiler::record(std::string_view name, double durationUs) {
    if (!m_enabled) return;

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
}

std::vector<ProfileMetric> Profiler::getSnapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ProfileMetric> result;
    result.reserve(m_metrics.size());
    for (const auto& [_, metric] : m_metrics) {
        result.push_back(metric);
    }
    return result;
}

ProfileMetric Profiler::getMetric(std::string_view name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_metrics.find(std::string(name));
    if (it != m_metrics.end()) {
        return it->second;
    }
    return ProfileMetric{std::string(name), 0, 0.0, 0.0, 0.0};
}

} // namespace optigd::profiling

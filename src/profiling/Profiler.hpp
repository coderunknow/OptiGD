#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <array>
#include <chrono>
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace optigd::profiling {

struct ProfileMetric {
    std::string name;
    uint64_t callCount{0};
    double totalTimeUs{0.0};
    double minTimeUs{1e9};
    double maxTimeUs{0.0};

    double getMeanTimeUs() const {
        return callCount > 0 ? (totalTimeUs / static_cast<double>(callCount)) : 0.0;
    }
};

// Metrics recorded on the hot path. Recording by id touches a plain array slot
// instead of taking a mutex and materialising a std::string key.
enum class MetricId : uint16_t {
    VisibilityCuller = 0,
    ParticleGovernor,
    TriggerOptimizer,
    MatrixCache,
    AdaptiveGovernor,
    FrameUpdateCpu,
    OptiGDOverhead,

    Count
};

class Profiler {
public:
    static Profiler& instance();

    // Zero-allocation, lock-free fast path.
    void recordById(MetricId id, double durationUs);
    ProfileMetric getById(MetricId id) const;

    // Name based lookup. Known names resolve to a MetricId through a small
    // static table (no allocation, no lock); unknown names fall back to a
    // mutex guarded overflow map and are intended for ad-hoc instrumentation
    // only, never for the per-frame path.
    void record(std::string_view name, double durationUs);
    void reset();

    std::vector<ProfileMetric> getSnapshot() const;
    ProfileMetric getMetric(std::string_view name) const;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    Profiler() = default;
    mutable std::mutex m_mutex;

    // Indexed by MetricId. Plain (non-atomic) doubles: the game loop that
    // records these is single threaded, and readers only sample the values for
    // display. Making this lock-free is what removes the per-record() mutex.
    std::array<ProfileMetric, static_cast<size_t>(MetricId::Count)> m_fastMetrics{};
    std::unordered_map<std::string, ProfileMetric> m_metrics;
    bool m_enabled{true};

    static bool lookupKnownMetric(std::string_view name, MetricId& outId);
};

class ProfileScope {
public:
    explicit ProfileScope(std::string_view name)
        : m_name(name), m_start(std::chrono::steady_clock::now()) {}

    ~ProfileScope() {
        if (Profiler::instance().isEnabled()) {
            auto end = std::chrono::steady_clock::now();
            double durationUs = std::chrono::duration<double, std::micro>(end - m_start).count();
            Profiler::instance().record(m_name, durationUs);
        }
    }

private:
    std::string_view m_name;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace optigd::profiling

#define OPTIGD_PROFILE_SCOPE(name) optigd::profiling::ProfileScope optigd_scope_##__LINE__(name)

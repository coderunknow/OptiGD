#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
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

// Transparent hash so lookups by std::string_view do not allocate a std::string.
struct StringViewHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

class Profiler {
public:
    static Profiler& instance();

    void record(std::string_view name, double durationUs);
    void reset();

    std::vector<ProfileMetric> getSnapshot() const;
    ProfileMetric getMetric(std::string_view name) const;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    Profiler() = default;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, ProfileMetric, StringViewHash, std::equal_to<>> m_metrics;
    bool m_enabled{true};
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

// Two-level paste so __LINE__ expands before concatenation (a direct ## would produce the
// literal identifier optigd_scope___LINE__ and collide across scopes).
#define OPTIGD_PROFILE_CONCAT_INNER(a, b) a##b
#define OPTIGD_PROFILE_CONCAT(a, b) OPTIGD_PROFILE_CONCAT_INNER(a, b)
// name must be a string literal: ProfileScope stores a view, not a copy.
#define OPTIGD_PROFILE_SCOPE(name) \
    optigd::profiling::ProfileScope OPTIGD_PROFILE_CONCAT(optigd_scope_, __LINE__)(name)

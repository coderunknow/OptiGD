#pragma once

#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>

namespace optigd::core {

struct Point2D {
    float x{0.0f};
    float y{0.0f};

    constexpr Point2D() = default;
    constexpr Point2D(float x_, float y_) : x(x_), y(y_) {}

    constexpr Point2D operator+(const Point2D& o) const { return {x + o.x, y + o.y}; }
    constexpr Point2D operator-(const Point2D& o) const { return {x - o.x, y - o.y}; }
    constexpr Point2D operator*(float s) const { return {x * s, y * s}; }
    constexpr bool operator==(const Point2D& o) const { return x == o.x && y == o.y; }
};

struct Size2D {
    float width{0.0f};
    float height{0.0f};

    constexpr Size2D() = default;
    constexpr Size2D(float w, float h) : width(w), height(h) {}
};

struct Rect2D {
    Point2D origin{};
    Size2D size{};

    constexpr Rect2D() = default;
    constexpr Rect2D(float x, float y, float w, float h) : origin(x, y), size(w, h) {}
    constexpr Rect2D(const Point2D& orig, const Size2D& sz) : origin(orig), size(sz) {}

    constexpr float minX() const { return origin.x; }
    constexpr float maxX() const { return origin.x + size.width; }
    constexpr float minY() const { return origin.y; }
    constexpr float maxY() const { return origin.y + size.height; }

    constexpr bool intersects(const Rect2D& o) const {
        return !(maxX() < o.minX() || minX() > o.maxX() ||
                 maxY() < o.minY() || minY() > o.maxY());
    }

    constexpr bool contains(const Point2D& pt) const {
        return pt.x >= minX() && pt.x <= maxX() &&
               pt.y >= minY() && pt.y <= maxY();
    }

    constexpr Rect2D expanded(float padX, float padY) const {
        return Rect2D(origin.x - padX, origin.y - padY,
                      size.width + padX * 2.0f, size.height + padY * 2.0f);
    }
};

enum class PerformanceTier : uint8_t {
    LowEnd = 0,     // High performance mode (aggressive culling, reduced particles)
    Balanced = 1,   // Balanced mode (default visual fidelity with smooth pacing)
    HighEnd = 2,    // Maximum visual fidelity, minimal throttling
    Custom = 3      // Custom user configuration
};

inline const char* tierToString(PerformanceTier tier) {
    switch (tier) {
        case PerformanceTier::LowEnd: return "LowEnd (High Performance)";
        case PerformanceTier::Balanced: return "Balanced";
        case PerformanceTier::HighEnd: return "HighEnd";
        case PerformanceTier::Custom: return "Custom";
        default: return "Unknown";
    }
}

struct OptimizationFlags {
    bool enableSpatialCulling{true};
    bool enableParticleThrottling{true};
    bool enableTriggerIndexing{true};
    bool enableMatrixCaching{true};
    bool enableAdaptiveGovernor{true};
    bool enableMemoryArena{true};
    bool enableDiagnosticsOverlay{false};
};

struct OptiConfig {
    PerformanceTier tier{PerformanceTier::LowEnd}; // LowEnd by default per user configuration
    OptimizationFlags flags{};

    // Spatial hash parameters
    float spatialCellSize{128.0f};
    float cameraPaddingX{150.0f};
    float cameraPaddingY{100.0f};

    // Particle throttling parameters
    float particleDensityScale{0.5f}; // 50% max density in LowEnd mode
    uint32_t maxActiveParticles{2048};
    bool cullOffscreenEmitters{true};

    // Trigger optimization parameters
    bool sortTriggersByPosition{true};
    float triggerLookaheadDistance{800.0f};

    // Adaptive Governor parameters
    float targetFrameRate{60.0f};
    float targetFrameTimeMs{16.666f};
    float fallbackVarianceThresholdMs{4.0f};
    uint32_t fallbackCooldownFrames{120}; // Hysteresis frames before re-evaluating
    float maxGovernorOverheadBudgetUs{50.0f}; // Microseconds limit for governor itself

    // Memory arena parameters
    size_t arenaInitialCapacity{1024 * 1024}; // 1 MB scratch arena
};

struct FrameStats {
    uint64_t frameIndex{0};
    double totalFrameTimeUs{0.0};
    double cullingTimeUs{0.0};
    double particleTimeUs{0.0};
    double triggerTimeUs{0.0};
    double matrixTimeUs{0.0};
    double governorOverheadUs{0.0};

    uint32_t totalObjects{0};
    uint32_t visibleObjects{0};
    uint32_t culledObjects{0};
    uint32_t totalParticles{0};
    uint32_t culledParticles{0};
    uint32_t totalTriggers{0};
    uint32_t evaluatedTriggers{0};

    size_t arenaAllocatedBytes{0};
    size_t heapAllocationsAvoided{0};
};

} // namespace optigd::core

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

// Axis-aligned rectangle. origin is the minimum corner for normalized rectangles;
// SpatialHash normalizes negative sizes before use.
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

    // Inclusive intersection test (touching edges intersect).
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

// Performance tiers. LowEnd is the most aggressive setting, HighEnd the least.
// Custom uses the user's own tuning values and is never changed automatically.
enum class PerformanceTier : uint8_t {
    LowEnd = 0,
    Balanced = 1,
    HighEnd = 2,
    Custom = 3
};

inline const char* tierToString(PerformanceTier tier) {
    switch (tier) {
        case PerformanceTier::LowEnd: return "LowEnd";
        case PerformanceTier::Balanced: return "Balanced";
        case PerformanceTier::HighEnd: return "HighEnd";
        case PerformanceTier::Custom: return "Custom";
        default: return "Unknown";
    }
}

// Per-feature switches. Defaults reflect the evidence available for each feature
// (see EVIDENCE_REPORT.md). A feature set to true whose status is not VERIFIED is
// still only an experiment; the status column documents what has been checked.
struct OptimizationFlags {
    bool enableSpatialCulling{false};      // shadow measurement only in v0.1.0-beta1 (no gameplay effect)
    bool enableParticleThrottling{false};  // not connected to gameplay in v0.1.0-beta1
    bool enableTriggerIndexing{false};     // not connected to gameplay in v0.1.0-beta1
    bool enableMatrixCaching{false};       // not connected to gameplay in v0.1.0-beta1
    bool enableAdaptiveGovernor{true};     // adjusts effective runtime tuning only
    bool enableDiagnosticsOverlay{false};  // in-game label
    bool enableFrameTimeRecorder{false};   // local CSV of frame timings
};

struct OptiConfig {
    PerformanceTier tier{PerformanceTier::LowEnd};
    OptimizationFlags flags{};

    // Spatial culling (shadow measurement) parameters
    float spatialCellSize{128.0f};
    float cameraPaddingX{120.0f};
    float cameraPaddingY{80.0f};

    // Particle parameters (used only when particle throttling is connected)
    float particleDensityScale{0.5f};
    uint32_t maxActiveParticles{2048};
    bool cullOffscreenEmitters{true};

    // Trigger parameters (used only when trigger indexing is connected)
    float triggerLookaheadDistance{800.0f};

    // Frame pacing
    float targetFrameRate{60.0f};
    float targetFrameTimeMs{1000.0f / 60.0f};
};

// Per-frame measurements. Every field has the exact meaning given here.
struct FrameStats {
    uint64_t frameIndex{0};
    // Wall-clock time between the start of this game frame and the previous one.
    // Includes rendering and any other work done between frames. 0 when there is no
    // valid previous frame (first frame, or a discontinuity such as a pause).
    double frameIntervalUs{0.0};
    // CPU time spent inside the original PlayLayer::postUpdate (game logic on the CPU thread).
    double gameUpdateUs{0.0};
    // CPU time spent in OptiGD code during this frame (excludes the original update).
    double optigdOverheadUs{0.0};
    // CPU time spent in the spatial-culling shadow measurement (subset of optigdOverheadUs).
    double cullingShadowUs{0.0};

    // Shadow measurement: objects registered with the culler and how many of them
    // lie outside the padded camera rectangle. No object is hidden by this.
    uint32_t totalObjects{0};
    uint32_t shadowVisibleObjects{0};
    uint32_t shadowCulledObjects{0};

    // Number of MemoryArena allocations during the frame (0 when the arena is unused).
    size_t arenaAllocations{0};
};

} // namespace optigd::core

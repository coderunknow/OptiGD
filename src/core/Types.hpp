#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <cmath>
#include <algorithm>

namespace optigd::core {

// NOTE: The version tag MUST satisfy Geode 5.10.1's VersionInfo grammar
// (`vMAJOR.MINOR.PATCH[-alpha|-beta|-prerelease][.N]`). A digit directly
// attached to the tag (e.g. `v0.1.0-beta1`) is REJECTED by the loader and
// surfaces only as "unexpected type string". See GeodeVersion.hpp for the
// full root-cause analysis and the regression test in tests/UnitTests.cpp.
inline constexpr const char* kOptiGDVersion = "0.1.0-beta.2";
inline constexpr const char* kOptiGDVersionTag = "v0.1.0-beta.2";
inline constexpr const char* kTargetGDVersion = "2.2081";
inline constexpr const char* kTargetGeodeVersion = "v5.10.1";

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

    constexpr float minX() const {
        return size.width >= 0.0f ? origin.x : (origin.x + size.width);
    }
    constexpr float maxX() const {
        return size.width >= 0.0f ? (origin.x + size.width) : origin.x;
    }
    constexpr float minY() const {
        return size.height >= 0.0f ? origin.y : (origin.y + size.height);
    }
    constexpr float maxY() const {
        return size.height >= 0.0f ? (origin.y + size.height) : origin.y;
    }

    Rect2D normalized() const {
        float ox = std::isfinite(origin.x) ? origin.x : 0.0f;
        float oy = std::isfinite(origin.y) ? origin.y : 0.0f;
        float w = std::isfinite(size.width) ? size.width : 0.0f;
        float h = std::isfinite(size.height) ? size.height : 0.0f;
        if (w < 0.0f) {
            ox += w;
            w = -w;
        }
        if (h < 0.0f) {
            oy += h;
            h = -h;
        }
        return Rect2D(ox, oy, w, h);
    }

    constexpr bool intersects(const Rect2D& o) const {
        return !(maxX() < o.minX() || minX() > o.maxX() ||
                 maxY() < o.minY() || minY() > o.maxY());
    }

    constexpr bool contains(const Point2D& pt) const {
        return pt.x >= minX() && pt.x <= maxX() &&
               pt.y >= minY() && pt.y <= maxY();
    }

    constexpr Rect2D expanded(float padX, float padY) const {
        return Rect2D(minX() - padX, minY() - padY,
                      (maxX() - minX()) + padX * 2.0f,
                      (maxY() - minY()) + padY * 2.0f);
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

// The one and only tier defaults table.
//
// `ConfigManager::applyTierPresetUnlocked()` and
// `AdaptiveGovernor::evaluateTierAdjustment()` used to carry independent copies
// of these numbers, which could drift apart and make an automatic tier
// transition apply different settings than the same tier selected by hand.
// Both now go through this table; `Custom` is a no-op so user values are kept.
struct TierDefaults {
    float particleDensityScale;
    uint32_t maxActiveParticles;
    bool cullOffscreenEmitters;
    float cameraPaddingX;
    float cameraPaddingY;
};

constexpr TierDefaults kTierDefaults[] = {
    /* LowEnd   */ {0.5f,  2048, true,  120.0f, 80.0f},
    /* Balanced */ {0.75f, 4096, true,  150.0f, 100.0f},
    /* HighEnd  */ {1.0f,  8192, false, 200.0f, 150.0f},
    /* Custom   */ {1.0f,  8192, false, 200.0f, 150.0f},
};

// Applies the table entry for `tier` to a config. `Custom` is intentionally a
// no-op: custom tiers preserve whatever the user configured.
template <typename ConfigT>
inline void applyTierDefaults(ConfigT& config, PerformanceTier tier) {
    if (tier == PerformanceTier::Custom) {
        return;
    }
    const TierDefaults& defaults = kTierDefaults[static_cast<size_t>(tier)];
    config.particleDensityScale = defaults.particleDensityScale;
    config.maxActiveParticles = defaults.maxActiveParticles;
    config.cullOffscreenEmitters = defaults.cullOffscreenEmitters;
    config.cameraPaddingX = defaults.cameraPaddingX;
    config.cameraPaddingY = defaults.cameraPaddingY;
}

struct OptimizationFlags {
    bool enableSpatialCulling{true};
    bool enableParticleThrottling{true};
    bool enableTriggerIndexing{false};
    bool enableMatrixCaching{true};
    bool enableAdaptiveGovernor{true};
    bool enableMemoryArena{true};
    bool enableDiagnosticsOverlay{false};
    bool exportDiagnosticsOnExit{true};
};

struct OptiConfig {
    PerformanceTier tier{PerformanceTier::LowEnd}; // LowEnd by default per user configuration
    OptimizationFlags flags{};

    // Spatial hash parameters
    float spatialCellSize{128.0f};
    float cameraPaddingX{120.0f};
    float cameraPaddingY{80.0f};

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
    double updateCpuTimeUs{0.0};
    double optigdOverheadUs{0.0};
    double cullingTimeUs{0.0};
    double particleTimeUs{0.0};
    double triggerTimeUs{0.0};
    double matrixTimeUs{0.0};
    double governorTimeUs{0.0};
    double governorOverheadUs{0.0};

    uint32_t totalObjects{0};
    uint32_t visibleObjects{0};
    uint32_t culledObjects{0};
    uint32_t newlyVisibleObjects{0};
    uint32_t newlyCulledObjects{0};

    uint32_t activeParticles{0};
    uint32_t totalParticles{0};
    uint32_t culledParticles{0};
    uint32_t activeEmitters{0};
    uint32_t sleepingEmitters{0};

    uint32_t totalTriggers{0};
    uint32_t evaluatedTriggers{0};
    uint32_t executedTriggers{0};

    uint64_t matrixCacheHits{0};
    uint64_t matrixCacheMisses{0};

    size_t arenaUsedBytes{0};
    size_t arenaPeakBytes{0};
    uint32_t arenaAllocations{0};
    size_t arenaAllocatedBytes{0};
    size_t arenaAllocationCount{0};
    uint32_t heapAllocationsAvoided{0};
};

} // namespace optigd::core

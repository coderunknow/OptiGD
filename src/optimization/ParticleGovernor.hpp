#pragma once

#include "../core/Types.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace optigd::optimization {

enum class ParticlePriority : uint8_t {
    Cosmetic = 0,   // Background dust, ambient embers, decorative bursts
    Standard = 1,   // General obstacle/hazard effects
    Critical = 2    // Player trail, portal glow, pads: never slept, never throttled
};

struct EmitterDecision {
    bool active{true};            // emitter should run this frame
    float emissionMultiplier{1.0f}; // scale for the emission rate while active (1.0 = unchanged)
    bool justSlept{false};        // transitioned awake -> asleep on this evaluation
    bool justWoke{false};         // transitioned asleep -> awake on this evaluation
};

// Decides, per emitter, whether it runs and at what emission density.
//
// This module is pure policy: it does not touch particle systems. A caller must
// apply the decision (pause/resume emission, scale the rate). Known requirements
// for the caller are documented in docs/ARCHITECTURE.md (particle governor).
//
// Hysteresis: an awake emitter sleeps only after it is outside the camera rectangle
// expanded by sleepMarginPx; a sleeping emitter wakes once it is inside the camera
// rectangle expanded by wakeMarginPx (< sleepMarginPx). Emitters near the boundary
// therefore do not flap.
//
// Waking grants no catch-up: the multiplier after a wake equals the normal density.
class ParticleGovernor {
public:
    using Handle = uint32_t;
    static constexpr Handle kInvalidHandle = 0xFFFFFFFFu;

    explicit ParticleGovernor(float densityScale = 0.5f, uint32_t globalMax = 2048);

    void setDensityScale(float scale);
    float getDensityScale() const { return m_densityScale; }

    void setGlobalParticleLimit(uint32_t limit);
    uint32_t getGlobalParticleLimit() const { return m_globalMaxParticles; }

    void setCullOffscreen(bool cull) { m_cullOffscreen = cull; }
    bool isCullOffscreen() const { return m_cullOffscreen; }

    void setHysteresis(float sleepMarginPx, float wakeMarginPx);
    float getSleepMargin() const { return m_sleepMargin; }
    float getWakeMargin() const { return m_wakeMargin; }

    void clear();
    Handle registerEmitter(const core::Point2D& pos, float radius, ParticlePriority prio);
    bool unregisterEmitter(Handle handle);

    EmitterDecision evaluateEmitter(Handle handle, const core::Rect2D& cameraRect);

    // Records the emitter's live particle count. Totals are updated incrementally.
    void recordActiveParticles(Handle handle, uint32_t count);

    size_t getTrackedEmitterCount() const { return m_liveEmitters; }
    uint32_t getTotalActiveParticles() const { return m_awakeParticles; }
    uint32_t getSleepingEmitterCount() const { return m_sleepingEmitters; }
    uint64_t getSleepTransitions() const { return m_sleepTransitions; }
    uint64_t getWakeTransitions() const { return m_wakeTransitions; }

private:
    struct Emitter {
        core::Point2D position{};
        float radius{64.0f};
        ParticlePriority priority{ParticlePriority::Cosmetic};
        uint32_t activeParticles{0};
        bool sleeping{false};
        bool alive{false};
    };

    float m_densityScale{0.5f};
    uint32_t m_globalMaxParticles{2048};
    bool m_cullOffscreen{true};
    float m_sleepMargin{240.0f};
    float m_wakeMargin{120.0f};

    std::vector<Emitter> m_emitters;
    size_t m_liveEmitters{0};
    uint32_t m_awakeParticles{0};
    uint32_t m_sleepingEmitters{0};
    uint64_t m_sleepTransitions{0};
    uint64_t m_wakeTransitions{0};
};

} // namespace optigd::optimization

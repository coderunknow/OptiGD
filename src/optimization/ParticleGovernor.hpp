#pragma once

#include "../core/Types.hpp"
#include <vector>
#include <cstdint>

namespace optigd::optimization {

enum class ParticlePriority : uint8_t {
    Cosmetic = 0,    // Background dust, ambient embers, decorative bursts
    Standard = 1,    // General obstacle/hazard effects
    Critical = 2     // Player icon trail, gravity portal glow, jump pads
};

struct ParticleEmitterInfo {
    uint32_t id{0};
    core::Point2D position{};
    float emitterRadius{64.0f};
    ParticlePriority priority{ParticlePriority::Cosmetic};
    uint32_t activeParticleCount{0};
    uint32_t maxAllowedParticles{500};
    bool isSleeping{false};
};

class ParticleGovernor {
public:
    explicit ParticleGovernor(float densityScale = 0.5f, uint32_t globalMax = 2048);

    void setDensityScale(float scale);
    float getDensityScale() const { return m_densityScale; }

    void setGlobalParticleLimit(uint32_t limit);
    uint32_t getGlobalParticleLimit() const { return m_globalMaxParticles; }

    void setCullOffscreen(bool cull) { m_cullOffscreen = cull; }
    bool isCullOffscreen() const { return m_cullOffscreen; }

    void clear();
    uint32_t registerEmitter(uint32_t id, const core::Point2D& pos, float radius, ParticlePriority prio);

    // Determines if an emitter should update & render, and gives scaled emission rate
    bool evaluateEmitter(uint32_t id, const core::Rect2D& cameraRect, float& outEmissionMultiplier);

    void recordActiveParticles(uint32_t emitterId, uint32_t count);

    size_t getTrackedEmitterCount() const { return m_emitters.size(); }
    uint32_t getTotalActiveParticles() const { return m_currentTotalParticles; }
    uint32_t getLastCulledEmitterCount() const { return m_lastCulledEmitters; }

private:
    float m_densityScale{0.5f};
    uint32_t m_globalMaxParticles{2048};
    bool m_cullOffscreen{true};

    std::vector<ParticleEmitterInfo> m_emitters;
    uint32_t m_currentTotalParticles{0};
    uint32_t m_lastCulledEmitters{0};
};

} // namespace optigd::optimization

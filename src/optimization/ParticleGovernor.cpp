#include "ParticleGovernor.hpp"
#include <algorithm>

namespace optigd::optimization {

ParticleGovernor::ParticleGovernor(float densityScale, uint32_t globalMax)
    : m_densityScale(densityScale), m_globalMaxParticles(globalMax) {}

void ParticleGovernor::setDensityScale(float scale) {
    m_densityScale = std::clamp(scale, 0.05f, 1.0f);
}

void ParticleGovernor::setGlobalParticleLimit(uint32_t limit) {
    m_globalMaxParticles = std::max<uint32_t>(limit, 128);
}

void ParticleGovernor::clear() {
    m_emitters.clear();
    m_currentTotalParticles = 0;
    m_lastCulledEmitters = 0;
}

uint32_t ParticleGovernor::registerEmitter(uint32_t id, const core::Point2D& pos, float radius, ParticlePriority prio) {
    m_emitters.push_back({id, pos, radius, prio, 0, 500, false});
    return id;
}

bool ParticleGovernor::evaluateEmitter(uint32_t id, const core::Rect2D& cameraRect, float& outEmissionMultiplier) {
    if (id >= m_emitters.size()) {
        outEmissionMultiplier = 1.0f;
        return true;
    }

    auto& emitter = m_emitters[id];

    // Critical particles always update regardless of offscreen position (e.g. player trail)
    if (emitter.priority == ParticlePriority::Critical) {
        outEmissionMultiplier = 1.0f;
        emitter.isSleeping = false;
        return true;
    }

    // Check if emitter bounding circle/rect intersects camera viewport (+ 100 margin)
    if (m_cullOffscreen) {
        core::Rect2D emitterBounds(
            emitter.position.x - emitter.emitterRadius,
            emitter.position.y - emitter.emitterRadius,
            emitter.emitterRadius * 2.0f,
            emitter.emitterRadius * 2.0f
        );

        core::Rect2D paddedCamera = cameraRect.expanded(120.0f, 120.0f);
        if (!paddedCamera.intersects(emitterBounds)) {
            // Offscreen: sleep emitter
            emitter.isSleeping = true;
            outEmissionMultiplier = 0.0f;
            return false;
        }
    }

    emitter.isSleeping = false;

    // Apply density scaling
    float multiplier = m_densityScale;

    // If approaching global particle budget, aggressively throttle cosmetic particles
    if (m_currentTotalParticles > m_globalMaxParticles) {
        if (emitter.priority == ParticlePriority::Cosmetic) {
            multiplier *= 0.25f;
        } else {
            multiplier *= 0.5f;
        }
    }

    outEmissionMultiplier = multiplier;
    return true;
}

void ParticleGovernor::recordActiveParticles(uint32_t emitterId, uint32_t count) {
    if (emitterId < m_emitters.size()) {
        m_emitters[emitterId].activeParticleCount = count;
    }

    // Recalculate total
    uint32_t total = 0;
    uint32_t culled = 0;
    for (const auto& em : m_emitters) {
        if (em.isSleeping) {
            culled++;
        } else {
            total += em.activeParticleCount;
        }
    }
    m_currentTotalParticles = total;
    m_lastCulledEmitters = culled;
}

} // namespace optigd::optimization

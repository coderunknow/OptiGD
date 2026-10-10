#include "ParticleGovernor.hpp"

#include <algorithm>
#include <cmath>

namespace optigd::optimization {

namespace {

bool emitterIntersects(const core::Point2D& center, float radius, const core::Rect2D& rect) {
    const core::Rect2D bounds(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
    return rect.intersects(bounds);
}

} // namespace

ParticleGovernor::ParticleGovernor(float densityScale, uint32_t globalMax)
    : m_densityScale(std::clamp(densityScale, 0.05f, 1.0f)),
      m_globalMaxParticles(std::max<uint32_t>(globalMax, 128)) {}

void ParticleGovernor::setDensityScale(float scale) {
    m_densityScale = std::isfinite(scale) ? std::clamp(scale, 0.05f, 1.0f) : 1.0f;
}

void ParticleGovernor::setGlobalParticleLimit(uint32_t limit) {
    m_globalMaxParticles = std::max<uint32_t>(limit, 128);
}

void ParticleGovernor::setHysteresis(float sleepMarginPx, float wakeMarginPx) {
    if (!std::isfinite(sleepMarginPx) || !std::isfinite(wakeMarginPx)) return;
    m_sleepMargin = std::max(0.0f, sleepMarginPx);
    // wake margin must not exceed sleep margin, otherwise the hysteresis band inverts.
    m_wakeMargin = std::clamp(wakeMarginPx, 0.0f, m_sleepMargin);
}

void ParticleGovernor::clear() {
    m_emitters.clear();
    m_liveEmitters = 0;
    m_awakeParticles = 0;
    m_sleepingEmitters = 0;
    m_sleepTransitions = 0;
    m_wakeTransitions = 0;
}

ParticleGovernor::Handle ParticleGovernor::registerEmitter(const core::Point2D& pos, float radius, ParticlePriority prio) {
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(radius) || radius < 0.0f) {
        return kInvalidHandle;
    }
    Emitter e;
    e.position = pos;
    e.radius = radius;
    e.priority = prio;
    e.alive = true;
    m_emitters.push_back(e);
    m_liveEmitters++;
    return static_cast<Handle>(m_emitters.size() - 1);
}

bool ParticleGovernor::unregisterEmitter(Handle handle) {
    if (handle >= m_emitters.size() || !m_emitters[handle].alive) return false;
    Emitter& e = m_emitters[handle];
    if (e.sleeping) {
        m_sleepingEmitters--;
    } else {
        m_awakeParticles -= e.activeParticles;
    }
    e.alive = false;
    e.activeParticles = 0;
    e.sleeping = false;
    m_liveEmitters--;
    return true;
}

EmitterDecision ParticleGovernor::evaluateEmitter(Handle handle, const core::Rect2D& cameraRect) {
    EmitterDecision decision;
    if (handle >= m_emitters.size() || !m_emitters[handle].alive) {
        return decision; // unknown emitter: leave it alone
    }
    Emitter& e = m_emitters[handle];

    // Critical emitters always run at full density.
    if (e.priority == ParticlePriority::Critical || !m_cullOffscreen) {
        if (e.sleeping) {
            e.sleeping = false;
            m_sleepingEmitters--;
            m_awakeParticles += e.activeParticles;
            m_wakeTransitions++;
            decision.justWoke = true;
        }
        decision.active = true;
        decision.emissionMultiplier = (e.priority == ParticlePriority::Critical) ? 1.0f : m_densityScale;
        return decision;
    }

    if (!e.sleeping) {
        // Awake: sleep only when clearly outside the camera.
        if (!emitterIntersects(e.position, e.radius, cameraRect.expanded(m_sleepMargin, m_sleepMargin))) {
            e.sleeping = true;
            m_awakeParticles -= e.activeParticles;
            m_sleepingEmitters++;
            m_sleepTransitions++;
            decision.justSlept = true;
        }
    } else {
        // Asleep: wake only once inside the (smaller) wake margin.
        if (emitterIntersects(e.position, e.radius, cameraRect.expanded(m_wakeMargin, m_wakeMargin))) {
            e.sleeping = false;
            m_sleepingEmitters--;
            m_awakeParticles += e.activeParticles;
            m_wakeTransitions++;
            decision.justWoke = true;
        }
    }

    if (e.sleeping) {
        decision.active = false;
        decision.emissionMultiplier = 0.0f;
        return decision;
    }

    float multiplier = m_densityScale;
    if (m_awakeParticles > m_globalMaxParticles) {
        // Over budget: throttle cosmetic emitters harder than standard ones.
        multiplier *= (e.priority == ParticlePriority::Cosmetic) ? 0.25f : 0.5f;
    }
    decision.active = true;
    decision.emissionMultiplier = multiplier;
    return decision;
}

void ParticleGovernor::recordActiveParticles(Handle handle, uint32_t count) {
    if (handle >= m_emitters.size() || !m_emitters[handle].alive) return;
    Emitter& e = m_emitters[handle];
    if (!e.sleeping) {
        m_awakeParticles -= e.activeParticles;
        m_awakeParticles += count;
    }
    e.activeParticles = count;
}

} // namespace optigd::optimization

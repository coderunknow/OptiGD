#include "ParticleGovernor.hpp"
#include <algorithm>
#include <cmath>

namespace optigd::optimization {

ParticleGovernor::ParticleGovernor(float densityScale, uint32_t globalMax)
    : m_densityScale(std::clamp(densityScale, 0.05f, 1.0f)),
      m_globalMaxParticles(std::max<uint32_t>(globalMax, 128)) {}

void ParticleGovernor::setDensityScale(float scale) {
    if (!std::isfinite(scale)) scale = 0.5f;
    m_densityScale = std::clamp(scale, 0.05f, 1.0f);
}

void ParticleGovernor::setGlobalParticleLimit(uint32_t limit) {
    m_globalMaxParticles = std::max<uint32_t>(limit, 128);
}

void ParticleGovernor::clear() {
    m_emitters.clear();
    m_idToIndex.clear();
    m_ptrToIndex.clear();
    m_activeEmitterCount = 0;
    m_currentTotalParticles = 0;
    m_lastCulledEmitters = 0;
    m_lastAvoidedParticles = 0;
}

uint32_t ParticleGovernor::registerEmitter(uint32_t id, const core::Point2D& pos, float radius,
                                           ParticlePriority prio, void* nativePtr,
                                           float baseEmissionRate, float duration,
                                           bool autoRemoveOnFinish) {
    if (nativePtr != nullptr) {
        auto ptrIt = m_ptrToIndex.find(nativePtr);
        if (ptrIt != m_ptrToIndex.end()) {
            auto& existing = m_emitters[ptrIt->second];
            existing.position = pos;
            existing.emitterRadius = std::max(1.0f, radius);
            existing.priority = prio;
            return existing.id;
        }
    }

    uint32_t index = static_cast<uint32_t>(m_emitters.size());
    ParticleEmitterInfo info{};
    info.id = id;
    info.nativePtr = nativePtr;
    info.position = pos;
    info.emitterRadius = std::max(1.0f, radius);
    info.priority = prio;
    info.baseEmissionRate = (baseEmissionRate > 0.0f && std::isfinite(baseEmissionRate)) ? baseEmissionRate : 30.0f;
    info.duration = duration;
    info.autoRemoveOnFinish = autoRemoveOnFinish;
    info.activeParticleCount = 0;
    info.maxAllowedParticles = 500;
    info.isSleeping = false;
    info.justEnteredSleep = false;
    info.justWokeFromSleep = false;
    info.active = true;

    m_emitters.push_back(info);
    m_idToIndex[id] = index;
    if (nativePtr != nullptr) {
        m_ptrToIndex[nativePtr] = index;
    }
    m_activeEmitterCount++;
    return id;
}

uint32_t ParticleGovernor::registerOrUpdateByPtr(void* nativePtr, const core::Point2D& pos, float radius,
                                                 ParticlePriority prio, float baseEmissionRate,
                                                 float duration, bool autoRemoveOnFinish) {
    if (nativePtr != nullptr) {
        auto it = m_ptrToIndex.find(nativePtr);
        if (it != m_ptrToIndex.end()) {
            auto& em = m_emitters[it->second];
            em.position = pos;
            em.emitterRadius = std::max(1.0f, radius);
            em.priority = prio;
            em.duration = duration;
            em.autoRemoveOnFinish = autoRemoveOnFinish;
            return em.id;
        }
    }
    uint32_t newId = static_cast<uint32_t>(m_emitters.size());
    return registerEmitter(newId, pos, radius, prio, nativePtr, baseEmissionRate, duration, autoRemoveOnFinish);
}

bool ParticleGovernor::updateEmitterPosition(uint32_t id, const core::Point2D& pos) {
    uint32_t idx = id;
    if (idx >= m_emitters.size() || m_emitters[idx].id != id) {
        auto it = m_idToIndex.find(id);
        if (it == m_idToIndex.end()) return false;
        idx = it->second;
    }
    if (!m_emitters[idx].active) return false;
    m_emitters[idx].position = pos;
    return true;
}

bool ParticleGovernor::unregisterEmitter(uint32_t id) {
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) return false;

    uint32_t idx = it->second;
    auto& em = m_emitters[idx];
    if (!em.active) return false;

    if (em.nativePtr != nullptr) {
        m_ptrToIndex.erase(em.nativePtr);
    }
    m_idToIndex.erase(it);
    em.active = false;
    em.isSleeping = false;
    em.activeParticleCount = 0;
    em.nativePtr = nullptr;

    if (m_activeEmitterCount > 0) {
        m_activeEmitterCount--;
    }
    recalculateParticleTotals();
    return true;
}

bool ParticleGovernor::unregisterEmitterByPtr(void* nativePtr) {
    if (!nativePtr) return false;
    auto it = m_ptrToIndex.find(nativePtr);
    if (it == m_ptrToIndex.end()) return false;
    return unregisterEmitter(m_emitters[it->second].id);
}

EmitterEvaluationResult ParticleGovernor::evaluateEmitterDetailed(
    uint32_t id, const core::Rect2D& cameraRect, float rawDt) {

    EmitterEvaluationResult res{};
    res.clampedDt = std::clamp(rawDt, 0.0f, 0.05f);

    uint32_t idx = id;
    if (idx >= m_emitters.size() || m_emitters[idx].id != id) {
        auto it = m_idToIndex.find(id);
        if (it == m_idToIndex.end()) {
            return res;
        }
        idx = it->second;
    }

    auto& emitter = m_emitters[idx];
    if (!emitter.active) {
        return res;
    }

    bool wasSleeping = emitter.isSleeping;
    emitter.justEnteredSleep = false;
    emitter.justWokeFromSleep = false;

    // 1. Critical gameplay particles ALWAYS update at 100% fidelity
    if (emitter.priority == ParticlePriority::Critical) {
        emitter.isSleeping = false;
        if (wasSleeping) {
            emitter.justWokeFromSleep = true;
            res.justWokeFromSleep = true;
            res.clampedDt = std::min(res.clampedDt, 1.0f / 60.0f);
        }
        res.shouldUpdate = true;
        res.emissionMultiplier = 1.0f;
        res.effectiveEmissionRate = emitter.baseEmissionRate;
        return res;
    }

    // 2. Finite-duration or auto-remove emitters must NOT sleep offscreen so their lifecycle completes
    bool isContinuousEmitter = (emitter.duration < 0.0f) && !emitter.autoRemoveOnFinish;

    if (m_cullOffscreen && isContinuousEmitter) {
        core::Rect2D emitterBounds(
            emitter.position.x - emitter.emitterRadius,
            emitter.position.y - emitter.emitterRadius,
            emitter.emitterRadius * 2.0f,
            emitter.emitterRadius * 2.0f
        );

        core::Rect2D paddedCamera = cameraRect.normalized().expanded(120.0f, 120.0f);
        if (!paddedCamera.intersects(emitterBounds)) {
            emitter.isSleeping = true;
            if (!wasSleeping) {
                emitter.justEnteredSleep = true;
                res.justEnteredSleep = true;
            }
            res.shouldUpdate = false;
            res.emissionMultiplier = 0.0f;
            res.effectiveEmissionRate = 0.0f;
            recalculateParticleTotals();
            return res;
        }
    }

    emitter.isSleeping = false;
    if (wasSleeping) {
        emitter.justWokeFromSleep = true;
        res.justWokeFromSleep = true;
        // Clamp dt on wake to prevent a multi-frame emission burst
        res.clampedDt = std::min(res.clampedDt, 1.0f / 60.0f);
    }

    // 3. Apply non-compounding density scaling
    float multiplier = m_densityScale;
    if (emitter.priority == ParticlePriority::Standard) {
        multiplier = std::max(m_densityScale, 0.75f);
    }

    // If exceeding global particle budget, throttle cosmetic/standard emitters further
    if (m_currentTotalParticles > m_globalMaxParticles) {
        if (emitter.priority == ParticlePriority::Cosmetic) {
            multiplier *= 0.25f;
        } else {
            multiplier *= 0.5f;
        }
    }

    multiplier = std::clamp(multiplier, 0.05f, 1.0f);
    res.shouldUpdate = true;
    res.emissionMultiplier = multiplier;
    res.effectiveEmissionRate = emitter.baseEmissionRate * multiplier;
    return res;
}

bool ParticleGovernor::evaluateEmitter(uint32_t id, const core::Rect2D& cameraRect, float& outEmissionMultiplier) {
    auto res = evaluateEmitterDetailed(id, cameraRect, 1.0f / 60.0f);
    outEmissionMultiplier = res.emissionMultiplier;
    return res.shouldUpdate;
}

void ParticleGovernor::recalculateParticleTotals() {
    uint32_t total = 0;
    uint32_t culled = 0;
    uint32_t avoided = 0;

    for (const auto& em : m_emitters) {
        if (!em.active) continue;
        if (em.isSleeping) {
            culled++;
            avoided += std::max<uint32_t>(em.activeParticleCount, 32);
        } else {
            total += em.activeParticleCount;
            if (em.priority != ParticlePriority::Critical && m_densityScale < 1.0f) {
                float fullCount = static_cast<float>(em.activeParticleCount) / m_densityScale;
                if (fullCount > static_cast<float>(em.activeParticleCount)) {
                    avoided += static_cast<uint32_t>(fullCount - static_cast<float>(em.activeParticleCount));
                }
            }
        }
    }

    m_currentTotalParticles = total;
    m_lastCulledEmitters = culled;
    m_lastAvoidedParticles = avoided;
}

void ParticleGovernor::recordActiveParticles(uint32_t emitterId, uint32_t count) {
    uint32_t idx = emitterId;
    if (idx >= m_emitters.size() || m_emitters[idx].id != emitterId) {
        auto it = m_idToIndex.find(emitterId);
        if (it != m_idToIndex.end()) {
            idx = it->second;
        } else {
            idx = static_cast<uint32_t>(m_emitters.size());
        }
    }
    if (idx < m_emitters.size() && m_emitters[idx].active) {
        m_emitters[idx].activeParticleCount = count;
    }

    recalculateParticleTotals();
}

const ParticleEmitterInfo* ParticleGovernor::getEmitterInfo(uint32_t id) const {
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) return nullptr;
    const auto& em = m_emitters[it->second];
    return em.active ? &em : nullptr;
}

} // namespace optigd::optimization

#pragma once

#include "../core/Types.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

namespace optigd::optimization {

enum class ParticlePriority : uint8_t {
    Cosmetic = 0,    // Background dust, ambient embers, decorative bursts
    Standard = 1,    // General obstacle/hazard effects
    Critical = 2     // Player icon trail, gravity portal glow, jump pads
};

struct ParticleEmitterInfo {
    uint32_t id{0};
    void* nativePtr{nullptr};
    core::Point2D position{};
    float emitterRadius{64.0f};
    ParticlePriority priority{ParticlePriority::Cosmetic};
    float baseEmissionRate{30.0f};
    float duration{-1.0f};
    bool autoRemoveOnFinish{false};
    uint32_t activeParticleCount{0};
    uint32_t maxAllowedParticles{500};
    bool isSleeping{false};
    bool justEnteredSleep{false};
    bool justWokeFromSleep{false};
    bool active{true};
};

struct EmitterEvaluationResult {
    bool shouldUpdate{true};
    float emissionMultiplier{1.0f};
    float effectiveEmissionRate{30.0f};
    float clampedDt{0.016667f};
    bool justEnteredSleep{false};
    bool justWokeFromSleep{false};
};

class ParticleGovernor {
public:
    explicit ParticleGovernor(float densityScale = 0.5f, uint32_t globalMax = 2048);

    void setDensityScale(float scale);
    float getDensityScale() const { return m_densityScale; }

    void setGlobalParticleLimit(uint32_t limit);
    uint32_t getGlobalParticleLimit() const { return m_globalMaxParticles; }

    void setCullOffscreen(bool cull) { m_cullOffscreen = cull; markDerivedStatsDirty(); }
    bool isCullOffscreen() const { return m_cullOffscreen; }

    void clear();

    uint32_t registerEmitter(uint32_t id, const core::Point2D& pos, float radius,
                             ParticlePriority prio, void* nativePtr = nullptr,
                             float baseEmissionRate = 30.0f, float duration = -1.0f,
                             bool autoRemoveOnFinish = false);

    uint32_t registerOrUpdateByPtr(void* nativePtr, const core::Point2D& pos, float radius,
                                   ParticlePriority prio, float baseEmissionRate,
                                   float duration, bool autoRemoveOnFinish);

    bool updateEmitterPosition(uint32_t id, const core::Point2D& pos);
    bool unregisterEmitter(uint32_t id);
    bool unregisterEmitterByPtr(void* nativePtr);

    // Determines if an emitter should update & render, and gives scaled emission rate
    bool evaluateEmitter(uint32_t id, const core::Rect2D& cameraRect, float& outEmissionMultiplier);

    // Detailed evaluation including sleep/wake state transitions and non-compounding rate
    EmitterEvaluationResult evaluateEmitterDetailed(uint32_t id, const core::Rect2D& cameraRect, float rawDt);

    void recordActiveParticles(uint32_t emitterId, uint32_t count);

    size_t getTrackedEmitterCount() const { return m_activeEmitterCount; }

    // O(1): the running total is maintained incrementally by
    // recordActiveParticles()/unregisterEmitter() instead of being recomputed
    // per emitter per frame (which was O(N^2) per frame).
    uint32_t getTotalActiveParticles() const { return m_currentTotalParticles; }

    // O(1): sleeping-emitter count is maintained on sleep/wake transitions.
    uint32_t getLastCulledEmitterCount() const { return m_sleepingEmitterCount; }

    // O(N) but computed at most once per frame, on demand, and only when the
    // underlying emitter state actually changed.
    uint32_t getLastAvoidedParticleCount() const;

    const ParticleEmitterInfo* getEmitterInfo(uint32_t id) const;

private:
    float m_densityScale{0.5f};
    uint32_t m_globalMaxParticles{2048};
    bool m_cullOffscreen{true};

    std::vector<ParticleEmitterInfo> m_emitters;
    std::unordered_map<uint32_t, uint32_t> m_idToIndex;
    std::unordered_map<void*, uint32_t> m_ptrToIndex;

    size_t m_activeEmitterCount{0};
    uint32_t m_currentTotalParticles{0};

    uint32_t m_sleepingEmitterCount{0};
    mutable uint32_t m_lastAvoidedParticles{0};
    mutable bool m_derivedStatsDirty{true};

    void recomputeDerivedStats() const;
    void markDerivedStatsDirty() { m_derivedStatsDirty = true; }
};

} // namespace optigd::optimization

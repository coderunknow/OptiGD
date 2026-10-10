#pragma once

#include "Types.hpp"
#include "MemoryArena.hpp"
#include "../optimization/VisibilityCuller.hpp"
#include "../optimization/ParticleGovernor.hpp"
#include "../optimization/TriggerOptimizer.hpp"
#include "../adaptive/AdaptiveGovernor.hpp"
#include "../adaptive/FallbackManager.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

namespace optigd::core {

// Central coordinator. Owns the modules, the user config snapshot, and the effective config.
//
// Threading: every public method takes m_mutex. Game hooks run on the main thread, so the
// lock is uncontended in practice.
//
// Frame timing (per game frame, reported in FrameStats):
//   beginFrame(): the previous beginFrame() time gives frameIntervalUs (0 on a discontinuity).
//   markGameUpdateBegin()/End(): wrap the original game update; gameUpdateUs.
//   endFrame(): optigdOverheadUs = time spent inside OptiGD methods during the frame.
class OptiGDManager {
public:
    static OptiGDManager& instance();

    void initialize();
    void shutdown();

    // Level lifecycle. onLevelLoaded resets per-level state (culler, trigger arming, fallbacks).
    void onLevelLoaded();
    void onLevelExited();

    // Re-reads ConfigManager. Adaptive state is reset because the user's tier may have changed.
    void onSettingsChanged();

    void beginFrame(uint64_t frameIndex);
    void markGameUpdateBegin();
    void markGameUpdateEnd();
    void endFrame();

    // Shadow culling: measures how many registered objects lie outside the padded camera
    // rectangle. It never hides or skips a game object. The optional refresh callback runs
    // first (for example, to update bounds of moving objects); its time is included in
    // cullingShadowUs and optigdOverheadUs.
    void recordCullingShadow(const Rect2D& cameraRect,
                             const std::function<void(optimization::VisibilityCuller&)>& refresh = {});

    // Accessors (not thread-safe: use from the main thread only).
    MemoryArena& getArena() { return m_frameArena; }
    optimization::VisibilityCuller& getCuller() { return m_culler; }
    optimization::ParticleGovernor& getParticleGovernor() { return m_particleGovernor; }
    optimization::TriggerOptimizer& getTriggerOptimizer() { return m_triggerOptimizer; }
    adaptive::AdaptiveGovernor& getAdaptiveGovernor() { return m_adaptiveGovernor; }
    adaptive::FallbackManager& getFallbackManager() { return m_fallbackManager; }

    OptiConfig getUserConfig() const;
    OptiConfig getEffectiveConfig() const;
    const FrameStats& getCurrentFrameStats() const { return m_currentStats; }
    bool isLevelActive() const { return m_levelActive; }
    bool isInitialized() const { return m_initialized; }

private:
    OptiGDManager();
    ~OptiGDManager();

    void applyEffectiveConfigLocked(const OptiConfig& effective);
    void chargeOverheadLocked(uint64_t startUs);

    mutable std::mutex m_mutex;
    bool m_initialized{false};
    bool m_levelActive{false};

    OptiConfig m_userConfig{};
    OptiConfig m_effectiveConfig{};

    MemoryArena m_frameArena{1024 * 1024};
    optimization::VisibilityCuller m_culler;
    optimization::ParticleGovernor m_particleGovernor;
    optimization::TriggerOptimizer m_triggerOptimizer;
    adaptive::AdaptiveGovernor m_adaptiveGovernor{OptiConfig{}};
    adaptive::FallbackManager m_fallbackManager{4.0f, 120};

    FrameStats m_currentStats{};
    bool m_frameOpen{false};
    uint64_t m_lastBeginUs{0};
    bool m_hasLastBegin{false};
    uint64_t m_gameUpdateStartUs{0};
    double m_overheadAccumUs{0.0};
    size_t m_arenaAllocBase{0};
};

} // namespace optigd::core

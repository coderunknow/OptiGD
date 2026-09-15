#pragma once

#include "Types.hpp"
#include "MemoryArena.hpp"
#include "../optimization/VisibilityCuller.hpp"
#include "../optimization/ParticleGovernor.hpp"
#include "../optimization/TriggerOptimizer.hpp"
#include "../adaptive/AdaptiveGovernor.hpp"
#include "../adaptive/FallbackManager.hpp"
#include "../diagnostics/DiagnosticsEngine.hpp"
#include "../compatibility/CompatibilityLayer.hpp"
#include "../config/ConfigManager.hpp"
#include <memory>
#include <mutex>

namespace optigd::core {

class OptiGDManager {
public:
    static OptiGDManager& instance();

    void initialize();
    void shutdown();

    void onLevelLoaded();
    void onLevelExited();

    void beginFrame(uint64_t frameIndex);
    void endFrame();

    // Accessors
    MemoryArena& getArena() { return m_frameArena; }
    optimization::VisibilityCuller& getCuller() { return m_culler; }
    optimization::ParticleGovernor& getParticleGovernor() { return m_particleGovernor; }
    optimization::TriggerOptimizer& getTriggerOptimizer() { return m_triggerOptimizer; }
    adaptive::AdaptiveGovernor& getAdaptiveGovernor() { return m_adaptiveGovernor; }
    adaptive::FallbackManager& getFallbackManager() { return m_fallbackManager; }

    const FrameStats& getCurrentFrameStats() const { return m_currentStats; }
    bool isLevelActive() const { return m_levelActive; }

private:
    OptiGDManager();
    ~OptiGDManager();

    std::mutex m_mutex;
    bool m_initialized{false};
    bool m_levelActive{false};

    MemoryArena m_frameArena;
    optimization::VisibilityCuller m_culler;
    optimization::ParticleGovernor m_particleGovernor;
    optimization::TriggerOptimizer m_triggerOptimizer;
    adaptive::AdaptiveGovernor m_adaptiveGovernor;
    adaptive::FallbackManager m_fallbackManager;

    FrameStats m_currentStats{};
    uint64_t m_frameStartTimeUs{0};
};

} // namespace optigd::core

#pragma once

#include "Types.hpp"
#include "MemoryArena.hpp"
#include "../optimization/VisibilityCuller.hpp"
#include "../optimization/ParticleGovernor.hpp"
#include "../optimization/TriggerOptimizer.hpp"
#include "../optimization/MatrixCache.hpp"
#include "../adaptive/AdaptiveGovernor.hpp"
#include "../adaptive/FallbackManager.hpp"
#include "../profiling/Profiler.hpp"
#include "../diagnostics/DiagnosticsEngine.hpp"
#include "../compatibility/CompatibilityLayer.hpp"
#include "../config/ConfigManager.hpp"
#include "../ui/DiagnosticsOverlay.hpp"
#include <chrono>
#include <string>
#include <string_view>

namespace optigd::core {

class OptiGDManager {
public:
    static OptiGDManager& instance();

    void initialize(std::string_view gdVersion = kTargetGDVersion,
                    std::string_view geodeVersion = kTargetGeodeVersion);
    void shutdown();

    void onLevelLoad();
    void onLevelReset();
    void onLevelExit();

    void beginFrame();
    void setExternalFrameDeltaUs(double frameDeltaUs) { m_externalFrameDeltaUs = frameDeltaUs; }
    void recordHeapAllocationAvoided(uint32_t count = 1) { m_frameHeapAllocationsAvoided += count; }
    void recordSubsystemOverheadUs(double overheadUs) { m_frameOptigdOverheadUs += overheadUs; }
    void endFrame();

    // Subsystem accessors
    MemoryArena& getFrameArena() { return m_frameArena; }
    optimization::VisibilityCuller& getVisibilityCuller() { return m_culler; }
    optimization::ParticleGovernor& getParticleGovernor() { return m_particleGovernor; }
    optimization::TriggerOptimizer& getTriggerOptimizer() { return m_triggerOptimizer; }
    adaptive::AdaptiveGovernor& getAdaptiveGovernor() { return m_governor; }
    adaptive::FallbackManager& getFallbackManager() { return m_fallbackManager; }

    const FrameStats& getLatestFrameStats() const { return m_currentStats; }
    const OptiConfig& getActiveConfig() const { return m_activeConfig; }
    void reloadConfig();

    bool isInitialized() const { return m_initialized; }

private:
    OptiGDManager();

    void applyConfigToSubsystems();

    bool m_initialized{false};
    uint64_t m_frameCounter{0};
    std::chrono::steady_clock::time_point m_frameStartTime;
    double m_externalFrameDeltaUs{0.0};
    double m_frameOptigdOverheadUs{0.0};
    uint32_t m_frameHeapAllocationsAvoided{0};
    uint64_t m_frameStartMatrixHits{0};
    uint64_t m_frameStartMatrixMisses{0};

    OptiConfig m_activeConfig;
    FrameStats m_currentStats;

    MemoryArena m_frameArena;
    optimization::VisibilityCuller m_culler;
    optimization::ParticleGovernor m_particleGovernor;
    optimization::TriggerOptimizer m_triggerOptimizer;
    adaptive::AdaptiveGovernor m_governor;
    adaptive::FallbackManager m_fallbackManager;
};

} // namespace optigd::core

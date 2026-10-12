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
    // Set by the frame hook when the game loop is running but the level is not
    // being played (pause menu open, level intro). Such a frame keeps an
    // ordinary-looking inter-frame interval, so without this flag it would be
    // indistinguishable from gameplay and would inflate every measurement.
    void setFramePaused(bool paused) { m_framePaused = paused; }
    void recordHeapAllocationAvoided(uint32_t count = 1) { m_frameHeapAllocationsAvoided += count; }
    void recordSubsystemOverheadUs(double overheadUs) { m_frameOptigdOverheadUs += overheadUs; }

    // Per-subsystem CPU accounting. These feed FrameStats::*TimeUs, which in
    // turn drive the Profiler subsystem metrics and FallbackManager's
    // per-feature CPU safety budgets. Without them those two consumers were
    // permanently dead (every budget check compared 0 against its threshold).
    //
    // beta.3 fix: attribution is now restricted to an *open* frame window
    // (`beginFrame()` .. `endFrame()`). `CCParticleSystem::update` can run
    // outside that window, and beta.2 added such time to `m_currentStats`
    // unconditionally, so it was silently reported as the *next* frame's cost.
    // Time recorded outside a window is counted separately and surfaced through
    // `getOutOfWindowSubsystemEvents()` instead of being attributed to a frame
    // that did not incur it.
    void recordCullingTimeUs(double us) { attributeSubsystemTime(m_currentStats.cullingTimeUs, us); }
    void recordParticleTimeUs(double us) { attributeSubsystemTime(m_currentStats.particleTimeUs, us); }
    void recordTriggerTimeUs(double us) { attributeSubsystemTime(m_currentStats.triggerTimeUs, us); }
    void recordMatrixTimeUs(double us) { attributeSubsystemTime(m_currentStats.matrixTimeUs, us); }

    void endFrame();

    // --- frame-window and mis-attribution accounting -------------------------
    bool isFrameOpen() const { return m_frameOpen; }
    // Number of subsystem-time recordings that arrived while no frame window
    // was open, and the microseconds they accounted for. Both are reported in
    // the diagnostics JSON so the overhead figure can be qualified.
    uint32_t getOutOfWindowSubsystemEvents() const { return m_outOfWindowSubsystemEvents; }
    double getOutOfWindowSubsystemUs() const { return m_outOfWindowSubsystemUs; }
    void clearOutOfWindowAccounting() {
        m_outOfWindowSubsystemEvents = 0;
        m_outOfWindowSubsystemUs = 0.0;
    }
    // Inter-frame interval of the most recent completed frame, in microseconds,
    // as measured by the frame hook (wall clock between consecutive
    // `GJBaseGameLayer::update` calls). Used by Benchmark Mode to classify
    // stalls without re-deriving a threshold.
    double getLastInterFrameIntervalUs() const { return m_lastInterFrameIntervalUs; }
    bool lastFrameWasStall() const { return m_lastFrameWasStall; }

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
    void attributeSubsystemTime(double& statSlotUs, double us);

    bool m_initialized{false};
    uint64_t m_frameCounter{0};
    std::chrono::steady_clock::time_point m_frameStartTime;
    bool m_frameOpen{false};
    uint64_t m_frameStartThreadCpuUs{0};
    uint32_t m_outOfWindowSubsystemEvents{0};
    double m_outOfWindowSubsystemUs{0.0};
    double m_lastInterFrameIntervalUs{0.0};
    bool m_lastFrameWasStall{false};
    double m_externalFrameDeltaUs{0.0};
    bool m_framePaused{false};
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

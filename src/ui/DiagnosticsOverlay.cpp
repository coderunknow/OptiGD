#include "DiagnosticsOverlay.hpp"
#include <sstream>
#include <iomanip>

namespace optigd::ui {

DiagnosticsOverlay& DiagnosticsOverlay::instance() {
    static DiagnosticsOverlay s_instance;
    return s_instance;
}

std::string DiagnosticsOverlay::formatOverlayText(
    const core::FrameStats& latestFrame,
    const adaptive::WindowStats& windowStats,
    const core::OptiConfig& config) const {

    if (!m_enabled) return "";

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "[OptiGD v" << core::kOptiGDVersion << " Diagnostics]\n";
    ss << "FPS: " << windowStats.fps
       << " | Mean: " << windowStats.meanMs << "ms"
       << " | P50: " << windowStats.medianMs << "ms"
       << " | P95: " << windowStats.p95Ms << "ms"
       << " | P99: " << windowStats.p99Ms << "ms\n";
    // Labelled by what each number actually is. `updateCpuTimeUs` is the WALL
    // span of the hooked update call; the thread CPU time for the same call is a
    // different measurement, and calling both "CPU" is how a pre-empted frame
    // gets read as slow code.
    ss << "Update: " << (latestFrame.updateCpuTimeUs / 1000.0) << "ms wall"
       << " / " << (latestFrame.updateThreadCpuUs / 1000.0) << "ms thread"
       << " | OptiGD Overhead: " << latestFrame.optigdOverheadUs << "us\n";
    ss << "Tier: " << core::tierToString(config.tier)
       << " (Target: " << config.targetFrameRate << " FPS)\n";
    ss << "Objects: " << latestFrame.visibleObjects << " vis / "
       << latestFrame.culledObjects << " culled ("
       << latestFrame.totalObjects << " total, +"
       << latestFrame.newlyVisibleObjects << "/-"
       << latestFrame.newlyCulledObjects << " delta)\n";
    ss << "Particles: " << latestFrame.activeParticles << " active / "
       << latestFrame.culledParticles << " avoided (Scale: "
       << static_cast<int>(config.particleDensityScale * 100.0f) << "%)\n";
    ss << "Triggers: " << latestFrame.evaluatedTriggers << " eval / "
       << latestFrame.executedTriggers << " exec ("
       << latestFrame.totalTriggers << " total)\n";
    ss << "Arena: " << (latestFrame.arenaUsedBytes / 1024) << " KB used | "
       << latestFrame.heapAllocationsAvoided << " heap allocs avoided\n";
    // Everything above is OptiGD's own instrumentation. Saying so on the overlay
    // itself keeps a screenshot of this HUD from being read as an external
    // presented-frame measurement, which it is not.
    ss << "Internal telemetry: inter-frame intervals from GJBaseGameLayer::update\n";
    ss << "(120-frame governor window). Not presented-frame or GPU timing.";

    return ss.str();
}

} // namespace optigd::ui

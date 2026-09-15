#include "DiagnosticsOverlay.hpp"
#include <sstream>
#include <iomanip>

namespace optigd::ui {

DiagnosticsOverlay& DiagnosticsOverlay::instance() {
    static DiagnosticsOverlay s_instance;
    return s_instance;
}

std::vector<std::string> DiagnosticsOverlay::formatOverlayLines(
    const adaptive::AdaptiveGovernor& governor,
    const diagnostics::DiagnosticsSummary& summary) const {

    std::vector<std::string> lines;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);

    // Line 1: FPS & Frame Time
    ss << "OptiGD v1.0.0 | " << summary.averageFps << " FPS ("
       << summary.meanFrameTimeMs << " ms, jitter: " << summary.frameTimeStdDevMs << " ms)";
    lines.push_back(ss.str());

    // Line 2: Tier & Adaptation
    ss.str("");
    ss.clear();
    ss << "Tier: " << core::tierToString(summary.activeTier);
    if (governor.isDynamicAdaptationEnabled()) {
        ss << " [Adaptive]";
    }
    if (summary.fallbacksTriggered > 0) {
        ss << " [Fallbacks: " << summary.fallbacksTriggered << "]";
    }
    lines.push_back(ss.str());

    // Line 3: Culling & Allocations
    ss.str("");
    ss.clear();
    ss << "Culling: " << (summary.averageCullingRatio * 100.0) << "% culled ("
       << summary.totalObjectsCulled << "/" << summary.totalObjectsSeen << ") | "
       << "Avoided Allocs: " << summary.totalHeapAllocsAvoided;
    lines.push_back(ss.str());

    return lines;
}

std::string DiagnosticsOverlay::getSingleLineSummary(
    const adaptive::AdaptiveGovernor& governor,
    const diagnostics::DiagnosticsSummary& summary) const {

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "OptiGD: " << summary.averageFps << " FPS | "
       << summary.meanFrameTimeMs << "ms | "
       << "Tier: " << core::tierToString(summary.activeTier) << " | "
       << (summary.averageCullingRatio * 100.0) << "% culled";
    return ss.str();
}

} // namespace optigd::ui

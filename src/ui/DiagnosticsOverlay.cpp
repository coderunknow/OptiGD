#include "DiagnosticsOverlay.hpp"
#include "../core/Version.hpp"

#include <iomanip>
#include <sstream>

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

    ss << "OptiGD " << kOptiGDVersion << " | " << summary.averageFps << " FPS ("
       << summary.meanFrameIntervalMs << " ms, jitter " << summary.frameIntervalStdDevMs << " ms)";
    lines.push_back(ss.str());

    ss.str("");
    ss.clear();
    ss << "Tier: " << core::tierToString(summary.effectiveTier);
    if (governor.isDynamicAdaptationEnabled()) {
        ss << " [adaptive]";
    }
    if (summary.fallbackEvents > 0) {
        ss << " [fallback events: " << summary.fallbackEvents << "]";
    }
    lines.push_back(ss.str());

    ss.str("");
    ss.clear();
    ss << "Shadow cull: " << (summary.shadowCullRatio * 100.0) << "% of "
       << summary.totalShadowObjectsSeen << " object-frames (not applied)";
    lines.push_back(ss.str());

    return lines;
}

std::string DiagnosticsOverlay::getSingleLineSummary(
    const adaptive::AdaptiveGovernor& governor,
    const diagnostics::DiagnosticsSummary& summary) const {
    (void)governor;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "OptiGD: " << summary.averageFps << " FPS | " << summary.meanFrameIntervalMs << " ms | "
       << "Tier: " << core::tierToString(summary.effectiveTier);
    return ss.str();
}

} // namespace optigd::ui

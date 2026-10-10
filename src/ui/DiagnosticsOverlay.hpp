#pragma once

#include "../core/Types.hpp"
#include "../adaptive/AdaptiveGovernor.hpp"
#include <string>

namespace optigd::ui {

class DiagnosticsOverlay {
public:
    static DiagnosticsOverlay& instance();

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // Formats HUD overlay text for rendering in Cocos2d-x CCLabelBMFont or console
    std::string formatOverlayText(const core::FrameStats& latestFrame,
                                  const adaptive::WindowStats& windowStats,
                                  const core::OptiConfig& config) const;

private:
    DiagnosticsOverlay() = default;
    bool m_enabled{false};
};

} // namespace optigd::ui

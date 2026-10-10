#pragma once

#include "../adaptive/AdaptiveGovernor.hpp"
#include "../diagnostics/DiagnosticsEngine.hpp"
#include <string>
#include <vector>

namespace optigd::ui {

// Text formatting for the in-game diagnostics label. Visibility is a user setting
// (show-diagnostics, default off); this class performs no drawing itself.
class DiagnosticsOverlay {
public:
    static DiagnosticsOverlay& instance();

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    std::vector<std::string> formatOverlayLines(const adaptive::AdaptiveGovernor& governor,
                                                const diagnostics::DiagnosticsSummary& summary) const;

    std::string getSingleLineSummary(const adaptive::AdaptiveGovernor& governor,
                                     const diagnostics::DiagnosticsSummary& summary) const;

private:
    DiagnosticsOverlay() = default;
    bool m_visible{false};
};

} // namespace optigd::ui

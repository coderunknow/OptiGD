#pragma once

#include "../core/Types.hpp"
#include "../adaptive/AdaptiveGovernor.hpp"
#include "../diagnostics/DiagnosticsEngine.hpp"
#include <string>
#include <vector>

namespace optigd::ui {

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

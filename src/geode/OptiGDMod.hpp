#pragma once

// This header bridges the standalone OptiGD C++20 engine with the Geode SDK
// when compiled inside a Geode Mod environment.

#include "../core/OptiGDManager.hpp"
#include "../config/ConfigManager.hpp"
#include "../diagnostics/DiagnosticsEngine.hpp"
#include "../ui/DiagnosticsOverlay.hpp"

namespace optigd::geode_integration {

void onModLoaded();
void syncSettingsWithConfig();

} // namespace optigd::geode_integration

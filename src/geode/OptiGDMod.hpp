#pragma once

#ifdef GEODE_MOD_ID

#include <Geode/Geode.hpp>
#include "../core/OptiGDManager.hpp"

namespace optigd::geode_mod {

void initializeMod();
void syncSettingsWithConfig();

} // namespace optigd::geode_mod

#endif // GEODE_MOD_ID

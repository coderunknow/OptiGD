#pragma once

#ifdef GEODE_MOD_ID

#include <Geode/Geode.hpp>

namespace optigd::geode_mod {

// Reads every setting into a config snapshot and applies it. Safe to call repeatedly.
void syncSettings();

} // namespace optigd::geode_mod

#endif // GEODE_MOD_ID

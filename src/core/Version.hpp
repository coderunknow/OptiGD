#pragma once

namespace optigd {

// Geometry Dash version the bindings target (must match the Geode SDK bindings).
inline constexpr const char* kGeometryDashVersion = "2.2081";

// Release identifier as used in git tags and documentation.
inline constexpr const char* kOptiGDVersion = "0.1.0-beta1";

// Version string in Geode's mod.json grammar. Geode's VersionInfo parser only
// accepts a dot before the prerelease number ("-beta.1"); "-beta1" is rejected.
inline constexpr const char* kOptiGDGeodeVersion = "v0.1.0-beta.1";

} // namespace optigd

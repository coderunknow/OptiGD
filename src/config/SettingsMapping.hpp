#pragma once

#include "../core/Types.hpp"
#include <cstdint>
#include <string>

namespace optigd::config {

// Plain-data copy of the Geode settings declared in mod.json. Kept free of Geode
// types so the mapping can be unit-tested natively.
struct SettingsSnapshot {
    std::string tierName{"LowEnd"};
    bool enableCulling{false};
    bool enableParticleLimits{false};
    bool enableAdaptive{true};
    bool showDiagnostics{false};
    bool recordFrameTimes{false};

    // Used only when tierName == "Custom".
    double customParticleDensity{0.5};
    int64_t customMaxParticles{2048};
    int64_t customPaddingX{120};
    int64_t customPaddingY{80};
};

struct ResolvedSettings {
    core::OptiConfig config{};
    // Non-empty when an input was invalid and a default was used instead.
    std::string warning;
};

// Builds the user configuration from settings.
//  - A preset tier (LowEnd/Balanced/HighEnd) sets the tuning values from the preset table.
//  - Custom uses the custom* values (clamped to documented ranges).
//  - An unknown tier name falls back to LowEnd and reports a warning.
//  - Feature flags always come from the individual switches.
// base supplies values not exposed as settings (frame target, cell size, ...).
ResolvedSettings resolveSettings(const SettingsSnapshot& settings, const core::OptiConfig& base);

} // namespace optigd::config

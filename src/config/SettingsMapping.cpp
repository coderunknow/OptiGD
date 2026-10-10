#include "SettingsMapping.hpp"
#include "Presets.hpp"

#include <algorithm>
#include <cmath>

namespace optigd::config {

ResolvedSettings resolveSettings(const SettingsSnapshot& settings, const core::OptiConfig& base) {
    ResolvedSettings out;
    out.config = base;

    core::PerformanceTier tier = core::PerformanceTier::LowEnd;
    if (auto parsed = parseTier(settings.tierName)) {
        tier = *parsed;
    } else {
        out.warning = "Unknown performance profile '" + settings.tierName + "'; using LowEnd.";
    }

    if (tier == core::PerformanceTier::Custom) {
        TierTuning custom;
        custom.particleDensityScale = static_cast<float>(
            std::isfinite(settings.customParticleDensity)
                ? std::clamp(settings.customParticleDensity, 0.05, 1.0)
                : 0.5);
        custom.maxActiveParticles = static_cast<uint32_t>(std::clamp<int64_t>(settings.customMaxParticles, 128, 16384));
        custom.cullOffscreenEmitters = true;
        custom.cameraPaddingX = static_cast<float>(std::clamp<int64_t>(settings.customPaddingX, 0, 600));
        custom.cameraPaddingY = static_cast<float>(std::clamp<int64_t>(settings.customPaddingY, 0, 600));
        applyTuning(out.config, core::PerformanceTier::Custom, custom);
    } else {
        applyPresetTier(out.config, tier);
    }

    out.config.flags.enableSpatialCulling = settings.enableCulling;
    out.config.flags.enableParticleThrottling = settings.enableParticleLimits;
    out.config.flags.enableAdaptiveGovernor = settings.enableAdaptive;
    out.config.flags.enableDiagnosticsOverlay = settings.showDiagnostics;
    out.config.flags.enableFrameTimeRecorder = settings.recordFrameTimes;
    // Not exposed as settings in v0.1.0-beta1; always off (see OptimizationFlags).
    out.config.flags.enableTriggerIndexing = false;
    out.config.flags.enableMatrixCaching = false;
    return out;
}

} // namespace optigd::config

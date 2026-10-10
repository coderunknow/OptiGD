#include "Presets.hpp"

namespace optigd::config {

std::optional<TierTuning> presetTuning(core::PerformanceTier tier) {
    switch (tier) {
        case core::PerformanceTier::LowEnd:
            return TierTuning{0.5f, 2048, true, 120.0f, 80.0f};
        case core::PerformanceTier::Balanced:
            return TierTuning{0.75f, 4096, true, 150.0f, 100.0f};
        case core::PerformanceTier::HighEnd:
            return TierTuning{1.0f, 8192, false, 200.0f, 150.0f};
        case core::PerformanceTier::Custom:
            return std::nullopt;
    }
    return std::nullopt;
}

void applyTuning(core::OptiConfig& cfg, core::PerformanceTier tier, const TierTuning& tuning) {
    cfg.tier = tier;
    cfg.particleDensityScale = tuning.particleDensityScale;
    cfg.maxActiveParticles = tuning.maxActiveParticles;
    cfg.cullOffscreenEmitters = tuning.cullOffscreenEmitters;
    cfg.cameraPaddingX = tuning.cameraPaddingX;
    cfg.cameraPaddingY = tuning.cameraPaddingY;
}

void applyPresetTier(core::OptiConfig& cfg, core::PerformanceTier tier) {
    if (auto tuning = presetTuning(tier)) {
        applyTuning(cfg, tier, *tuning);
    } else {
        cfg.tier = tier; // Custom: keep the user's tuning values
    }
}

std::optional<core::PerformanceTier> parseTier(std::string_view name) {
    if (name == "LowEnd") return core::PerformanceTier::LowEnd;
    if (name == "Balanced") return core::PerformanceTier::Balanced;
    if (name == "HighEnd") return core::PerformanceTier::HighEnd;
    if (name == "Custom") return core::PerformanceTier::Custom;
    return std::nullopt;
}

} // namespace optigd::config

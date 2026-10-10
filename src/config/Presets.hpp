#pragma once

#include "../core/Types.hpp"
#include <optional>
#include <string_view>

namespace optigd::config {

// Tuning values associated with a preset tier.
struct TierTuning {
    float particleDensityScale{0.5f};
    uint32_t maxActiveParticles{2048};
    bool cullOffscreenEmitters{true};
    float cameraPaddingX{120.0f};
    float cameraPaddingY{80.0f};
};

// Returns the preset tuning for LowEnd, Balanced, or HighEnd. Returns nullopt for Custom,
// which has no preset values: its tuning comes from the user's own settings.
std::optional<TierTuning> presetTuning(core::PerformanceTier tier);

// Writes the tuning into cfg and sets cfg.tier. Feature flags are never changed here.
void applyTuning(core::OptiConfig& cfg, core::PerformanceTier tier, const TierTuning& tuning);

// Applies a preset tier to cfg. For Custom, only the tier field changes; tuning values
// are left as they are.
void applyPresetTier(core::OptiConfig& cfg, core::PerformanceTier tier);

// Parses "LowEnd", "Balanced", "HighEnd", or "Custom" (exact, case-sensitive).
std::optional<core::PerformanceTier> parseTier(std::string_view name);

} // namespace optigd::config

#pragma once

#include "../core/Types.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace optigd::compat {

enum class CapabilityStatus : uint8_t {
    Available = 0,
    Conflicted = 1,
    Unsupported = 2,
    Disabled = 3
};

struct ModFeatureInfo {
    std::string name;
    std::string description;
    CapabilityStatus status{CapabilityStatus::Available};
    std::string conflictReason;
};

class CompatibilityLayer {
public:
    static CompatibilityLayer& instance();

    void initialize(std::string_view gdVersion = core::kTargetGDVersion,
                    std::string_view geodeVersion = core::kTargetGeodeVersion);

    std::string_view getGDVersion() const { return m_gdVersion; }
    std::string_view getGeodeVersion() const { return m_geodeVersion; }
    bool isEnvironmentCompatible() const { return m_environmentCompatible; }

    void registerFeature(std::string_view featureName, std::string_view description);
    void reportConflict(std::string_view featureName, std::string_view reason);
    bool isFeatureUsable(std::string_view featureName) const;

    void disableFeature(std::string_view featureName, std::string_view reason);
    void enableFeature(std::string_view featureName);

    std::vector<ModFeatureInfo> getAllFeatures() const;

    static int parseMajorVersion(std::string_view ver);

    // Splits "2.2081" / "v2.2081" into numeric components. Returns false when
    // the string does not start with a dotted numeric version.
    static bool parseVersionComponents(std::string_view ver, int& major, int& minor, int& patch);

    // True when `gdVersion` denotes the exact GD build OptiGD was compiled
    // against. A prefix test such as starts_with("2.2") is not sufficient: GD
    // 2.21x would pass while using different structure layouts.
    static bool isSupportedGDVersion(std::string_view gdVersion);

private:
    CompatibilityLayer() = default;
    std::string m_gdVersion{core::kTargetGDVersion};
    std::string m_geodeVersion{core::kTargetGeodeVersion};
    bool m_environmentCompatible{true};
    std::unordered_map<std::string, ModFeatureInfo> m_features;
};

} // namespace optigd::compat

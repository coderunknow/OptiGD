#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include <cstddef>
#include "../core/Version.hpp"

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

    void initialize(std::string_view gdVersion, std::string_view geodeVersion);

    std::string_view getGDVersion() const { return m_gdVersion; }
    std::string_view getGeodeVersion() const { return m_geodeVersion; }

    void registerFeature(std::string_view featureName, std::string_view description);
    void reportConflict(std::string_view featureName, std::string_view reason);
    bool isFeatureUsable(std::string_view featureName) const;

    void disableFeature(std::string_view featureName, std::string_view reason);
    void enableFeature(std::string_view featureName);

    std::vector<ModFeatureInfo> getAllFeatures() const;

private:
    CompatibilityLayer() = default;
    std::string m_gdVersion{optigd::kGeometryDashVersion};
    std::string m_geodeVersion{optigd::kOptiGDGeodeVersion};
    std::unordered_map<std::string, ModFeatureInfo> m_features;
};

} // namespace optigd::compat

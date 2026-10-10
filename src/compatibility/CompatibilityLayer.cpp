#include "CompatibilityLayer.hpp"
#include "../core/Logger.hpp"

namespace optigd::compat {

CompatibilityLayer& CompatibilityLayer::instance() {
    static CompatibilityLayer s_instance;
    return s_instance;
}

void CompatibilityLayer::initialize(std::string_view gdVersion, std::string_view geodeVersion) {
    m_gdVersion = std::string(gdVersion);
    m_geodeVersion = std::string(geodeVersion);

    // Descriptions state what is connected in this release. "Available" means the feature
    // may be used if its config flag is on; it does not mean the feature is connected.
    registerFeature("spatial_culling", "Spatial index and camera-rectangle shadow measurement (no gameplay effect)");
    registerFeature("particle_governor", "Particle emitter sleep and density policy (not connected to particles in v0.1.0-beta1)");
    registerFeature("trigger_indexer", "Trigger index model (not connected to gameplay in v0.1.0-beta1)");
    registerFeature("matrix_cache", "Local transform cache (not connected in v0.1.0-beta1)");
    registerFeature("adaptive_governor", "Effective tier selection from frame intervals");
    registerFeature("memory_arena", "Per-frame bump allocator (diagnostic counters only)");

    OPTIGD_LOG_INFO(std::string("CompatibilityLayer initialized for GD ") +
                    m_gdVersion + ", Geode " + m_geodeVersion);
}

void CompatibilityLayer::registerFeature(std::string_view featureName, std::string_view description) {
    m_features[std::string(featureName)] = {
        std::string(featureName),
        std::string(description),
        CapabilityStatus::Available,
        ""
    };
}

void CompatibilityLayer::reportConflict(std::string_view featureName, std::string_view reason) {
    auto it = m_features.find(std::string(featureName));
    if (it != m_features.end()) {
        it->second.status = CapabilityStatus::Conflicted;
        it->second.conflictReason = std::string(reason);
        OPTIGD_LOG_WARN(std::string("Feature '") + std::string(featureName) +
                        "' marked as Conflicted: " + std::string(reason) +
                        ". Disabling feature gracefully to preserve game stability.");
    }
}

bool CompatibilityLayer::isFeatureUsable(std::string_view featureName) const {
    auto it = m_features.find(std::string(featureName));
    if (it == m_features.end()) return false;
    return it->second.status == CapabilityStatus::Available;
}

void CompatibilityLayer::disableFeature(std::string_view featureName, std::string_view reason) {
    auto it = m_features.find(std::string(featureName));
    if (it != m_features.end()) {
        it->second.status = CapabilityStatus::Disabled;
        it->second.conflictReason = std::string(reason);
        OPTIGD_LOG_INFO(std::string("Feature '") + std::string(featureName) +
                        "' disabled: " + std::string(reason));
    }
}

void CompatibilityLayer::enableFeature(std::string_view featureName) {
    auto it = m_features.find(std::string(featureName));
    if (it != m_features.end()) {
        it->second.status = CapabilityStatus::Available;
        it->second.conflictReason.clear();
        OPTIGD_LOG_INFO(std::string("Feature '") + std::string(featureName) + "' enabled.");
    }
}

std::vector<ModFeatureInfo> CompatibilityLayer::getAllFeatures() const {
    std::vector<ModFeatureInfo> list;
    list.reserve(m_features.size());
    for (const auto& [_, info] : m_features) {
        list.push_back(info);
    }
    return list;
}

} // namespace optigd::compat

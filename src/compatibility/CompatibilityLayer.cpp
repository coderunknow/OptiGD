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

    registerFeature("spatial_culling", "2D Spatial Hash Frustum Culling");
    registerFeature("particle_governor", "Particle Emitter Sleep & Density Scaling");
    registerFeature("trigger_indexer", "Spatial & Temporal Trigger Dispatch");
    registerFeature("matrix_cache", "CCNode Transform Matrix Cache");
    registerFeature("adaptive_governor", "Dynamic Frame-Time Adaptation");
    registerFeature("memory_arena", "Per-Frame Bump Allocation");

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

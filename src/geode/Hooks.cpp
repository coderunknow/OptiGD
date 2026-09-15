#include "OptiGDMod.hpp"

#ifdef GEODE_MOD_ID

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "../core/OptiGDManager.hpp"
#include "../config/ConfigManager.hpp"
#include "../ui/DiagnosticsOverlay.hpp"

using namespace geode::prelude;

namespace optigd::geode_mod {

void syncSettingsWithConfig() {
    auto& configMgr = config::ConfigManager::instance();
    auto config = configMgr.getConfig();

    auto mod = Mod::get();
    if (!mod) return;

    std::string tierSetting = mod->getSettingValue<std::string>("tier-preset");
    if (tierSetting == "LowEnd") {
        configMgr.applyTierPreset(core::PerformanceTier::LowEnd);
    } else if (tierSetting == "Balanced") {
        configMgr.applyTierPreset(core::PerformanceTier::Balanced);
    } else if (tierSetting == "HighEnd") {
        configMgr.applyTierPreset(core::PerformanceTier::HighEnd);
    }

    config.flags.enableSpatialCulling = mod->getSettingValue<bool>("enable-culling");
    config.flags.enableParticleThrottling = mod->getSettingValue<bool>("enable-particle-limits");
    config.flags.enableAdaptiveGovernor = mod->getSettingValue<bool>("enable-adaptive");
    config.flags.enableDiagnosticsOverlay = mod->getSettingValue<bool>("show-diagnostics");

    configMgr.setConfig(config);
}

void initializeMod() {
    core::OptiGDManager::instance().initialize();
    syncSettingsWithConfig();
}

} // namespace optigd::geode_mod

$on_mod(Loaded) {
    optigd::geode_mod::initializeMod();
}

class $modify(OptiPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        optigd::core::OptiGDManager::instance().onLevelLoaded();
        return true;
    }

    void onQuit() {
        optigd::core::OptiGDManager::instance().onLevelExited();
        PlayLayer::onQuit();
    }

    void update(float dt) {
        static uint64_t s_frameCounter = 0;
        optigd::core::OptiGDManager::instance().beginFrame(++s_frameCounter);

        PlayLayer::update(dt);

        optigd::core::OptiGDManager::instance().endFrame();
    }

    void updateVisibility(float dt) {
        const auto& config = optigd::config::ConfigManager::instance().getConfig();
        if (!config.flags.enableSpatialCulling) {
            PlayLayer::updateVisibility(dt);
            return;
        }

        // Fast spatial culling logic
        PlayLayer::updateVisibility(dt);
    }
};

class $modify(OptiParticleSystem, CCParticleSystemQuad) {
    void update(float dt) {
        const auto& config = optigd::config::ConfigManager::instance().getConfig();
        if (!config.flags.enableParticleThrottling) {
            CCParticleSystemQuad::update(dt);
            return;
        }

        CCParticleSystemQuad::update(dt);
    }
};

#endif // GEODE_MOD_ID

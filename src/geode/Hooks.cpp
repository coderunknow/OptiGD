#include "OptiGDMod.hpp"

#ifdef GEODE_MOD_ID

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "../config/ConfigManager.hpp"
#include "../config/SettingsMapping.hpp"
#include "../core/OptiGDManager.hpp"
#include "../core/Version.hpp"
#include "../diagnostics/FrameTimeRecorder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace optigd::geode_mod {

namespace {

// Shadow-culling bookkeeping: one entry per registered game object.
struct ShadowEntry {
    optimization::VisibilityCuller::Handle handle;
    GameObject* object;
};

std::vector<ShadowEntry> s_shadowEntries;
diagnostics::FrameTimeRecorder s_frameTimeRecorder;

core::Rect2D toRect(const CCRect& r) {
    return core::Rect2D(r.origin.x, r.origin.y, r.size.width, r.size.height);
}

// Camera rectangle in the object layer's space, derived from the window corners.
// UNVERIFIED: this assumes convertToNodeSpace maps the window's world corners into the
// layer's space as expected. Confirm in a real run before trusting shadow counts.
core::Rect2D cameraRectInLayer(CCNode* layer) {
    const CCSize win = CCDirector::sharedDirector()->getWinSize();
    const CCPoint a = layer->convertToNodeSpace(CCPoint(0.0f, 0.0f));
    const CCPoint b = layer->convertToNodeSpace(CCPoint(win.width, win.height));
    return core::Rect2D(std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
}

// Registers every object of the current level with the spatial index.
// UNVERIFIED: GJBaseGameLayer::m_objects is a cocos2d::CCArray. Accessors count() and
// objectAtIndex() are commented out in bindings/2.2081/Cocos2d.bro, so the first CI
// compile is expected to confirm or reject this call.
void registerShadowObjects(PlayLayer* layer) {
    s_shadowEntries.clear();
    auto& mgr = core::OptiGDManager::instance();
    CCArray* objects = layer->m_objects;
    if (!objects) return;

    const unsigned int count = objects->count();
    s_shadowEntries.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        auto* obj = static_cast<GameObject*>(objects->objectAtIndex(i));
        if (!obj) continue;
        const auto handle = mgr.getCuller().registerObject(toRect(obj->getObjectRect()), obj, false);
        if (handle != optimization::VisibilityCuller::kInvalidHandle) {
            s_shadowEntries.push_back({handle, obj});
        }
    }
}

void refreshShadowBounds(optimization::VisibilityCuller& culler) {
    for (const auto& entry : s_shadowEntries) {
        culler.updateObjectBounds(entry.handle, toRect(entry.object->getObjectRect()));
    }
}

void applyRecorderSetting(bool enabled) {
    if (!enabled) {
        s_frameTimeRecorder.close();
        return;
    }
    auto mod = Mod::get();
    const auto path = (mod->getSaveDir() / "frametimes.csv").string();
    if (!s_frameTimeRecorder.open(path)) {
        log::warn("OptiGD: could not open frame-time CSV at {}", path);
    } else {
        log::info("OptiGD: recording frame times to {}", path);
    }
}

} // namespace

void syncSettings() {
    auto mod = Mod::get();
    if (!mod) return;

    config::SettingsSnapshot snap;
    snap.tierName = mod->getSettingValue<std::string>("tier-preset");
    snap.enableCulling = mod->getSettingValue<bool>("enable-culling");
    snap.enableParticleLimits = mod->getSettingValue<bool>("enable-particle-limits");
    snap.enableAdaptive = mod->getSettingValue<bool>("enable-adaptive");
    snap.showDiagnostics = mod->getSettingValue<bool>("show-diagnostics");
    snap.recordFrameTimes = mod->getSettingValue<bool>("record-frametimes");
    snap.customParticleDensity = mod->getSettingValue<double>("custom-particle-density");
    snap.customMaxParticles = mod->getSettingValue<int64_t>("custom-max-particles");
    snap.customPaddingX = mod->getSettingValue<int64_t>("custom-padding-x");
    snap.customPaddingY = mod->getSettingValue<int64_t>("custom-padding-y");

    auto& configMgr = config::ConfigManager::instance();
    const auto resolved = config::resolveSettings(snap, configMgr.getConfig());
    if (!resolved.warning.empty()) {
        log::warn("OptiGD: {}", resolved.warning);
    }
    configMgr.setConfig(resolved.config);
    core::OptiGDManager::instance().onSettingsChanged();
    applyRecorderSetting(resolved.config.flags.enableFrameTimeRecorder);
}

void initializeMod() {
    core::OptiGDManager::instance().initialize();
    syncSettings();
}

} // namespace optigd::geode_mod

$on_mod(Loaded) {
    optigd::geode_mod::initializeMod();
    listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) {
        optigd::geode_mod::syncSettings();
    });
}

using namespace optigd::geode_mod;

// Hooks are not platform-guarded: the bindings declare PlayLayer for win and android
// (class-level [[link(android)]] in GeometryDash.bro). Confirm per-platform symbols in CI.
class $modify(OptiPlayLayer, PlayLayer) {
    struct Fields {
        uint64_t frameCounter = 0;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }
        auto& mgr = optigd::core::OptiGDManager::instance();
        mgr.onLevelLoaded();
        if (mgr.getUserConfig().flags.enableSpatialCulling) {
            registerShadowObjects(this);
        }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto& mgr = optigd::core::OptiGDManager::instance();
        mgr.onLevelLoaded();
        if (mgr.getUserConfig().flags.enableSpatialCulling) {
            registerShadowObjects(this);
        }
    }

    void onQuit() {
        optigd::core::OptiGDManager::instance().onLevelExited();
        s_shadowEntries.clear();
        PlayLayer::onQuit();
    }

    void postUpdate(float dt) {
        auto& mgr = optigd::core::OptiGDManager::instance();
        mgr.beginFrame(++m_fields->frameCounter);

        mgr.markGameUpdateBegin();
        PlayLayer::postUpdate(dt);
        mgr.markGameUpdateEnd();

        if (mgr.getUserConfig().flags.enableSpatialCulling && !s_shadowEntries.empty() && m_objectLayer) {
            const auto cameraRect = cameraRectInLayer(m_objectLayer);
            mgr.recordCullingShadow(cameraRect, [](optigd::optimization::VisibilityCuller& culler) {
                refreshShadowBounds(culler);
            });
        }

        mgr.endFrame();

        if (s_frameTimeRecorder.isOpen()) {
            s_frameTimeRecorder.append(mgr.getCurrentFrameStats(),
                                       optigd::core::tierToString(mgr.getEffectiveConfig().tier));
        }
    }
};

class $modify(OptiMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }
        log::info("OptiGD v{} active.", optigd::kOptiGDVersion);
        return true;
    }
};

#endif // GEODE_MOD_ID

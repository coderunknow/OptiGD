#include "OptiGDMod.hpp"
#include "../core/Logger.hpp"

#ifdef GEODE_IS_COMPILING

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <chrono>

using namespace geode::prelude;

namespace optigd::geode_integration {

void syncSettingsWithConfig() {
    auto* mod = Mod::get();
    if (!mod) return;

    config::RuntimeSettingsInput input{};
    input.tierPreset = mod->getSettingValue<std::string>("tier-preset");
    input.targetFrameRate = static_cast<float>(mod->getSettingValue<double>("target-fps"));
    input.enableSpatialCulling = mod->getSettingValue<bool>("enable-spatial-culling");
    input.enableParticleThrottling = mod->getSettingValue<bool>("enable-particle-throttling");
    input.enableAdaptiveGovernor = mod->getSettingValue<bool>("enable-adaptive-governor");
    input.enableMatrixCaching = mod->getSettingValue<bool>("enable-matrix-caching");
    input.enableTriggerIndexing = mod->getSettingValue<bool>("enable-trigger-indexing");
    input.enableDiagnosticsOverlay = mod->getSettingValue<bool>("enable-diagnostics-overlay");
    input.exportDiagnosticsOnExit = mod->getSettingValue<bool>("export-diagnostics-on-exit");

    input.customParticleDensity = static_cast<float>(mod->getSettingValue<double>("custom-particle-density"));
    input.customMaxParticles = static_cast<uint32_t>(mod->getSettingValue<int64_t>("custom-max-particles"));
    input.customCullOffscreenParticles = mod->getSettingValue<bool>("custom-cull-offscreen-particles");
    input.customCameraPaddingX = static_cast<float>(mod->getSettingValue<double>("custom-camera-padding-x"));
    input.customCameraPaddingY = static_cast<float>(mod->getSettingValue<double>("custom-camera-padding-y"));

    config::ConfigManager::instance().syncFromSettings(input);
    core::OptiGDManager::instance().reloadConfig();
}

void onModLoaded() {
    core::Logger::instance().setCallback([](core::LogLevel lvl, std::string_view msg) {
        switch (lvl) {
            case core::LogLevel::Debug: log::debug("{}", msg); break;
            case core::LogLevel::Info:  log::info("{}", msg); break;
            case core::LogLevel::Warn:  log::warn("{}", msg); break;
            case core::LogLevel::Error: log::error("{}", msg); break;
        }
    });

    std::string geodeVer = Loader::get()->getVersion().toVString();
    std::string gdVer = Loader::get()->getGameVersion();
    if (gdVer.empty()) {
        gdVer = std::string(core::kTargetGDVersion);
    }

    core::OptiGDManager::instance().initialize(gdVer, geodeVer);
    syncSettingsWithConfig();

    listenForSettingChanges<std::string>("tier-preset", [](std::string) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<double>("target-fps", [](double) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("enable-spatial-culling", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("enable-particle-throttling", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("enable-adaptive-governor", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("enable-matrix-caching", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("enable-trigger-indexing", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("enable-diagnostics-overlay", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("export-diagnostics-on-exit", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<double>("custom-particle-density", [](double) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<int64_t>("custom-max-particles", [](int64_t) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<bool>("custom-cull-offscreen-particles", [](bool) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<double>("custom-camera-padding-x", [](double) {
        syncSettingsWithConfig();
    });
    listenForSettingChanges<double>("custom-camera-padding-y", [](double) {
        syncSettingsWithConfig();
    });
}

namespace {

core::Rect2D computeCameraWorldRect(GJBaseGameLayer* layer) {
    auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
    float zoom = (layer && layer->m_gameState.m_cameraZoom > 0.01f)
                     ? layer->m_gameState.m_cameraZoom
                     : 1.0f;
    float camX = layer ? layer->m_gameState.m_cameraPosition.x : 0.0f;
    float camY = layer ? layer->m_gameState.m_cameraPosition.y : 0.0f;
    return core::Rect2D(camX, camY, winSize.width / zoom, winSize.height / zoom);
}

core::Rect2D extractGameObjectBounds(GameObject* object, bool useMatrixCache) {
    if (!object) return core::Rect2D{};
    auto pos = object->getPosition();
    float w = (object->m_width > 0.0f) ? object->m_width : 30.0f;
    float h = (object->m_height > 0.0f) ? object->m_height : 30.0f;

    if (useMatrixCache) {
        auto mat = optimization::MatrixCache::instance().getOrCompute(
            object,
            pos.x,
            pos.y,
            object->getRotation(),
            object->m_scaleX,
            object->m_scaleY
        );
        core::Rect2D localRect(-w * 0.5f, -h * 0.5f, w, h);
        return mat.transformRect(localRect);
    }

    float sx = std::abs(object->m_scaleX) > 0.01f ? std::abs(object->m_scaleX) : 1.0f;
    float sy = std::abs(object->m_scaleY) > 0.01f ? std::abs(object->m_scaleY) : 1.0f;
    float scaledW = w * sx;
    float scaledH = h * sy;
    return core::Rect2D(pos.x - scaledW * 0.5f, pos.y - scaledH * 0.5f, scaledW, scaledH);
}

} // namespace

} // namespace optigd::geode_integration

$on_mod(Loaded) {
    optigd::geode_integration::onModLoaded();
}

// ============================================================================
// Cross-Platform GJBaseGameLayer Hook (Windows x64, Android32, Android64)
// ============================================================================
class $modify(OptiGDBaseGameLayer, GJBaseGameLayer) {
    struct Fields {
        std::chrono::steady_clock::time_point lastFrameTimestamp{};
        bool hasLastFrameTimestamp{false};
    };

    void update(float dt) {
        auto& mgr = optigd::core::OptiGDManager::instance();
        auto now = std::chrono::steady_clock::now();

        double interFrameUs = static_cast<double>(dt) * 1000000.0;
        if (m_fields->hasLastFrameTimestamp) {
            double wallDeltaUs = std::chrono::duration<double, std::micro>(
                now - m_fields->lastFrameTimestamp).count();
            if (wallDeltaUs > 100.0 && wallDeltaUs < 250000.0) {
                interFrameUs = wallDeltaUs;
            }
        }
        m_fields->lastFrameTimestamp = now;
        m_fields->hasLastFrameTimestamp = true;

        mgr.beginFrame();
        mgr.setExternalFrameDeltaUs(interFrameUs);

        GJBaseGameLayer::update(dt);

        mgr.endFrame();
    }

    void updateObjectSection(GameObject* object) {
        GJBaseGameLayer::updateObjectSection(object);
        if (!object) return;

        auto& mgr = optigd::core::OptiGDManager::instance();
        const auto& cfg = mgr.getActiveConfig();

        if (cfg.flags.enableMatrixCaching) {
            optigd::optimization::MatrixCache::instance().markDirty(object);
        }
        if (cfg.flags.enableSpatialCulling) {
            auto bounds = optigd::geode_integration::extractGameObjectBounds(
                object, cfg.flags.enableMatrixCaching);
            mgr.getVisibilityCuller().updateObjectBoundsByPtr(object, bounds);
        }
    }

    void removeObjectFromSection(GameObject* object) {
        GJBaseGameLayer::removeObjectFromSection(object);
        if (!object) return;

        optigd::optimization::MatrixCache::instance().evict(object);
    }

    void spawnGroup(int group, bool ordered, double delay, gd::vector<int> const& remapKeys, int triggerID, int controlID) {
        GJBaseGameLayer::spawnGroup(group, ordered, delay, remapKeys, triggerID, controlID);
        if (group <= 0 || delay > 0.0) return;

        auto& mgr = optigd::core::OptiGDManager::instance();
        if (mgr.getActiveConfig().flags.enableTriggerIndexing &&
            optigd::compat::CompatibilityLayer::instance().isFeatureUsable("trigger_indexer")) {
            std::vector<void*> spawned;
            mgr.getTriggerOptimizer().invokeSpawnGroup(static_cast<uint32_t>(group), spawned);
        }
    }

    void toggleGroup(int id, bool activate) {
        GJBaseGameLayer::toggleGroup(id, activate);
        if (id <= 0) return;

        auto& mgr = optigd::core::OptiGDManager::instance();
        mgr.getTriggerOptimizer().setGroupEnabled(static_cast<uint32_t>(id), activate);
    }
};

// ============================================================================
// Cross-Platform PlayLayer Hook (Windows x64, Android32, Android64)
// ============================================================================
class $modify(OptiGDPlayLayer, PlayLayer) {
    struct Fields {
        cocos2d::CCLabelBMFont* overlayLabel{nullptr};
        uint32_t registeredObjectSeq{0};
        uint32_t registeredTriggerSeq{0};
        uint32_t frameTick{0};
        std::vector<void*> newlyVisibleBuffer;
        std::vector<void*> newlyCulledBuffer;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        optigd::geode_integration::syncSettingsWithConfig();
        optigd::core::OptiGDManager::instance().onLevelLoad();

        m_fields->registeredObjectSeq = 0;
        m_fields->registeredTriggerSeq = 0;
        m_fields->frameTick = 0;
        m_fields->newlyVisibleBuffer.reserve(512);
        m_fields->newlyCulledBuffer.reserve(512);

        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        m_fields->overlayLabel = cocos2d::CCLabelBMFont::create("", "chatFont.fnt");
        if (m_fields->overlayLabel) {
            m_fields->overlayLabel->setScale(0.45f);
            m_fields->overlayLabel->setAnchorPoint({0.0f, 1.0f});
            auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
            m_fields->overlayLabel->setPosition({10.0f, winSize.height - 10.0f});
            m_fields->overlayLabel->setZOrder(9999);
            m_fields->overlayLabel->setOpacity(220);
            m_fields->overlayLabel->setVisible(optigd::ui::DiagnosticsOverlay::instance().isEnabled());
            this->addChild(m_fields->overlayLabel);
        }

        return true;
    }

    void addObject(GameObject* object) {
        PlayLayer::addObject(object);
        if (!object) return;

        auto& mgr = optigd::core::OptiGDManager::instance();
        const auto& cfg = mgr.getActiveConfig();

        bool isDeco = (object->m_isDecoration || object->m_isDecoration2) && !object->m_isTrigger;
        bool isStatic = !object->m_isTrigger;
        uint32_t objId = (object->m_uniqueID > 0)
                             ? static_cast<uint32_t>(object->m_uniqueID)
                             : (++m_fields->registeredObjectSeq);

        auto bounds = optigd::geode_integration::extractGameObjectBounds(
            object, cfg.flags.enableMatrixCaching);

        mgr.getVisibilityCuller().registerObject(objId, bounds, object, isStatic, isDeco);

        if (object->m_isTrigger) {
            uint32_t trigId = ++m_fields->registeredTriggerSeq;
            uint32_t groupId = 0;
            if (object->m_groups && object->m_groupCount > 0 && (*object->m_groups)[0] > 0) {
                groupId = static_cast<uint32_t>((*object->m_groups)[0]);
            }

            optigd::optimization::TriggerType trigType = optigd::optimization::TriggerType::TouchOrPosition;
            bool isMulti = false;
            int32_t spawnOrder = 0;

            if (auto* eff = typeinfo_cast<EffectGameObject*>(object)) {
                isMulti = eff->m_isMultiTriggered;
                spawnOrder = eff->m_spawnOrder;
                if (eff->m_isSpawnTriggered) {
                    trigType = optigd::optimization::TriggerType::Spawn;
                } else if (eff->m_isTouchTriggered) {
                    trigType = optigd::optimization::TriggerType::TouchOnly;
                }
            }

            mgr.getTriggerOptimizer().registerTrigger(
                trigId, groupId, trigType, object->getPositionX(),
                object, isMulti, spawnOrder, bounds);
        }

        if (object->m_particle != nullptr) {
            auto pos = object->getPosition();
            mgr.getParticleGovernor().registerOrUpdateByPtr(
                object->m_particle,
                {pos.x, pos.y},
                64.0f,
                isDeco ? optigd::optimization::ParticlePriority::Cosmetic
                       : optigd::optimization::ParticlePriority::Standard,
                object->m_particle->getEmissionRate(),
                object->m_particle->getDuration(),
                object->m_particle->isAutoRemoveOnFinish()
            );
        }
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        optigd::core::OptiGDManager::instance().getTriggerOptimizer().prepareLevel();
    }

    void updateVisibility(float dt) {
        // Always run native visibility logic first so section activation and gameplay triggers remain intact
        PlayLayer::updateVisibility(dt);

        auto& mgr = optigd::core::OptiGDManager::instance();
        const auto& cfg = mgr.getActiveConfig();

        if (!cfg.flags.enableSpatialCulling ||
            !optigd::compat::CompatibilityLayer::instance().isFeatureUsable("spatial_culling")) {
            return;
        }

        auto startTime = std::chrono::steady_clock::now();
        auto cameraRect = optigd::geode_integration::computeCameraWorldRect(this);

        // Arena-backed differential frustum query (avoids heap allocations and O(N) scans)
        mgr.recordHeapAllocationAvoided(1);
        mgr.getVisibilityCuller().evaluateVisibilityDelta(
            cameraRect,
            mgr.getFrameArena(),
            &m_fields->newlyVisibleBuffer,
            &m_fields->newlyCulledBuffer
        );

        // Apply decorative visibility transitions safely without touching gameplay hitboxes
        for (void* ptr : m_fields->newlyCulledBuffer) {
            auto* obj = static_cast<GameObject*>(ptr);
            if (!obj || obj->m_isTrigger) continue;
            if (obj->m_isDecoration || obj->m_isDecoration2) {
                obj->setVisible(false);
                if (obj->m_glowSprite) {
                    obj->m_glowSprite->setVisible(false);
                }
            }
        }

        for (void* ptr : m_fields->newlyVisibleBuffer) {
            auto* obj = static_cast<GameObject*>(ptr);
            if (!obj || obj->m_isTrigger) continue;
            if (!obj->m_isHide && !obj->m_isInvisible && !obj->m_isDisabled && !obj->m_isDisabled2) {
                obj->setVisible(true);
                if (obj->m_glowSprite && !obj->m_hasNoGlow) {
                    obj->m_glowSprite->setVisible(true);
                }
            }
        }

        auto endTime = std::chrono::steady_clock::now();
        double cullUs = std::chrono::duration<double, std::micro>(endTime - startTime).count();
        mgr.recordSubsystemOverheadUs(cullUs);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        auto& mgr = optigd::core::OptiGDManager::instance();
        const auto& cfg = mgr.getActiveConfig();

        if (cfg.flags.enableTriggerIndexing &&
            optigd::compat::CompatibilityLayer::instance().isFeatureUsable("trigger_indexer") &&
            this->m_player1 != nullptr) {
            auto t0 = std::chrono::steady_clock::now();
            mgr.getTriggerOptimizer().inspectLookaheadWindow(this->m_player1->getPositionX());
            auto t1 = std::chrono::steady_clock::now();
            mgr.recordSubsystemOverheadUs(
                std::chrono::duration<double, std::micro>(t1 - t0).count());
        }

        m_fields->frameTick++;
        if (m_fields->overlayLabel) {
            bool showOverlay = optigd::ui::DiagnosticsOverlay::instance().isEnabled();
            m_fields->overlayLabel->setVisible(showOverlay);
            if (showOverlay && (m_fields->frameTick % 10 == 0)) {
                std::string text = optigd::ui::DiagnosticsOverlay::instance().formatOverlayText(
                    mgr.getLatestFrameStats(),
                    mgr.getAdaptiveGovernor().getRecentStats(),
                    cfg
                );
                m_fields->overlayLabel->setString(text.c_str());
            }
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        optigd::core::OptiGDManager::instance().onLevelReset();
    }

    void onQuit() {
        auto& mgr = optigd::core::OptiGDManager::instance();
        if (mgr.getActiveConfig().flags.exportDiagnosticsOnExit) {
            auto saveDir = Mod::get()->getSaveDir();
            std::string reportPath = (saveDir / "optigd_diagnostics.json").string();
            optigd::diagnostics::DiagnosticsEngine::instance().exportJsonReportToFile(
                reportPath,
                mgr.getActiveConfig(),
                mgr.getFallbackManager().getFallbackHistory(),
                mgr.getAdaptiveGovernor().getTierTransitions()
            );
            log::info("OptiGD exported diagnostics report to {}", reportPath);
        }

        mgr.onLevelExit();
        PlayLayer::onQuit();
    }
};

// ============================================================================
// Cross-Platform CCParticleSystem Hook (Windows x64, Android32, Android64)
// ============================================================================
class $modify(OptiGDParticleSystem, cocos2d::CCParticleSystem) {
    struct Fields {
        uint32_t emitterId{0};
        float initialEmissionRate{0.0f};
        bool tracked{false};
    };

    void update(float dt) {
        auto* playLayer = PlayLayer::get();
        if (!playLayer) {
            cocos2d::CCParticleSystem::update(dt);
            return;
        }

        auto& mgr = optigd::core::OptiGDManager::instance();
        const auto& cfg = mgr.getActiveConfig();
        if (!cfg.flags.enableParticleThrottling ||
            !optigd::compat::CompatibilityLayer::instance().isFeatureUsable("particle_governor")) {
            cocos2d::CCParticleSystem::update(dt);
            return;
        }

        auto startTime = std::chrono::steady_clock::now();
        auto& gov = mgr.getParticleGovernor();

        auto worldPos = this->getPosition();
        if (this->getParent() != nullptr && this->getPositionType() == cocos2d::kCCPositionTypeGrouped) {
            worldPos = this->getParent()->convertToWorldSpace(worldPos);
        }

        if (!m_fields->tracked) {
            m_fields->initialEmissionRate = (this->m_fEmissionRate > 0.0f) ? this->m_fEmissionRate : 30.0f;
            auto prio = optigd::optimization::ParticlePriority::Standard;
            if (playLayer->m_player1 && this->getParent() == playLayer->m_player1) {
                prio = optigd::optimization::ParticlePriority::Critical;
            } else if (playLayer->m_player2 && this->getParent() == playLayer->m_player2) {
                prio = optigd::optimization::ParticlePriority::Critical;
            }

            m_fields->emitterId = gov.registerOrUpdateByPtr(
                this,
                {worldPos.x, worldPos.y},
                64.0f,
                prio,
                m_fields->initialEmissionRate,
                this->m_fDuration,
                this->m_bIsAutoRemoveOnFinish
            );
            m_fields->tracked = true;
        } else {
            gov.updateEmitterPosition(m_fields->emitterId, {worldPos.x, worldPos.y});
        }

        auto cameraRect = optigd::geode_integration::computeCameraWorldRect(playLayer);
        auto eval = gov.evaluateEmitterDetailed(m_fields->emitterId, cameraRect, dt);

        if (!eval.shouldUpdate) {
            if (eval.justEnteredSleep) {
                this->m_uParticleCount = 0;
                this->m_fEmitCounter = 0.0f;
            }
            auto endTime = std::chrono::steady_clock::now();
            mgr.recordSubsystemOverheadUs(
                std::chrono::duration<double, std::micro>(endTime - startTime).count());
            return;
        }

        if (eval.justWokeFromSleep) {
            this->m_fEmitCounter = 0.0f;
        }

        // Non-compounding emission rate derived from initial base rate
        this->m_fEmissionRate = eval.effectiveEmissionRate;
        cocos2d::CCParticleSystem::update(eval.clampedDt);
        gov.recordActiveParticles(m_fields->emitterId, this->m_uParticleCount);

        auto endTime = std::chrono::steady_clock::now();
        mgr.recordSubsystemOverheadUs(
            std::chrono::duration<double, std::micro>(endTime - startTime).count());
    }
};

#endif // GEODE_IS_COMPILING

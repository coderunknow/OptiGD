#include "OptiGDMod.hpp"
#include "BenchmarkUI.hpp"
#include "../core/Logger.hpp"
#include "../ui/BenchmarkPanel.hpp"

#ifdef GEODE_IS_COMPILING

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCParticleSystem.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
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
    // `reloadConfig()` reads the ACTIVE configuration layer, so while a
    // benchmark session owns a transient override this re-reads the override and
    // leaves the user's saved settings alone. That is the property Benchmark
    // Mode depends on: `PlayLayer::init` calls this function on every level
    // load, and it must not be able to undo a capture in progress.
    core::OptiGDManager::instance().reloadConfig();

    // Benchmark settings are policy, not configuration: they never enter
    // OptiConfig and therefore never enter a saved session's configuration
    // block, which must describe the optimization settings only.
    applyBenchmarkSettingsToPolicy();
}

void onModLoaded() {
    core::Logger::instance().setCallback([](core::LogLevel lvl, std::string_view msg) {
        switch (lvl) {
            case core::LogLevel::Debug: log::debug("{}", msg); break;
            case core::LogLevel::Info:  log::info("{}", msg); break;
            case core::LogLevel::Warn:  log::warn("{}", msg); break;
            case core::LogLevel::Error: log::error("{}", msg); break;
            case core::LogLevel::None:  break;
        }
    });

    std::string geodeVer = Loader::get()->getVersion().toVString();
    std::string gdVer = Loader::get()->getGameVersion();
    if (gdVer.empty()) {
        gdVer = std::string(core::kTargetGDVersion);
    }

    core::OptiGDManager::instance().initialize(gdVer, geodeVer);

    // Wires the Geode-backed services (save directory, active configuration,
    // level identity, environment, clocks) into the platform-agnostic benchmark
    // controller before anything can start a session.
    installBenchmarkRuntimeBridge();

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
    listenForSettingChanges<int64_t>("benchmark-duration-seconds", [](int64_t) {
        applyBenchmarkSettingsToPolicy();
    });
    listenForSettingChanges<bool>("benchmark-stop-on-death", [](bool) {
        applyBenchmarkSettingsToPolicy();
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
        bool lastPaused{false};
    };

    void update(float dt) {
        // beta.3 fix (defect C): `GJBaseGameLayer` is also the base class of
        // `LevelEditorLayer`, so beta.2 folded editor frames into gameplay
        // telemetry. Measurement now runs only for the live PlayLayer.
        auto* playLayer = PlayLayer::get();
        // beta.3 fix (defect Y): `GJBaseGameLayer::update` keeps running after the
        // level ends - through the end animation, the completion effects and the
        // results screen. Those frames are cheap to render, so counting them as
        // gameplay would inflate avg FPS and pull every percentile down in exactly
        // the sessions a player is most likely to keep open (a successful run).
        // GD's own `PlayLayer::canPauseGame()` is
        // `!m_hasCompletedLevel && !m_levelEndAnimationStarted`, but that
        // reconstruction is `win inline` only, so the members are read directly to
        // stay available on Android32/Android64 (both are plain data members:
        // `m_hasCompletedLevel` on PlayLayer, `m_levelEndAnimationStarted` on
        // GJBaseGameLayer). The attempt's own intro and death animation frames are
        // still measured on purpose: the level is being played during those, and
        // they are symmetric between a baseline and an optimized capture.
        const bool isGameplayFrame =
            (playLayer != nullptr) &&
            (static_cast<cocos2d::CCNode*>(playLayer) == static_cast<cocos2d::CCNode*>(this)) &&
            !playLayer->m_hasCompletedLevel &&
            !playLayer->m_levelEndAnimationStarted;
        if (!isGameplayFrame) {
            GJBaseGameLayer::update(dt);
            return;
        }

        auto& mgr = optigd::core::OptiGDManager::instance();
        auto now = std::chrono::steady_clock::now();

        double interFrameUs = static_cast<double>(dt) * 1000000.0;
        if (m_fields->hasLastFrameTimestamp) {
            double wallDeltaUs = std::chrono::duration<double, std::micro>(
                now - m_fields->lastFrameTimestamp).count();
            // beta.3 fix (defect O): the upper bound is gone. beta.2 accepted the
            // wall delta only below 250 ms and otherwise silently substituted
            // the engine's clamped `dt`, which turned a multi-second pause,
            // alt-tab or load stall into an ordinary-looking fast frame. The
            // real interval is now passed through unchanged and classified
            // downstream against `core::kStallThresholdUs`, so a stall is
            // visible in the level report and excludable from a benchmark
            // capture. The 100 us lower bound stays: it rejects a bogus delta
            // when update() runs twice within one tick.
            if (wallDeltaUs > 100.0) {
                interFrameUs = wallDeltaUs;
            }
        }
        m_fields->lastFrameTimestamp = now;
        m_fields->hasLastFrameTimestamp = true;

        // Pause observation. A paused game keeps calling update() at the display
        // rate, so a pause is invisible to interval-based gap detection and can
        // only be caught from the game's own state. Without this a benchmark
        // capture could be "completed" while the player sat in the pause menu.
        const bool paused = playLayer->m_isPaused;
        if (paused != m_fields->lastPaused) {
            m_fields->lastPaused = paused;
            if (paused) {
                optigd::benchmark::BenchmarkController::instance().onPauseObserved();
            } else {
                optigd::benchmark::BenchmarkController::instance().onResumeObserved();
            }
        }

        mgr.beginFrame();
        mgr.setExternalFrameDeltaUs(interFrameUs);
        mgr.setFramePaused(paused);

        GJBaseGameLayer::update(dt);

        mgr.endFrame();
    }

    void updateObjectSection(GameObject* object) {
        GJBaseGameLayer::updateObjectSection(object);
        if (!object) return;

        auto& mgr = optigd::core::OptiGDManager::instance();
        const auto& cfg = mgr.getActiveConfig();

        if (!cfg.flags.enableMatrixCaching && !cfg.flags.enableSpatialCulling) return;

        auto matrixStart = std::chrono::steady_clock::now();

        if (cfg.flags.enableMatrixCaching) {
            optigd::optimization::MatrixCache::instance().markDirty(object);
        }
        if (cfg.flags.enableSpatialCulling) {
            auto bounds = optigd::geode_integration::extractGameObjectBounds(
                object, cfg.flags.enableMatrixCaching);
            mgr.getVisibilityCuller().updateObjectBoundsByPtr(object, bounds);
        }

        auto matrixEnd = std::chrono::steady_clock::now();
        mgr.recordMatrixTimeUs(
            std::chrono::duration<double, std::micro>(matrixEnd - matrixStart).count());
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
        // Benchmark Mode HUD. Kept separate from the diagnostics overlay so the
        // two can be enabled independently, and placed in the opposite corner so
        // they cannot overlap.
        cocos2d::CCLabelBMFont* benchLabel{nullptr};
        float benchHideCountdown{0.0f};
        bool benchWasRunning{false};
        uint32_t registeredObjectSeq{0};
        uint32_t registeredTriggerSeq{0};
        uint32_t frameTick{0};
        // True while OptiGD's differential culling owns the visibility state of
        // decorative objects. Used to restore them when the feature is turned
        // off (or becomes unusable) mid-level instead of leaving them hidden.
        bool cullingEngaged{false};
        std::vector<void*> newlyVisibleBuffer;
        std::vector<void*> newlyCulledBuffer;
        std::vector<void*> restoreBuffer;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        optigd::geode_integration::syncSettingsWithConfig();
        optigd::core::OptiGDManager::instance().onLevelLoad();

        m_fields->registeredObjectSeq = 0;
        m_fields->registeredTriggerSeq = 0;
        m_fields->frameTick = 0;
        m_fields->cullingEngaged = false;
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

        auto benchWinSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        m_fields->benchLabel = cocos2d::CCLabelBMFont::create("", "chatFont.fnt");
        if (m_fields->benchLabel) {
            m_fields->benchLabel->setScale(0.42f);
            // beta.3 fix (defect Z): bottom-right, not top-right. GD 2.2's
            // standard play-layer layout puts the pause button top-right, and this
            // label is several lines tall at zOrder 9999, so beta.3's first
            // placement drew straight over the one control a player needs during a
            // capture. The diagnostics overlay owns the top-left corner and the
            // progress bar the top centre, which leaves the bottom-right free.
            // The label never swallows touches (it is not in a menu), so gameplay
            // input and the pause button keep working regardless - this is about
            // not hiding them. Placement is derived from GD's UI convention, not
            // from a rendered frame; it is item 7 of the maintainer smoke test.
            m_fields->benchLabel->setAnchorPoint({1.0f, 0.0f});
            m_fields->benchLabel->setPosition({benchWinSize.width - 10.0f, 10.0f});
            m_fields->benchLabel->setZOrder(9999);
            m_fields->benchLabel->setOpacity(225);
            m_fields->benchLabel->setColor({255, 214, 110});
            m_fields->benchLabel->setVisible(false);
            this->addChild(m_fields->benchLabel);
        }

        // A level load is a hard boundary for measurement: any capture still
        // open belongs to the previous level and must not continue into this one.
        optigd::benchmark::BenchmarkController::instance().onLevelExit();

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
            // Feature was just switched off: reveal everything OptiGD hid so the
            // level returns to its native (vanilla) visibility state.
            if (m_fields->cullingEngaged) {
                m_fields->cullingEngaged = false;
                m_fields->restoreBuffer.clear();
                mgr.getVisibilityCuller().restoreAllVisibility(&m_fields->restoreBuffer,
                                                               /*decorativeOnly=*/true);
                for (void* ptr : m_fields->restoreBuffer) {
                    auto* obj = static_cast<GameObject*>(ptr);
                    if (!obj || obj->m_isTrigger) continue;
                    if (!obj->m_isHide && !obj->m_isInvisible &&
                        !obj->m_isDisabled && !obj->m_isDisabled2) {
                        obj->setVisible(true);
                    }
                }
            }
            return;
        }
        m_fields->cullingEngaged = true;

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
        mgr.recordCullingTimeUs(cullUs);
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
            mgr.recordTriggerTimeUs(
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

        // --- Benchmark Mode HUD -------------------------------------------
        auto& controller = optigd::benchmark::BenchmarkController::instance();
        const bool benchRunning = controller.isRunning();
        if (m_fields->benchWasRunning && !benchRunning) {
            // The capture just reached a terminal state. The result is kept on
            // screen for a few seconds and announced, because the panel that
            // started it may already be closed.
            m_fields->benchHideCountdown = 8.0f;
            const auto finished = controller.getSnapshot();
            optigd::geode_integration::showBenchmarkNotification(
                std::string("OptiGD benchmark ") +
                optigd::benchmark::stateDisplayName(finished.state) +
                (finished.persisted ? " - saved locally" : " - NOT SAVED"),
                !finished.persisted);
        }
        m_fields->benchWasRunning = benchRunning;
        if (!benchRunning && m_fields->benchHideCountdown > 0.0f) {
            m_fields->benchHideCountdown -= dt;
        }
        if (m_fields->benchLabel) {
            const bool showBench = benchRunning || m_fields->benchHideCountdown > 0.0f;
            m_fields->benchLabel->setVisible(showBench);
            // Rebuilt every kHudRefreshFrames frames rather than every frame:
            // formatting a string per frame would make the instrumentation part
            // of what it measures. The live percentiles shown here are computed
            // with StatPrecision::Fast and are printed with a '~' marker; the
            // saved session always carries the exact values.
            if (showBench &&
                (m_fields->frameTick % optigd::ui::BenchmarkPanel::kHudRefreshFrames == 0)) {
                const auto snapshot = controller.getSnapshot();
                const auto live = controller.getLiveStatistics(
                    optigd::diagnostics::StatPrecision::Fast);
                m_fields->benchLabel->setString(
                    optigd::ui::BenchmarkPanel::formatHud(snapshot, live).c_str());
            }
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->cullingEngaged = false;
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
            mgr.recordParticleTimeUs(
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
        mgr.recordParticleTimeUs(
            std::chrono::duration<double, std::micro>(endTime - startTime).count());
    }
};

// ============================================================================
// Cross-Platform PauseLayer Hook: Benchmark Mode entry point
// (Windows x64, Android32, Android64 - PauseLayer is [[link(android)]] and
//  exposes `virtual void customSetup()` in bindings/2.2081)
// ============================================================================
class $modify(OptiGDPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto* director = cocos2d::CCDirector::get();
        if (!director) return;
        const auto winSize = director->getWinSize();

        // A dedicated CCMenu rather than the pause layer's own container:
        // PauseLayer builds its widgets inside customSetup() and does not expose
        // a menu member in the 2.2081 bindings.
        auto* menu = cocos2d::CCMenu::create();
        if (!menu) return;
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 100);

        auto* sprite = ButtonSprite::create("OptiGD Benchmark", "goldFont.fnt",
                                            "GJ_button_01.png", 0.7f);
        if (!sprite) return;
        sprite->setScale(0.85f);

        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(OptiGDPauseLayer::onBenchmark));
        if (!button) return;

        // Bottom-right. The pause menu's own widgets occupy the centre column
        // (restart / edit / settings), the left column (practice toggle, music
        // and SFX sliders) and the top-right corner (close), so this corner is
        // the least contested. NOTE: the placement is derived from the binding's
        // widget list, not from a visual check - no Geometry Dash runtime was
        // available in the build environment, so the exact on-screen position is
        // UNVERIFIED against a live 2.2081 pause menu.
        button->setPosition({winSize.width - 80.f, 44.f});
        menu->addChild(button);
    }

    void onBenchmark(cocos2d::CCObject*) {
        auto* popup = optigd::geode_integration::BenchmarkPopup::create();
        if (!popup) {
            optigd::geode_integration::showBenchmarkNotification(
                "OptiGD Benchmark Mode could not open its panel.", true);
            return;
        }
        popup->show();
    }
};

#endif // GEODE_IS_COMPILING

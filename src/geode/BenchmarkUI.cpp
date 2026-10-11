#include "BenchmarkUI.hpp"

#ifdef GEODE_IS_COMPILING

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlatformToolbox.hpp>

#include "../compatibility/CompatibilityLayer.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/OptiGDManager.hpp"
#include "../core/Types.hpp"
#include "../platform/Platform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>

using namespace geode::prelude;

namespace optigd::geode_integration {

namespace {

// Popup geometry. Kept as constants so the layout maths below is readable, and
// deliberately inside the smallest window size Geometry Dash runs at.
constexpr float kPopupWidth = 460.f;
constexpr float kPopupHeight = 296.f;
constexpr float kTextScale = 0.30f;
constexpr size_t kButtonsPerRow = 4;
constexpr float kButtonFontScale = 0.55f;
constexpr float kButtonSpriteScale = 0.85f;

uint64_t steadyNowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint64_t systemNowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string gdStringToStd(const gd::string& value) {
    return std::string(value);
}

benchmark::BenchmarkController& controller() {
    return benchmark::BenchmarkController::instance();
}

const std::shared_ptr<benchmark::BenchmarkStore>& store() {
    return controller().getStore();
}

// Best-effort level identity. Everything here is read from a live PlayLayer;
// nothing is invented when the game does not expose it, and `source` records
// where the values came from so a report can be qualified.
benchmark::LevelIdentity readLevelIdentity() {
    benchmark::LevelIdentity identity;
    auto* playLayer = PlayLayer::get();
    if (!playLayer) {
        identity.source = "unavailable (no active PlayLayer)";
        return identity;
    }

    identity.attempt = playLayer->m_attempts;
    identity.attemptKnown = true;
    identity.practiceMode = playLayer->m_isPracticeMode;
    identity.practiceModeKnown = true;

    auto* level = playLayer->m_level;
    if (level) {
        identity.name = gdStringToStd(level->m_levelName);
        identity.creator = gdStringToStd(level->m_creatorName);
        identity.levelId = static_cast<long long>(level->m_levelID.value());
        identity.levelIdKnown = true;
        identity.source = "PlayLayer::m_level";
    } else {
        identity.source = "PlayLayer (m_level was null)";
    }
    return identity;
}

benchmark::BenchmarkEnvironment readEnvironment() {
    benchmark::BenchmarkEnvironment env;
    auto& compat = compat::CompatibilityLayer::instance();
    env.optigdVersion = std::string(core::kOptiGDVersion);
    env.gdVersion = std::string(compat.getGDVersion());
    env.geodeVersion = std::string(compat.getGeodeVersion());

    auto hw = platform::Platform::getSystemInfo();
    env.osName = hw.osName;
    env.architecture = hw.architecture;
    env.compiler = hw.compiler;
    env.logicalCores = hw.logicalCores;
    env.totalRamMb = hw.totalRamMb;

    // The display refresh rate is readable; Geometry Dash's own frame-rate
    // limiter and VSync state are not reliably readable from a mod, and guessing
    // them would put a fabricated value into the evidence record.
    std::ostringstream hint;
    hint.precision(2);
    hint << std::fixed << "display refresh " << PlatformToolbox::getDeviceRefreshRate()
         << " Hz; GD frame-rate limiter/vsync not readable from a mod";
    env.refreshHint = hint.str();
    return env;
}

} // namespace

void showBenchmarkNotification(const std::string& text, bool isError) {
    auto* notification = Notification::create(
        text, isError ? NotificationIcon::Error : NotificationIcon::Info,
        isError ? NOTIFICATION_LONG_TIME : NOTIFICATION_DEFAULT_TIME);
    if (notification) notification->show();
}

void installBenchmarkRuntimeBridge() {
    auto& controller = benchmark::BenchmarkController::instance();

    // This is the only place allowed to make that claim: it runs inside the Geode
    // loader, in the game process. Sessions written by the standalone binaries
    // keep the default and are stamped [SIMULATED].
    controller.setLiveGameRuntime(true);

    benchmark::BenchmarkRuntimeBridge bridge;

    // Read the configuration the ENGINE is actually using, not the one
    // ConfigManager would hand out, so `verifyOverride()` detects a manager that
    // failed to pick the override up.
    bridge.readActiveConfig = [] {
        return core::OptiGDManager::instance().getActiveConfig();
    };

    // Installing the override must reach the subsystems in the same call, or the
    // first frames of a baseline capture would still run the user's settings.
    bridge.installOverride = [](const core::OptiConfig& config, const std::string& reason) {
        if (!config::ConfigManager::instance().setRuntimeOverride(config, reason)) {
            return false;
        }
        core::OptiGDManager::instance().reloadConfig();
        return true;
    };

    bridge.updateOverride = [](const core::OptiConfig& config) {
        if (!config::ConfigManager::instance().updateRuntimeOverride(config)) {
            return false;
        }
        core::OptiGDManager::instance().reloadConfig();
        return true;
    };

    bridge.clearOverride = [] {
        if (!config::ConfigManager::instance().clearRuntimeOverride()) {
            return false;
        }
        core::OptiGDManager::instance().reloadConfig();
        return true;
    };

    bridge.monotonicMs = &steadyNowMs;
    bridge.epochMs = &systemNowMs;
    bridge.levelIdentity = &readLevelIdentity;
    bridge.environment = &readEnvironment;

    controller.setRuntimeBridge(bridge);
    controller.setEnvironment(readEnvironment());

    auto* mod = Mod::get();
    if (mod) {
        auto root = mod->getSaveDir() / "benchmarks";
        controller.setStore(std::make_shared<benchmark::BenchmarkStore>(root));
        std::string error;
        if (!controller.getStore()->ensureDirectory(error)) {
            log::error("OptiGD could not create the benchmark directory {}: {}",
                       root.string(), error);
        }
    } else {
        log::error("OptiGD Benchmark Mode has no Mod handle; sessions cannot be saved.");
    }
}

void applyBenchmarkSettingsToPolicy() {
    auto* mod = Mod::get();
    if (!mod) return;

    auto policy = benchmark::BenchmarkController::instance().getPolicy();

    const double seconds = mod->getSettingValue<double>("benchmark-duration-seconds");
    if (std::isfinite(seconds) && seconds >= 1.0) {
        policy.targetDurationMs = seconds * 1000.0;
    }
    // A capture must never be longer than the wall-clock ceiling, or it could
    // only ever finish as `incomplete`.
    if (policy.targetDurationMs > policy.maxWallClockMs * 0.9) {
        policy.maxWallClockMs = policy.targetDurationMs / 0.9;
    }
    policy.stopOnLevelReset = mod->getSettingValue<bool>("benchmark-stop-on-death");

    benchmark::BenchmarkController::instance().setPolicy(policy);
}

// ===========================================================================
// BenchmarkPopup
// ===========================================================================

BenchmarkPopup* BenchmarkPopup::create() {
    auto* popup = new BenchmarkPopup();
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool BenchmarkPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight, "square01_001.png")) {
        return false;
    }

    this->setTitle("OptiGD Benchmark");

    // Children are added to the popup layer itself (not to `m_mainLayer`, whose
    // CopySizeLayout repositions the nodes it manages) and positioned from the
    // window centre, so nothing depends on a layout pass.
    auto* director = cocos2d::CCDirector::get();
    const auto winSize = director ? director->getWinSize() : cocos2d::CCSize{568.f, 320.f};
    const float left = winSize.width * 0.5f - kPopupWidth * 0.5f;
    const float bottom = winSize.height * 0.5f - kPopupHeight * 0.5f;

    m_textLabel = cocos2d::CCLabelBMFont::create("", "chatFont.fnt");
    if (!m_textLabel) return false;
    m_textLabel->setAnchorPoint({0.f, 1.f});
    m_textLabel->setScale(kTextScale);
    m_textLabel->setPosition({left + 16.f, bottom + kPopupHeight - 40.f});
    m_textLabel->setZOrder(2);
    this->addChild(m_textLabel);

    m_pageLabel = cocos2d::CCLabelBMFont::create("", "chatFont.fnt");
    if (m_pageLabel) {
        m_pageLabel->setAnchorPoint({1.f, 0.f});
        m_pageLabel->setScale(0.26f);
        m_pageLabel->setPosition({left + kPopupWidth - 16.f, bottom + 58.f});
        m_pageLabel->setZOrder(2);
        this->addChild(m_pageLabel);
    }

    m_menu = cocos2d::CCMenu::create();
    if (!m_menu) return false;
    m_menu->setPosition({0.f, 0.f});
    m_menu->setZOrder(101);
    this->addChild(m_menu);

    buildButtons();
    reloadSessions();
    setView(View::Main);

    this->scheduleUpdate();
    return true;
}

void BenchmarkPopup::buildButtons() {
    struct Spec {
        Btn id;
        const char* caption;
        cocos2d::SEL_MenuHandler selector;
    };
    // `menu_selector` only expands against a member of this class, so the table
    // is written out rather than generated.
    const Spec specs[] = {
        {Btn::StartBaseline, "Baseline 60s", menu_selector(BenchmarkPopup::onStartBaseline)},
        {Btn::StartOptimized, "Optimized 60s", menu_selector(BenchmarkPopup::onStartOptimized)},
        {Btn::Cancel, "Cancel", menu_selector(BenchmarkPopup::onCancel)},
        {Btn::Results, "Results", menu_selector(BenchmarkPopup::onResults)},
        {Btn::Compare, "Compare", menu_selector(BenchmarkPopup::onCompare)},
        {Btn::Detail, "Detail", menu_selector(BenchmarkPopup::onDetail)},
        {Btn::Protocol, "Protocol", menu_selector(BenchmarkPopup::onProtocol)},
        {Btn::External, "External", menu_selector(BenchmarkPopup::onExternal)},
        {Btn::Export, "Export", menu_selector(BenchmarkPopup::onExport)},
        {Btn::AsBaseline, "As Baseline", menu_selector(BenchmarkPopup::onAsBaseline)},
        {Btn::AsOptimized, "As Optimized", menu_selector(BenchmarkPopup::onAsOptimized)},
        {Btn::Prev, "Prev", menu_selector(BenchmarkPopup::onPrev)},
        {Btn::Next, "Next", menu_selector(BenchmarkPopup::onNext)},
        {Btn::Back, "Back", menu_selector(BenchmarkPopup::onBack)},
    };

    for (const auto& spec : specs) {
        auto* sprite = ButtonSprite::create(spec.caption, "goldFont.fnt", "GJ_button_01.png",
                                            kButtonFontScale);
        if (!sprite) continue;
        sprite->setScale(kButtonSpriteScale);
        auto* item = CCMenuItemSpriteExtra::create(sprite, this, spec.selector);
        if (!item) continue;
        item->setVisible(false);
        m_menu->addChild(item);
        m_buttons[spec.id] = item;
    }
}

void BenchmarkPopup::layoutButtons() {
    std::vector<Btn> visible;
    switch (m_view) {
        case View::Main:
            visible = {Btn::StartBaseline, Btn::StartOptimized, Btn::Cancel, Btn::Results,
                       Btn::Protocol, Btn::External};
            break;
        case View::Results:
            visible = {Btn::Prev, Btn::Next, Btn::AsBaseline, Btn::AsOptimized,
                       Btn::Compare, Btn::Detail, Btn::Export, Btn::Back};
            break;
        case View::Detail:
            visible = {Btn::Prev, Btn::Next, Btn::Export, Btn::Back};
            break;
        case View::Compare:
            visible = {Btn::Export, Btn::Back};
            break;
        case View::Protocol:
            visible = {Btn::External, Btn::Back};
            break;
        case View::External:
            visible = {Btn::Back};
            break;
    }

    auto* director = cocos2d::CCDirector::get();
    const auto winSize = director ? director->getWinSize() : cocos2d::CCSize{568.f, 320.f};
    const float left = winSize.width * 0.5f - kPopupWidth * 0.5f;
    const float bottom = winSize.height * 0.5f - kPopupHeight * 0.5f;

    for (auto& entry : m_buttons) entry.second->setVisible(false);

    for (size_t i = 0; i < visible.size(); ++i) {
        auto it = m_buttons.find(visible[i]);
        if (it == m_buttons.end()) continue;
        const size_t row = i / kButtonsPerRow;
        const size_t col = i % kButtonsPerRow;
        const float x = left + 62.f + static_cast<float>(col) * 112.f;
        const float y = bottom + (row == 0 ? 44.f : 18.f);
        it->second->setPosition({x, y});
        it->second->setVisible(true);
    }

    // A running capture cannot be started again, and cannot be compared or
    // exported until it has been finalized.
    const bool running = benchmark::BenchmarkController::instance().isRunning();
    auto enable = [&](Btn id, bool enabled) {
        auto it = m_buttons.find(id);
        if (it != m_buttons.end()) it->second->setEnabled(enabled);
    };
    enable(Btn::StartBaseline, !running);
    enable(Btn::StartOptimized, !running);
    enable(Btn::Cancel, running);
}

void BenchmarkPopup::setView(View view) {
    m_view = view;
    m_pageIndex = 0;
    rebuildPages();
    layoutButtons();
}

void BenchmarkPopup::setStatus(const std::string& message) {
    m_status = message;
}

void BenchmarkPopup::rebuildPages() {
    std::string text;

    switch (m_view) {
        case View::Main: {
            const auto snapshot = controller().getSnapshot();
            const auto live = controller().getLiveStatistics(diagnostics::StatPrecision::Fast);
            text = ui::BenchmarkPanel::formatMain(snapshot, live, m_status);
            break;
        }
        case View::Results: {
            const std::string root = store() ? store()->rootDir().string() : std::string();
            text = ui::BenchmarkPanel::formatResults(m_sessions, m_cursor, m_selectedBaselineId,
                                                     m_selectedOptimizedId, root);
            if (!m_status.empty()) text += "\n" + m_status;
            break;
        }
        case View::Detail: {
            const std::string id = cursorSessionId();
            if (id.empty()) {
                text = "No session selected. Use Results and Prev/Next to choose one.";
                if (!m_status.empty()) text += "\n\n" + m_status;
                break;
            }
            benchmark::BenchmarkSession session;
            std::string error;
            if (store() && store()->load(id, session, error)) {
                text = ui::BenchmarkPanel::formatSessionDetail(session);
            } else {
                text = "Could not load session '" + id + "': " + error;
            }
            if (!m_status.empty()) text += "\n" + m_status;
            break;
        }
        case View::Compare: {
            if (m_hasComparison) {
                text = ui::BenchmarkPanel::formatComparison(m_comparison);
            } else {
                text = "No comparison has been run yet.\n\n";
                text += "From Results, select one baseline session (As Baseline) and one\n";
                text += "optimized session (As Optimized), then press Compare.\n\n";
                if (m_selectedBaselineId.empty()) text += "baseline : <none selected>\n";
                else text += "baseline : " + m_selectedBaselineId + "\n";
                if (m_selectedOptimizedId.empty()) text += "optimized: <none selected>\n";
                else text += "optimized: " + m_selectedOptimizedId + "\n";
            }
            if (!m_status.empty()) text += "\n" + m_status;
            break;
        }
        case View::Protocol:
            text = ui::BenchmarkPanel::formatProtocol();
            break;
        case View::External:
            text = ui::BenchmarkPanel::formatExternalProcedure();
            break;
    }

    m_pages = ui::BenchmarkPanel::paginate(text);
    if (m_pageIndex >= m_pages.size()) m_pageIndex = m_pages.empty() ? 0 : m_pages.size() - 1;
    showPage(m_pageIndex);
}

void BenchmarkPopup::showPage(size_t index) {
    if (m_pages.empty()) return;
    m_pageIndex = std::min(index, m_pages.size() - 1);
    if (m_textLabel) m_textLabel->setString(m_pages[m_pageIndex].c_str());
    if (m_pageLabel) {
        m_pageLabel->setString(
            ui::BenchmarkPanel::formatPageIndicator(m_pageIndex, m_pages.size()).c_str());
    }
}

void BenchmarkPopup::refresh() {
    rebuildPages();
    layoutButtons();
}

void BenchmarkPopup::update(float dt) {
    // While a capture runs the panel is refreshed four times a second. That is
    // enough to read and cheap enough not to become part of what is measured;
    // the in-level HUD carries the per-frame numbers.
    m_refreshTimer += dt;
    if (m_refreshTimer < 0.25f) return;
    m_refreshTimer = 0.f;
    if (m_view == View::Main || m_view == View::Results) refresh();
}

void BenchmarkPopup::reloadSessions() {
    m_sessions.clear();
    m_cursor = 0;
    if (store()) m_sessions = store()->list();
    if (m_cursor >= m_sessions.size()) m_cursor = m_sessions.empty() ? 0 : m_sessions.size() - 1;
}

std::string BenchmarkPopup::cursorSessionId() const {
    if (m_cursor >= m_sessions.size()) return std::string();
    return m_sessions[m_cursor].id;
}

// --- actions ---------------------------------------------------------------

void BenchmarkPopup::onStartBaseline(CCObject*) {
    const auto result = benchmark::BenchmarkController::instance().startSession(
        benchmark::BenchmarkMode::Baseline);
    setStatus(result.message);
    showBenchmarkNotification(result.message, !result.ok);
    setView(View::Main);
}

void BenchmarkPopup::onStartOptimized(CCObject*) {
    const auto result = benchmark::BenchmarkController::instance().startSession(
        benchmark::BenchmarkMode::Optimized);
    setStatus(result.message);
    showBenchmarkNotification(result.message, !result.ok);
    setView(View::Main);
}

void BenchmarkPopup::onCancel(CCObject*) {
    auto& controller = benchmark::BenchmarkController::instance();
    if (!controller.isRunning()) {
        setStatus("No capture is running.");
        refresh();
        return;
    }
    // Cancelling discards the measurement, so confirm it: the whole point of the
    // store is that recorded evidence is not thrown away by one stray tap.
    geode::createQuickPopup(
        "Cancel capture?",
        "The running capture will be finalized as CANCELLED and saved with whatever "
        "was recorded so far. Your configuration will be restored.",
        "Keep running", "Cancel capture",
        [this](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            const auto result = benchmark::BenchmarkController::instance().cancelSession();
            setStatus(result.message);
            showBenchmarkNotification(result.message, !result.ok);
            reloadSessions();
            setView(View::Main);
        });
}

void BenchmarkPopup::onResults(CCObject*) {
    reloadSessions();
    setStatus(std::string());
    setView(View::Results);
}

void BenchmarkPopup::onDetail(CCObject*) {
    setStatus(std::string());
    setView(View::Detail);
}

void BenchmarkPopup::onProtocol(CCObject*) {
    setStatus(std::string());
    setView(View::Protocol);
}

void BenchmarkPopup::onExternal(CCObject*) {
    setStatus(std::string());
    setView(View::External);
}

void BenchmarkPopup::onBack(CCObject*) {
    setStatus(std::string());
    if (m_view == View::External || m_view == View::Protocol || m_view == View::Compare ||
        m_view == View::Detail) {
        setView(View::Results);
    } else {
        setView(View::Main);
    }
}

void BenchmarkPopup::onPrev(CCObject*) {
    if (m_view == View::Results) {
        if (m_cursor > 0) --m_cursor;
        setStatus(std::string());
        refresh();
        return;
    }
    if (m_pageIndex > 0) showPage(m_pageIndex - 1);
}

void BenchmarkPopup::onNext(CCObject*) {
    if (m_view == View::Results) {
        if (m_cursor + 1 < m_sessions.size()) ++m_cursor;
        setStatus(std::string());
        refresh();
        return;
    }
    if (m_pageIndex + 1 < m_pages.size()) showPage(m_pageIndex + 1);
}

void BenchmarkPopup::onAsBaseline(CCObject*) {
    const std::string id = cursorSessionId();
    if (id.empty()) {
        setStatus("Nothing selected.");
        refresh();
        return;
    }
    if (m_selectedOptimizedId == id) m_selectedOptimizedId.clear();
    m_selectedBaselineId = id;
    setStatus("Baseline side set to " + id);
    refresh();
}

void BenchmarkPopup::onAsOptimized(CCObject*) {
    const std::string id = cursorSessionId();
    if (id.empty()) {
        setStatus("Nothing selected.");
        refresh();
        return;
    }
    if (m_selectedBaselineId == id) m_selectedBaselineId.clear();
    m_selectedOptimizedId = id;
    setStatus("Optimized side set to " + id);
    refresh();
}

bool BenchmarkPopup::ensureComparisonSelection() {
    if (!m_selectedBaselineId.empty() && !m_selectedOptimizedId.empty()) return true;

    // Default to the newest comparable session of each mode, so a user who ran
    // the documented protocol can compare without assigning sides by hand.
    for (const auto& summary : m_sessions) {
        if (!summary.readable || !summary.comparable) continue;
        if (summary.mode == benchmark::BenchmarkMode::Baseline && m_selectedBaselineId.empty()) {
            m_selectedBaselineId = summary.id;
        } else if (summary.mode == benchmark::BenchmarkMode::Optimized &&
                   m_selectedOptimizedId.empty()) {
            m_selectedOptimizedId = summary.id;
        }
    }
    if (m_selectedBaselineId.empty() || m_selectedOptimizedId.empty()) {
        setStatus("Need one completed baseline and one completed optimized session. "
                  "Saved sessions: " + std::to_string(m_sessions.size()) + ".");
        return false;
    }
    return true;
}

void BenchmarkPopup::runComparison() {
    m_hasComparison = false;
    if (!ensureComparisonSelection()) {
        setView(View::Compare);
        return;
    }
    const auto& store = benchmark::BenchmarkController::instance().getStore();
    if (!store) {
        setStatus("No benchmark store is available.");
        setView(View::Compare);
        return;
    }

    benchmark::BenchmarkSession baseline;
    benchmark::BenchmarkSession optimized;
    std::string error;
    if (!store->load(m_selectedBaselineId, baseline, error)) {
        setStatus("Could not load baseline '" + m_selectedBaselineId + "': " + error);
        setView(View::Compare);
        return;
    }
    if (!store->load(m_selectedOptimizedId, optimized, error)) {
        setStatus("Could not load optimized '" + m_selectedOptimizedId + "': " + error);
        setView(View::Compare);
        return;
    }

    m_comparison = benchmark::compareSessions(baseline, optimized);
    m_hasComparison = true;
    setStatus(std::string());
    setView(View::Compare);
}

void BenchmarkPopup::onCompare(CCObject*) {
    runComparison();
}

void BenchmarkPopup::exportCurrent() {
    const auto& store = benchmark::BenchmarkController::instance().getStore();
    if (!store) {
        setStatus("No benchmark store is available.");
        showBenchmarkNotification("No benchmark store is available.", true);
        refresh();
        return;
    }

    // Export target: the session currently under the cursor in Results/Detail,
    // otherwise the comparison's optimized side, otherwise the in-memory session.
    std::string id = cursorSessionId();
    if (id.empty() && m_hasComparison) id = m_comparison.optimizedId;
    if (id.empty()) {
        const auto* session = benchmark::BenchmarkController::instance().getSession();
        if (session) id = session->id;
    }
    if (id.empty()) {
        setStatus("Nothing selected to export.");
        refresh();
        return;
    }

    const auto destination = store->rootDir() / "exports" / (id + ".json");
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    std::string error;
    if (!store->exportTo(id, destination, error)) {
        setStatus("Export failed: " + error);
        showBenchmarkNotification("Export failed: " + error, true);
        refresh();
        return;
    }
    setStatus("Exported " + id + " to " + destination.string());
    showBenchmarkNotification("Exported " + id, false);
    refresh();
}

void BenchmarkPopup::onExport(CCObject*) {
    if (m_view == View::Compare && m_hasComparison) {
        // Exporting a comparison writes both sides plus the comparison text next
        // to them, so the artefact cannot be mistaken for a single session.
        const auto& store = benchmark::BenchmarkController::instance().getStore();
        if (!store) {
            setStatus("No benchmark store is available.");
            refresh();
            return;
        }
        const auto destination =
            store->rootDir() / "exports" /
            ("compare-" + m_comparison.baselineId + "-vs-" + m_comparison.optimizedId + ".txt");
        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);
        const std::string text = ui::BenchmarkPanel::formatComparison(m_comparison) + "\n" +
                                 m_comparison.summary + "\n";
        std::string error;
        if (!benchmark::BenchmarkStore::writeAtomic(destination, text, error)) {
            setStatus("Export failed: " + error);
            showBenchmarkNotification("Export failed: " + error, true);
        } else {
            setStatus("Exported comparison to " + destination.string());
            showBenchmarkNotification("Comparison exported", false);
        }
        refresh();
        return;
    }
    exportCurrent();
}

} // namespace optigd::geode_integration

#endif // GEODE_IS_COMPILING

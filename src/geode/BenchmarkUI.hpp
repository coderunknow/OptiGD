#pragma once

// Geode-side Benchmark Mode UI.
//
// Scope discipline: this header and its .cpp contain ONLY what needs the Geode
// loader or a cocos2d node. Everything that decides whether a session is valid,
// what a comparison means, and what the user reads lives in
// `src/benchmark/*` and `src/ui/BenchmarkPanel.*`, which compile and are unit
// tested in the standalone build. That is deliberate: the Geode target can only
// be compile-verified through CI in this repository, so the surface that CI has
// to carry is kept as small as possible.
//
// API choices below are restricted to bindings that are declared `inline` in
// `geode-sdk/bindings/bindings/2.2081` (therefore emitted for Win64, Android32
// and Android64 alike) or to plain cocos2d-x 2.2 calls:
//   * `ButtonSprite::create(caption, font, texture, scale)`  - inline
//   * `CCMenuItemSpriteExtra::create(node, target, selector)` - inline
//   * `geode::Popup::init`, `setTitle`, `m_mainLayer`, `m_buttonMenu`
//   * `CCLabelBMFont`, `CCMenu`, `CCDirector`, `setPosition/addChild`
//   * `geode::Notification::create/show`, `geode::createQuickPopup`
// Deliberately NOT used: `geode::ScrollLayer` (needs the generated
// content-layer member), `TextArea` and `addChildAtPosition` (both unverifiable
// from this repository for the Android targets). Long text is paginated by
// `ui::BenchmarkPanel::paginate()` instead of scrolled.

#ifdef GEODE_IS_COMPILING

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>

#include "../benchmark/BenchmarkCompare.hpp"
#include "../benchmark/BenchmarkController.hpp"
#include "../benchmark/BenchmarkStore.hpp"
#include "../ui/BenchmarkPanel.hpp"

#include <map>
#include <string>
#include <vector>

namespace optigd::geode_integration {

// Installs the Geode-backed runtime services into `BenchmarkController`:
// reading the *engine's* active configuration, installing/clearing the transient
// override through `ConfigManager` plus `OptiGDManager::reloadConfig()`, the
// level identity from the live `PlayLayer`, and the save directory from
// `Mod::getSaveDir()`. Called once from `onModLoaded()`.
void installBenchmarkRuntimeBridge();

// Pushes the `benchmark-duration-seconds` and `benchmark-stop-on-death` mod
// settings into the controller policy. Called at mod load and from every
// settings listener, so a change made mid-session takes effect on the next
// capture and never rewrites a session that is already recorded.
void applyBenchmarkSettingsToPolicy();

// Transient message shown over the game (result of an action, save failure...).
void showBenchmarkNotification(const std::string& text, bool isError = false);

class BenchmarkPopup : public geode::Popup {
public:
    static BenchmarkPopup* create();

    // Refreshes the visible text and button set. Called after every action and
    // periodically from `update()` while a capture is running.
    void refresh();

protected:
    bool init() override;
    void update(float dt) override;

private:
    enum class View {
        Main = 0,
        Results,
        Detail,
        Compare,
        Protocol,
        External,
    };

    enum class Btn {
        StartBaseline = 0,
        StartOptimized,
        Cancel,
        Results,
        Compare,
        Detail,
        Protocol,
        External,
        Export,
        AsBaseline,
        AsOptimized,
        Prev,
        Next,
        Back,
    };

    void buildButtons();
    void layoutButtons();
    void setView(View view);
    void rebuildPages();
    void showPage(size_t index);
    void setStatus(const std::string& message);

    // Button handlers (cocos2d menu selectors).
    void onStartBaseline(cocos2d::CCObject*);
    void onStartOptimized(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onResults(cocos2d::CCObject*);
    void onCompare(cocos2d::CCObject*);
    void onDetail(cocos2d::CCObject*);
    void onProtocol(cocos2d::CCObject*);
    void onExternal(cocos2d::CCObject*);
    void onExport(cocos2d::CCObject*);
    void onAsBaseline(cocos2d::CCObject*);
    void onAsOptimized(cocos2d::CCObject*);
    void onPrev(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);

    void reloadSessions();
    // Id of the session under the results cursor, or empty.
    std::string cursorSessionId() const;
    void exportCurrent();
    void runComparison();

    // Ensures a baseline and an optimized side are selected for comparison,
    // defaulting to the newest comparable session of each mode. Returns false
    // and sets a status message when no such pair exists.
    bool ensureComparisonSelection();

    cocos2d::CCLabelBMFont* m_textLabel{nullptr};
    cocos2d::CCLabelBMFont* m_pageLabel{nullptr};
    cocos2d::CCMenu* m_menu{nullptr};
    // `CCMenuItemSpriteExtra` is a *Geometry Dash* class: the generated
    // bindings declare it in the GLOBAL namespace (`class
    // CCMenuItemSpriteExtra : public cocos2d::CCMenuItemSprite`), so
    // qualifying it with `cocos2d::` does not compile. The explicit `::`
    // records that and keeps the name from being shadowed inside `optigd::`.
    std::map<Btn, ::CCMenuItemSpriteExtra*> m_buttons;

    View m_view{View::Main};
    std::vector<std::string> m_pages;
    size_t m_pageIndex{0};

    std::vector<benchmark::SessionSummary> m_sessions;
    size_t m_cursor{0};
    std::string m_selectedBaselineId;
    std::string m_selectedOptimizedId;

    benchmark::ComparisonResult m_comparison;
    bool m_hasComparison{false};
    std::string m_status;

    float m_refreshTimer{0.0f};
};

} // namespace optigd::geode_integration

#endif // GEODE_IS_COMPILING

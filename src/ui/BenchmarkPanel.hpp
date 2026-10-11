#pragma once

// Platform-agnostic text rendering for Benchmark Mode.
//
// Every string the in-game UI shows is produced here, from values computed
// elsewhere. Two reasons:
//   1. it is unit-testable without a game process, so wording, units and the
//      "approximate vs exact" labelling are covered by `ctest`;
//   2. it keeps `src/geode/BenchmarkUI.cpp` a thin renderer, which is the part
//      of the change that can only be compile-verified through CI.
//
// Nothing here allocates per frame in a hot path: the HUD string is rebuilt at
// most every `kHudRefreshFrames` frames by the caller.

#include "../benchmark/BenchmarkCompare.hpp"
#include "../benchmark/BenchmarkController.hpp"
#include "../benchmark/BenchmarkSession.hpp"
#include "../benchmark/BenchmarkStore.hpp"
#include "../diagnostics/SampleWindow.hpp"

#include <string>
#include <vector>

namespace optigd::ui {

class BenchmarkPanel {
public:
    // How often the caller should rebuild the HUD string. Rebuilding every frame
    // would make the instrumentation itself a measurable cost.
    static constexpr uint32_t kHudRefreshFrames = 10;

    // Compact 3-4 line HUD shown while a capture is running (or just finished).
    // Percentiles computed with `StatPrecision::Fast` are printed with a `~`
    // prefix so an approximate live value can never be mistaken for the exact
    // value in the saved report.
    static std::string formatHud(const benchmark::BenchmarkSnapshot& snapshot,
                                 const diagnostics::WindowStatistics& live);

    // Landing view: current state, what the buttons do, the last session, and
    // the disclosure that a baseline is an internal control.
    static std::string formatMain(const benchmark::BenchmarkSnapshot& snapshot,
                                  const diagnostics::WindowStatistics& live,
                                  const std::string& statusMessage);

    // Numbered list of saved sessions. `cursor` is highlighted.
    static std::string formatResults(const std::vector<benchmark::SessionSummary>& sessions,
                                     size_t cursor,
                                     const std::string& selectedBaselineId,
                                     const std::string& selectedOptimizedId,
                                     const std::string& storeRoot);

    // Full record of one session, including the measurement definitions and the
    // limitations that qualify it.
    static std::string formatSessionDetail(const benchmark::BenchmarkSession& session);

    static std::string formatComparison(const benchmark::ComparisonResult& comparison);

    // The repeated-run protocol: how to produce a pair of captures that can
    // actually be compared, and which confounders to hold constant.
    static std::string formatProtocol();

    // How to run the external Windows comparison with CapFrameX, and what that
    // measurement does and does not establish relative to OptiGD's telemetry.
    static std::string formatExternalProcedure();

    // Short status line used by the panel header.
    static std::string formatStateLine(const benchmark::BenchmarkSnapshot& snapshot);

    // --- pagination ---------------------------------------------------------
    // The in-game popup is deliberately NOT a scroll view: `geode::ScrollLayer`
    // needs its generated content-layer member, which cannot be verified for
    // Android32/Android64 from this repository. Paging the text with Prev/Next
    // depends only on `CCLabelBMFont`, so the layout is deterministic and this
    // function is unit-testable. `maxColumns` hard-wraps long lines so nothing
    // can run off the edge of the popup on a narrow (4:3 / phone) resolution.
    static constexpr size_t kDefaultPageLines = 24;
    static constexpr size_t kDefaultPageColumns = 74;

    static std::vector<std::string> paginate(const std::string& text,
                                             size_t maxLines = kDefaultPageLines,
                                             size_t maxColumns = kDefaultPageColumns);

    // Page indicator rendered under the text, e.g. "page 2/5".
    static std::string formatPageIndicator(size_t pageIndex, size_t pageCount);
};

} // namespace optigd::ui

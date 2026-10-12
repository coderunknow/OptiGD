#pragma once

// A/B comparison of two benchmark sessions.
//
// The point of this file is to make a comparison *defensible* rather than
// flattering. It therefore:
//   * shows both values plus the absolute and relative delta for every metric;
//   * uses each metric's own direction (lower frame time is better, higher FPS
//     is better) instead of applying one rule to everything;
//   * treats a difference below the noise threshold as "effectively unchanged"
//     so a noisy 0.4% cannot be reported as a gain;
//   * handles a zero denominator, missing data and non-comparable sessions
//     explicitly instead of producing `inf`/`nan` or a confident wrong answer;
//   * lists every material difference between the two captures (duration, level
//     identity, platform, versions, integrity warnings) and marks which ones are
//     blocking;
//   * never claims statistical significance. One pair of captures cannot support
//     that claim, and `significanceClaimed` is hardcoded false so a future edit
//     has to justify changing it.

#include "BenchmarkSession.hpp"

#include <string>
#include <vector>

namespace optigd::benchmark {

enum class ComparisonVerdict : uint8_t {
    Improved = 0,
    Worsened,
    Unchanged,
    Unavailable,
};

const char* verdictName(ComparisonVerdict verdict);

struct MetricComparison {
    WindowMetricId metric{WindowMetricId::AvgFps};
    std::string name;
    MetricDirection direction{MetricDirection::Informational};
    bool available{false};
    double baselineValue{0.0};
    double optimizedValue{0.0};
    double absoluteDelta{0.0};   // optimized - baseline
    double relativePct{0.0};     // (optimized - baseline) / baseline * 100
    bool relativeAvailable{false};
    ComparisonVerdict verdict{ComparisonVerdict::Unavailable};
    std::string note;
    std::string unit;
};

struct CompatibilityIssue {
    std::string code;
    std::string message;
    bool blocking{false};  // true => no conclusion may be drawn from this pair
};

struct ComparisonResult {
    std::string baselineId;
    std::string optimizedId;
    std::string baselineMode;
    std::string optimizedMode;

    bool hasBlockingIssue{false};
    bool comparisonMeaningful{false};

    std::vector<CompatibilityIssue> issues;
    std::vector<MetricComparison> metrics;

    size_t improvedCount{0};
    size_t worsenedCount{0};
    size_t unchangedCount{0};
    size_t unavailableCount{0};

    double noiseThresholdPct{2.0};
    bool significanceClaimed{false};
    std::string significanceNote;
    std::string headline;      // one sentence, safe to show in the UI
    std::string summary;       // multi-line explanation

    const MetricComparison* find(WindowMetricId id) const;
};

// `noiseThresholdPct` is the relative change below which a metric is reported as
// effectively unchanged. 2% is the default: comfortably above frame-to-frame
// variation on a fixed setup, small enough that a real optimization is visible.
ComparisonResult compareSessions(const BenchmarkSession& baseline,
                                 const BenchmarkSession& optimized,
                                 double noiseThresholdPct = 2.0);

// Renders a comparison as plain text for the in-game panel and for
// `tools/generate_report.py`, which parses the same numbers from the JSON.
std::string formatComparisonText(const ComparisonResult& result);

} // namespace optigd::benchmark

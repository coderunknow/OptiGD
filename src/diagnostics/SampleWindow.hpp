#pragma once

// Bounded measurement window shared by DiagnosticsEngine and Benchmark Mode.
//
// Why this exists
// ---------------
// beta.2 kept `std::deque<FrameStats> m_history` capped at 3600 entries while a
// separate `m_cumulativeFrames` counter grew without bound. `computeSummary()`
// derived the mean and P50/P95/P99 from the capped deque but reported
// `total_frames` from the uncapped counter, so at any refresh rate above 60 Hz a
// "60 second" report silently described two different measurement windows
// (3600 frames is ~25 s at 144 Hz).
//
// `SampleWindow` replaces that with one structure that cannot disagree with
// itself:
//   * every accepted sample contributes to `samples`, to `totalFrameTimeMs`, to
//     the accumulators, to the ring buffer and to the histogram — one set of
//     numbers, one window;
//   * memory is bounded and allocated once by `configure()`, never per frame;
//   * the window *states* how its percentiles were produced (`exact` from the
//     ring, or `histogram-approximate` after the ring overflowed) instead of
//     silently discarding observations and calling the result a full capture.
//
// Percentile definitions (documented because they are load-bearing):
//   * P50 — the median: for an even sample count the mean of the two central
//     values, for an odd count the central value.
//   * P95 / P99 — nearest-rank: index `ceil(p * n) - 1`, clamped to `[0, n-1]`.
//     beta.2 used `floor(p * n)` clamped, which for small `n` is biased toward
//     the maximum (n = 20 -> P95 index 19, i.e. the largest sample).
//   * Because the samples are sorted and `ceil(0.5n) <= ceil(0.95n) <=
//     ceil(0.99n)`, the ordering `P50 <= P95 <= P99` holds for every non-empty
//     window under both the exact and the histogram estimator.
//   * The histogram estimator returns the **upper bound** of the bin that
//     contains the target rank, so it over-estimates by at most one bin width
//     (`histogramBinWidthMs`) and never under-reports a slow frame.
//
// Stall / hitch policy (this is where measurement honesty lives):
//   * A **hitch** is a counted frame slower than `hitchThresholdMs`. Hitches stay
//     in the distribution — they are real gameplay stutter and hiding them would
//     understate exactly what OptiGD is supposed to improve. They are also
//     counted separately so a report can show stutter without re-deriving it.
//   * A **stall** is an inter-frame interval longer than `stallThresholdMs`:
//     longer than any plausible rendered frame, so it is dominated by a pause,
//     an application switch, a level load or the OS descheduling the game.
//     Whether a stall is counted is the *caller's* decision, and it differs by
//     window on purpose:
//       - `DiagnosticsEngine` (level-wide report) pushes stalls as samples and
//         reports them in `stallFrames`, because a level report should not hide
//         a multi-second freeze;
//       - `BenchmarkController` (session window) routes stalls to
//         `recordExcluded()`, because a 60 s benchmark window must contain 60 s
//         of gameplay and an alt-tab is not gameplay. The exclusion is counted,
//         timed (`excludedTimeMs`, `longestExcludedMs`) and surfaced as an
//         integrity warning, so nothing is discarded silently.

#include "../core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace optigd::diagnostics {

enum class PercentileMethod : uint8_t {
    None = 0,               // no samples: percentile fields are 0 and meaningless
    Exact,                  // computed from every retained sample
    HistogramApproximate,   // ring overflowed (or Fast precision requested)
};

const char* percentileMethodName(PercentileMethod method);

// `Exact` sorts the retained ring (O(n log n), once). `Fast` skips the sort and
// answers from the fixed-bin histogram in O(bins) so a 4 Hz HUD refresh over a
// 1000 Hz / 60 s capture (60k samples) cannot stall the frame it is measuring.
// A `Fast` result is reported as `HistogramApproximate`, never as exact.
enum class StatPrecision : uint8_t { Fast = 0, Exact = 1 };

// Per-frame quantities accumulated exactly (sum/min/max/count) without storing
// one value per sample. The aggregation kind decides how a report reads them.
enum class WindowMetric : uint8_t {
    UpdateCpuUs = 0,          // wall time inside GJBaseGameLayer::update()
    UpdateThreadCpuUs,        // thread CPU time inside the same call
    OptigdOverheadUs,         // OptiGD's own measured subsystem cost
    CullingUs,
    ParticleUs,
    TriggerUs,
    MatrixUs,
    GovernorUs,
    CulledObjects,            // counter: aggregated as a sum
    ParticlesAvoided,         // counter: aggregated as a sum
    TriggersSkipped,          // counter: aggregated as a sum
    MatrixCacheHits,          // counter: aggregated as a sum
    MatrixCacheMisses,        // counter: aggregated as a sum
    ArenaAllocations,         // counter: aggregated as a sum
    HeapAllocationsAvoided,   // counter: aggregated as a sum
    ArenaPeakBytes,           // peak: aggregated as a max
    TotalObjects,             // peak: aggregated as a max
    ActiveParticles,          // peak: aggregated as a max
    VisibleObjects,           // peak: aggregated as a max
    Count
};

enum class MetricAggregation : uint8_t { Mean = 0, Sum, Max };

const char* windowMetricName(WindowMetric metric);
MetricAggregation windowMetricAggregation(WindowMetric metric);

inline constexpr size_t kWindowMetricCount = static_cast<size_t>(WindowMetric::Count);

struct MetricAccumulator {
    uint64_t count{0};
    double sum{0.0};
    double min{std::numeric_limits<double>::infinity()};
    double max{-std::numeric_limits<double>::infinity()};

    void reset();
    void add(double value);
    double mean() const;      // 0 when empty
    double safeMin() const;   // 0 when empty (infinity is not a valid report value)
    double safeMax() const;   // 0 when empty
    double aggregate(WindowMetric metric) const;
};

struct SampleWindowConfig {
    // Exact samples retained. Sized by the caller from
    // duration * maxTrackedFps, so a 60 s capture at 1000 Hz keeps every frame.
    size_t ringCapacity{60000};
    double histogramMaxMs{250.0};
    size_t histogramBins{2500};
    double hitchThresholdMs{1000.0 / 30.0};  // slower than 30 FPS
    double stallThresholdMs{250.0};          // longer than any plausible frame
};

struct WindowStatistics {
    // --- sample accounting -------------------------------------------------
    uint64_t samples{0};            // accepted into the window
    uint64_t rejectedSamples{0};    // non-finite or non-positive frame time
    uint64_t excludedSamples{0};    // deliberately kept out (stalls, pauses)
    uint64_t overflowSamples{0};    // accepted, but evicted from the ring
    uint64_t hitchFrames{0};        // counted samples slower than the hitch threshold
    uint64_t stallFrames{0};        // counted samples slower than the stall threshold
    double excludedTimeMs{0.0};     // total interval time of excluded samples
    double longestExcludedMs{0.0};  // worst single excluded interval
    double hitchTotalMs{0.0};

    // --- frame-time distribution -------------------------------------------
    double totalFrameTimeMs{0.0};   // sum of accepted frame times == the window
    double meanFrameTimeMs{0.0};
    double minFrameTimeMs{0.0};
    double maxFrameTimeMs{0.0};
    double varianceFrameTimeMs{0.0};
    double stdDevFrameTimeMs{0.0};
    double p50FrameTimeMs{0.0};
    double p95FrameTimeMs{0.0};
    double p99FrameTimeMs{0.0};

    // --- FPS (see the definitions in the report; not a presentation rate) ----
    double fpsFromMeanFrameTime{0.0};  // 1000 / meanFrameTimeMs
    double fpsFromWindow{0.0};         // samples / (totalFrameTimeMs / 1000)

    // --- provenance ---------------------------------------------------------
    PercentileMethod percentileMethod{PercentileMethod::None};
    bool precisionWasFast{false};
    bool ringOverflow{false};
    bool histogramSaturated{false};
    size_t ringCapacity{0};
    size_t ringUsed{0};
    double histogramMaxMs{0.0};
    double histogramBinWidthMs{0.0};
    double hitchThresholdMs{0.0};
    double stallThresholdMs{0.0};

    std::array<MetricAccumulator, kWindowMetricCount> metrics{};

    bool empty() const { return samples == 0; }
    bool percentilesAreMonotonic() const {
        return p50FrameTimeMs <= p95FrameTimeMs && p95FrameTimeMs <= p99FrameTimeMs;
    }
};

class SampleWindow {
public:
    SampleWindow() = default;

    // Allocates (or reallocates) the ring, the histogram and the sort scratch
    // buffer. Called on configure/reset-with-new-capacity, never per frame.
    // `ringCapacity == 0` degrades to histogram-only operation, which is still
    // correct but always reports `HistogramApproximate` percentiles.
    void configure(const SampleWindowConfig& config);

    // Clears all data, keeping the configured capacity and allocations.
    void reset();

    // Accepts one frame observation. Non-finite or non-positive frame times are
    // rejected (counted in `rejectedSamples`) rather than silently skewing the
    // distribution.
    void push(const core::FrameStats& stats);
    void pushFrameTime(double frameTimeUs);

    // Records an interval deliberately kept out of the window (a stall, a pause,
    // an application switch). The count and the time are reported.
    void recordExcluded(double frameTimeUs);

    WindowStatistics computeStatistics(StatPrecision precision = StatPrecision::Exact) const;

    // Exact percentile over a sorted ascending buffer: nearest-rank for
    // p95/p99, interpolated median for p50. Exposed for tests and reused by
    // AdaptiveGovernor so the two percentile paths cannot drift apart.
    static double percentileFromSorted(const double* sorted, size_t n, double p);
    static double medianFromSorted(const double* sorted, size_t n);

    const SampleWindowConfig& getConfig() const { return m_config; }
    size_t getRingCapacity() const { return m_config.ringCapacity; }
    size_t getHistogramBins() const { return m_histogram.size(); }
    uint64_t getSampleCount() const { return m_samples; }
    uint64_t getExcludedCount() const { return m_excludedSamples; }
    double getTotalFrameTimeMs() const { return m_sumFrameTimeMs; }
    bool hasRingOverflow() const { return m_overflowSamples > 0; }
    bool isStallIntervalUs(double intervalUs) const;
    // Bytes owned by this window (ring + histogram + scratch). Bounded and
    // reported so the HUD can prove instrumentation is not growing unbounded.
    size_t getOwnedBytes() const;

private:
    SampleWindowConfig m_config{};
    size_t m_ringWritePos{0};
    std::vector<float> m_ring;          // frame times in ms
    mutable std::vector<double> m_sortScratch;

    double m_histogramBinWidthMs{0.1};
    std::vector<uint32_t> m_histogram;  // last bin collects >= histogramMaxMs
    uint64_t m_histogramSaturatedSamples{0};

    uint64_t m_samples{0};
    uint64_t m_rejectedSamples{0};
    uint64_t m_excludedSamples{0};
    uint64_t m_overflowSamples{0};
    uint64_t m_hitchFrames{0};
    uint64_t m_stallFrames{0};
    double m_excludedTimeMs{0.0};
    double m_longestExcludedMs{0.0};
    double m_hitchTotalMs{0.0};

    double m_sumFrameTimeMs{0.0};
    double m_sumFrameTimeSqMs{0.0};
    double m_minFrameTimeMs{std::numeric_limits<double>::infinity()};
    double m_maxFrameTimeMs{-std::numeric_limits<double>::infinity()};

    std::array<MetricAccumulator, kWindowMetricCount> m_metrics{};

    void pushAccepted(double frameTimeMs, const core::FrameStats* stats);
    size_t histogramBinFor(double frameTimeMs);
};

} // namespace optigd::diagnostics

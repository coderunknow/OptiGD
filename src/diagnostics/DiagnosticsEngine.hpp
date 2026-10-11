#pragma once

// Level-wide telemetry capture and the `optigd_diagnostics.json` exporter.
//
// beta.3 changes, and why:
//   * the frame history is a `SampleWindow` instead of a
//     `std::deque<FrameStats>` capped at 3600 next to an uncapped
//     `m_cumulativeFrames`. The report now states, for every statistic, how many
//     samples it came from and over what window, so a reader cannot mistake a
//     25 s percentile window (3600 frames at 144 Hz) for a 60 s one.
//   * `reset()` is called at level load, not only at mod init, so a level report
//     cannot contain the previous level.
//   * the report gained `report_schema_version: 2` plus a `capture` block and a
//     `sampling` block. Version 1 reports (no `report_schema_version` key) are
//     still accepted by `tools/validate_diagnostics_json.py`; version 2 reports
//     are additionally required to disclose their window.
//   * `exportJsonReportToFile()` writes atomically and checks the stream, so a
//     failed export can no longer destroy the previous report while returning
//     success.
//   * every interpolated double is checked for finiteness. A NaN or an infinity
//     used to produce `nan` / `inf` in the JSON, which is not parseable.

#include "../adaptive/FallbackManager.hpp"
#include "../core/Types.hpp"
#include "../platform/Platform.hpp"
#include "SampleWindow.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace optigd::diagnostics {

// Reports written by beta.2 and earlier have no `report_schema_version` key and
// are treated as version 1 by the validators.
inline constexpr int kDiagnosticsReportSchemaV1 = 1;
inline constexpr int kDiagnosticsReportSchemaV2 = 2;

struct DiagnosticsSummary {
    // --- values that existed in beta.2 (meanings unchanged) -----------------
    double avgFps{0.0};
    double minFps{0.0};
    double maxFps{0.0};
    double avgFrameTimeMs{0.0};
    double medianFrameTimeMs{0.0};
    double p95FrameTimeMs{0.0};
    double p99FrameTimeMs{0.0};
    double avgUpdateCpuTimeMs{0.0};
    double avgOptigdOverheadUs{0.0};
    uint64_t totalFrames{0};
    uint64_t totalObjectsCulled{0};
    uint64_t totalParticlesAvoided{0};
    uint64_t totalTriggersSkipped{0};
    uint64_t totalMatrixCacheHits{0};
    uint64_t totalMatrixCacheMisses{0};
    size_t peakArenaBytes{0};
    uint64_t totalArenaAllocations{0};
    uint64_t totalHeapAllocationsAvoided{0};
    uint32_t fallbackEventsCount{0};
    uint32_t tierTransitionsCount{0};
    std::string activeTier;

    // --- beta.3: window disclosure ------------------------------------------
    // `totalFrames` counts every frame offered to the engine since the capture
    // started. `sampleCount` counts the frames the statistics were computed
    // from. In beta.2 these could disagree silently; here the disagreement is
    // reported and explained.
    uint64_t sampleCount{0};
    double sampleWindowMs{0.0};
    size_t historyCapacity{0};
    bool historyTruncated{false};
    PercentileMethod percentileMethod{PercentileMethod::None};
    uint64_t rejectedSamples{0};
    uint64_t excludedSamples{0};
    double excludedTimeMs{0.0};
    uint64_t hitchFrames{0};
    double hitchTotalMs{0.0};
    uint64_t stallFrames{0};
    // Frames the engine flagged as not part of active gameplay (pause menu open,
    // level intro). Excluded from the distribution but accounted for here and in
    // `excludedTimeMs`, so the report can show where the wall time went.
    uint64_t pausedFrames{0};
    double histogramMaxMs{0.0};
    double histogramBinWidthMs{0.0};

    // --- beta.3: additional statistics --------------------------------------
    double minFrameTimeMs{0.0};
    double maxFrameTimeMs{0.0};
    double stdDevFrameTimeMs{0.0};
    double fpsFromWindow{0.0};
    double avgUpdateThreadCpuMs{0.0};
    bool threadCpuTimeAvailable{false};
    double avgCullingUs{0.0};
    double avgParticleUs{0.0};
    double avgTriggerUs{0.0};
    double avgMatrixUs{0.0};
    double avgGovernorUs{0.0};
    uint32_t levelResets{0};
    double captureWallDurationMs{0.0};
    double captureActiveDurationMs{0.0};
    size_t windowOwnedBytes{0};

    bool percentilesAreMonotonic() const {
        return medianFrameTimeMs <= p95FrameTimeMs && p95FrameTimeMs <= p99FrameTimeMs;
    }
};

class DiagnosticsEngine {
public:
    // Default retained-sample capacity: 60 s at 1000 Hz, 250 s at 240 Hz,
    // 1000 s at 60 Hz. Sized so the ring plus its sort scratch uses about the
    // same memory as the beta.2 3600-entry FrameStats deque while retaining
    // more than 16x as many samples.
    static constexpr size_t kDefaultHistoryCapacity = 60000;

    static DiagnosticsEngine& instance();

    // Starts a new capture. Called by `OptiGDManager::onLevelLoad()` (beta.3
    // fix: previously only at mod init) and by tests.
    void reset();

    void recordFrameStats(const core::FrameStats& stats);
    void noteLevelReset() { ++m_levelResets; }
    uint64_t getPausedFrames() const { return m_pausedFrames; }

    DiagnosticsSummary computeSummary(const core::OptiConfig& currentConfig,
                                      uint32_t fallbackCount,
                                      uint32_t tierTransitions) const;

    std::string generateJsonReport(const core::OptiConfig& config,
                                   const std::vector<adaptive::FallbackEvent>& fallbacks,
                                   uint32_t tierTransitions = 0) const;

    // Atomic: writes to `<path>.tmp`, verifies the bytes, then renames. Returns
    // false (and leaves any existing file intact) on any failure.
    bool exportJsonReportToFile(const std::string& filePath,
                                const core::OptiConfig& config,
                                const std::vector<adaptive::FallbackEvent>& fallbacks,
                                uint32_t tierTransitions = 0) const;

    // Sizes the retained-sample ring. The default keeps 240 s at 240 Hz or
    // 144 s at 1000 Hz, and anything beyond that is still counted (the
    // histogram covers every sample) but reported as approximate percentiles.
    void setHistoryCapacity(size_t frames);
    size_t getHistoryCapacity() const;

    const SampleWindow& getWindow() const { return m_window; }
    WindowStatistics getWindowStatistics(StatPrecision precision = StatPrecision::Exact) const;

    uint64_t getTotalFramesOffered() const { return m_framesOffered; }
    uint64_t getCaptureStartedEpochMs() const { return m_captureStartedEpochMs; }
    double getCaptureWallDurationMs() const;

private:
    DiagnosticsEngine();

    SampleWindow m_window;
    size_t m_historyCapacity{0};
    uint64_t m_framesOffered{0};
    uint64_t m_pausedFrames{0};
    uint32_t m_levelResets{0};
    uint64_t m_captureStartedEpochMs{0};
    uint64_t m_captureStartedMonotonicUs{0};
};

} // namespace optigd::diagnostics

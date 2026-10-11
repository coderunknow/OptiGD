#pragma once

// The persistent record of one benchmark capture.
//
// A session is *evidence*. It carries the measurement window, the statistics
// derived from exactly that window, the configuration that was actually in
// force, the environment, and every integrity warning that qualifies the
// result. It is written once and never mutated afterwards, and it is
// self-describing: `measurement_definitions` states what each number is and
// what it cannot establish, so a reader cannot mistake OptiGD's internal
// inter-frame telemetry for an externally measured presentation rate.

#include "../core/JsonValue.hpp"
#include "../core/Types.hpp"
#include "../diagnostics/SampleWindow.hpp"
#include "BenchmarkTypes.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace optigd::benchmark {

// Formats milliseconds since the Unix epoch as `YYYY-MM-DDTHH:MM:SS.mmmZ`.
// Returns an empty string for out-of-range input rather than a wrong date.
std::string formatUtcIso8601(uint64_t epochMs);

core::json::Value configToJson(const core::OptiConfig& config);
// Tolerant: unknown keys are ignored, missing keys keep `fallback` values, and
// out-of-range numbers are clamped. Returns false only when `root` is not an
// object at all, because a partially readable configuration is more useful
// than none when re-opening a saved session.
bool configFromJson(const core::json::Value& root, core::OptiConfig& out,
                    const core::OptiConfig& fallback = core::OptiConfig{});

// Stable identifiers for the metrics the comparator and the UI iterate over.
// Declared here (rather than in BenchmarkCompare.hpp) so a session can answer
// "what is my value for metric X" without depending on the comparator.
enum class WindowMetricId : uint8_t {
    AvgFps = 0,
    MeanFrameTimeMs,
    P50FrameTimeMs,
    P95FrameTimeMs,
    P99FrameTimeMs,
    MaxFrameTimeMs,
    StdDevFrameTimeMs,
    AvgUpdateCpuMs,
    AvgUpdateThreadCpuMs,
    AvgOverheadUs,
    CapturedDurationMs,
    FramesCounted,
    ObjectsCulled,
    ParticlesAvoided,
    MatrixCacheHits,
    HeapAllocationsAvoided,
    Count
};

const char* metricIdName(WindowMetricId id);
// Lower is better (frame times, CPU cost), higher is better (FPS, counters that
// represent work avoided). `0` means the metric is informational only and has
// no good direction — used for counts whose value depends on the level.
enum class MetricDirection : uint8_t { LowerIsBetter = 0, HigherIsBetter, Informational };
MetricDirection metricDirection(WindowMetricId id);

struct BenchmarkSession {
    // Bumped when the on-disk shape changes in a way readers must detect.
    // Version 1 is the first persisted benchmark session schema.
    static constexpr int kSchemaVersion = 1;
    static constexpr const char* kReportKind = "optigd_benchmark_session";

    std::string id;
    BenchmarkMode mode{BenchmarkMode::Baseline};
    BenchmarkState state{BenchmarkState::Invalid};

    // --- measurement window ------------------------------------------------
    double targetDurationMs{60000.0};
    double capturedDurationMs{0.0};   // accumulated ACTIVE gameplay time
    double wallDurationMs{0.0};       // monotonic start -> end, pauses included
    uint64_t startedEpochMs{0};
    uint64_t endedEpochMs{0};
    std::string startedUtc;
    std::string endedUtc;

    uint64_t framesObserved{0};       // every frame the controller was offered
    uint64_t framesCounted{0};        // accepted into the window
    uint64_t framesExcluded{0};       // gaps / not part of active gameplay
    uint64_t framesRejected{0};       // non-finite or non-positive interval

    diagnostics::WindowStatistics stats;

    // --- configuration that was actually in force ---------------------------
    core::OptiConfig config;
    bool configOverrideApplied{false};
    // Names of the features Benchmark Mode forced into passthrough for a
    // baseline session. Recorded from the applied config, not from the setting
    // labels, so it cannot claim a change that never reached the runtime.
    std::vector<std::string> featuresDisabledForBaseline;

    LevelIdentity level;
    BenchmarkEnvironment environment;

    // --- integrity ----------------------------------------------------------
    uint32_t pauseEvents{0};
    uint32_t resetEvents{0};
    uint32_t gapEvents{0};
    uint32_t tierTransitions{0};
    uint32_t fallbackEvents{0};
    double excludedGapMs{0.0};
    // Of `excludedGapMs`, how much was excluded because the game was paused
    // (pause menu open, level intro) rather than because of a stall. Kept apart
    // because a paused frame looks exactly like a normal one to the frame-time
    // distribution: only an explicit pause signal can identify it, and if it
    // were counted a "60 second" capture could be completed without playing.
    double pausedMs{0.0};
    std::vector<std::string> warnings;

    // --- provenance ---------------------------------------------------------
    std::string sourcePath;
    bool loadedFromDisk{false};
    // What this record is allowed to claim about where it came from
    // (`evidence::kMeasuredInGame` / `evidence::kSimulated`). Stamped by the
    // controller from the runtime it is actually attached to, never from the
    // data. The default is the conservative one, so a session built by hand, by
    // a standalone binary, or from a file that lost the field can only ever
    // claim LESS than a real in-game capture.
    std::string evidenceClassification{evidence::kSimulated};
    std::string evidenceClassificationNote{evidence::kSimulatedNote};

    // `completed` / `incomplete` / `cancelled` / `invalid`.
    std::string statusId() const;
    bool isComparable() const;
    bool hasSamples() const { return stats.samples > 0; }

    void addWarning(std::string_view code);   // de-duplicated, order preserved
    bool hasWarning(std::string_view code) const;

    // Derives the warnings that follow mechanically from the recorded numbers
    // (truncation, approximation, short capture, drift, rejected samples).
    // Called before serialization so a session cannot describe itself more
    // favourably than its own data supports.
    void refreshDerivedWarnings();

    // --- metric accessors used by the comparator and the UI ------------------
    double avgFps() const { return stats.fpsFromMeanFrameTime; }
    double meanFrameTimeMs() const { return stats.meanFrameTimeMs; }
    double minFrameTimeMs() const { return stats.minFrameTimeMs; }
    double maxFrameTimeMs() const { return stats.maxFrameTimeMs; }
    double p50FrameTimeMs() const { return stats.p50FrameTimeMs; }
    double p95FrameTimeMs() const { return stats.p95FrameTimeMs; }
    double p99FrameTimeMs() const { return stats.p99FrameTimeMs; }
    double stdDevFrameTimeMs() const { return stats.stdDevFrameTimeMs; }
    double avgUpdateCpuMs() const;
    double avgUpdateThreadCpuMs() const;
    double avgOverheadUs() const;
    double metricValue(WindowMetricId id) const;

    // --- serialization ------------------------------------------------------
    core::json::Value toJson() const;
    static bool fromJson(const core::json::Value& root, BenchmarkSession& out,
                         std::string& error);
    std::string serialize() const;
    static bool parse(std::string_view text, BenchmarkSession& out, std::string& error);
};

} // namespace optigd::benchmark

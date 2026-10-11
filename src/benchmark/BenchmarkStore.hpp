#pragma once

// Persistent, session-specific benchmark storage.
//
// Everything OptiGD writes stays on the local filesystem inside the mod's own
// Geode save directory. There is no network path in this project and this file
// does not add one.
//
// Durability rules implemented here (each one exists because the beta.2
// diagnostics exporter violated it):
//   * writes are ATOMIC — a temporary file is written, flushed and closed, and
//     only then renamed over the destination, so a crash or a full disk cannot
//     leave a half-written report in place of a good one;
//   * an existing session file is NEVER overwritten — session ids are unique and
//     `save()` refuses to clobber a different record, which is what "retaining
//     multiple completed sessions without overwriting previous evidence" means
//     in practice;
//   * reading is TOLERANT — a malformed, truncated or newer-schema file is
//     reported and skipped, it does not abort the listing of the sessions that
//     are fine;
//   * nothing is deleted automatically. There is no pruning policy, because the
//     request explicitly forbids silent loss of existing results.

#include "BenchmarkSession.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace optigd::benchmark {

// Compact, filesystem-safe and human-sortable session identifier:
//   20261011T013512Z-baseline-0001
// The UTC timestamp makes ids sort chronologically as strings; the mode makes a
// pair easy to spot; the counter disambiguates two sessions started in the same
// second (and across process restarts, since the store probes for collisions).
std::string generateSessionId(BenchmarkMode mode, uint64_t epochMs, uint32_t counter);

// Rejects anything that is not [A-Za-z0-9._-], so a session id can never be
// used to escape the store directory.
bool isSafeSessionId(std::string_view id);

struct SaveResult {
    bool ok{false};
    bool wrote{false};         // false when the identical file already existed
    std::filesystem::path path;
    std::string error;
    std::string errorCode;     // "io_error" | "already_exists" | "invalid_id" | ...
};

// Lightweight record used by the in-game results list. Reading every session in
// full just to draw a list would be wasteful, so the list carries only what the
// browser needs and flags anything that could not be read.
struct SessionSummary {
    std::string id;
    std::string fileName;
    BenchmarkMode mode{BenchmarkMode::Baseline};
    BenchmarkState state{BenchmarkState::Invalid};
    std::string startedUtc;
    std::string levelName;
    double targetDurationMs{0.0};
    double capturedDurationMs{0.0};
    double avgFps{0.0};
    double meanFrameTimeMs{0.0};
    double p99FrameTimeMs{0.0};
    uint64_t samples{0};
    bool comparable{false};
    bool readable{true};
    std::string readError;
    uint64_t fileSizeBytes{0};
};

class BenchmarkStore {
public:
    explicit BenchmarkStore(std::filesystem::path rootDir);

    const std::filesystem::path& rootDir() const { return m_rootDir; }
    std::filesystem::path sessionPath(const std::string& id) const;
    std::filesystem::path indexPath() const;

    // Creates the directory if needed. Returns false with a message when the
    // path cannot be used, which callers must surface rather than pretend the
    // session was saved.
    bool ensureDirectory(std::string& error);

    SaveResult save(const BenchmarkSession& session);

    bool load(const std::string& id, BenchmarkSession& out, std::string& error) const;

    // Newest first. Malformed files are included with `readable == false` so a
    // corrupt record is visible instead of silently vanishing.
    std::vector<SessionSummary> list() const;

    size_t count() const;
    bool exists(const std::string& id) const;

    // Copies a session to an arbitrary destination (the "export" action).
    bool exportTo(const std::string& id, const std::filesystem::path& destination,
                  std::string& error) const;

    // Returns a session id that does not collide with an existing file, probing
    // `counter` upwards. Used by the controller before a session starts so the
    // id is known while the capture is still running.
    std::string allocateSessionId(BenchmarkMode mode, uint64_t epochMs, uint32_t& counter) const;

    // index.json is a cache, never the source of truth: `rebuildIndex()`
    // reconstructs it from the directory, and a missing or corrupt index simply
    // falls back to scanning.
    bool rebuildIndex(std::string& error) const;

    // Writes raw bytes atomically. Exposed for tests and for the index.
    static bool writeAtomic(const std::filesystem::path& destination, const std::string& contents,
                            std::string& error);

private:
    std::filesystem::path m_rootDir;
};

} // namespace optigd::benchmark

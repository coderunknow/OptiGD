#include "BenchmarkStore.hpp"

#include "../core/JsonValue.hpp"
#include "../core/Logger.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

namespace optigd::benchmark {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

bool isSafeSessionId(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    if (id == "." || id == "..") return false;
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    // A path separator can never appear given the character allow-list, but the
    // leading-dot case still has to be rejected so a session file cannot become
    // a hidden or special entry.
    if (id.front() == '.') return false;
    return true;
}

std::string generateSessionId(BenchmarkMode mode, uint64_t epochMs, uint32_t counter) {
    std::time_t seconds = static_cast<std::time_t>(epochMs / 1000ULL);
    std::tm tmUtc{};
    bool converted = false;
#if defined(_WIN32)
    converted = (gmtime_s(&tmUtc, &seconds) == 0);
#else
    converted = (gmtime_r(&seconds, &tmUtc) != nullptr);
#endif

    char stamp[64];
    if (converted) {
        std::snprintf(stamp, sizeof(stamp), "%04d%02d%02dT%02d%02d%02dZ",
                      tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                      tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);
    } else {
        std::snprintf(stamp, sizeof(stamp), "epoch%llu",
                      static_cast<unsigned long long>(epochMs));
    }

    std::ostringstream ss;
    ss << stamp << '-' << modeId(mode) << '-';
    ss.width(4);
    ss.fill('0');
    ss << counter;
    return ss.str();
}

// ---------------------------------------------------------------------------
// BenchmarkStore
// ---------------------------------------------------------------------------

BenchmarkStore::BenchmarkStore(fs::path rootDir) : m_rootDir(std::move(rootDir)) {}

fs::path BenchmarkStore::sessionPath(const std::string& id) const {
    if (!isSafeSessionId(id)) return fs::path();
    return m_rootDir / (id + ".json");
}

fs::path BenchmarkStore::indexPath() const {
    return m_rootDir / "index.json";
}

bool BenchmarkStore::ensureDirectory(std::string& error) {
    std::error_code ec;
    if (m_rootDir.empty()) {
        error = "benchmark store directory is not configured";
        return false;
    }
    if (fs::exists(m_rootDir, ec)) {
        if (!fs::is_directory(m_rootDir, ec)) {
            error = "benchmark store path exists but is not a directory: " + m_rootDir.string();
            return false;
        }
        return true;
    }
    if (!fs::create_directories(m_rootDir, ec)) {
        if (ec) {
            error = "could not create benchmark directory '" + m_rootDir.string() + "': " + ec.message();
            return false;
        }
    }
    if (!fs::is_directory(m_rootDir, ec)) {
        error = "benchmark directory is not usable: " + m_rootDir.string();
        return false;
    }
    return true;
}

bool BenchmarkStore::writeAtomic(const fs::path& destination, const std::string& contents,
                                 std::string& error) {
    std::error_code ec;
    fs::path parent = destination.parent_path();
    if (!parent.empty() && !fs::exists(parent, ec)) {
        if (!fs::create_directories(parent, ec) && ec) {
            error = "could not create directory '" + parent.string() + "': " + ec.message();
            return false;
        }
    }

    // Unique-ish temporary name. Two writers in the same process would already
    // be a bug (the controller serializes saves), but a stale temp file from a
    // crashed run must not be picked up by the rename below.
    fs::path temp = destination;
    temp += ".tmp";

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "could not open temporary file for writing: " + temp.string();
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out.good()) {
            error = "write failed (disk full or I/O error): " + temp.string();
            out.close();
            fs::remove(temp, ec);
            return false;
        }
        out.close();
    }

    // Verify what was written before it replaces anything. A rename over a good
    // file is the one operation that must not be based on an unreadable payload.
    {
        std::ifstream verify(temp, std::ios::binary);
        if (!verify.is_open()) {
            error = "could not re-open the temporary file for verification: " + temp.string();
            fs::remove(temp, ec);
            return false;
        }
        std::string roundTrip((std::istreambuf_iterator<char>(verify)),
                              std::istreambuf_iterator<char>());
        if (roundTrip != contents) {
            error = "temporary file did not round-trip (" +
                    std::to_string(roundTrip.size()) + " of " +
                    std::to_string(contents.size()) + " bytes): " + temp.string();
            fs::remove(temp, ec);
            return false;
        }
    }

    // std::filesystem::rename replaces an existing destination atomically on
    // both POSIX and Windows. Deleting first is explicitly NOT done: a crash
    // between the delete and the rename would lose the previous good file,
    // which is exactly the failure this function exists to prevent.
    fs::rename(temp, destination, ec);
    if (ec) {
        error = "rename failed: " + temp.string() + " -> " + destination.string() + " (" + ec.message() + ")";
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

SaveResult BenchmarkStore::save(const BenchmarkSession& session) {
    SaveResult result;
    if (!isSafeSessionId(session.id)) {
        result.errorCode = "invalid_id";
        result.error = "session id '" + session.id + "' is not filesystem-safe";
        return result;
    }

    std::string dirError;
    if (!ensureDirectory(dirError)) {
        result.errorCode = "directory_unavailable";
        result.error = dirError;
        return result;
    }

    fs::path destination = sessionPath(session.id);
    if (destination.empty()) {
        result.errorCode = "invalid_id";
        result.error = "could not build a path for session id '" + session.id + "'";
        return result;
    }
    result.path = destination;

    std::error_code ec;
    if (fs::exists(destination, ec)) {
        // Never silently replace evidence. If the identical bytes are already
        // there (a retried save), report success without writing.
        BenchmarkSession existing;
        std::string loadError;
        if (load(session.id, existing, loadError) && existing.serialize() == session.serialize()) {
            result.ok = true;
            result.wrote = false;
            return result;
        }
        result.errorCode = "already_exists";
        result.error = "a different session with id '" + session.id +
                       "' is already stored; refusing to overwrite it";
        return result;
    }

    std::string writeError;
    if (!writeAtomic(destination, session.serialize(), writeError)) {
        result.errorCode = "io_error";
        result.error = writeError;
        return result;
    }
    result.ok = true;
    result.wrote = true;
    return result;
}

bool BenchmarkStore::load(const std::string& id, BenchmarkSession& out, std::string& error) const {
    if (!isSafeSessionId(id)) {
        error = "session id '" + id + "' is not filesystem-safe";
        return false;
    }
    fs::path path = sessionPath(id);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        error = "no session file for id '" + id + "'";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        error = "could not open '" + path.string() + "'";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (text.empty()) {
        error = "session file is empty: " + path.string();
        return false;
    }
    if (!BenchmarkSession::parse(text, out, error)) {
        error = "could not parse '" + path.string() + "': " + error;
        return false;
    }
    out.sourcePath = path.string();
    out.loadedFromDisk = true;
    return true;
}

bool BenchmarkStore::exists(const std::string& id) const {
    if (!isSafeSessionId(id)) return false;
    fs::path path = sessionPath(id);
    std::error_code ec;
    return fs::exists(path, ec);
}

size_t BenchmarkStore::count() const {
    std::error_code ec;
    if (!fs::is_directory(m_rootDir, ec)) return 0;
    size_t n = 0;
    for (const auto& entry : fs::directory_iterator(m_rootDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".json" && entry.path().filename() != "index.json") ++n;
    }
    return n;
}

std::vector<SessionSummary> BenchmarkStore::list() const {
    std::vector<SessionSummary> summaries;
    std::error_code ec;
    if (!fs::is_directory(m_rootDir, ec)) return summaries;

    for (const auto& entry : fs::directory_iterator(m_rootDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const fs::path path = entry.path();
        if (path.extension() != ".json") continue;
        if (path.filename() == "index.json") continue;
        // Ignore our own temporary files.
        if (path.filename().string().find(".tmp") != std::string::npos) continue;

        SessionSummary summary;
        summary.fileName = path.filename().string();
        summary.id = path.stem().string();
        std::error_code sizeEc;
        summary.fileSizeBytes = fs::file_size(path, sizeEc);

        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            summary.readable = false;
            summary.readError = "could not open the file";
            summaries.push_back(std::move(summary));
            continue;
        }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        BenchmarkSession session;
        std::string parseError;
        if (!BenchmarkSession::parse(text, session, parseError)) {
            summary.readable = false;
            summary.readError = parseError;
            summaries.push_back(std::move(summary));
            continue;
        }
        summary.id = session.id.empty() ? summary.id : session.id;
        summary.mode = session.mode;
        summary.state = session.state;
        summary.startedUtc = session.startedUtc;
        summary.levelName = session.level.name;
        summary.targetDurationMs = session.targetDurationMs;
        summary.capturedDurationMs = session.capturedDurationMs;
        summary.avgFps = session.avgFps();
        summary.meanFrameTimeMs = session.meanFrameTimeMs();
        summary.p99FrameTimeMs = session.p99FrameTimeMs();
        summary.samples = session.stats.samples;
        summary.comparable = session.isComparable();
        summaries.push_back(std::move(summary));
    }

    // Newest first. Session ids start with a UTC timestamp, so a string compare
    // is a chronological compare; unreadable files keep their filename order.
    std::stable_sort(summaries.begin(), summaries.end(),
                     [](const SessionSummary& a, const SessionSummary& b) {
                         return a.fileName > b.fileName;
                     });
    return summaries;
}

bool BenchmarkStore::exportTo(const std::string& id, const fs::path& destination,
                              std::string& error) const {
    fs::path source = sessionPath(id);
    std::error_code ec;
    if (!isSafeSessionId(id) || !fs::exists(source, ec)) {
        error = "no session file for id '" + id + "'";
        return false;
    }
    std::ifstream in(source, std::ios::binary);
    if (!in.is_open()) {
        error = "could not open '" + source.string() + "'";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (!writeAtomic(destination, text, error)) return false;
    return true;
}

std::string BenchmarkStore::allocateSessionId(BenchmarkMode mode, uint64_t epochMs,
                                              uint32_t& counter) const {
    for (uint32_t attempt = 0; attempt < 10000; ++attempt) {
        std::string id = generateSessionId(mode, epochMs, counter);
        if (!exists(id)) return id;
        ++counter;
    }
    // Pathological case (10k collisions in one second): fall back to an id that
    // embeds the raw epoch milliseconds, which is still unique and still safe.
    std::ostringstream ss;
    ss << "s" << epochMs << '-' << modeId(mode) << '-' << counter;
    return ss.str();
}

bool BenchmarkStore::rebuildIndex(std::string& error) const {
    std::vector<SessionSummary> summaries = list();

    core::json::Value root = core::json::Value::object();
    root.set("index_kind", core::json::Value::string("optigd_benchmark_index"));
    root.set("schema_version", core::json::Value::integer(BenchmarkSession::kSchemaVersion));
    root.set("optigd_version", core::json::Value::string(core::kOptiGDVersion));
    root.set("session_count", core::json::Value::integer(static_cast<long long>(summaries.size())));
    root.set("note", core::json::Value::string(
        "Cache only. The per-session JSON files are the source of truth; this "
        "index is rebuilt from them and may be deleted safely."));

    core::json::Value sessions = core::json::Value::array();
    for (const auto& s : summaries) {
        core::json::Value item = core::json::Value::object();
        item.set("id", core::json::Value::string(s.id));
        item.set("file", core::json::Value::string(s.fileName));
        item.set("mode", core::json::Value::string(modeId(s.mode)));
        item.set("state", core::json::Value::string(stateId(s.state)));
        item.set("started_utc", core::json::Value::string(s.startedUtc));
        item.set("level_name", core::json::Value::string(s.levelName));
        item.set("captured_duration_ms", core::json::Value::number(s.capturedDurationMs));
        item.set("avg_fps", core::json::Value::number(s.avgFps));
        item.set("samples", core::json::Value::integer(static_cast<long long>(s.samples)));
        item.set("comparable", core::json::Value::boolean(s.comparable));
        item.set("readable", core::json::Value::boolean(s.readable));
        if (!s.readable) {
            item.set("read_error", core::json::Value::string(s.readError));
        }
        sessions.push(std::move(item));
    }
    root.set("sessions", std::move(sessions));

    return writeAtomic(indexPath(), core::json::serialize(root, 2), error);
}

} // namespace optigd::benchmark

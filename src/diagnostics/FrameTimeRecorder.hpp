#pragma once

#include "../core/Types.hpp"
#include <cstdint>
#include <fstream>
#include <string>

namespace optigd::diagnostics {

// Writes one CSV row per game frame. Enabled only by the record-frametimes setting.
//
// Columns: frame,interval_us,game_update_us,optigd_overhead_us,culling_shadow_us,
//          total_objects,shadow_visible,shadow_culled,arena_allocations,tier
//
// The file is plain text in the mod save directory. It contains no game assets and no
// personal data beyond frame timings.
class FrameTimeRecorder {
public:
    static constexpr const char* kHeader =
        "frame,interval_us,game_update_us,optigd_overhead_us,culling_shadow_us,"
        "total_objects,shadow_visible,shadow_culled,arena_allocations,tier";

    FrameTimeRecorder() = default;
    ~FrameTimeRecorder();

    // Opens (truncates) the file and writes the header. Returns false on failure.
    bool open(const std::string& path);
    void close();
    bool isOpen() const { return m_file.is_open(); }

    // Appends one row. No-op when not open.
    void append(const core::FrameStats& stats, const char* tierName);

    uint64_t getRowsWritten() const { return m_rows; }

private:
    std::ofstream m_file;
    uint64_t m_rows{0};
};

} // namespace optigd::diagnostics

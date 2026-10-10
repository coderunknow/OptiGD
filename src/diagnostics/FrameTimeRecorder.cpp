#include "FrameTimeRecorder.hpp"

#include <iomanip>

namespace optigd::diagnostics {

FrameTimeRecorder::~FrameTimeRecorder() {
    close();
}

bool FrameTimeRecorder::open(const std::string& path) {
    close();
    m_file.open(path, std::ios::out | std::ios::trunc);
    if (!m_file.is_open()) return false;
    m_file << kHeader << "\n";
    m_rows = 0;
    return m_file.good();
}

void FrameTimeRecorder::close() {
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
}

void FrameTimeRecorder::append(const core::FrameStats& s, const char* tierName) {
    if (!m_file.is_open()) return;
    m_file << std::fixed << std::setprecision(1)
           << s.frameIndex << ','
           << s.frameIntervalUs << ','
           << s.gameUpdateUs << ','
           << s.optigdOverheadUs << ','
           << s.cullingShadowUs << ','
           << s.totalObjects << ','
           << s.shadowVisibleObjects << ','
           << s.shadowCulledObjects << ','
           << s.arenaAllocations << ','
           << (tierName ? tierName : "unknown") << '\n';
    m_rows++;
}

} // namespace optigd::diagnostics

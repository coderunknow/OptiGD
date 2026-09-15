#include "Logger.hpp"
#include <fstream>
#include <iostream>

namespace optigd::core {

Logger& Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::~Logger() {
    disableFileLogging();
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

LogLevel Logger::getLevel() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_minLevel;
}

void Logger::log(LogLevel level, std::string_view msg) {
    if (level < m_minLevel || level == LogLevel::None) {
        return;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    LogEntry entry{level, static_cast<uint64_t>(now), std::string(msg)};

    std::lock_guard<std::mutex> lock(m_mutex);
    appendToRingBuffer(entry);
    if (m_fileLoggingEnabled) {
        writeToFile(entry);
    }
}

void Logger::debug(std::string_view msg) { log(LogLevel::Debug, msg); }
void Logger::info(std::string_view msg)  { log(LogLevel::Info, msg); }
void Logger::warn(std::string_view msg)  { log(LogLevel::Warn, msg); }
void Logger::error(std::string_view msg) { log(LogLevel::Error, msg); }

void Logger::appendToRingBuffer(LogEntry entry) {
    if (m_ringBuffer.size() >= m_maxRingBufferSize) {
        m_ringBuffer.erase(m_ringBuffer.begin());
    }
    m_ringBuffer.push_back(std::move(entry));
}

void Logger::writeToFile(const LogEntry& entry) {
    std::ofstream ofs(m_filePath, std::ios::app);
    if (!ofs.is_open()) return;

    const char* lvlStr = "INFO";
    switch (entry.level) {
        case LogLevel::Debug: lvlStr = "DEBUG"; break;
        case LogLevel::Info:  lvlStr = "INFO"; break;
        case LogLevel::Warn:  lvlStr = "WARN"; break;
        case LogLevel::Error: lvlStr = "ERROR"; break;
        default: break;
    }
    ofs << "[" << entry.timestampMs << "] [" << lvlStr << "] " << entry.message << "\n";
}

std::vector<LogEntry> Logger::getRecentLogs(size_t maxEntries) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_ringBuffer.size() <= maxEntries) {
        return m_ringBuffer;
    }
    return std::vector<LogEntry>(m_ringBuffer.end() - maxEntries, m_ringBuffer.end());
}

void Logger::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ringBuffer.clear();
}

void Logger::enableFileLogging(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_filePath = path;
    m_fileLoggingEnabled = true;
}

void Logger::disableFileLogging() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fileLoggingEnabled = false;
}

} // namespace optigd::core

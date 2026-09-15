#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <mutex>
#include <chrono>
#include <sstream>

namespace optigd::core {

enum class LogLevel : uint8_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    None = 4
};

struct LogEntry {
    LogLevel level{LogLevel::Info};
    uint64_t timestampMs{0};
    std::string message;
};

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level);
    LogLevel getLevel() const;

    void log(LogLevel level, std::string_view msg);
    void debug(std::string_view msg);
    void info(std::string_view msg);
    void warn(std::string_view msg);
    void error(std::string_view msg);

    std::vector<LogEntry> getRecentLogs(size_t maxEntries = 100);
    void clear();

    void enableFileLogging(const std::string& path);
    void disableFileLogging();

private:
    Logger() = default;
    ~Logger();

    LogLevel m_minLevel{LogLevel::Info};
    mutable std::mutex m_mutex;
    std::vector<LogEntry> m_ringBuffer;
    size_t m_maxRingBufferSize{500};
    std::string m_filePath;
    bool m_fileLoggingEnabled{false};

    void appendToRingBuffer(LogEntry entry);
    void writeToFile(const LogEntry& entry);
};

} // namespace optigd::core

#define OPTIGD_LOG_DEBUG(msg) optigd::core::Logger::instance().debug(msg)
#define OPTIGD_LOG_INFO(msg)  optigd::core::Logger::instance().info(msg)
#define OPTIGD_LOG_WARN(msg)  optigd::core::Logger::instance().warn(msg)
#define OPTIGD_LOG_ERROR(msg) optigd::core::Logger::instance().error(msg)

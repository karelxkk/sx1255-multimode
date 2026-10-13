#pragma once

#include <cstdarg>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>

namespace sx1255::util {

enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
};

inline const char* logLevelName(LogLevel level)
{
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        default:              return "????";
    }
}

bool parseLogLevel(const std::string& text, LogLevel& level);

// Thread-safe console + rotating-file logger.  Log files rotate by size
// (maxBytes) with a bounded number of backups (maxFiles).
class Logger {
public:
    static Logger& instance();

    // An empty filePrefix disables file output.
    void configure(LogLevel level, const std::string& filePrefix,
                   std::size_t maxBytes = 2 * 1024 * 1024,
                   std::size_t maxFiles = 3);

    LogLevel level() const;
    void setLevel(LogLevel level);

    void log(LogLevel level, const char* fmt, ...);
    void vlog(LogLevel level, const char* fmt, std::va_list args);

    void debug(const char* fmt, ...);
    void info(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void error(const char* fmt, ...);

private:
    Logger() = default;
    void writeLine(LogLevel level, const std::string& line);
    void rotateIfNeeded();

    mutable std::mutex m_mutex;
    LogLevel m_level = LogLevel::Info;
    std::string m_filePrefix;
    std::size_t m_maxBytes = 2 * 1024 * 1024;
    std::size_t m_maxFiles = 3;
    std::ofstream m_file;
};

}  // namespace sx1255::util

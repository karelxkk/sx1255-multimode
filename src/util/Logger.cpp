#include "sx1255/util/Logger.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace sx1255::util {

namespace {

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;

    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    std::ostringstream out;
    out << buf << '.' << (ms.count() < 100 ? "0" : "") << (ms.count() < 10 ? "0" : "")
        << ms.count();
    return out.str();
}

}  // namespace

bool parseLogLevel(const std::string& text, LogLevel& level)
{
    std::string value = text;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (value == "debug")
        level = LogLevel::Debug;
    else if (value == "info")
        level = LogLevel::Info;
    else if (value == "warn" || value == "warning")
        level = LogLevel::Warn;
    else if (value == "error")
        level = LogLevel::Error;
    else
        return false;
    return true;
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::configure(LogLevel level, const std::string& filePrefix,
                       std::size_t maxBytes, std::size_t maxFiles)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
    m_filePrefix = filePrefix;
    m_maxBytes = maxBytes;
    m_maxFiles = maxFiles;

    if (m_file.is_open())
        m_file.close();

    if (!m_filePrefix.empty()) {
        m_file.open(m_filePrefix + ".log", std::ios::app);
    }
}

LogLevel Logger::level() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_level;
}

void Logger::setLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
}

void Logger::vlog(LogLevel level, const char* fmt, std::va_list args)
{
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (static_cast<int>(level) < static_cast<int>(m_level))
        return;

    const std::string line =
        timestamp() + " [" + logLevelName(level) + "] " + buffer;

    std::fprintf(stderr, "%s\n", line.c_str());

    if (m_file.is_open()) {
        rotateIfNeeded();
        m_file << line << '\n';
        m_file.flush();
    }
}

void Logger::log(LogLevel level, const char* fmt, ...)
{
    std::va_list args;
    va_start(args, fmt);
    vlog(level, fmt, args);
    va_end(args);
}

void Logger::debug(const char* fmt, ...)
{
    std::va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Debug, fmt, args);
    va_end(args);
}

void Logger::info(const char* fmt, ...)
{
    std::va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Info, fmt, args);
    va_end(args);
}

void Logger::warn(const char* fmt, ...)
{
    std::va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Warn, fmt, args);
    va_end(args);
}

void Logger::error(const char* fmt, ...)
{
    std::va_list args;
    va_start(args, fmt);
    vlog(LogLevel::Error, fmt, args);
    va_end(args);
}

void Logger::rotateIfNeeded()
{
    m_file.flush();
    const auto pos = m_file.tellp();
    if (pos < static_cast<std::streamoff>(m_maxBytes))
        return;

    m_file.close();

    // Drop the oldest backup, then shift .N -> .N+1.
    for (std::size_t i = m_maxFiles; i > 0; --i) {
        const std::string from = i == 1
            ? m_filePrefix + ".log"
            : m_filePrefix + ".log." + std::to_string(i - 1);
        const std::string to = m_filePrefix + ".log." + std::to_string(i);
        std::remove(to.c_str());
        std::rename(from.c_str(), to.c_str());
    }

    m_file.open(m_filePrefix + ".log", std::ios::trunc);
}

}  // namespace sx1255::util

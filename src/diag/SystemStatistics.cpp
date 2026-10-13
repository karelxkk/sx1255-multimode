#include "sx1255/diag/SystemStatistics.hpp"

#include <fstream>
#include <cstdio>
#include <sstream>

namespace sx1255 {

SystemStatistics::SystemStatistics()
    : m_start(std::chrono::steady_clock::now())
{
}

void SystemStatistics::recordDetection(Mode mode)
{
    switch (mode) {
        case Mode::FM:    m_fmDetected++; break;
        case Mode::DStar: m_dstarDetected++; break;
        case Mode::M17:   m_m17Detected++; break;
        default: break;
    }
}

void SystemStatistics::recordSession(Mode mode)
{
    switch (mode) {
        case Mode::FM:    m_fmSessions++; break;
        case Mode::DStar: m_dstarSessions++; break;
        case Mode::M17:   m_m17Sessions++; break;
        default: break;
    }
}

void SystemStatistics::recordFalsePositive()
{
    m_falsePositives++;
}

void SystemStatistics::setCurrentMode(std::string mode)
{
    std::lock_guard<std::mutex> lock(m_modeMutex);
    m_currentMode = std::move(mode);
}

void SystemStatistics::setRssi(double rssiDbm)
{
    m_rssi = rssiDbm;
}

std::uint64_t SystemStatistics::fmDetected() const { return m_fmDetected.load(); }
std::uint64_t SystemStatistics::dstarDetected() const { return m_dstarDetected.load(); }
std::uint64_t SystemStatistics::m17Detected() const { return m_m17Detected.load(); }
std::uint64_t SystemStatistics::fmSessions() const { return m_fmSessions.load(); }
std::uint64_t SystemStatistics::dstarSessions() const { return m_dstarSessions.load(); }
std::uint64_t SystemStatistics::m17Sessions() const { return m_m17Sessions.load(); }
std::uint64_t SystemStatistics::falsePositives() const { return m_falsePositives.load(); }
double SystemStatistics::rssiDbm() const { return m_rssi.load(); }

std::string SystemStatistics::currentMode() const
{
    std::lock_guard<std::mutex> lock(m_modeMutex);
    return m_currentMode;
}

std::uint64_t SystemStatistics::uptimeSeconds() const
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_start).count());
}

std::uint64_t SystemStatistics::totalUptimeSeconds() const
{
    return m_totalUptime.load() + uptimeSeconds();
}

namespace {

// Minimal flat-JSON number extractor for the controlled stats.json format:
// finds "key":<integer>.
bool readJsonCounter(const std::string& json, const std::string& key, std::uint64_t& out)
{
    const std::string needle = "\"" + key + "\":";
    const auto pos = json.find(needle);
    if (pos == std::string::npos)
        return false;

    std::size_t i = pos + needle.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n'))
        ++i;

    std::string number;
    while (i < json.size() && (json[i] >= '0' && json[i] <= '9')) {
        number += json[i];
        ++i;
    }
    if (number.empty())
        return false;

    out = std::stoull(number);
    return true;
}

}  // namespace

bool SystemStatistics::save(const std::string& path) const
{
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
        return false;

    file << "{\n"
         << "  \"fm_detected\": " << fmDetected() << ",\n"
         << "  \"dstar_detected\": " << dstarDetected() << ",\n"
         << "  \"m17_detected\": " << m17Detected() << ",\n"
         << "  \"fm_sessions\": " << fmSessions() << ",\n"
         << "  \"dstar_sessions\": " << dstarSessions() << ",\n"
         << "  \"m17_sessions\": " << m17Sessions() << ",\n"
         << "  \"false_positives\": " << falsePositives() << ",\n"
         << "  \"total_uptime_seconds\": " << totalUptimeSeconds() << "\n"
         << "}\n";
    return static_cast<bool>(file);
}

bool SystemStatistics::load(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    std::uint64_t value = 0;
    if (readJsonCounter(json, "fm_detected", value))
        m_fmDetected = value;
    if (readJsonCounter(json, "dstar_detected", value))
        m_dstarDetected = value;
    if (readJsonCounter(json, "m17_detected", value))
        m_m17Detected = value;
    if (readJsonCounter(json, "fm_sessions", value))
        m_fmSessions = value;
    if (readJsonCounter(json, "dstar_sessions", value))
        m_dstarSessions = value;
    if (readJsonCounter(json, "m17_sessions", value))
        m_m17Sessions = value;
    if (readJsonCounter(json, "false_positives", value))
        m_falsePositives = value;
    if (readJsonCounter(json, "total_uptime_seconds", value))
        m_totalUptime = value;

    m_start = std::chrono::steady_clock::now();
    return true;
}

std::string SystemStatistics::statusJson() const
{
    const std::string mode = currentMode();
    std::string modeName;
    if (mode == "ACTIVE_FM") modeName = "FM";
    else if (mode == "ACTIVE_DSTAR") modeName = "D-Star";
    else if (mode == "ACTIVE_M17") modeName = "M17";
    else modeName = "";  // idle/detecting

    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\"state\":\"%s\",\"mode\":\"%s\",\"rssi\":%.0f,\"uptime\":%llu,"
        "\"stats\":{"
        "\"fm_detected\":%llu,\"dstar_detected\":%llu,\"m17_detected\":%llu,"
        "\"fm_sessions\":%llu,\"dstar_sessions\":%llu,\"m17_sessions\":%llu,"
        "\"false_positives\":%llu}}",
        mode.c_str(), modeName.c_str(), rssiDbm(),
        static_cast<unsigned long long>(uptimeSeconds()),
        static_cast<unsigned long long>(fmDetected()),
        static_cast<unsigned long long>(dstarDetected()),
        static_cast<unsigned long long>(m17Detected()),
        static_cast<unsigned long long>(fmSessions()),
        static_cast<unsigned long long>(dstarSessions()),
        static_cast<unsigned long long>(m17Sessions()),
        static_cast<unsigned long long>(falsePositives()));
    return buf;
}

}  // namespace sx1255

#include "sx1255/util/SystemInfo.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace sx1255::util {

namespace {

std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::uint64_t parseU64After(const std::string& text, const std::string& key)
{
    const auto pos = text.find(key);
    if (pos == std::string::npos)
        return 0;
    std::size_t i = pos + key.size();
    while (i < text.size() && (text[i] < '0' || text[i] > '9'))
        ++i;
    std::uint64_t value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + static_cast<std::uint64_t>(text[i] - '0');
        ++i;
    }
    return value;
}

}  // namespace

ProcessInfo readProcessInfo()
{
    ProcessInfo info;

    const std::string status = readFile("/proc/self/status");
    info.rssKb = parseU64After(status, "VmRSS:");
    info.threads = parseU64After(status, "Threads:");
    info.voluntaryCtxSwitches = parseU64After(status, "voluntary_ctxt_switches:");

    // /proc/self/stat: field 14 = utime, field 15 = stime (after the comm
    // field, which may contain spaces and parentheses).
    const std::string stat = readFile("/proc/self/stat");
    const auto closeParen = stat.rfind(')');
    if (closeParen != std::string::npos && closeParen + 1 < stat.size()) {
        std::istringstream fields(stat.substr(closeParen + 2));
        std::string token;
        int field = 3;  // state is field 3; utime=14, stime=15
        double utime = 0.0, stime = 0.0;
        while (fields >> token) {
            if (field == 14)
                utime = std::stod(token);
            else if (field == 15)
                stime = std::stod(token);
            ++field;
        }
        const long ticks = ::sysconf(_SC_CLK_TCK);
        if (ticks > 0)
            info.cpuSeconds = (utime + stime) / static_cast<double>(ticks);
    }

    const std::string uptime = readFile("/proc/uptime");
    info.systemUptimeSeconds = std::stod(uptime);

    return info;
}

std::string systemInfoJson(const std::string& frontendState)
{
    const ProcessInfo info = readProcessInfo();
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "{\"memory_rss_kb\":%llu,\"cpu_seconds\":%.3f,\"threads\":%llu,"
        "\"system_uptime_s\":%.1f,\"voluntary_ctx_switches\":%llu,"
        "\"frontend_state\":\"%s\"}",
        static_cast<unsigned long long>(info.rssKb),
        info.cpuSeconds,
        static_cast<unsigned long long>(info.threads),
        info.systemUptimeSeconds,
        static_cast<unsigned long long>(info.voluntaryCtxSwitches),
        frontendState.c_str());
    return buf;
}

}  // namespace sx1255::util

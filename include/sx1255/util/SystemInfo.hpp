#pragma once

#include <cstdint>
#include <string>

namespace sx1255::util {

struct ProcessInfo {
    std::uint64_t rssKb = 0;        // resident set size
    double        cpuSeconds = 0.0; // process utime+stime
    std::uint64_t threads = 0;
    double        systemUptimeSeconds = 0.0;
    std::uint64_t voluntaryCtxSwitches = 0;
};

ProcessInfo readProcessInfo();

// JSON body for GET /api/system.  frontendState is a short human-readable
// string supplied by the caller (e.g. "running", "stopped").
std::string systemInfoJson(const std::string& frontendState);

}  // namespace sx1255::util

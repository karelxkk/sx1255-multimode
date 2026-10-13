#pragma once

#include "sx1255/Types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace sx1255 {

// Runtime diagnostics shared between the plugins, the session manager and the
// HTTP status endpoint.
class SystemStatistics {
public:
    SystemStatistics();

    void recordDetection(Mode mode);
    void recordSession(Mode mode);
    void recordFalsePositive();
    void setCurrentMode(std::string mode);
    void setRssi(double rssiDbm);

    std::uint64_t fmDetected() const;
    std::uint64_t dstarDetected() const;
    std::uint64_t m17Detected() const;
    std::uint64_t fmSessions() const;
    std::uint64_t dstarSessions() const;
    std::uint64_t m17Sessions() const;
    std::uint64_t falsePositives() const;
    std::string currentMode() const;
    double rssiDbm() const;
    std::uint64_t uptimeSeconds() const;
    std::uint64_t totalUptimeSeconds() const;

    // Builds the JSON body for GET /api/status.
    std::string statusJson() const;

    // Persist/restore the cumulative counters (stats.json).  Session uptime
    // is accumulated into the persisted total on save.
    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    std::atomic<std::uint64_t> m_fmDetected{0};
    std::atomic<std::uint64_t> m_dstarDetected{0};
    std::atomic<std::uint64_t> m_m17Detected{0};
    std::atomic<std::uint64_t> m_fmSessions{0};
    std::atomic<std::uint64_t> m_dstarSessions{0};
    std::atomic<std::uint64_t> m_m17Sessions{0};
    std::atomic<std::uint64_t> m_falsePositives{0};
    std::atomic<double> m_rssi{-120.0};
    std::atomic<std::uint64_t> m_totalUptime{0};

    mutable std::mutex m_modeMutex;
    std::string m_currentMode = "IDLE";
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace sx1255

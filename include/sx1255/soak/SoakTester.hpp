#pragma once

#include "sx1255/config/Config.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sx1255::soak {

struct SoakMetrics {
    std::uint64_t durationSeconds = 0;
    std::uint64_t samplesProcessed = 0;
    std::uint64_t blocksProcessed = 0;
    std::uint64_t sessionsCompleted = 0;
    std::uint64_t fmSessions = 0;
    std::uint64_t dstarSessions = 0;
    std::uint64_t m17Sessions = 0;
    std::uint64_t errors = 0;
    double        peakRssKb = 0.0;
    double        avgRssKb = 0.0;
    double        cpuSeconds = 0.0;
    std::uint64_t allocations = 0;   // optional external counter (new/delete)
};

// ---------------------------------------------------------------------------
// SoakTester
//
// Long-running stability harness that drives the full dispatcher/plugin/
// session pipeline with generated FM, D-Star, M17 and noise traffic, then
// reports RAM, CPU, allocation, session and error counters as soak_report.json.
// ---------------------------------------------------------------------------
class SoakTester {
public:
    SoakTester(Config config, std::chrono::seconds duration);

    // Blocking run.  Returns false if the pipeline could not be set up.
    bool run();

    const SoakMetrics& metrics() const;
    std::string toJson() const;
    void writeReport(const std::string& path) const;

    // Optional hook to an external allocation counter (e.g. a global
    // operator new/delete override in the soak executable).
    void setAllocationCounter(std::function<std::uint64_t()> counter);

private:
    Config m_config;
    std::chrono::seconds m_duration;
    SoakMetrics m_metrics;
    std::function<std::uint64_t()> m_allocationCounter;
};

}  // namespace sx1255::soak

#pragma once

#include "sx1255/config/Config.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/diag/SystemStatistics.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace sx1255 {

class PluginRegistry;

// Snapshot of the IQ capture/replay subsystem reported by GET /api/iq.
struct IQStatusSnapshot {
    bool         recording = false;
    bool         replay    = false;
    std::uint64_t capturedSamples = 0;
    std::string  file;
};

// Minimal dependency-free HTTP server exposing the diagnostic API and the
// web dashboard: GET /, /api/status, /api/plugins, /api/m17/diagnostics,
// POST /api/m17/diagnostics/reset, /api/iq, /api/spectrum, /api/waterfall.
class HttpStatusServer {
public:
    HttpStatusServer(std::uint16_t port,
                     SystemStatistics& stats,
                     SessionManager& sessions,
                     const Config& config,
                     std::function<double()> rssiGetter);
    ~HttpStatusServer();

    bool start();
    void stop();
    bool running() const { return m_running.load(); }

    void setIqStatusProvider(std::function<IQStatusSnapshot()> provider);
    void setSpectrumProvider(std::function<std::string()> provider);
    void setWaterfallProvider(std::function<std::string(std::uint64_t)> provider);
    void setHealthProvider(std::function<std::string()> provider);
    void setSystemProvider(std::function<std::string()> provider);
    void setPluginRegistry(PluginRegistry* registry);

private:
    void run();
    std::string handleStatus();
    std::string handlePlugins() const;
    std::string handleM17Diagnostics() const;
    std::string handleM17DiagnosticsReset();
    std::string handleIq() const;
    std::string handleSpectrum() const;
    std::string handleWaterfall(std::uint64_t sinceSeq) const;
    std::string handleHealth() const;
    std::string handleSystem() const;
    std::string handleDashboard() const;

    std::uint16_t m_port;
    SystemStatistics& m_stats;
    SessionManager& m_sessions;
    const Config& m_config;
    std::function<double()> m_rssiGetter;
    std::function<IQStatusSnapshot()> m_iqStatusProvider;
    std::function<std::string()> m_spectrumProvider;
    std::function<std::string(std::uint64_t)> m_waterfallProvider;
    std::function<std::string()> m_healthProvider;
    std::function<std::string()> m_systemProvider;
    PluginRegistry* m_registry = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    int m_fd = -1;
};

}  // namespace sx1255

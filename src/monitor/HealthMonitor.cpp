#include "sx1255/monitor/HealthMonitor.hpp"

#include "sx1255/util/Logger.hpp"

#include <algorithm>
#include <cstdio>

namespace sx1255::monitor {

HealthMonitor::HealthMonitor(std::chrono::milliseconds pollInterval, unsigned threshold)
    : m_pollInterval(pollInterval)
    , m_threshold(std::max(1u, threshold))
{
}

HealthMonitor::~HealthMonitor()
{
    stop();
}

void HealthMonitor::registerComponent(std::string name, Check check, Restart restart)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_components.push_back(Component{std::move(name), std::move(check), std::move(restart)});
}

void HealthMonitor::setProblemHandler(std::function<void(const std::string&)> handler)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_problemHandler = std::move(handler);
}

void HealthMonitor::start()
{
    if (m_running.load())
        return;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void HealthMonitor::stop()
{
    if (!m_running.load())
        return;
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

std::vector<ComponentHealth> HealthMonitor::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ComponentHealth> out;
    out.reserve(m_components.size());
    for (const auto& c : m_components)
        out.push_back({c.name, c.healthy, c.healthy ? "ok" : "unhealthy", c.restarts});
    return out;
}

std::string HealthMonitor::healthJson() const
{
    const auto components = snapshot();
    std::string out = "{\"healthy\":";
    bool allHealthy = true;
    for (const auto& c : components)
        if (!c.healthy)
            allHealthy = false;
    out += allHealthy ? "true" : "false";
    out += ",\"components\":[";
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i)
            out += ',';
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "{\"name\":\"%s\",\"healthy\":%s,\"detail\":\"%s\",\"restarts\":%llu}",
            components[i].name.c_str(),
            components[i].healthy ? "true" : "false",
            components[i].detail.c_str(),
            static_cast<unsigned long long>(components[i].restarts));
        out += buf;
    }
    out += "]}";
    return out;
}

void HealthMonitor::run()
{
    while (m_running.load()) {
        std::vector<std::pair<std::string, Restart>> toRestart;
        std::function<void(const std::string&)> problem;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            problem = m_problemHandler;
            for (auto& c : m_components) {
                bool healthy = false;
                try {
                    healthy = c.check ? c.check() : false;
                } catch (...) {
                    healthy = false;
                }

                if (healthy) {
                    c.healthy = true;
                    c.unhealthyStreak = 0;
                } else {
                    c.unhealthyStreak++;
                    if (c.unhealthyStreak >= m_threshold) {
                        c.healthy = false;
                        if (c.restart) {
                            toRestart.emplace_back(c.name, c.restart);
                            c.restarts++;
                        }
                        c.unhealthyStreak = 0;  // wait for recovery before retrying
                    }
                }
            }
        }

        for (const auto& [name, restart] : toRestart) {
            util::Logger::instance().error("HealthMonitor: restarting '%s'", name.c_str());
            if (problem)
                problem(name);
            try {
                restart();
            } catch (...) {
                util::Logger::instance().error("HealthMonitor: restart of '%s' threw", name.c_str());
            }
        }

        std::this_thread::sleep_for(m_pollInterval);
    }
}

}  // namespace sx1255::monitor

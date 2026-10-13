#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sx1255::monitor {

struct ComponentHealth {
    std::string name;
    bool healthy = true;
    std::string detail;
    std::uint64_t restarts = 0;
};

// Polls registered subsystems and, when a component stays unhealthy for
// `threshold` consecutive polls, logs the failure, emits onProblem() and
// invokes that component's restart hook.  It restarts subsystems, never the
// whole process.
class HealthMonitor {
public:
    using Check = std::function<bool()>;
    using Restart = std::function<void()>;

    HealthMonitor(std::chrono::milliseconds pollInterval = std::chrono::milliseconds(1000),
                  unsigned threshold = 3);
    ~HealthMonitor();

    void registerComponent(std::string name, Check check, Restart restart);
    void setProblemHandler(std::function<void(const std::string&)> handler);

    void start();
    void stop();

    std::vector<ComponentHealth> snapshot() const;
    std::string healthJson() const;

private:
    void run();

    struct Component {
        std::string name;
        Check check;
        Restart restart;
        bool healthy = true;
        unsigned unhealthyStreak = 0;
        std::uint64_t restarts = 0;
    };

    std::chrono::milliseconds m_pollInterval;
    unsigned m_threshold;

    mutable std::mutex m_mutex;
    std::vector<Component> m_components;
    std::function<void(const std::string&)> m_problemHandler;

    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

}  // namespace sx1255::monitor

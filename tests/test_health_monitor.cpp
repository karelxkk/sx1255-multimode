#include "sx1255/monitor/HealthMonitor.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace sx1255;

int main()
{
    std::atomic<int> restarts{0};
    std::atomic<bool> healthy{true};
    std::atomic<int> problems{0};

    monitor::HealthMonitor monitor(std::chrono::milliseconds(20), 2);
    monitor.registerComponent(
        "test_component",
        [&healthy] { return healthy.load(); },
        [&restarts] { restarts.fetch_add(1); });
    monitor.setProblemHandler([&problems](const std::string&) { problems.fetch_add(1); });

    monitor.start();

    // Initially healthy.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    if (!monitor.snapshot().empty() && !monitor.snapshot()[0].healthy)
        return 1;

    // Go unhealthy for enough consecutive polls to trigger a restart.
    healthy = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if (restarts.load() == 0)
        return 2;
    if (problems.load() == 0)
        return 3;

    // Recover.
    healthy = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    monitor.stop();

    const auto components = monitor.snapshot();
    if (components.size() != 1)
        return 4;
    if (components[0].restarts == 0)
        return 5;
    if (monitor.healthJson().find("\"healthy\"") == std::string::npos)
        return 6;

    return 0;
}

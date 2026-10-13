#include "sx1255/core/SessionManager.hpp"

#include "mock_plugin.hpp"

#include <chrono>
#include <memory>
#include <vector>

using namespace sx1255;
using namespace std::chrono_literals;

int main()
{
    auto m17 = std::make_shared<test::MockPlugin>(
        "m17", Mode::M17, 30, [](const IQBlock&) { return Detection{true, 0.9F}; });
    auto dstar = std::make_shared<test::MockPlugin>(
        "dstar", Mode::DStar, 20, [](const IQBlock&) { return Detection{false, 0.0F}; });
    auto fm = std::make_shared<test::MockPlugin>(
        "fm", Mode::FM, 10, [](const IQBlock&) { return Detection{false, 0.0F}; });

    SessionManager sessions(SessionManager::Settings{120000ms, 0ms});
    sessions.start();

    std::vector<DetectionReport> reports = {
        {m17, m17->detect(IQBlock{})},
        {dstar, dstar->detect(IQBlock{})},
        {fm, fm->detect(IQBlock{})},
    };

    IModePlugin* granted = sessions.evaluateDetections(reports, nullptr);
    if (granted != m17.get())
        return 1;
    if (sessions.state() != SessionManager::State::ActiveM17)
        return 2;
    if (!sessions.isActive())
        return 3;
    if (sessions.owner() != m17)
        return 4;
    if (m17->log.starts != 1)
        return 5;

    // While active, new detections must not preempt the owner.
    if (sessions.evaluateDetections(reports, nullptr) != nullptr)
        return 6;
    if (m17->log.starts != 1)
        return 7;

    // Normal finish -> return-to-idle -> idle.
    sessions.finishSession();
    if (m17->log.stops != 1)
        return 8;
    if (sessions.state() != SessionManager::State::ReturnToIdle)
        return 9;

    sessions.timeoutCheck();  // ReturnToIdle -> Idle with zero grace
    if (sessions.state() != SessionManager::State::Idle)
        return 10;

    return 0;
}

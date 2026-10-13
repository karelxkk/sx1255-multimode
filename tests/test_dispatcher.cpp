#include "sx1255/core/IQDispatcher.hpp"
#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"

#include "mock_plugin.hpp"

#include <chrono>
#include <memory>

using namespace sx1255;
using namespace std::chrono_literals;

int main()
{
    int fmDetections = 0;

    auto m17 = std::make_shared<test::MockPlugin>(
        "m17", Mode::M17, 30, [](const IQBlock&) { return Detection{true, 0.8F}; });
    auto fm = std::make_shared<test::MockPlugin>(
        "fm", Mode::FM, 10, [&](const IQBlock&) {
            ++fmDetections;
            return Detection{false, 0.0F};
        });

    PluginRegistry registry;
    registry.registerPlugin(m17);
    registry.registerPlugin(fm);

    SessionManager sessions(SessionManager::Settings{120000ms, 0ms});
    IQDispatcher dispatcher(registry, sessions, nullptr);

    const ComplexFloat sample{0.1F, -0.2F};
    const IQBlock block{0, &sample, 1, 125000.0};

    // First block: monitoring, M17 claims the session.
    dispatcher.dispatch(block);
    if (sessions.state() != SessionManager::State::ActiveM17)
        return 1;
    if (m17->log.starts != 1)
        return 2;
    if (fmDetections != 1)
        return 3;

    // Second block: exclusive phase, only M17 processes, no more detection.
    dispatcher.dispatch(block);
    if (m17->log.processes != 1)
        return 4;
    if (fmDetections != 1)
        return 5;

    // Third block: M17 reports end of session.
    m17->processResult = false;
    dispatcher.dispatch(block);
    if (m17->log.stops != 1)
        return 6;
    if (sessions.state() != SessionManager::State::ReturnToIdle)
        return 7;

    return 0;
}

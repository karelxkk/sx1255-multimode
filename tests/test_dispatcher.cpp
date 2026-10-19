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

    // Second block: exclusive phase.  M17 processes and the other plugins keep
    // detecting: that keeps their receive path alive and lets a mode with a
    // higher priority take the channel over from a wrong owner.
    dispatcher.dispatch(block);
    if (m17->log.processes != 1)
        return 4;
    if (fmDetections != 2)
        return 5;

    // A higher priority detection takes the session away from the owner.
    {
        auto low = std::make_shared<test::MockPlugin>(
            "low", Mode::FM, 10, [](const IQBlock&) { return Detection{true, 0.9F}; });
        bool highOnAir = false;
        auto high = std::make_shared<test::MockPlugin>(
            "high", Mode::M17, 30,
            [&highOnAir](const IQBlock&) { return Detection{highOnAir, 0.9F}; });
        PluginRegistry reg2;
        reg2.registerPlugin(low);
        reg2.registerPlugin(high);
        SessionManager s2(SessionManager::Settings{120000ms, 0ms});
        s2.start();
        IQDispatcher d2(reg2, s2, nullptr);
        d2.dispatch(block);                       // only FM is on the air
        if (s2.state() != SessionManager::State::ActiveFm)
            return 8;
        highOnAir = true;
        // A valid M17 LSF is CRC-checked and preempts before the owner gets
        // another block, so no M17 audio is forwarded as FM.
        d2.dispatch(block);
        if (s2.state() != SessionManager::State::ReturnToIdle)
            return 9;
        d2.dispatch(block);                       // next block grants it to high
        if (s2.state() != SessionManager::State::ActiveM17 || s2.owner() != high)
            return 10;
    }

    // Third block: M17 reports end of session.
    m17->processResult = false;
    dispatcher.dispatch(block);
    if (m17->log.stops != 1)
        return 6;
    if (sessions.state() != SessionManager::State::ReturnToIdle)
        return 7;

    return 0;
}

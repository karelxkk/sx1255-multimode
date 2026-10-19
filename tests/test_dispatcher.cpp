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
    registry.registerPlugin(fm);
    registry.registerPlugin(m17);

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
    // FM must not forward the block that already contained a valid M17 LSF
    // into the SVXLink bridge, even though FM is registered first.
    if (fm->log.detectsWhileBlocked != 1)
        return 23;

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

    // ACTIVE_SVXLINK has no plugin owner.  Keep M17 detection alive so a
    // local M17 call can be granted after the network PTT ends, but never feed
    // the local RX block to the FM/SVXLink bridge in the meantime.
    {
        bool m17OnAir = true;
        auto netFm = std::make_shared<test::MockPlugin>(
            "net-fm", Mode::FM, 10,
            [](const IQBlock&) { return Detection{false, 0.0F}; });
        auto netM17 = std::make_shared<test::MockPlugin>(
            "net-m17", Mode::M17, 30,
            [&m17OnAir](const IQBlock&) {
                return Detection{m17OnAir, m17OnAir ? 1.0F : 0.0F};
            });
        PluginRegistry netRegistry;
        netRegistry.registerPlugin(netFm);
        netRegistry.registerPlugin(netM17);
        SessionManager netSessions(SessionManager::Settings{120000ms, 0ms});
        netSessions.start();
        if (!netSessions.acquireTx(Mode::FM))
            return 24;
        IQDispatcher netDispatcher(netRegistry, netSessions, nullptr);

        netDispatcher.dispatch(block);
        if (netSessions.state() != SessionManager::State::ActiveSvxlink)
            return 25;
        if (netFm->log.detectsWhileBlocked != 1)
            return 26;
        if (netM17->log.detects != 1 || netM17->log.starts != 0)
            return 27;

        netSessions.releaseTx();
        m17OnAir = false;
        netDispatcher.dispatch(block);
        if (netSessions.state() != SessionManager::State::Detecting)
            return 28;

        m17OnAir = true;
        netDispatcher.dispatch(block);
        if (netSessions.state() != SessionManager::State::ActiveM17 ||
            netSessions.owner() != netM17)
            return 29;
        if (netM17->log.starts != 1)
            return 30;
    }

    return 0;
}

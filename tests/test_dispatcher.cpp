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

    // ACTIVE_SVXLINK has no plugin owner.  Local FM RX must still be fed to
    // SVXLink while the network owns TX; only a digital detection suppresses
    // that feed.  M17 detection stays alive for a future local session.
    {
        bool m17OnAir = false;
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
        if (netFm->log.detectsWhileBlocked != 0 ||
            netFm->rxBlockReason != "svxlink_tx_active")
            return 26;
        if (netM17->log.detects != 1 || netM17->log.starts != 0)
            return 27;

        m17OnAir = true;
        netDispatcher.dispatch(block);
        if (netFm->log.detectsWhileBlocked != 1 ||
            netFm->rxBlockReason != "session_busy")
            return 31;

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

    // During M17 network TX, local FM RX is blocked for the different reason
    // "tx_locked"; the M17 detector itself remains active.
    {
        auto netFm = std::make_shared<test::MockPlugin>(
            "net-fm-2", Mode::FM, 10,
            [](const IQBlock&) { return Detection{false, 0.0F}; });
        auto netM17 = std::make_shared<test::MockPlugin>(
            "net-m17-2", Mode::M17, 30,
            [](const IQBlock&) { return Detection{false, 0.0F}; });
        PluginRegistry netRegistry;
        netRegistry.registerPlugin(netFm);
        netRegistry.registerPlugin(netM17);
        SessionManager netSessions(SessionManager::Settings{120000ms, 0ms});
        netSessions.start();
        netSessions.setPreferredNetworkMode(Mode::M17);
        if (!netSessions.acquireTx(Mode::M17))
            return 32;
        IQDispatcher netDispatcher(netRegistry, netSessions, nullptr);
        netDispatcher.dispatch(block);
        if (netSessions.state() != SessionManager::State::ActiveM17Network ||
            netFm->log.detectsWhileBlocked != 1 ||
            netFm->rxBlockReason != "tx_locked" ||
            netM17->log.detects != 1) {
            return 33;
        }
        netSessions.releaseTx();
    }

    // Idle power save: below the sleep threshold no plugin detector runs.
    // The first block above the wake threshold resumes detection immediately.
    {
        auto quietM17 = std::make_shared<test::MockPlugin>(
            "quiet-m17", Mode::M17, 30,
            [](const IQBlock&) { return Detection{true, 1.0F}; });
        PluginRegistry quietRegistry;
        quietRegistry.registerPlugin(quietM17);
        SessionManager quietSessions(SessionManager::Settings{120000ms, 0ms});
        quietSessions.start();
        IQDispatcher quietDispatcher(quietRegistry, quietSessions, nullptr);
        quietDispatcher.setIdlePowerSave(true, -57.0, -55.0);

        const ComplexFloat lowSample{0.0005F, 0.0F};
        const IQBlock lowBlock{0, &lowSample, 1, 125000.0};
        quietDispatcher.dispatch(lowBlock);
        if (!quietDispatcher.idlePowerSaveActive() ||
            quietM17->log.detects != 0) {
            return 34;
        }

        const ComplexFloat hysteresisSample{0.0013F, 0.0F};
        const IQBlock hysteresisBlock{1, &hysteresisSample, 1, 125000.0};
        quietDispatcher.dispatch(hysteresisBlock);
        if (!quietDispatcher.idlePowerSaveActive() ||
            quietM17->log.detects != 0) {
            return 35;
        }

        const ComplexFloat loudSample{0.0025F, 0.0F};
        const IQBlock loudBlock{2, &loudSample, 1, 125000.0};
        quietDispatcher.dispatch(loudBlock);
        if (quietDispatcher.idlePowerSaveActive() ||
            quietM17->log.detects != 1 ||
            quietSessions.state() != SessionManager::State::ActiveM17) {
            return 36;
        }
    }

    return 0;
}

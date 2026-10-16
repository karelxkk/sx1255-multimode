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

    // A pinned network mode lets a gateway transmit without a local session
    // of that mode (D-Star gateway use case).
    sessions.setPreferredNetworkMode(Mode::DStar);
    if (sessions.networkMode() != Mode::DStar)
        return 11;
    if (!sessions.acquireTx(Mode::DStar))
        return 12;
    if (sessions.state() != SessionManager::State::ActiveM17Network)
        return 13;
    sessions.releaseTx();
    if (sessions.state() != SessionManager::State::Idle)
        return 14;

    // A data packet must reach the radio even when the network audio source is
    // another mode: an APRS message cannot be lost just because the last local
    // session was FM (network.mode = auto after a restart).
    sessions.setPreferredNetworkMode(Mode::Unknown);
    auto fm2 = std::make_shared<test::MockPlugin>(
        "fm2", Mode::FM, 10, [](const IQBlock&) { return Detection{true, 0.9F}; });
    std::vector<DetectionReport> fmReports = {{fm2, fm2->detect(IQBlock{})}};
    if (sessions.evaluateDetections(fmReports, nullptr) != fm2.get())
        return 15;
    if (sessions.networkMode() != Mode::FM)
        return 16;
    sessions.finishSession();
    sessions.timeoutCheck();

    if (sessions.acquireTx(Mode::M17))
        return 17;   // network audio of another mode stays blocked
    if (!sessions.acquireTxPacket(Mode::M17))
        return 18;   // but packets are data and must go through
    sessions.releaseTx();

    return 0;
}

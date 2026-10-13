#include "sx1255/config/Config.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include "mock_transmit.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace sx1255;

int main()
{
    FmConfig config;
    config.enabled = true;
    config.ctcssRx = 88.5;
    config.ctcssTx = 88.5;
    config.hangTimeMs = 120;

    auto plugin = createFmPlugin(config);

    test::MockTransmit tx;
    SessionContext ctx;
    ctx.tx = &tx;
    plugin->startSession(ctx);

    if (!tx.ptt)
        return 1;
    if (tx.pttCalls.size() != 1 || !tx.pttCalls[0])
        return 2;

    std::vector<ComplexFloat> silence(2048, ComplexFloat{0.0F, 0.0F});
    const IQBlock block{0, silence.data(), silence.size(), 125000.0};

    // While inside the hang window the session must stay keyed.
    bool released = false;
    for (int i = 0; i < 60; ++i) {
        if (!plugin->process(block)) {
            released = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!released)
        return 3;

    plugin->stopSession();
    if (tx.ptt)
        return 4;
    if (tx.pttCalls.size() != 2 || tx.pttCalls[1])
        return 5;

    return 0;
}

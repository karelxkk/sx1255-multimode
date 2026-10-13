#include "sx1255/config/Config.hpp"
#include "sx1255/dsp/Goertzel.hpp"
#include "sx1255/fm/FMDsp.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include "mock_transmit.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace sx1255;

namespace {

constexpr double kSampleRate = 125000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<ComplexFloat> voicePlusCtcss(std::size_t count)
{
    std::vector<ComplexFloat> samples(count);
    double phase = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        const double freq = 1200.0 * std::sin(2.0 * kPi * 440.0 * t) +
                             500.0 * std::sin(2.0 * kPi * 88.5 * t);
        phase += 2.0 * kPi * freq / kSampleRate;
        samples[n] = {static_cast<float>(std::cos(phase)),
                      static_cast<float>(std::sin(phase))};
    }
    return samples;
}

}  // namespace

int main()
{
    FmConfig config;
    config.enabled = true;
    config.ctcssRx = 88.5;
    config.ctcssTx = 88.5;
    config.hangTimeMs = 100;

    const auto rxIq = voicePlusCtcss(32768);

    auto plugin = createFmPlugin(config);

    // Detect the FM+CTCSS signal.
    bool detected = false;
    for (std::size_t off = 0; off < rxIq.size(); off += 2048) {
        const IQBlock block{0, rxIq.data() + off,
                            std::min<std::size_t>(2048, rxIq.size() - off),
                            kSampleRate};
        plugin->feedRx(block);
        if (plugin->detect(block).detected)
            detected = true;
    }
    if (!detected)
        return 1;

    test::MockTransmit tx;
    SessionContext ctx;
    ctx.tx = &tx;
    plugin->startSession(ctx);

    if (!tx.ptt)
        return 2;

    // Bridge the whole signal through the plugin into TX.
    for (std::size_t off = 0; off < rxIq.size(); off += 2048) {
        const IQBlock block{0, rxIq.data() + off,
                            std::min<std::size_t>(2048, rxIq.size() - off),
                            kSampleRate};
        plugin->process(block);
    }

    if (tx.iq.size() < 8192)
        return 3;

    // Demodulate the captured TX IQ and verify CTCSS + voice survived.
    fm::FmDiscriminator disc;
    std::vector<float> audio(tx.iq.size());
    for (std::size_t n = 0; n < tx.iq.size(); ++n)
        audio[n] = disc.process(tx.iq[n]);

    double energy = 0.0;
    for (const float v : audio)
        energy += static_cast<double>(v) * v;

    const double ctcssPower = goertzelPower(audio.data(), audio.size(), 88.5, kSampleRate);
    const double voicePower = goertzelPower(audio.data(), audio.size(), 440.0, kSampleRate);

    if (ctcssPower / (energy > 1e-12 ? energy : 1.0) < 100.0)
        return 4;
    if (voicePower / (energy > 1e-12 ? energy : 1.0) < 1.0)
        return 5;

    plugin->stopSession();
    if (tx.ptt)
        return 6;

    return 0;
}

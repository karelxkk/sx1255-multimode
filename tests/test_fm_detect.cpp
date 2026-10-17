#include "sx1255/config/Config.hpp"
#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace sx1255;

namespace {

constexpr double kSampleRate = 125000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<ComplexFloat> fmModulated(double toneHz, double deviationHz,
                                      std::size_t count)
{
    std::vector<ComplexFloat> samples(count);
    double phase = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        const double freq = deviationHz * std::sin(2.0 * kPi * toneHz * t);
        phase += 2.0 * kPi * freq / kSampleRate;
        samples[n] = {static_cast<float>(std::cos(phase)),
                      static_cast<float>(std::sin(phase))};
    }
    return samples;
}

}  // namespace

int main()
{
    FmConfig fmConfig;
    fmConfig.enabled = true;
    fmConfig.ctcssTx = 88.5;
    fmConfig.hangTimeMs = 100;

    // Any carrier above the RSSI threshold opens FM, even without modulation.
    {
        auto fm = createFmPlugin(fmConfig);
        auto iq = fmModulated(88.5, 500.0, 131072);
        bool detected = false;
        for (std::size_t off = 0; off < iq.size(); off += 2048) {
            const IQBlock block{0, iq.data() + off,
                                std::min<std::size_t>(2048, iq.size() - off),
                                kSampleRate};
            if (fm->detect(block).detected)
                detected = true;
        }
        if (!detected)
            return 1;
    }

    // Silence must not trigger FM.
    {
        auto fm = createFmPlugin(fmConfig);
        std::vector<ComplexFloat> iq(8192, ComplexFloat{0.0F, 0.0F});
        for (std::size_t off = 0; off < iq.size(); off += 2048) {
            const IQBlock block{0, iq.data() + off,
                                std::min<std::size_t>(2048, iq.size() - off),
                                kSampleRate};
            if (fm->detect(block).detected)
                return 2;
        }
    }

    // An off-frequency carrier is still a carrier: RSSI must open the squelch.
    {
        auto fm = createFmPlugin(fmConfig);
        std::vector<ComplexFloat> iq(131072);
        double phase = 0.0;
        for (std::size_t n = 0; n < iq.size(); ++n) {
            phase += 2.0 * kPi * 700.0 / kSampleRate;
            iq[n] = {static_cast<float>(std::cos(phase)),
                     static_cast<float>(std::sin(phase))};
        }
        bool detected = false;
        for (std::size_t off = 0; off < iq.size(); off += 2048) {
            const IQBlock block{0, iq.data() + off,
                                std::min<std::size_t>(2048, iq.size() - off),
                                kSampleRate};
            if (fm->detect(block).detected)
                detected = true;
        }
        if (!detected)
            return 3;
    }

    // A continuous carrier is one detection, not one per IQ block.
    {
        SystemStatistics stats;
        auto fm = createFmPlugin(fmConfig, &stats);
        auto iq = fmModulated(88.5, 500.0, 131072);
        for (std::size_t off = 0; off < iq.size(); off += 2048) {
            const IQBlock block{0, iq.data() + off,
                                std::min<std::size_t>(2048, iq.size() - off),
                                kSampleRate};
            fm->detect(block);
        }
        if (stats.fmDetected() != 1)
            return 6;
    }

    // An active FM session must release after the hang timer once the carrier
    // disappears.
    {
        auto fm = createFmPlugin(fmConfig);
        SessionContext ctx;
        fm->startSession(ctx);

        std::vector<ComplexFloat> silence(2048, ComplexFloat{0.0F, 0.0F});
        const IQBlock block{0, silence.data(), silence.size(), kSampleRate};

        // Feed a few blocks of silence; the hang timer should expire well
        // before the loop finishes.
        bool released = false;
        for (int i = 0; i < 40; ++i) {
            if (!fm->process(block)) {
                released = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!released)
            return 4;
    }

    return 0;
}

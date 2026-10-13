#include "sx1255/dsp/Goertzel.hpp"
#include "sx1255/fm/FMDsp.hpp"

#include <vector>

using namespace sx1255;

int main()
{
    constexpr double kSampleRate = 125000.0;
    constexpr double kToneHz = 88.5;
    constexpr std::size_t kCount = 16384;

    fm::CtcssGenerator gen(kToneHz, fm::kCtcssDevHz, kSampleRate);

    std::vector<float> samples(kCount);
    double energy = 0.0;
    for (std::size_t n = 0; n < kCount; ++n) {
        samples[n] = gen.process();
        energy += static_cast<double>(samples[n]) * samples[n];
    }

    const double tonePower = goertzelPower(samples.data(), kCount, kToneHz, kSampleRate);
    const double presence = energy > 1e-12 ? tonePower / energy : 0.0;

    // A pure generated CTCSS tone must dominate its own Goertzel bin.
    if (presence < 1000.0)
        return 1;

    // And it must be absent at a far-away bin.
    const double away = goertzelPower(samples.data(), kCount, 1000.0, kSampleRate);
    if (away > 0.01 * tonePower)
        return 2;

    return 0;
}

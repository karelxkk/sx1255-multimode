#include "sx1255/dsp/Goertzel.hpp"
#include "sx1255/fm/FMDsp.hpp"

#include <vector>

using namespace sx1255;

int main()
{
    constexpr double kSampleRate = 125000.0;
    constexpr double kToneHz = 88.5;
    constexpr std::size_t kCount = 16384;

    // TX: CTCSS tone -> FM modulator -> IQ.
    fm::CtcssGenerator gen(kToneHz, fm::kCtcssDevHz, kSampleRate);
    fm::FmModulator mod(1.0);
    std::vector<ComplexFloat> iq(kCount);
    for (std::size_t n = 0; n < kCount; ++n)
        iq[n] = mod.process(gen.process());

    // RX: IQ -> FM discriminator -> audio.
    fm::FmDiscriminator disc;
    std::vector<float> audio(kCount);
    for (std::size_t n = 0; n < kCount; ++n)
        audio[n] = disc.process(iq[n]);

    double energy = 0.0;
    for (const float v : audio)
        energy += static_cast<double>(v) * v;

    const double tonePower = goertzelPower(audio.data(), kCount, kToneHz, kSampleRate);
    const double presence = energy > 1e-12 ? tonePower / energy : 0.0;

    if (presence < 1000.0)
        return 1;

    return 0;
}

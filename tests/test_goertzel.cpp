#include "sx1255/dsp/Goertzel.hpp"

#include <cmath>
#include <vector>

int main()
{
    constexpr double kSampleRate = 125000.0;
    constexpr std::size_t kN = 4096;
    constexpr double kTone = 88.5;

    auto sine = [](double hz) {
        std::vector<float> samples(kN);
        constexpr double kPi = 3.14159265358979323846;
        for (std::size_t n = 0; n < kN; ++n)
            samples[n] = static_cast<float>(std::sin(2.0 * kPi * hz * n / kSampleRate));
        return samples;
    };

    const auto tone = sine(kTone);
    const auto other = sine(1000.0);

    const double pTone = sx1255::goertzelPower(tone.data(), kN, kTone, kSampleRate);
    const double pOther = sx1255::goertzelPower(other.data(), kN, kTone, kSampleRate);

    // The 88.5 Hz bin must strongly dominate when an 88.5 Hz tone is present.
    if (pTone <= 100.0 * pOther)
        return 1;

    return 0;
}

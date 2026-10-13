#include "sx1255/dsp/Goertzel.hpp"
#include "sx1255/fm/Resampler.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace sx1255;

int main()
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kInputRate = 125000.0;
    constexpr double kOutputRate = 16000.0;
    constexpr std::size_t kInputCount = 125000;  // 1 second

    // 1 kHz tone at 125 kSa/s.
    std::vector<float> input(kInputCount);
    for (std::size_t n = 0; n < kInputCount; ++n)
        input[n] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * n / kInputRate));

    fm::LinearResampler resampler(kOutputRate, kInputRate);
    std::vector<float> output;
    output.reserve(16000);
    resampler.process(input.data(), input.size(), output);

    if (output.size() < 15000 || output.size() > 17000)
        return 1;

    // The resampled signal must still contain the 1 kHz tone.
    const double tonePower = goertzelPower(output.data(), output.size(), 1000.0, kOutputRate);
    double energy = 0.0;
    for (const float v : output)
        energy += static_cast<double>(v) * v;
    if (tonePower / energy < 10.0)
        return 2;

    // Upsample back 16k -> 125k and confirm the sample count is sane.
    fm::LinearResampler up(kInputRate, kOutputRate);
    std::vector<float> roundtrip;
    roundtrip.reserve(125000);
    up.process(output.data(), output.size(), roundtrip);
    if (roundtrip.size() < 120000 || roundtrip.size() > 130000)
        return 3;

    // Streaming (one sample at a time), as used by the SVXLink bridge, must
    // produce the same sample count and preserve the tone.
    {
        fm::LinearResampler stream(kOutputRate, kInputRate);
        std::vector<float> out;
        out.reserve(16000);
        for (const float v : input)
            stream.process(&v, 1, out);
        if (out.size() < 15000 || out.size() > 17000)
            return 4;
        const double tone = goertzelPower(out.data(), out.size(), 1000.0, kOutputRate);
        double energy = 0.0;
        for (const float v : out)
            energy += static_cast<double>(v) * v;
        if (tone / energy < 10.0)
            return 5;
    }

    return 0;
}

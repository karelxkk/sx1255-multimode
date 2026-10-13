#include "sx1255/spectrum/FFT.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace sx1255;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<ComplexFloat> tone(std::size_t n, std::size_t bin, double sampleRateHz)
{
    const double freq = spectrum::binFrequencyHz(bin, sampleRateHz, n);
    std::vector<ComplexFloat> samples(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double phase = 2.0 * kPi * freq * static_cast<double>(i) / sampleRateHz;
        samples[i] = {static_cast<float>(std::cos(phase)),
                      static_cast<float>(std::sin(phase))};
    }
    return samples;
}

}  // namespace

int main()
{
    constexpr std::size_t kN = 1024;
    constexpr double kRate = 125000.0;
    constexpr std::size_t kBin = 64;

    const auto iq = tone(kN, kBin, kRate);
    const std::vector<float> psd = spectrum::powerSpectrum(iq.data(), kN, true);

    if (psd.size() != kN / 2 + 1)
        return 1;

    // Peak must sit exactly at the expected bin.
    const auto peak = std::max_element(psd.begin(), psd.end());
    const std::size_t peakBin = static_cast<std::size_t>(peak - psd.begin());
    if (peakBin != kBin)
        return 2;

    // The tone must dominate the rest of the spectrum.
    double sum = 0.0;
    for (const float v : psd)
        sum += static_cast<double>(v);
    if (*peak <= 10.0 * (sum / static_cast<double>(psd.size())))
        return 3;

    // DC is silence -> flat, low noise floor.
    std::vector<ComplexFloat> zeros(kN, ComplexFloat{0.0F, 0.0F});
    const std::vector<float> quiet = spectrum::powerSpectrum(zeros.data(), kN, false);
    for (const float v : quiet) {
        if (v > 1e-6F)
            return 4;
    }

    // powerToDb must be monotonic and finite.
    if (spectrum::powerToDb(1.0F) != 0.0F)
        return 5;
    if (!std::isfinite(spectrum::powerToDb(0.0F)))
        return 6;

    return 0;
}

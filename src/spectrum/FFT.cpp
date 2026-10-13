#include "sx1255/spectrum/FFT.hpp"

#include <algorithm>
#include <cmath>

namespace sx1255::spectrum {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::size_t bitReverse(std::size_t value, std::size_t bits)
{
    std::size_t result = 0;
    for (std::size_t i = 0; i < bits; ++i) {
        result = (result << 1) | (value & 1);
        value >>= 1;
    }
    return result;
}

}  // namespace

void fftInPlace(std::vector<std::complex<float>>& data)
{
    const std::size_t n = data.size();
    if (n < 2)
        return;

    std::size_t bits = 0;
    while ((std::size_t{1} << bits) < n)
        ++bits;
    if ((std::size_t{1} << bits) != n)
        return;  // not a power of two: leave untouched

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = bitReverse(i, bits);
        if (j > i)
            std::swap(data[i], data[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float angle = static_cast<float>(-2.0 * kPi / static_cast<double>(len));
        const std::complex<float> wlen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0F, 0.0F);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> even = data[i + j];
                const std::complex<float> odd  = data[i + j + len / 2] * w;
                data[i + j]            = even + odd;
                data[i + j + len / 2]  = even - odd;
                w *= wlen;
            }
        }
    }
}

void applyHann(std::vector<float>& samples)
{
    const std::size_t n = samples.size();
    if (n < 2)
        return;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) /
                                               static_cast<double>(n - 1)));
        samples[i] = static_cast<float>(samples[i] * w);
    }
}

std::vector<float> powerSpectrum(const ComplexFloat* iq, std::size_t n, bool windowed)
{
    std::vector<std::complex<float>> data(n);
    for (std::size_t i = 0; i < n; ++i)
        data[i] = {iq[i].i, iq[i].q};

    if (windowed) {
        std::vector<float> re(n);
        std::vector<float> im(n);
        for (std::size_t i = 0; i < n; ++i) {
            re[i] = data[i].real();
            im[i] = data[i].imag();
        }
        applyHann(re);
        applyHann(im);
        for (std::size_t i = 0; i < n; ++i)
            data[i] = {re[i], im[i]};
    }

    fftInPlace(data);

    const std::size_t bins = n / 2 + 1;
    std::vector<float> psd(bins);
    const double norm = 1.0 / static_cast<double>(n * n);
    for (std::size_t i = 0; i < bins; ++i) {
        const float re = data[i].real();
        const float im = data[i].imag();
        psd[i] = static_cast<float>((re * re + im * im) * norm);
    }
    return psd;
}

double binFrequencyHz(std::size_t bin, double sampleRateHz, std::size_t fftSize)
{
    if (fftSize == 0)
        return 0.0;
    return static_cast<double>(bin) * sampleRateHz / static_cast<double>(fftSize);
}

float powerToDb(float linearPower)
{
    constexpr float kMinPower = 1e-12F;
    return 10.0F * std::log10(std::max(linearPower, kMinPower));
}

}  // namespace sx1255::spectrum

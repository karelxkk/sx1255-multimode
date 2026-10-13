#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/spectrum/FFT.hpp"
#include "sx1255/spectrum/WaterfallGenerator.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

double seconds(std::chrono::steady_clock::time_point a,
               std::chrono::steady_clock::time_point b)
{
    return std::chrono::duration<double>(b - a).count();
}

}  // namespace

int main()
{
    using Clock = std::chrono::steady_clock;

    // FFT 4096
    {
        constexpr std::size_t kSize = 4096;
        std::vector<sx1255::ComplexFloat> iq(kSize);
        for (std::size_t i = 0; i < kSize; ++i)
            iq[i] = {static_cast<float>(std::sin(0.01 * static_cast<double>(i))),
                     static_cast<float>(std::cos(0.01 * static_cast<double>(i)))};

        const int iterations = 2000;
        const auto start = Clock::now();
        for (int i = 0; i < iterations; ++i)
            sx1255::spectrum::powerSpectrum(iq.data(), kSize, true);
        const double dt = seconds(start, Clock::now());
        std::printf("fft_4096: %.1f ms/op (%.0f ops/s)\n",
                    dt * 1000.0 / iterations, iterations / dt);
    }

    // Waterfall
    {
        constexpr std::size_t kBins = 2049;
        sx1255::spectrum::WaterfallGenerator waterfall(kBins, 500);
        std::vector<float> row(kBins, -90.0F);
        const int iterations = 100000;
        const auto start = Clock::now();
        for (int i = 0; i < iterations; ++i)
            waterfall.addRow(row);
        const double dt = seconds(start, Clock::now());
        std::printf("waterfall_add: %.3f us/row (%.0f rows/s)\n",
                    dt * 1e6 / iterations, iterations / dt);
    }

    // M17 RX chain (125 kSa/s -> matched-filter samples)
    {
        constexpr std::size_t kSamples = 250000;  // 2 s
        std::vector<sx1255::m17::CD> iq(kSamples);
        for (std::size_t i = 0; i < kSamples; ++i)
            iq[i] = {std::sin(0.02 * static_cast<double>(i)),
                     std::cos(0.02 * static_cast<double>(i))};

        const auto start = Clock::now();
        const std::vector<float> out = sx1255::m17::runRXChain(iq);
        const double dt = seconds(start, Clock::now());
        const double realtime = kSamples / 125000.0;
        std::printf("m17_rx_chain: %.1f ms for %.1f s of IQ (%.2fx realtime)\n",
                    dt * 1000.0, realtime, realtime / dt);
    }

    return 0;
}

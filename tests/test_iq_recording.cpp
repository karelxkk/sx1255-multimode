#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/debug/IQRecorder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

using namespace sx1255;

namespace {

// Deterministic ISX1255 source producing a known ramp + tone pattern.
class FakeSource final : public ISX1255 {
public:
    std::string name() const override { return "fake-source"; }
    bool open() override { m_open = true; return true; }
    void close() override { m_open = false; }
    bool setRxFrequency(Hertz) override { return true; }
    bool setTxFrequency(Hertz) override { return true; }
    bool setPower(dBm) override { return true; }
    bool setPtt(bool) override { return true; }
    double readRssi() override { return -80.0; }
    bool setSquelch(dBm) override { return true; }

    bool readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples) override
    {
        if (!m_open)
            return false;
        const std::size_t before = out.size();
        out.resize(before + maxSamples);
        for (std::size_t n = 0; n < maxSamples; ++n) {
            const double idx = static_cast<double>(m_index++);
            out[before + n] = {
                static_cast<float>(std::sin(idx * 0.031)),
                static_cast<float>(std::cos(idx * 0.017)),
            };
        }
        return true;
    }

    bool writeIQ(const ComplexFloat*, std::size_t) override { return true; }

private:
    bool m_open = false;
    std::uint64_t m_index = 0;
};

std::string tempFile(const char* tag)
{
    return "/tmp/sx1255_" + std::string(tag) + "_" + std::to_string(::getpid()) + ".iq";
}

std::vector<ComplexFloat> expectedSamples(std::size_t count)
{
    std::vector<ComplexFloat> samples(count);
    for (std::size_t n = 0; n < count; ++n) {
        samples[n] = {
            static_cast<float>(std::sin(static_cast<double>(n) * 0.031)),
            static_cast<float>(std::cos(static_cast<double>(n) * 0.017)),
        };
    }
    return samples;
}

}  // namespace

int main()
{
    constexpr std::size_t kTotal = 4096;
    constexpr std::size_t kChunk = 1000;
    const std::string path = tempFile("rec");

    const auto expected = expectedSamples(kTotal);

    {
        auto recorder = std::make_unique<debug::IQRecorder>(
            std::make_unique<FakeSource>(), path);

        if (!recorder->open())
            return 1;

        recorder->setRxFrequency(430975000);

        std::size_t read = 0;
        while (read < kTotal) {
            std::vector<ComplexFloat> block;
            if (!recorder->readIQ(block, std::min(kChunk, kTotal - read)))
                return 2;
            if (block.size() != std::min(kChunk, kTotal - read))
                return 3;

            for (std::size_t n = 0; n < block.size(); ++n) {
                if (block[n].i != expected[read + n].i || block[n].q != expected[read + n].q)
                    return 4;
            }
            read += block.size();
        }

        if (recorder->capturedSamples() != kTotal)
            return 5;
        if (!recorder->isRecording())
            return 6;

        recorder->close();
    }

    // Verify the on-disk container: header + exact sample round-trip.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return 7;

    debug::IQFileHeader header;
    if (!debug::readIQHeader(in, header))
        return 8;
    if (!debug::validateIQHeader(header))
        return 9;
    if (header.sampleCount != kTotal)
        return 10;
    if (header.sampleRateHz != 125000)
        return 11;
    if (header.frequencyHz != 430975000)
        return 15;

    std::vector<ComplexFloat> replayed(kTotal);
    if (debug::readIQSamples(in, replayed.data(), kTotal) != kTotal)
        return 12;
    for (std::size_t n = 0; n < kTotal; ++n) {
        if (replayed[n].i != expected[n].i || replayed[n].q != expected[n].q)
            return 13;
    }

    std::remove(path.c_str());
    return 0;
}

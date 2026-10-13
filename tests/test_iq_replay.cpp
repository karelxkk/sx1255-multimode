#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/debug/IQReplay.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

using namespace sx1255;

namespace {

std::string tempFile()
{
    return "/tmp/sx1255_replay_" + std::to_string(::getpid()) + ".iq";
}

std::vector<ComplexFloat> makeSamples(std::size_t count)
{
    std::vector<ComplexFloat> samples(count);
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n);
        samples[n] = {
            static_cast<float>(std::sin(t * 0.05)),
            static_cast<float>(std::cos(t * 0.05)),
        };
    }
    return samples;
}

}  // namespace

int main()
{
    constexpr std::size_t kTotal = 3000;
    const std::string path = tempFile();
    const auto source = makeSamples(kTotal);

    {
        debug::IQFileHeader header;
        header.sampleRateHz = 125000;
        header.frequencyHz  = 430975000;
        header.sampleCount  = kTotal;

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return 1;
        if (!debug::writeIQHeader(out, header))
            return 2;
        if (!debug::writeIQSamples(out, source.data(), source.size()))
            return 3;
    }

    debug::IQReplay replay(path);
    if (!replay.open())
        return 4;
    if (replay.sampleRateHz() != 125000)
        return 5;
    if (replay.frequencyHz() != 430975000)
        return 6;
    if (replay.totalSamples() != kTotal)
        return 7;
    if (replay.eof())
        return 8;

    std::vector<ComplexFloat> got;
    got.reserve(kTotal);
    while (true) {
        std::vector<ComplexFloat> block;
        const bool ok = replay.readIQ(block, 700);
        if (block.empty())
            break;
        got.insert(got.end(), block.begin(), block.end());
        if (!ok)
            break;
    }

    if (got.size() != kTotal)
        return 9;
    for (std::size_t n = 0; n < kTotal; ++n) {
        if (got[n].i != source[n].i || got[n].q != source[n].q)
            return 10;
    }
    if (replay.replayedSamples() != kTotal)
        return 11;
    if (!replay.eof())
        return 12;

    // Reading past EOF must stay at EOF and return false.
    std::vector<ComplexFloat> extra;
    if (replay.readIQ(extra, 16))
        return 13;
    if (!extra.empty())
        return 14;

    replay.close();
    std::remove(path.c_str());
    return 0;
}

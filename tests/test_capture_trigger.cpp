#include "sx1255/debug/AutomaticCapture.hpp"
#include "sx1255/debug/IQFileFormat.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace sx1255;

namespace {

std::string tempDir()
{
    return "/tmp";
}

std::vector<ComplexFloat> ramp(std::size_t start, std::size_t count)
{
    std::vector<ComplexFloat> samples(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float v = static_cast<float>(start + i);
        samples[i] = {v, -v};
    }
    return samples;
}

}  // namespace

int main()
{
    constexpr std::size_t kRate = 1000;       // 1000 samples/sec
    constexpr std::size_t kPre  = 100;        // 0.1 s
    constexpr std::size_t kPost = 100;        // 0.1 s

    debug::AutomaticCapture capture(kRate, 0.1, 0.1, tempDir());
    capture.start();

    const auto pre = ramp(0, kPre);
    capture.push(pre.data(), pre.size());

    capture.trigger(debug::CaptureTrigger::UnknownSignal);

    const auto post = ramp(kPre, kPost);
    capture.push(post.data(), post.size());

    // Wait for the writer thread to finish the dump.
    for (int i = 0; i < 2000 && capture.completedCaptures() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto result = capture.lastResult();
    capture.stop();

    if (result.ok != true)
        return 1;
    if (result.samples != kPre + kPost)
        return 2;
    if (result.trigger != debug::CaptureTrigger::UnknownSignal)
        return 3;
    if (result.file.find("capture_") == std::string::npos)
        return 4;

    std::ifstream in(result.file, std::ios::binary);
    if (!in.is_open())
        return 5;

    debug::IQFileHeader header;
    if (!debug::readIQHeader(in, header))
        return 6;
    if (header.sampleRateHz != kRate)
        return 7;
    if (header.sampleCount != kPre + kPost)
        return 8;

    std::vector<ComplexFloat> got(kPre + kPost);
    if (debug::readIQSamples(in, got.data(), got.size()) != got.size())
        return 9;

    for (std::size_t i = 0; i < kPre; ++i) {
        if (got[i].i != pre[i].i || got[i].q != pre[i].q)
            return 10;
    }
    for (std::size_t i = 0; i < kPost; ++i) {
        if (got[kPre + i].i != post[i].i || got[kPre + i].q != post[i].q)
            return 11;
    }

    std::remove(result.file.c_str());

    if (std::string(debug::toString(debug::CaptureTrigger::M17NearMiss)) !=
        "m17_near_miss") {
        return 12;
    }
    return 0;
}

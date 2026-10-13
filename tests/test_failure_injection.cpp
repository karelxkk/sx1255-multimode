#include "sx1255/core/IQDispatcher.hpp"
#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/debug/AutomaticCapture.hpp"
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/diag/SystemStatistics.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace sx1255;

int main()
{
    // 1) Capture buffer overflow: the rolling ring must wrap without crashing
    //    and still produce a valid dump when triggered.
    {
        constexpr std::size_t kRate = 1000;
        debug::AutomaticCapture capture(kRate, 0.1, 0.1, "/tmp");
        capture.start();

        const std::size_t huge = 100000;  // far beyond the 0.2s ring
        std::vector<ComplexFloat> samples(huge);
        for (std::size_t i = 0; i < huge; ++i)
            samples[i] = {static_cast<float>(i & 0xFF), 0.0F};

        capture.push(samples.data(), samples.size());
        capture.trigger(debug::CaptureTrigger::Health);

        // post-trigger window
        std::vector<ComplexFloat> post(200, ComplexFloat{1.0F, 2.0F});
        capture.push(post.data(), post.size());

        for (int i = 0; i < 2000 && capture.completedCaptures() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        const auto result = capture.lastResult();
        capture.stop();

        if (result.ok != true)
            return 1;
        if (result.samples != 200)  // 0.1s pre + 0.1s post at 1 kHz
            return 2;

        std::ifstream in(result.file, std::ios::binary);
        debug::IQFileHeader header;
        if (!in.is_open() || !debug::readIQHeader(in, header))
            return 3;
        if (header.sampleCount != 200)
            return 4;
        std::remove(result.file.c_str());
    }

    // 2) Invalid / empty IQ block must be safe for the dispatcher.
    {
        SystemStatistics stats;
        PluginRegistry registry;
        SessionManager sessions;
        IQDispatcher dispatcher(registry, sessions, nullptr);

        const IQBlock empty{0, nullptr, 0, 125000.0};
        dispatcher.dispatch(empty);
        if (dispatcher.blocksProcessed() != 1)
            return 5;
    }

    return 0;
}

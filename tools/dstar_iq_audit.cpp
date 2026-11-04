// Offline audit of a D-Star IQ recording through the multimode D-Star
// detector.  Tries both differential polarity and a small carrier-offset
// sweep so a failed real-world acquisition can be attributed to the front end,
// polarity or header decoding rather than guessing.
//
//   dstar_iq_audit capture.iq [start_seconds] [duration_seconds]

#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/dstar/DStarDetector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace sx1255;

namespace {

ComplexFloat rotate(ComplexFloat sample, double phase)
{
    const double c = std::cos(phase);
    const double s = std::sin(phase);
    return {
        static_cast<float>(sample.i * c - sample.q * s),
        static_cast<float>(sample.i * s + sample.q * c)};
}

struct Result {
    double offsetHz = 0.0;
    bool invert = false;
    std::uint64_t frames = 0;
    std::uint64_t bits = 0;
    unsigned int frameDistance = 24;
    unsigned int dataDistance = 24;
    bool inData = false;
    std::string header;
};

std::string callsignField(const dstar::DStarHeader& header,
                          std::size_t offset, std::size_t width)
{
    std::string out;
    for (std::size_t i = 0; i < width; ++i) {
        const char c = static_cast<char>(header.bytes[offset + i]);
        if (c != '\0' && c != ' ')
            out.push_back(c);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s capture.iq [start_seconds] [duration_seconds]\n",
                     argv[0]);
        return 2;
    }

    const double startSeconds = argc > 2 ? std::atof(argv[2]) : 1.2;
    const double durationSeconds = argc > 3 ? std::atof(argv[3]) : 5.0;
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }

    debug::IQFileHeader header;
    if (!debug::readIQHeader(in, header) || !debug::validateIQHeader(header)) {
        std::fprintf(stderr, "not a valid SX1255 IQ recording\n");
        return 1;
    }
    const double sampleRate = static_cast<double>(header.sampleRateHz);
    const std::size_t skip = static_cast<std::size_t>(
        std::max(0.0, startSeconds) * sampleRate);
    const std::size_t wanted = static_cast<std::size_t>(
        std::max(0.1, durationSeconds) * sampleRate);

    std::vector<ComplexFloat> scratch(4096);
    std::size_t skipped = 0;
    while (skipped < skip) {
        const std::size_t want = std::min(scratch.size(), skip - skipped);
        const std::size_t got = debug::readIQSamples(in, scratch.data(), want);
        if (got == 0)
            break;
        skipped += got;
    }

    std::vector<ComplexFloat> base;
    base.reserve(wanted);
    // Live RadioFrontend shifts the +25 kHz low-IF recording to zero IF
    // before dispatching it to plugins.
    const double baseStep =
        2.0 * std::numbers::pi * 25000.0 / sampleRate;
    double basePhase = 0.0;
    while (base.size() < wanted) {
        const std::size_t want = std::min(scratch.size(), wanted - base.size());
        const std::size_t got = debug::readIQSamples(in, scratch.data(), want);
        if (got == 0)
            break;
        for (std::size_t i = 0; i < got; ++i) {
            ComplexFloat sample = rotate(scratch[i], -basePhase);
            basePhase += baseStep;
            base.push_back(sample);
        }
    }

    std::printf("file=%s start=%.2fs duration=%.2fs samples=%zu\n",
                argv[1], startSeconds, durationSeconds, base.size());

    std::vector<Result> results;
    for (int offsetStep = -8; offsetStep <= 8; ++offsetStep) {
        const double offsetHz = offsetStep * 250.0;
        for (int polarity = 0; polarity < 2; ++polarity) {
            const bool invert = polarity != 0;
            dstar::DStarDetector detector;
            const double offsetStepRad =
                2.0 * std::numbers::pi * (-offsetHz) / sampleRate;
            double offsetPhase = 0.0;

            std::vector<ComplexFloat> variant(4096);
            for (std::size_t pos = 0; pos < base.size(); pos += variant.size()) {
                const std::size_t count = std::min(variant.size(), base.size() - pos);
                for (std::size_t i = 0; i < count; ++i) {
                    ComplexFloat sample = rotate(base[pos + i], offsetPhase);
                    offsetPhase += offsetStepRad;
                    if (invert)
                        sample.q = -sample.q;
                    variant[i] = sample;
                }
                const IQBlock block{
                    0, variant.data(), count, header.sampleRateHz};
                (void) detector.detect(block);
            }

            std::string headerText;
            dstar::DStarRxEvent event{};
            dstar::DStarHeader header{};
            dstar::DStarFrame frame{};
            while (detector.nextEvent(event, &header, &frame)) {
                if (event == dstar::DStarRxEvent::Header) {
                    char flags[8];
                    std::snprintf(flags, sizeof(flags), "0x%02x",
                                  static_cast<unsigned int>(header.bytes[0]));
                    headerText =
                        std::string("flags=") + flags +
                        " RPT2=" + callsignField(header, 3, 8) +
                        " RPT1=" + callsignField(header, 11, 8) +
                        " YOUR=" + callsignField(header, 19, 8) +
                        " MY1=" + callsignField(header, 27, 8) +
                        " MY2=" + callsignField(header, 35, 4);
                }
            }

            results.push_back({
                offsetHz,
                invert,
                detector.frameSyncsSeen(),
                detector.bitsSeen(),
                detector.minFrameSyncDistance(),
                detector.minDataSyncDistance(),
                detector.inData(),
                std::move(headerText)});
        }
    }

    std::sort(results.begin(), results.end(),
              [](const Result& a, const Result& b) {
                  if (a.frameDistance != b.frameDistance)
                      return a.frameDistance < b.frameDistance;
                  if (a.frames != b.frames)
                      return a.frames > b.frames;
                  if (a.dataDistance != b.dataDistance)
                      return a.dataDistance < b.dataDistance;
                  return a.offsetHz < b.offsetHz;
              });

    for (const Result& r : results) {
        std::printf("offset=%+7.0f Hz invert=%d frames=%llu bits=%llu "
                    "frame_dist=%u data_dist=%u in_data=%d%s%s\n",
                    r.offsetHz, r.invert ? 1 : 0,
                    static_cast<unsigned long long>(r.frames),
                    static_cast<unsigned long long>(r.bits),
                    r.frameDistance, r.dataDistance, r.inData ? 1 : 0,
                    r.header.empty() ? "" : " header=",
                    r.header.c_str());
    }
    return 0;
}

// Offline analysis of a recorded IQ file through the M17 receive chain.
//
// Prints every detected sync burst (type, distance components, sample index,
// decoder state), every decoded packet frame (frame number, byte count, channel
// bit errors) and every packet outcome.  It uses exactly the same chain as
// M17Plugin::feed(), so what it prints is what the multimode would do.
//
//   m17_rx_dump capture.iq
//
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cstdint>
#include <deque>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

namespace {

std::string hex(const std::vector<std::uint8_t>& data)
{
    std::string out;
    for (const std::uint8_t b : data) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        out += buf;
    }
    return out;
}

std::string text(const std::vector<std::uint8_t>& data)
{
    std::string out;
    for (const std::uint8_t b : data)
        out += (b >= 0x20 && b < 0x7f) ? static_cast<char>(b) : '.';
    return out;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: m17_rx_dump <file.iq> [gain]\n");
        return 2;
    }
    // A recording can be taken at a different scale than the live receive path;
    // the gain lets the offline run match what the plugin saw.
    const double gain = argc > 2 ? std::atof(argv[2]) : 1.0;
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::printf("cannot open %s\n", argv[1]);
        return 1;
    }
    debug::IQFileHeader header;
    if (!debug::readIQHeader(in, header)) {
        std::printf("not an IQ recording: %s\n", argv[1]);
        return 1;
    }
    std::printf("recording: %u Hz, %llu samples, %s\n", header.sampleRateHz,
                static_cast<unsigned long long>(header.sampleCount), argv[1]);

    ComplexDCRemoval dcr(0.9999);
    PolyphaseDecimator decimator(
        designChannelFilter(channelBWSX1255, sampleRateSX1255, channelTapsSX1255), rxDecimSX1255);
    FMDemodAFC afc(basebandDCAvgCntSX1255, rxFastNSX1255, rxSlowNSX1255);
    RationalResampler resampler(24, 25);
    SampleToSymbol s2s(std::vector<double>(rrcTaps5.begin(), rrcTaps5.end()), rxScalingCoeffSX1255);

    Decoder::Callbacks callbacks;
    callbacks.onLSF = [](const LSF& lsf, double ber) {
        std::printf("  LSF ok %s>%s %s ber=%.1f%%\n", lsf.srcCallsign().c_str(),
                    lsf.dstCallsign().c_str(), lsf.lsfType() == 0 ? "packet" : "stream", ber);
    };
    callbacks.onLSFBadCRC = [](const LSF&, double ber) {
        std::printf("  LSF bad crc ber=%.1f%%\n", ber);
    };
    callbacks.onPacket = [](const LSF& lsf, const std::vector<std::uint8_t>& payload, double ber) {
        std::printf("  PACKET %s>%s len=%zu ber=%.1f%% type=0x%02x \"%s\" hex=%s\n",
                    lsf.srcCallsign().c_str(), lsf.dstCallsign().c_str(), payload.size(), ber,
                    payload.empty() ? 0 : payload[0], text(payload).c_str(), hex(payload).c_str());
    };
    callbacks.onPacketBadCRC = [](const LSF&, const std::vector<std::uint8_t>& payload, double ber) {
        std::printf("  packet bad crc len=%zu ber=%.1f%% hex=%s \"%s\"\n", payload.size(), ber,
                    hex(payload).c_str(), text(payload).c_str());
    };
    callbacks.onFrameDropped = [](std::uint16_t typ, const char* reason) {
        std::printf("  frame dropped type=0x%04x %s\n", typ, reason);
    };
    callbacks.onPacketReattached = [](const LSF&) { std::printf("  frame re-attached\n"); };
    callbacks.onPacketFrame = [](std::uint8_t field, bool lastFrame, int errors) {
        std::printf("  frame %s=%u errors=%d\n", lastFrame ? "bytes" : "number", field, errors);
    };
    Decoder decoder(std::move(callbacks));

    SyncThresholds thresholds;   // the configuration defaults
    // The last symbols, so a burst can report the amplitude of the signal it
    // came from: M17 levels are +-3 and +-1, so a clean signal has a mean
    // magnitude near 2.  A low value means the demodulated signal is small
    // (low deviation) and every sync distance is inflated.
    std::deque<float> recent;
    M17Synchronizer* syncPtr = nullptr;
    M17Synchronizer synchronizer(5, [&](std::uint16_t typ, const std::vector<SoftBit>& bits) {
        const SyncMetrics m = syncPtr->lastMetrics();
        // The payload that was just extracted sits right before the samples
        // still buffered, sampled once per symbol.
        double amp = 0.0;
        const std::size_t payloadEnd = static_cast<std::size_t>(syncPtr->samples()) -
                                       syncPtr->bufferedSamples();
        const std::size_t payloadStart = payloadEnd > 184 * 5 ? payloadEnd - 184 * 5 : 0;
        const std::size_t base = syncPtr->samples() >= recent.size()
            ? static_cast<std::size_t>(syncPtr->samples()) - recent.size() : 0;
        std::size_t counted = 0;
        for (std::size_t i = payloadStart; i < payloadEnd; i += 5) {
            if (i < base || i - base >= recent.size())
                continue;
            amp += std::fabs(static_cast<double>(recent[i - base]));
            ++counted;
        }
        if (counted != 0)
            amp /= static_cast<double>(counted);
        std::printf("burst %llu type=0x%04x dist=%.2f only=%.2f pair=%.2f end=%.2f eot=%.2f follow=%d mean|sym|=%.2f decoder=%s\n",
                    static_cast<unsigned long long>(syncPtr->samples()), typ,
                    static_cast<double>(syncPtr->lastDistance()),
                    static_cast<double>(typ == PacketSync ? m.packetOnly
                                                          : (typ == StreamSync ? m.streamOnly : m.lsf)),
                    static_cast<double>(typ == StreamSync ? m.streamPair : m.packetPair),
                    static_cast<double>(typ == StreamSync ? m.streamEnd : m.packetEnd),
                    static_cast<double>(m.eotPair), syncPtr->lastFollow() ? 1 : 0,
                    amp,
                    decoder.syncedType() == PacketSync ? "packet"
                        : (decoder.syncedType() == StreamSync ? "stream" : "idle"));
        decoder.decodeFrame(typ, bits);
        syncPtr->setExpectedType(decoder.syncedType() == PacketSync ? PacketSync : 0);
    }, thresholds);
    syncPtr = &synchronizer;

    std::vector<ComplexFloat> block(4096);
    std::uint64_t pushed = 0;
    for (;;) {
        const std::size_t got = debug::readIQSamples(in, block.data(), block.size());
        if (got == 0)
            break;
        for (std::size_t n = 0; n < got; ++n) {
            const CD sample{gain * static_cast<double>(block[n].i),
                            gain * static_cast<double>(block[n].q)};
            for (const CD dc : decimator.process(dcr.process(sample))) {
                const double dem = afc.process(dc);
                for (const double r : resampler.process(dem)) {
                    const float sym = s2s.process(r);
                    recent.push_back(sym);
                    if (recent.size() > 4096)
                        recent.pop_front();
                    synchronizer.push(sym);
                    ++pushed;
                }
            }
        }
    }
    std::printf("done: %llu IQ samples\n", static_cast<unsigned long long>(pushed));
    return 0;
}

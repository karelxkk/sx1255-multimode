// Offline stream-only analysis for M17 recordings.
//
// Unlike m17_rx_dump this tool deliberately ignores LSF validity: it decodes
// every StreamSync burst with the normal de-randomize / de-interleave /
// Viterbi chain and hands the 16-byte Codec2-3200 payload to the system
// codec2 library.  This is useful for diagnosing a radio whose LSF fails CRC
// (bad callsign / header bits) but whose voice payload is still worth
// listening to.
//
//   m17_stream_pcm capture.iq [gain] [out.wav] [first_sym] [last_sym]
//
// Outputs:
//   - one line per decoded stream frame
//   - out.wav (16 kHz PCM, by default capture-stream.wav)
//   - capture-stream.bit (raw 16-byte Codec2-3200 chunks)
//
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/m17/M17Constants.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <codec2/codec2.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

namespace {

bool lsfCrcOk(const std::vector<SoftBit>& softBits)
{
    std::vector<SoftBit> bits = softBits;
    derandomizeSoftBits(bits);
    const std::vector<SoftBit> dSoftBit = deinterleaveSoftBits(bits);
    ViterbiDecoder vd;
    auto [lsfBytes, cost] = vd.decodePunctured(
        dSoftBit, std::vector<bool>(LSFPuncturePattern.begin(), LSFPuncturePattern.end()));
    (void)cost;
    if (lsfBytes.size() < LSFLen + 1)
        return false;
    const std::vector<std::uint8_t> lsf(lsfBytes.begin() + 1, lsfBytes.begin() + 1 + LSFLen);
    const LSF parsed = LSF::newFromBytes(lsf.data());
    return parsed.checkCRC();
}

void writeWavHeader(std::ofstream& out, std::uint32_t dataBytes)
{
    const std::uint16_t channels = 1;
    const std::uint32_t sampleRate = 8000;
    const std::uint16_t bitsPerSample = 16;
    const std::uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bitsPerSample / 8);

    auto put32 = [&](std::uint32_t v) {
        out.put(static_cast<char>(v & 0xff));
        out.put(static_cast<char>((v >> 8) & 0xff));
        out.put(static_cast<char>((v >> 16) & 0xff));
        out.put(static_cast<char>((v >> 24) & 0xff));
    };
    auto put16 = [&](std::uint16_t v) {
        out.put(static_cast<char>(v & 0xff));
        out.put(static_cast<char>((v >> 8) & 0xff));
    };

    out.write("RIFF", 4);
    put32(36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    put32(16);
    put16(1);  // PCM
    put16(channels);
    put32(sampleRate);
    put32(byteRate);
    put16(blockAlign);
    put16(bitsPerSample);
    out.write("data", 4);
    put32(dataBytes);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: m17_stream_pcm <file.iq> [gain] [out.wav] [first_sym] [last_sym]\n");
        return 2;
    }
    const double gain = argc > 2 ? std::atof(argv[2]) : 1.0;
    std::string outPath = argc > 3 ? argv[3] : "capture-stream.wav";
    const std::uint64_t firstSym = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 0;
    const std::uint64_t lastSym = argc > 5 ? std::strtoull(argv[5], nullptr, 10)
                                           : ~std::uint64_t{0};

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::printf("cannot open %s\n", argv[1]);
        return 1;
    }
    debug::IQFileHeader header;
    bool raw = false;
    if (!debug::readIQHeader(in, header) || !debug::validateIQHeader(header)) {
        std::printf("no valid IQ header, treating as raw IQ\n");
        in.clear();
        in.seekg(0, std::ios::beg);
        raw = true;
    }
    if (raw) {
        std::printf("raw IQ recording: %s\n", argv[1]);
    } else {
        std::printf("recording: %u Hz, %llu samples, %s\n", header.sampleRateHz,
                    static_cast<unsigned long long>(header.sampleCount), argv[1]);
    }

    ComplexDCRemoval dcr(0.9999);
    PolyphaseDecimator decimator(
        designChannelFilter(channelBWSX1255, sampleRateSX1255, channelTapsSX1255), rxDecimSX1255);
    FMDemodAFC afc(basebandDCAvgCntSX1255, rxFastNSX1255, rxSlowNSX1255);
    RationalResampler resampler(24, 25);
    SampleToSymbol s2s(std::vector<double>(rrcTaps5.begin(), rrcTaps5.end()), rxScalingCoeffSX1255);

    struct CODEC2* codec2 = codec2_create(CODEC2_MODE_3200);
    std::ofstream wav(outPath, std::ios::binary | std::ios::trunc);
    std::ofstream bitFile(outPath + ".bit", std::ios::binary | std::ios::trunc);
    std::vector<std::int16_t> pcm;
    pcm.reserve(320000);
    std::uint32_t wavDataBytes = 0;
    bool headerWritten = false;

    std::uint16_t lastFn7 = 0xffff;
    bool haveLastFn = false;
    bool streamSeen = false;
    std::uint64_t frameCount = 0;

    const auto flushPcm = [&](const std::int16_t* samples, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            pcm.push_back(samples[i]);
        if (pcm.size() >= 320000) {
            if (!headerWritten) {
                // Placeholder, patched at the end.
                writeWavHeader(wav, 0);
                headerWritten = true;
            }
            wav.write(reinterpret_cast<const char*>(pcm.data()),
                      static_cast<std::streamsize>(pcm.size() * sizeof(std::int16_t)));
            wavDataBytes += static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
            pcm.clear();
        }
    };

    M17Synchronizer synchronizer(5, [&](std::uint16_t typ, const std::vector<SoftBit>& softBits) {
        if (typ == LSFSync) {
            // A false LSF in the middle of a transmission would swallow the
            // stream frame that follows it.  Retract rejected LSFs exactly like
            // the live receiver does so the next frame is not lost.
            if (!lsfCrcOk(softBits))
                synchronizer.retract(streamSeen);
            return;
        }
        if (typ == PacketSync) {
            // A packet sync cannot belong to an ongoing voice stream.  Undo the
            // extraction so the stream frame behind it is not lost.
            if (streamSeen)
                synchronizer.retract(true);
            return;
        }
        if (typ != StreamSync)
            return;
        streamSeen = true;
        const std::uint64_t sampleIndex = synchronizer.samples();
        if (sampleIndex < firstSym || sampleIndex > lastSym)
            return;

        std::vector<SoftBit> bits = softBits;
        derandomizeSoftBits(bits);
        const std::vector<SoftBit> dSoftBit = deinterleaveSoftBits(bits);
        const std::array<std::uint8_t, 6> lich = decodeLICH(dSoftBit, 0);
        const std::uint8_t lichCnt = lich[5] >> 5;

        ViterbiDecoder vd;
        const std::vector<SoftBit> frameBits(dSoftBit.begin() + 96, dSoftBit.end());
        auto [frameData, cost] = vd.decodePunctured(
            frameBits, std::vector<bool>(StreamPuncturePattern.begin(), StreamPuncturePattern.end()));

        const std::uint16_t fn = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(frameData[1]) << 8) | frameData[2]);
        const std::uint16_t fn7 = fn & 0x7fff;
        const bool lastFrame = (fn & 0x8000) != 0;

        std::printf("frame %llu sym=%llu fn=%u lich=%u viterbi=%d bytes=%zu%s\n",
                    static_cast<unsigned long long>(frameCount),
                    static_cast<unsigned long long>(sampleIndex), fn, lichCnt, cost,
                    frameData.size(), lastFrame ? " LAST" : "");

        std::vector<std::uint8_t> payload(frameData.begin() + 3, frameData.end());
        if (payload.size() >= 16) {
            bitFile.write(reinterpret_cast<const char*>(payload.data()), 16);
            // M17 packs two Codec2-3200 frames (8 bytes / 20 ms each) into one
            // 16-byte stream payload.
            const int samplesPerFrame = codec2_samples_per_frame(codec2);
            std::vector<std::int16_t> speech(static_cast<std::size_t>(samplesPerFrame));
            for (int half = 0; half < 2; ++half) {
                codec2_decode(codec2, speech.data(), payload.data() + half * 8);
                flushPcm(speech.data(), speech.size());
            }
        }

        if (!haveLastFn) {
            lastFn7 = fn7;
            haveLastFn = true;
        } else if (fn7 != lastFn7) {
            lastFn7 = fn7;
        }
        ++frameCount;
    }, SyncThresholds{});

    std::vector<ComplexFloat> block(4096);
    double ncoPhase = 0.0;
    const double ncoStep =
        -2.0 * std::numbers::pi * 25000.0 / static_cast<double>(sampleRateSX1255);
    for (;;) {
        const std::size_t got = debug::readIQSamples(in, block.data(), block.size());
        if (got == 0)
            break;
        for (std::size_t n = 0; n < got; ++n) {
            double i = gain * static_cast<double>(block[n].i);
            double q = gain * static_cast<double>(block[n].q);
            if (!raw) {
                const double c = std::cos(ncoPhase);
                const double s = std::sin(ncoPhase);
                const double ri = i * c - q * s;
                const double rq = i * s + q * c;
                i = ri;
                q = rq;
                ncoPhase += ncoStep;
            }
            const CD sample{i, q};
            for (const CD dc : decimator.process(dcr.process(sample))) {
                const double dem = afc.process(dc);
                for (const double r : resampler.process(dem)) {
                    synchronizer.push(s2s.process(r));
                }
            }
        }
    }

    if (!pcm.empty()) {
        if (!headerWritten) {
            writeWavHeader(wav, 0);
            headerWritten = true;
        }
        wav.write(reinterpret_cast<const char*>(pcm.data()),
                  static_cast<std::streamsize>(pcm.size() * sizeof(std::int16_t)));
        wavDataBytes += static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
        pcm.clear();
    }

    // Patch the real data size into the RIFF header.
    wav.seekp(0, std::ios::beg);
    writeWavHeader(wav, wavDataBytes);
    wav.close();
    bitFile.close();
    codec2_destroy(codec2);

    std::printf("decoded %llu stream frames, wrote %s and %s.bit (%u PCM bytes)\n",
                static_cast<unsigned long long>(frameCount), outPath.c_str(), outPath.c_str(),
                wavDataBytes);
    return 0;
}

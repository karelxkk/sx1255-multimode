#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s CODEC2_BITS.bin OUTPUT.iq DURATION_S\n", argv[0]);
        return 2;
    }

    const std::string bitsPath = argv[1];
    const std::string outPath = argv[2];
    const int durationSec = std::atoi(argv[3]);
    if (durationSec <= 0) {
        std::fprintf(stderr, "invalid duration\n");
        return 2;
    }

    std::ifstream bits(bitsPath, std::ios::binary);
    if (!bits.is_open()) {
        std::fprintf(stderr, "cannot open %s\n", bitsPath.c_str());
        return 1;
    }

    const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);

    // One 40 ms preamble frame before the LSF sync word.  The detector needs
    // only the final 8 preamble symbols in front of the LSF sync.
    std::vector<Symbol> symbols;
    for (std::size_t i = 0; i < SymbolsPerFrame; ++i)
        symbols.push_back((i % 2) == 0 ? +3.0F : -3.0F);

    // LSF (stream header).
    const std::vector<Symbol> lsfSymbols = generateLSFSymbols(lsf);
    symbols.insert(symbols.end(), lsfSymbols.begin(), lsfSymbols.end());

    // Stream frames, two Codec2 frames (16 bytes) per M17 stream frame.
    std::uint16_t fn = 0;
    std::vector<std::uint8_t> payload(16);   // two 8-byte Codec2 frames
    for (;;) {
        bits.read(reinterpret_cast<char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        if (bits.gcount() != static_cast<std::streamsize>(payload.size()))
            break;
        const std::vector<Symbol> stream = generateStreamSymbols(lsf, fn, payload);
        symbols.insert(symbols.end(), stream.begin(), stream.end());
        fn = static_cast<std::uint16_t>((fn + 1) & 0x7fff);
        if (fn == 0 || static_cast<int>(fn) >= durationSec * 25)
            break;
    }

    // End-of-transmission marker.
    const std::vector<Symbol> eot = appendEOT({});
    symbols.insert(symbols.end(), eot.begin(), eot.end());

    // Modulate at baseband, then place the M17 signal at +25 kHz so the
    // replay path sees exactly what the SX1255 low-IF frontend would produce.
    std::vector<CD> iq = modulateSymbols(symbols);
    const double shift = 2.0 * 3.14159265358979323846 * 25000.0 / 125000.0;
    for (std::size_t i = 0; i < iq.size(); ++i) {
        const double phase = shift * static_cast<double>(i);
        const double c = std::cos(phase);
        const double s = std::sin(phase);
        const double re = iq[i].re * c - iq[i].im * s;
        const double im = iq[i].re * s + iq[i].im * c;
        iq[i] = {re, im};
    }

    debug::IQFileHeader header;
    header.sampleRateHz = 125000;
    header.frequencyHz = 0;
    header.sampleCount = iq.size();
    header.timestampSec = 0;

    std::vector<ComplexFloat> outSamples(iq.size());
    for (std::size_t i = 0; i < iq.size(); ++i)
        outSamples[i] = {static_cast<float>(iq[i].re), static_cast<float>(iq[i].im)};

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    if (!debug::writeIQHeader(out, header) ||
        !debug::writeIQSamples(out, outSamples.data(), outSamples.size())) {
        std::fprintf(stderr, "failed writing IQ file\n");
        return 1;
    }

    std::printf("wrote %s: %zu samples (%.3f s), %d stream frames\n",
                outPath.c_str(), iq.size(),
                static_cast<double>(iq.size()) / 125000.0, fn);
    return 0;
}

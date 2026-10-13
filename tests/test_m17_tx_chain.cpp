#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace sx1255::m17;

int main()
{
    const LSF txLsf = makeLSF("M17-CZ1 B", "OK1LBC", 1, 2, 0);

    // Persistent TX pipeline (the same one the M17 plugin uses).
    TXPulseShaper shaper(std::vector<double>(rrcTaps5.begin(), rrcTaps5.end()), 5);
    BatchResampler resampler(125, 24);
    BatchFMModulator fm(125000.0);

    std::vector<CD> iq;
    auto txSymbols = [&](const std::vector<Symbol>& symbols) {
        std::vector<double> baseband = shaper.process(symbols);
        for (double& b : baseband)
            b *= std::sqrt(5.0);
        const std::vector<double> up = resampler.process(baseband);
        const std::vector<CD> part = fm.modulate(up, 800.0);
        iq.insert(iq.end(), part.begin(), part.end());
    };

    std::vector<Symbol> pre = appendPreamble({}, false);
    txSymbols(pre);
    txSymbols(generateLSFSymbols(txLsf));
    for (int n = 0; n < 10; ++n) {
        std::vector<std::uint8_t> payload(16, static_cast<std::uint8_t>(n));
        txSymbols(generateStreamSymbols(txLsf, static_cast<std::uint16_t>(n), payload));
    }

    // RX chain: full DSP -> RRC matched samples -> synchronizer -> decoder.
    const std::vector<float> samples = runRXChain(iq);
    bool gotLSF = false;
    LSF rxLsf;

    Decoder::Callbacks cb;
    cb.onLSF = [&](const LSF& lsf, double) { gotLSF = true; rxLsf = lsf; };
    Decoder decoder(std::move(cb));
    M17Synchronizer sync(5, [&](std::uint16_t typ, const std::vector<SoftBit>& softBits) {
        decoder.decodeFrame(typ, softBits);
    });
    for (const float s : samples)
        sync.push(s);

    if (!gotLSF)
        return 1;
    if (rxLsf.srcCallsign() != "OK1LBC")
        return 2;
    return 0;
}

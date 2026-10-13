#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cstdint>
#include <vector>

using namespace sx1255::m17;

int main()
{
    const LSF txLsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);

    // TX: preamble + LSF frame + filler symbols -> 125 kSa/s IQ.
    std::vector<Symbol> symbols = appendPreamble({}, false);
    const std::vector<Symbol> lsfSyms = generateLSFSymbols(txLsf);
    symbols.insert(symbols.end(), lsfSyms.begin(), lsfSyms.end());
    for (int i = 0; i < 600; ++i)
        symbols.push_back(i % 2 ? +3.0F : -3.0F);

    const std::vector<CD> iq = modulateSymbols(symbols);

    // RX: full DSP chain -> matched-filter samples.
    const std::vector<float> samples = runRXChain(iq);

    bool gotLSF = false;
    LSF rxLsf;
    std::uint16_t syncType = 0;

    Decoder::Callbacks callbacks;
    callbacks.onLSF = [&](const LSF& lsf, double) { gotLSF = true; rxLsf = lsf; };
    Decoder decoder(std::move(callbacks));

    M17Synchronizer synchronizer(5, [&](std::uint16_t typ, const std::vector<SoftBit>& softBits) {
        if (typ == LSFSync) {
            syncType = typ;
            decoder.decodeFrame(typ, softBits);
        }
    });

    for (const float s : samples)
        synchronizer.push(s);

    if (syncType != LSFSync)
        return 1;
    if (!gotLSF)
        return 2;
    if (rxLsf.toBytes() != txLsf.toBytes())
        return 3;
    if (rxLsf.dstCallsign() != "N1ADJ" || rxLsf.srcCallsign() != "OK1LBC")
        return 4;

    return 0;
}

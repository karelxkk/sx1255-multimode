#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sx1255::m17;

int main()
{
    // Build an LSF-shaped false sync followed by real stream frames.  When the
    // decoder rejects the LSF, the synchronizer must restore its complete
    // window, skip the same false LSF, and still acquire the stream frames.
    std::vector<Symbol> symbols;
    symbols.insert(symbols.end(), LSFPreambleSymbols.begin(), LSFPreambleSymbols.end());
    symbols.insert(symbols.end(), LSFSyncSymbols.begin(), LSFSyncSymbols.end());
    for (std::size_t i = 0; i < SymbolsPerPayload; ++i)
        symbols.push_back((i & 1U) ? -1.0F : +1.0F);

    const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
    const std::vector<std::uint8_t> payload(16, 0x42);
    for (std::uint16_t frame = 0; frame < 8; ++frame) {
        const std::vector<Symbol> stream = generateStreamSymbols(lsf, frame, payload);
        symbols.insert(symbols.end(), stream.begin(), stream.end());
    }

    int falseLsfBursts = 0;
    int streamBursts = 0;
    M17Synchronizer* syncPtr = nullptr;
    M17Synchronizer synchronizer(5, [&](std::uint16_t typ,
                                        const std::vector<SoftBit>&) {
        if (typ == LSFSync) {
            ++falseLsfBursts;
            syncPtr->retract();
        } else if (typ == StreamSync) {
            ++streamBursts;
            syncPtr->setExpectedType(StreamSync);
        }
    });
    syncPtr = &synchronizer;

    for (const Symbol symbol : symbols) {
        for (int i = 0; i < 5; ++i)
            synchronizer.push(symbol);
    }

    if (falseLsfBursts != 1) {
        std::printf("false LSF bursts=%d\n", falseLsfBursts);
        return 1;
    }
    if (streamBursts < 2) {
        std::printf("stream bursts=%d\n", streamBursts);
        return 2;
    }
    return 0;
}

#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace sx1255::m17;

int main()
{
    // CRC check value from the M17 spec: CRC("123456789") == 0x772B.
    {
        const std::uint8_t check[] = "123456789";
        if (crc16(check, 9) != 0x772B)
            return 1;
    }

    // Callsign round-trip.
    {
        const std::string cs = decodeCallsign(encodeCallsign("N1ADJ"));
        if (cs != "N1ADJ")
            return 2;
        if (decodeCallsign(encodeCallsign("@ALL")) != "@ALL")
            return 3;
    }

    // Golay(24,12) encode -> soft decode round-trip.
    {
        const std::uint16_t data = 0xABC;
        const std::uint32_t cw = golayEncode24(data);
        std::array<SoftBit, 24> soft{};
        for (int i = 0; i < 24; ++i)
            soft[static_cast<std::size_t>(i)] = ((cw >> (23 - i)) & 1) ? SoftTrue : SoftFalse;
        if (golaySoftDecode24(soft) != data)
            return 4;
    }

    // CalcSoftbits: symbol -> bit pair mapping.
    {
        const auto bits = calcSoftbits({+3, +1, -1, -3});
        // +3 -> (MSB,LSB) = (0,1); +1 -> (0,0); -1 -> (1,0); -3 -> (1,1)
        if (bits[0] != SoftFalse || bits[1] != SoftTrue)   return 5;
        if (bits[2] != SoftFalse || bits[3] != SoftFalse)  return 6;
        if (bits[4] != 0xFFFE    || bits[5] != SoftFalse)  return 7;
        if (bits[6] != SoftTrue  || bits[7] != SoftTrue)   return 8;
    }

    // LSF CRC.
    {
        const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
        if (!lsf.checkCRC())
            return 9;
        if (lsf.dstCallsign() != "N1ADJ" || lsf.srcCallsign() != "OK1LBC")
            return 10;
        if (lsf.toBytes().size() != LSFLen)
            return 11;
    }

    // SyncDistance on a clean LSF burst.
    {
        const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
        std::vector<Symbol> symbols = appendPreamble({}, false);
        const std::vector<Symbol> lsfSyms = generateLSFSymbols(lsf);
        symbols.insert(symbols.end(), lsfSyms.begin(), lsfSyms.end());
        for (int i = 0; i < 300; ++i)
            symbols.push_back(i % 2 ? +3.0F : -3.0F);

        // The LSF syncword starts at symbol 192; the extended sync is the
        // last 8 preamble symbols + 8 syncword symbols.
        const auto [dist, typ] = syncDistance(symbols, 184, 1);
        if (typ != LSFSync)
            return 12;
        if (dist >= 4.5F)
            return 13;
    }

    return 0;
}

#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cstdint>
#include <vector>

using namespace sx1255::m17;

namespace {

std::vector<std::uint8_t> buildPacketPayload()
{
    // M17 packet type 0x05 = SMS, then text and CRC.
    std::vector<std::uint8_t> payload;
    payload.push_back(0x05);
    const char text[] = "Ahoj z M17";
    payload.insert(payload.end(), text, text + sizeof(text) - 1);
    const std::uint16_t crc = crc16(payload.data(), payload.size());
    payload.push_back(static_cast<std::uint8_t>(crc >> 8));
    payload.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    return payload;
}

}  // namespace

int main()
{
    const LSF txLsf = makeLSF("N1ADJ", "OK1LBC", 0, 1, 0);  // packet, data
    const std::vector<std::uint8_t> txPayload = buildPacketPayload();

    std::vector<Symbol> symbols = generatePacketSymbols(txLsf, txPayload);
    // The synchronizer needs lookahead beyond the packet to finish the
    // packet-frame/EOT detection; continuous RX would provide that naturally.
    for (int i = 0; i < 600; ++i)
        symbols.push_back(i % 2 ? +3.0F : -3.0F);
    const std::vector<CD> iq = modulateSymbols(symbols);
    const std::vector<float> samples = runRXChain(iq);

    bool gotLSF = false;
    bool gotPacket = false;
    LSF rxLsf;
    std::vector<std::uint8_t> rxPayload;

    Decoder::Callbacks callbacks;
    callbacks.onLSF = [&](const LSF& lsf, double) { gotLSF = true; rxLsf = lsf; };
    callbacks.onPacket = [&](const LSF&, const std::vector<std::uint8_t>& payload, double) {
        gotPacket = true;
        rxPayload = payload;
    };
    Decoder decoder(std::move(callbacks));

    M17Synchronizer synchronizer(5, [&](std::uint16_t typ, const std::vector<SoftBit>& softBits) {
        if (typ == LSFSync || typ == PacketSync)
            decoder.decodeFrame(typ, softBits);
    });

    for (const float s : samples)
        synchronizer.push(s);

    if (!gotLSF)
        return 1;
    if (rxLsf.toBytes() != txLsf.toBytes())
        return 2;
    if (!gotPacket)
        return 3;
    if (rxPayload != txPayload)
        return 4;

    return 0;
}

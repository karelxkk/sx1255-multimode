// Decoder state machine: a packet must survive a spurious end marker, frames
// have to assemble regardless of order, and a frame without any header must be
// reported and dropped instead of being routed anywhere.
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace sx1255::m17;

namespace {

std::vector<std::uint8_t> packetPayload(const std::string& text)
{
    std::vector<std::uint8_t> payload;
    payload.push_back(0x05);
    payload.insert(payload.end(), text.begin(), text.end());
    const std::uint16_t crc = crc16(payload.data(), payload.size());
    payload.push_back(static_cast<std::uint8_t>(crc >> 8));
    payload.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    return payload;
}

// sections splits a generated packet transmission into the soft bits the
// decoder receives: the LSF payload and the payload of every packet frame.
struct Sections {
    std::vector<SoftBit> lsf;
    std::vector<std::vector<SoftBit>> frames;
};

Sections sections(const std::string& text)
{
    const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 0, 1, 0);   // packet, data
    const std::vector<std::uint8_t> payload = packetPayload(text);
    const std::vector<Symbol> syms = generatePacketSymbols(lsf, payload);

    auto slice = [&](std::size_t firstSymbol) {
        std::vector<Symbol> part(syms.begin() + static_cast<std::ptrdiff_t>(firstSymbol),
                                 syms.begin() + static_cast<std::ptrdiff_t>(firstSymbol + SymbolsPerPayload));
        return calcSoftbits(part);
    };

    Sections s;
    const std::size_t preamble = SymbolsPerFrame;   // the generator emits a full preamble
    s.lsf = slice(preamble + SymbolsPerSyncword);
    const std::size_t frameCount = (payload.size() + 24) / 25;
    for (std::size_t i = 0; i < frameCount; ++i)
        s.frames.push_back(slice(preamble + SymbolsPerFrame + i * SymbolsPerFrame + SymbolsPerSyncword));
    return s;
}

struct Log {
    int packets = 0;
    int dropped = 0;
    int reattached = 0;
    std::vector<std::uint8_t> lastPayload;
};

Decoder makeDecoder(Log& log)
{
    Decoder::Callbacks cb;
    cb.onPacket = [&](const LSF&, const std::vector<std::uint8_t>& payload, double) {
        ++log.packets;
        log.lastPayload = payload;
    };
    cb.onFrameDropped = [&](std::uint16_t, const char*) { ++log.dropped; };
    cb.onPacketReattached = [&](const LSF&) { ++log.reattached; };
    return Decoder(std::move(cb));
}

bool delivered(const Log& log, const std::string& text)
{
    if (log.packets != 1)
        return false;
    return std::string(log.lastPayload.begin() + 1, log.lastPayload.end() - 2) == text;
}

}  // namespace

int main()
{
    int failures = 0;
    const std::string longText = "Zprava pres tri ramce z rádia, at to stoji za to";
    const std::string shortText = "Kratka";

    {   // Baseline: LSF then its frames.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        decoder.decodeFrame(LSFSync, s.lsf);
        for (const auto& frame : s.frames)
            decoder.decodeFrame(PacketSync, frame);
        if (!delivered(log, shortText)) {
            std::printf("baseline: FAIL (packets %d, dropped %d)\n", log.packets, log.dropped);
            ++failures;
        }
    }

    {   // A spurious end marker between the header and the frames must not
        // cost the message: the frames re-attach to the recent header.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        decoder.decodeFrame(LSFSync, s.lsf);
        decoder.decodeFrame(EOTMarker, {});
        for (const auto& frame : s.frames)
            decoder.decodeFrame(PacketSync, frame);
        if (!delivered(log, shortText) || log.reattached != 1) {
            std::printf("after spurious EOT: FAIL (packets %d, reattached %d)\n",
                        log.packets, log.reattached);
            ++failures;
        }
    }

    {   // Frames arriving out of order still land on their frame number.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(longText);
        if (s.frames.size() < 3) {
            std::printf("out of order: FAIL test text needs three frames (%zu)\n", s.frames.size());
            ++failures;
        } else {
            decoder.decodeFrame(LSFSync, s.lsf);
            decoder.decodeFrame(PacketSync, s.frames[1]);
            decoder.decodeFrame(PacketSync, s.frames[0]);
            decoder.decodeFrame(PacketSync, s.frames[2]);
            if (!delivered(log, longText)) {
                std::printf("out of order: FAIL (packets %d)\n", log.packets);
                ++failures;
            }
        }
    }

    {   // A frame with no header at all cannot be routed: reported, not routed.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        decoder.decodeFrame(PacketSync, s.frames[0]);
        if (log.packets != 0 || log.dropped != 1) {
            std::printf("no header: FAIL (packets %d, dropped %d)\n", log.packets, log.dropped);
            ++failures;
        }
    }

    {   // A header that belongs to a different transmission expires.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        decoder.decodeFrame(LSFSync, s.lsf);
        decoder.decodeFrame(EOTMarker, {});   // the transmission state is gone
        decoder.expireRetention();
        for (const auto& frame : s.frames)
            decoder.decodeFrame(PacketSync, frame);
        if (log.packets != 0 || log.dropped != 1) {
            std::printf("expired header: FAIL (packets %d, dropped %d)\n", log.packets, log.dropped);
            ++failures;
        }
    }

    {   // A second transmission with a fresh header still decodes.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        decoder.decodeFrame(LSFSync, s.lsf);
        decoder.decodeFrame(PacketSync, s.frames[0]);
        decoder.decodeFrame(LSFSync, s.lsf);
        decoder.decodeFrame(PacketSync, s.frames[0]);
        if (log.packets != 2) {
            std::printf("repeat: FAIL (packets %d, want 2)\n", log.packets);
            ++failures;
        }
    }

    if (failures == 0)
        std::printf("decoder state: ok\n");
    return failures == 0 ? 0 : 1;
}

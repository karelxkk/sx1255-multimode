// Reception of M17 packets must not lose anything: multi-frame messages,
// transmissions without an end-of-transmission marker, packets sent back to
// back and packets that follow a stream which never ended cleanly all have to
// be decoded and delivered.
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace sx1255::m17;

namespace {

struct Results {
    int lsfs = 0;
    int streams = 0;
    int packets = 0;
    int badCrc = 0;
    int dropped = 0;
    std::vector<std::vector<std::uint8_t>> payloads;
    std::vector<std::string> sources;
};

std::vector<std::uint8_t> packetPayload(const std::string& text)
{
    // M17 packet type 0x05 = SMS, then text and CRC.
    std::vector<std::uint8_t> payload;
    payload.push_back(0x05);
    payload.insert(payload.end(), text.begin(), text.end());
    const std::uint16_t crc = crc16(payload.data(), payload.size());
    payload.push_back(static_cast<std::uint8_t>(crc >> 8));
    payload.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    return payload;
}

// filler stands in for the idle carrier between transmissions.
std::vector<Symbol> filler(std::size_t count)
{
    std::vector<Symbol> syms;
    syms.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        syms.push_back(i % 2 ? +3.0F : -3.0F);
    return syms;
}

std::vector<Symbol> concat(std::vector<Symbol> head, const std::vector<Symbol>& tail)
{
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

std::vector<Symbol> packetSymbols(const std::string& text, const std::string& src = "OK1LBC",
                                  bool withEot = true)
{
    const LSF lsf = makeLSF("N1ADJ", src, 0, 1, 0);   // packet, data
    std::vector<Symbol> syms = generatePacketSymbols(lsf, packetPayload(text));
    if (!withEot) {
        // Replace the trailing EOT frame with the idle pattern: a sender that
        // stops right after its last frame.
        for (std::size_t i = syms.size() - SymbolsPerFrame; i < syms.size(); ++i)
            syms[i] = (i % 2 == 0) ? +3.0F : -3.0F;
    }
    return syms;
}

std::vector<Symbol> streamSymbols(int frames, bool withEot)
{
    const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);   // stream, voice
    std::vector<Symbol> syms = appendPreamble({}, false);
    const std::vector<Symbol> lsfSyms = generateLSFSymbols(lsf);
    syms.insert(syms.end(), lsfSyms.begin(), lsfSyms.end());
    for (int i = 0; i < frames; ++i) {
        const std::vector<std::uint8_t> payload(16, static_cast<std::uint8_t>(0x40 + i));
        const std::vector<Symbol> frame =
            generateStreamSymbols(lsf, static_cast<std::uint16_t>(i), payload);
        syms.insert(syms.end(), frame.begin(), frame.end());
    }
    if (withEot) {
        const std::vector<Symbol> eot = appendEOT({});
        syms.insert(syms.end(), eot.begin(), eot.end());
    }
    return syms;
}

// runRX decodes a symbol stream exactly like the plugin does: every sync burst
// goes to the decoder, and the synchronizer's follow mode tracks the decoder.
Results runRX(const std::vector<Symbol>& symbols)
{
    Results results;
    Decoder::Callbacks callbacks;
    callbacks.onLSF = [&](const LSF&, double) { ++results.lsfs; };
    callbacks.onStream = [&](const LSF&, const std::vector<std::uint8_t>&, std::uint16_t,
                             std::uint16_t, double) { ++results.streams; };
    callbacks.onPacket = [&](const LSF& lsf, const std::vector<std::uint8_t>& payload, double) {
        ++results.packets;
        results.payloads.push_back(payload);
        results.sources.push_back(lsf.srcCallsign());
    };
    callbacks.onPacketBadCRC = [&](const LSF&, const std::vector<std::uint8_t>&, double) {
        ++results.badCrc;
    };
    callbacks.onFrameDropped = [&](std::uint16_t, const char*) { ++results.dropped; };

    Decoder decoder(std::move(callbacks));
    M17Synchronizer* syncPtr = nullptr;
    M17Synchronizer synchronizer(5, [&](std::uint16_t typ, const std::vector<SoftBit>& softBits) {
        decoder.decodeFrame(typ, softBits);
        if (syncPtr)
            syncPtr->setExpectedType(decoder.syncedType() == PacketSync ? PacketSync : 0);
    });
    syncPtr = &synchronizer;

    const std::vector<CD> iq = modulateSymbols(symbols);
    for (const float sample : runRXChain(iq))
        synchronizer.push(sample);

    return results;
}

bool check(const char* name, const Results& results, int wantPackets, const char* wantText)
{
    if (results.packets != wantPackets) {
        std::printf("%s: FAIL got %d packet(s), want %d (bad crc %d, dropped %d)\n",
                    name, results.packets, wantPackets, results.badCrc, results.dropped);
        return false;
    }
    if (wantText != nullptr) {
        const std::string text(results.payloads.back().begin() + 1, results.payloads.back().end() - 2);
        if (text != wantText) {
            std::printf("%s: FAIL payload \"%s\", want \"%s\"\n", name, text.c_str(), wantText);
            return false;
        }
    }
    std::printf("%s: ok (%d packet(s), src %s)\n", name, results.packets,
                results.sources.empty() ? "-" : results.sources.back().c_str());
    return true;
}

}  // namespace

int main()
{
    int failures = 0;

    {   // Baseline: a short packet with its EOT.
        const Results r = runRX(concat(packetSymbols("Ahoj z M17"), filler(600)));
        failures += check("one frame", r, 1, "Ahoj z M17") ? 0 : 1;
    }

    {   // A message longer than 25 bytes needs more than one packet frame.
        const std::string text = "Tohle je delsi textova zprava pres dve ramce";
        const Results r = runRX(concat(packetSymbols(text), filler(600)));
        failures += check("two frames", r, 1, text.c_str()) ? 0 : 1;
    }

    {   // The sender stopped right after its last frame (no EOT marker).
        const Results r = runRX(concat(packetSymbols("Bez EOT"), filler(600)));
        failures += check("without EOT", r, 1, "Bez EOT") ? 0 : 1;
    }

    {   // Two packets back to back must both arrive.
        const Results r = runRX(concat(concat(concat(packetSymbols("Prvni"), filler(200)),
                                              packetSymbols("Druha")), filler(600)));
        failures += check("two packets", r, 2, "Druha") ? 0 : 1;
    }

    {   // A stream that never ended (carrier dropped) followed by a packet.
        const Results r = runRX(concat(concat(concat(streamSymbols(3, false), filler(200)),
                                              packetSymbols("Po strime")), filler(600)));
        if (r.streams == 0) {
            std::printf("packet after stream: FAIL no stream frames decoded\n");
            ++failures;
        }
        failures += check("packet after stream", r, 1, "Po strime") ? 0 : 1;
    }

    {   // Two-frame packet without an EOT: both fixes together.
        const std::string text = "Dlouha zprava bez EOT na konci prenosu";
        const Results r = runRX(concat(packetSymbols(text, "OK1LBC", false), filler(600)));
        failures += check("two frames without EOT", r, 1, text.c_str()) ? 0 : 1;
    }

    if (failures != 0)
        std::printf("%d scenario(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}

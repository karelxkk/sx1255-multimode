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
    int duplicates = 0;
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

// corrupt flips every n-th soft bit, which is what a marginal signal does.
std::vector<SoftBit> corrupt(std::vector<SoftBit> bits, std::size_t every)
{
    for (std::size_t i = 0; i < bits.size(); i += every)
        bits[i] = (bits[i] == SoftFalse) ? SoftTrue : SoftFalse;
    return bits;
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
        const LayerCountersSnapshot layers = decoder.layerCounters();
        if (layers.syncDetected != 1 + s.frames.size() ||
            layers.lsfOk != 1 || layers.lsfFail != 0 ||
            layers.crcOk < 2 || layers.viterbiOk < 1 + s.frames.size() ||
            layers.frameOk < 1 + s.frames.size()) {
            std::printf("baseline layers: FAIL sync=%llu lsf=%llu/%llu crc=%llu/%llu "
                        "viterbi=%llu/%llu frame=%llu/%llu\n",
                        static_cast<unsigned long long>(layers.syncDetected),
                        static_cast<unsigned long long>(layers.lsfOk),
                        static_cast<unsigned long long>(layers.lsfFail),
                        static_cast<unsigned long long>(layers.crcOk),
                        static_cast<unsigned long long>(layers.crcFail),
                        static_cast<unsigned long long>(layers.viterbiOk),
                        static_cast<unsigned long long>(layers.viterbiFail),
                        static_cast<unsigned long long>(layers.frameOk),
                        static_cast<unsigned long long>(layers.frameFail));
            ++failures;
        }
    }

    {   // The LSF reaches Viterbi and produces the expected output length, but
        // the payload CRC fails.  This is the layer boundary we need to see.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        decoder.decodeFrame(LSFSync, corrupt(s.lsf, 2));
        const LayerCountersSnapshot layers = decoder.layerCounters();
        const FrameAudit audit = decoder.lastFrameAudit();
        if (layers.syncDetected != 1 || layers.lsfFail != 1 ||
            layers.crcFail != 1 || layers.viterbiOk != 1 ||
            layers.viterbiFail != 0 || layers.frameFail != 1 ||
            audit.crcStatus != 0 || audit.lsfStatus != 0 ||
            !audit.viterbiOk || audit.softbitStats.count != 2 * SymbolsPerPayload) {
            std::printf("corrupt LSF layers: FAIL lsf=%llu/%llu crc=%llu/%llu "
                        "viterbi=%llu/%llu frame=%llu/%llu\n",
                        static_cast<unsigned long long>(layers.lsfOk),
                        static_cast<unsigned long long>(layers.lsfFail),
                        static_cast<unsigned long long>(layers.crcOk),
                        static_cast<unsigned long long>(layers.crcFail),
                        static_cast<unsigned long long>(layers.viterbiOk),
                        static_cast<unsigned long long>(layers.viterbiFail),
                        static_cast<unsigned long long>(layers.frameOk),
                        static_cast<unsigned long long>(layers.frameFail));
            ++failures;
        }
        if (decoder.currentBerPercent() <= 0.0 || audit.channelBits != 368 ||
            audit.channelErrors <= 0) {
            std::printf("corrupt LSF BER: FAIL ber=%.3f bits=%d errors=%d\n",
                        decoder.currentBerPercent(), audit.channelBits,
                        audit.channelErrors);
            ++failures;
        }
        decoder.resetLayerCounters();
        if (decoder.layerCounters().syncDetected != 0 ||
            decoder.layerCounters().crcFail != 0) {
            std::printf("layer reset: FAIL\n");
            ++failures;
        }
    }

    {   // A clean stream frame exercises the four Golay LICH blocks.  All four
        // must be observable even though they do not participate in LSF CRC.
        Log log;
        Decoder decoder = makeDecoder(log);
        const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
        std::vector<Symbol> symbols = appendPreamble({}, false);
        const std::vector<Symbol> lsfSymbols = generateLSFSymbols(lsf);
        symbols.insert(symbols.end(), lsfSymbols.begin(), lsfSymbols.end());
        const std::vector<std::uint8_t> payload(16, 0x42);
        const std::vector<Symbol> stream = generateStreamSymbols(lsf, 0, payload);
        symbols.insert(symbols.end(), stream.begin(), stream.end());

        auto slice = [&](std::size_t first) {
            return calcSoftbits(std::vector<Symbol>(
                symbols.begin() + static_cast<std::ptrdiff_t>(first),
                symbols.begin() + static_cast<std::ptrdiff_t>(first + SymbolsPerPayload)));
        };
        const std::size_t lsfPayload = SymbolsPerFrame + SymbolsPerSyncword;
        const std::size_t streamPayload = 2 * SymbolsPerFrame + SymbolsPerSyncword;
        decoder.decodeFrame(LSFSync, slice(lsfPayload));
        decoder.decodeFrame(StreamSync, slice(streamPayload));
        const LayerCountersSnapshot layers = decoder.layerCounters();
        if (layers.golayOk != 4 || layers.golayFail != 0 ||
            layers.viterbiOk != 2 || layers.viterbiFail != 0 ||
            layers.frameFail != 0) {
            std::printf("stream layers: FAIL golay=%llu/%llu viterbi=%llu/%llu frame=%llu/%llu\n",
                        static_cast<unsigned long long>(layers.golayOk),
                        static_cast<unsigned long long>(layers.golayFail),
                        static_cast<unsigned long long>(layers.viterbiOk),
                        static_cast<unsigned long long>(layers.viterbiFail),
                        static_cast<unsigned long long>(layers.frameOk),
                        static_cast<unsigned long long>(layers.frameFail));
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

    {   // The three attempts a radio sends are one message, not three.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        for (int attempt = 0; attempt < 3; ++attempt) {
            decoder.decodeFrame(LSFSync, s.lsf);
            for (const auto& frame : s.frames)
                decoder.decodeFrame(PacketSync, frame);
        }
        if (log.packets != 1 || decoder.counters().duplicates != 2) {
            std::printf("repeats: FAIL (packets %d, duplicates %llu)\n", log.packets,
                        static_cast<unsigned long long>(decoder.counters().duplicates));
            ++failures;
        }
    }

    {   // The middle frame is lost in the first attempt and arrives with the
        // second one; the message must still come out.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(longText);
        decoder.decodeFrame(LSFSync, s.lsf);
        decoder.decodeFrame(PacketSync, s.frames[0]);
        decoder.decodeFrame(PacketSync, s.frames[2]);
        decoder.decodeFrame(LSFSync, s.lsf);
        for (const auto& frame : s.frames)
            decoder.decodeFrame(PacketSync, frame);
        if (!delivered(log, longText)) {
            std::printf("missing frame: FAIL (packets %d, bad crc %llu)\n", log.packets,
                        static_cast<unsigned long long>(decoder.counters().packetsBadCRC));
            ++failures;
        }
    }

    {   // The middle frame is damaged in the first attempt; the second attempt
        // brings a clean copy of the very same frame.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(longText);
        decoder.decodeFrame(LSFSync, s.lsf);
        decoder.decodeFrame(PacketSync, s.frames[0]);
        decoder.decodeFrame(PacketSync, corrupt(s.frames[1], 5));
        decoder.decodeFrame(PacketSync, s.frames[2]);
        decoder.decodeFrame(LSFSync, s.lsf);
        for (const auto& frame : s.frames)
            decoder.decodeFrame(PacketSync, frame);
        if (!delivered(log, longText)) {
            std::printf("repaired frame: FAIL (packets %d, improved %llu)\n", log.packets,
                        static_cast<unsigned long long>(decoder.counters().framesImproved));
            ++failures;
        }
    }

    {   // A packet LSF that never completes must time out and release the
        // decoder.  Otherwise a false header keeps M17 "active" forever.
        Log log;
        Decoder decoder = makeDecoder(log);
        const Sections s = sections(shortText);
        decoder.decodeFrame(LSFSync, s.lsf);
        if (decoder.syncedType() != PacketSync) {
            std::printf("stale packet header: FAIL not in packet sync\n");
            ++failures;
        }
        decoder.expireRetention();
        if (decoder.syncedType() != 0) {
            std::printf("stale packet header: FAIL sync did not clear\n");
            ++failures;
        }
        decoder.decodeFrame(PacketSync, s.frames[0]);
        if (log.packets != 0 || log.dropped != 1) {
            std::printf("stale packet header: FAIL frame was routed\n");
            ++failures;
        }
    }

    if (failures == 0)
        std::printf("decoder state: ok\n");
    return failures == 0 ? 0 : 1;
}

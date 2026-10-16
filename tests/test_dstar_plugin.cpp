#include "sx1255/dstar/DStarPlugin.hpp"
#include "sx1255/dstar/DStarTX.hpp"
#include "sx1255/frontend/ITransmit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace sx1255;

namespace {

class FakeTx final : public ITransmit {
public:
    bool setTxFrequency(Hertz) override { return true; }

    bool setPtt(bool on) override
    {
        ptt = on;
        pttOnCount += on ? 1 : 0;
        pttOffCount += on ? 0 : 1;
        return true;
    }

    bool writeIQ(const ComplexFloat*, std::size_t count) override
    {
        written += count;
        return true;
    }

    bool drainTx() override
    {
        drained = true;
        return true;
    }

    bool isReady() const override { return true; }

    bool ptt = false;
    std::size_t written = 0;
    std::size_t pttOnCount = 0;
    std::size_t pttOffCount = 0;
    bool drained = false;
};

void putCallsign(dstar::DStarHeader& header, std::size_t offset,
                 const std::string& callsign, std::size_t width)
{
    for (std::size_t i = 0; i < width; ++i) {
        const char c = i < callsign.size() ? callsign[i] : ' ';
        header.bytes[offset + i] = static_cast<std::uint8_t>(c);
    }
}

dstar::DStarHeader makeHeader()
{
    dstar::DStarHeader header;
    header.bytes.fill(0);
    putCallsign(header, 3,  "OK1LBC", 8);
    putCallsign(header, 11, "DIRECT", 8);
    putCallsign(header, 19, "CQCQCQ", 8);
    putCallsign(header, 27, "OK2ABC", 8);
    putCallsign(header, 35, "  A", 4);
    const std::uint16_t crc = dstar::dstarHeaderCrc(header.bytes.data(), 39);
    header.bytes[39] = static_cast<std::uint8_t>(crc & 0xFF);
    header.bytes[40] = static_cast<std::uint8_t>(crc >> 8);
    return header;
}

std::vector<ComplexFloat> makeSignal()
{
    const dstar::DStarHeader tx =
        dstar::dstarRepeaterHeader(makeHeader(), "OK1LBC");
    dstar::DStarTXModulator mod;
    std::vector<ComplexFloat> iq;
    const auto header = mod.modulateHeader(tx, 210);
    iq.insert(iq.end(), header.begin(), header.end());

    constexpr std::array<std::uint8_t, 9> nullAmbe = {
        0x9E, 0x8D, 0x32, 0x88, 0x26, 0x1A, 0x3F, 0x61, 0xE8};
    constexpr std::array<std::uint8_t, 3> dataSync = {0x55, 0x2D, 0x16};
    constexpr std::array<std::uint8_t, 3> nullSlow = {0x16, 0x29, 0xF5};
    for (int frame = 0; frame < 6; ++frame) {
        dstar::DStarFrame f;
        std::copy(nullAmbe.begin(), nullAmbe.end(), f.bytes.begin());
        if (frame == 0)
            std::copy(dataSync.begin(), dataSync.end(), f.bytes.begin() + 9);
        else
            std::copy(nullSlow.begin(), nullSlow.end(), f.bytes.begin() + 9);
        const auto frameIq = mod.modulateFrame(f);
        iq.insert(iq.end(), frameIq.begin(), frameIq.end());
    }
    const auto end = mod.modulateEnd(8);
    iq.insert(iq.end(), end.begin(), end.end());
    return iq;
}

}  // namespace

int main()
{
    DStarConfig config;
    config.callsign = "OK1LBC";
    config.txPreambleBytes = 210;
    config.txTailMs = 50;
    DStarPlugin plugin(config, nullptr);
    FakeTx tx;
    plugin.setTransmitter(&tx);

    const std::vector<ComplexFloat> iq = makeSignal();
    constexpr std::size_t blockSize = 2048;
    const ComplexFloat zero{0.0F, 0.0F};

    bool started = false;
    bool finished = false;
    const std::size_t maxBlocks = iq.size() / blockSize + 200;

    for (std::size_t i = 0; i < maxBlocks && !finished; ++i) {
        std::vector<ComplexFloat> block;
        block.reserve(blockSize);
        const std::size_t base = i * blockSize;
        for (std::size_t j = 0; j < blockSize; ++j) {
            const std::size_t at = base + j;
            block.push_back(at < iq.size() ? iq[at] : zero);
        }
        const IQBlock iqBlock{static_cast<std::uint64_t>(i), block.data(), block.size(), 125000.0};

        if (!started) {
            if (plugin.detect(iqBlock).detected) {
                SessionContext ctx;
                ctx.sessionId = 1;
                ctx.mode = Mode::DStar;
                ctx.pluginId = "dstar";
                ctx.tx = &tx;
                plugin.startSession(ctx);
                started = true;
            }
        } else {
            finished = !plugin.process(iqBlock);
        }
    }

    if (!started)
        return 1;
    if (!finished)
        return 2;
    if (tx.pttOnCount == 0)
        return 3;
    if (tx.pttOffCount == 0)
        return 4;
    if (tx.written == 0)
        return 5;
    if (!tx.drained)
        return 6;
    if (tx.ptt)
        return 7;
    if (plugin.telemetry().metrics.at("frame_count") != 1.0)
        return 8;

    return 0;
}

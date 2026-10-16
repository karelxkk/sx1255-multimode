#include "sx1255/dstar/DStarDetector.hpp"
#include "sx1255/dstar/DStarTX.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace sx1255;

namespace {

void putCallsign(dstar::DStarHeader& header, std::size_t offset,
                 const std::string& callsign, std::size_t width)
{
    for (std::size_t i = 0; i < width; ++i) {
        const char c = i < callsign.size() ? callsign[i] : ' ';
        header.bytes[offset + i] = static_cast<std::uint8_t>(c);
    }
}

dstar::DStarHeader makeRxHeader()
{
    dstar::DStarHeader header;
    header.bytes.fill(0);
    header.bytes[0] = 0x40;  // received header already marked as repeater-routed
    putCallsign(header, 3,  "OK1LBC", 8);   // RPT2
    putCallsign(header, 11, "DIRECT", 8);   // RPT1
    putCallsign(header, 19, "CQCQCQ", 8);   // YOUR
    putCallsign(header, 27, "OK2ABC", 8);   // MY1
    putCallsign(header, 35, "  A", 4);      // MY2
    const std::uint16_t crc = dstar::dstarHeaderCrc(header.bytes.data(), 39);
    header.bytes[39] = static_cast<std::uint8_t>(crc & 0xFF);
    header.bytes[40] = static_cast<std::uint8_t>(crc >> 8);
    return header;
}

}  // namespace

int main()
{
    const dstar::DStarHeader rx = makeRxHeader();
    const dstar::DStarHeader tx = dstar::dstarRepeaterHeader(rx, "OK1LBC");

    // RPT1/RPT2 must be rewritten and the repeater flag cleared.
    for (std::size_t i = 0; i < 8; ++i) {
        const char want = i < 6 ? "OK1LBC"[i] : ' ';
        if (tx.bytes[3 + i] != static_cast<std::uint8_t>(want))
            return 1;
        if (tx.bytes[11 + i] != static_cast<std::uint8_t>(want))
            return 2;
    }
    if ((tx.bytes[0] & 0x40) != 0)
        return 3;
    const std::uint16_t crc = dstar::dstarHeaderCrc(tx.bytes.data(), 39);
    if (static_cast<std::uint8_t>(crc) != tx.bytes[39] ||
        static_cast<std::uint8_t>(crc >> 8) != tx.bytes[40])
        return 4;

    dstar::DStarTXModulator mod;
    std::vector<ComplexFloat> iq;
    const auto headerIq = mod.modulateHeader(tx, 210);
    iq.insert(iq.end(), headerIq.begin(), headerIq.end());

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

    const auto endIq = mod.modulateEnd(8);
    iq.insert(iq.end(), endIq.begin(), endIq.end());

    dstar::DStarDetector detector;
    for (const ComplexFloat& sample : iq) {
        const IQBlock block{0, &sample, 1, 125000.0};
        detector.detect(block);
    }

    unsigned int headers = 0;
    unsigned int frames = 0;
    unsigned int eots = 0;
    unsigned int losts = 0;
    dstar::DStarRxEvent event{};
    dstar::DStarHeader decodedHeader{};
    dstar::DStarFrame decodedFrame{};
    while (detector.nextEvent(event, &decodedHeader, &decodedFrame)) {
        if (event == dstar::DStarRxEvent::Header)
            ++headers;
        else if (event == dstar::DStarRxEvent::Data)
            ++frames;
        else if (event == dstar::DStarRxEvent::Eot)
            ++eots;
        else if (event == dstar::DStarRxEvent::Lost)
            ++losts;
    }

    if (headers != 1)
        return 5;
    if (frames != 6)
        return 6;
    if (eots != 1)
        return 7;
    if (losts != 0)
        return 8;
    if (detector.frameSyncsSeen() != 1)
        return 9;

    return 0;
}

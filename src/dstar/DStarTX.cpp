#include "sx1255/dstar/DStarTX.hpp"

#include <algorithm>
#include <cstring>

namespace sx1255::dstar {

namespace {

constexpr std::array<std::uint8_t, 2> kFrameSyncBytes = {0xEA, 0xA6};
constexpr std::array<std::uint8_t, 6> kEndPatternBytes = {
    0x55, 0x55, 0x55, 0x55, 0xC8, 0x7A};
constexpr std::uint8_t kRepeaterMask = 0x40;
constexpr std::size_t kLongCallsignLength = 8;

void appendBits(std::vector<bool>& bits, const std::uint8_t* data, std::size_t count)
{
    bits.reserve(bits.size() + count * 8);
    for (std::size_t i = 0; i < count; ++i)
        for (int j = 0; j < 8; ++j)
            bits.push_back((data[i] >> j) & 1);
}

}  // namespace

DStarTXModulator::DStarTXModulator() = default;

void DStarTXModulator::reset()
{
    m_filterBuf.fill(0.0F);
    m_filterPtr = 0;
    m_resampler.reset();
    m_fm.reset();
}

std::vector<ComplexFloat> DStarTXModulator::modulateBits(const std::vector<bool>& bits)
{
    const auto& taps = dstarGaussianTaps();

    std::vector<double> baseband;
    baseband.reserve(bits.size() * 10);
    for (const bool bit : bits) {
        const float input = bit ? -0.5F : 0.5F;
        for (int i = 0; i < 10; ++i) {
            m_filterBuf[static_cast<std::size_t>(m_filterPtr)] = input;
            m_filterPtr = (m_filterPtr + 1) % 43;

            float out = 0.0F;
            int idx = m_filterPtr;
            for (int t = 0; t < 43; ++t) {
                out += taps[static_cast<std::size_t>(t)] *
                       m_filterBuf[static_cast<std::size_t>(idx)];
                idx = (idx + 1) % 43;
            }
            baseband.push_back(static_cast<double>(out));
        }
    }

    const std::vector<double> resampled = m_resampler.process(baseband);
    // +-0.5 baseband maps to +-1200 Hz (D-Star GMSK peak deviation).
    const std::vector<m17::CD> iq = m_fm.modulate(resampled, 2400.0);

    std::vector<ComplexFloat> out;
    out.reserve(iq.size());
    for (const auto& sample : iq)
        out.push_back({static_cast<float>(sample.re), static_cast<float>(sample.im)});
    return out;
}

std::vector<ComplexFloat> DStarTXModulator::modulateBytes(const std::uint8_t* data,
                                                          std::size_t count)
{
    std::vector<bool> bits;
    appendBits(bits, data, count);
    return modulateBits(bits);
}

std::vector<ComplexFloat> DStarTXModulator::modulateHeader(const DStarHeader& header,
                                                           unsigned int preambleBytes)
{
    std::vector<bool> bits;
    bits.reserve(static_cast<std::size_t>(preambleBytes + 2 + 83) * 8);

    constexpr std::uint8_t bitSync = 0xAA;
    for (unsigned int i = 0; i < preambleBytes; ++i)
        appendBits(bits, &bitSync, 1);

    appendBits(bits, kFrameSyncBytes.data(), kFrameSyncBytes.size());

    const auto fec = dstarFecEncode(header.bytes);
    appendBits(bits, fec.data(), fec.size());
    return modulateBits(bits);
}

std::vector<ComplexFloat> DStarTXModulator::modulateFrame(const DStarFrame& frame)
{
    return modulateBytes(frame.bytes.data(), frame.bytes.size());
}

std::vector<ComplexFloat> DStarTXModulator::modulateEnd(unsigned int tailBlocks)
{
    std::vector<bool> bits;
    bits.reserve(static_cast<std::size_t>(tailBlocks) * kEndPatternBytes.size() * 8);
    for (unsigned int block = 0; block < tailBlocks; ++block)
        appendBits(bits, kEndPatternBytes.data(), kEndPatternBytes.size());
    return modulateBits(bits);
}

DStarHeader dstarRepeaterHeader(const DStarHeader& rx, const std::string& callsign)
{
    DStarHeader out = rx;

    std::array<char, kLongCallsignLength> padded{};
    padded.fill(' ');
    const std::size_t n = std::min(callsign.size(), padded.size());
    std::copy_n(callsign.begin(), n, padded.begin());

    // RPT1 is bytes 11..18, RPT2 is bytes 3..10.
    std::memcpy(out.bytes.data() + 3, padded.data(), padded.size());
    std::memcpy(out.bytes.data() + 11, padded.data(), padded.size());
    out.bytes[0] &= static_cast<std::uint8_t>(~kRepeaterMask);

    const std::uint16_t crc = dstarHeaderCrc(out.bytes.data(), 39);
    out.bytes[39] = static_cast<std::uint8_t>(crc & 0xFF);
    out.bytes[40] = static_cast<std::uint8_t>(crc >> 8);
    return out;
}

}  // namespace sx1255::dstar

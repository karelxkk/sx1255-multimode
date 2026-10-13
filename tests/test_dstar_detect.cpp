#include "sx1255/dstar/DStarDetector.hpp"
#include "sx1255/m17/M17DSP.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace sx1255;

namespace {

std::vector<bool> byteToBits(const std::vector<std::uint8_t>& bytes)
{
    std::vector<bool> bits;
    bits.reserve(bytes.size() * 8);
    for (const std::uint8_t b : bytes)
        for (int j = 0; j < 8; ++j)
            bits.push_back((b >> j) & 1);
    return bits;
}

std::vector<bool> buildHeaderBits(bool valid)
{
    std::array<std::uint8_t, 41> header{};
    header[0] = 0x0A; header[1] = 0x0B; header[2] = 0x0C;  // arbitrary payload
    if (valid) {
        const std::uint16_t crc = dstar::dstarHeaderCrc(header.data(), 39);
        header[39] = static_cast<std::uint8_t>(crc & 0xFF);
        header[40] = static_cast<std::uint8_t>(crc >> 8);
    }

    const std::array<std::uint8_t, 83> fec = dstar::dstarFecEncode(header);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(64 + 2 + 83);
    for (int i = 0; i < 200; ++i) bytes.push_back(0xAA);  // bit sync preamble
    bytes.push_back(0xEA);  // frame sync
    bytes.push_back(0xA6);
    bytes.insert(bytes.end(), fec.begin(), fec.end());
    // End-pattern tail so the detector has enough symbols to finish the
    // 660-bit FEC header even with the FM-chain group delay.
    for (int i = 0; i < 50; ++i) bytes.push_back(0x55);
    return byteToBits(bytes);
}

std::vector<m17::CD> gmskModulate(const std::vector<bool>& bits)
{
    // Gaussian pulse shaping at 48 kSa/s (10 samples/bit).
    const auto& taps = dstar::dstarGaussianTaps();
    std::array<float, 43> buf{};
    int idx = 0;
    std::vector<double> baseband;
    baseband.reserve(bits.size() * 10);
    for (const bool bit : bits) {
        const float in = bit ? -0.5F : 0.5F;
        for (int i = 0; i < 10; ++i) {
            buf[static_cast<std::size_t>(idx)] = in;
            idx = (idx + 1) % 43;
            float out = 0.0F;
            int p = idx;
            for (int t = 0; t < 43; ++t) {
                out += taps[static_cast<std::size_t>(t)] * buf[static_cast<std::size_t>(p)];
                p = (p + 1) % 43;
            }
            baseband.push_back(out);
        }
    }

    // 48k -> 125k, then FM modulate: +-0.5 -> +-1200 Hz.
    m17::BatchResampler resampler(125, 48);
    const std::vector<double> up = resampler.process(baseband);
    m17::BatchFMModulator fm(125000.0);
    return fm.modulate(up, 2400.0);
}

}  // namespace

int main()
{
    dstar::DStarDetector detector;

    // Valid header decoded at the bit level.
    for (const bool bit : buildHeaderBits(true))
        detector.feedBit(bit);
    if (detector.frameSyncsSeen() == 0)
        return 1;

    // Invalid header (bad CRC) must not be accepted.
    {
        dstar::DStarDetector bad;
        for (const bool bit : buildHeaderBits(false))
            bad.feedBit(bit);
        if (bad.frameSyncsSeen() != 0)
            return 2;
    }

    // Full RF path: valid GMSK signal detected.
    {
        dstar::DStarDetector d2;
        const auto iq = gmskModulate(buildHeaderBits(true));
        bool found = false;
        for (const auto& s : iq) {
            const ComplexFloat one{static_cast<float>(s.re), static_cast<float>(s.im)};
            const IQBlock b{0, &one, 1, 125000.0};
            if (d2.detect(b).detected) { found = true; break; }
        }
        if (!found)
            return 3;
    }

    // FM (88.5 Hz CTCSS tone) must not trigger D-Star.
    {
        dstar::DStarDetector d;
        std::vector<m17::CD> iq(8192);
        double phase = 0.0;
        for (std::size_t i = 0; i < iq.size(); ++i) {
            const double t = static_cast<double>(i) / 125000.0;
            const double f = 500.0 * std::sin(2.0 * 3.14159265358979323846 * 88.5 * t);
            phase += 2.0 * 3.14159265358979323846 * f / 125000.0;
            iq[i] = {std::cos(phase), std::sin(phase)};
        }
        bool found = false;
        for (const auto& s : iq) {
            const ComplexFloat one{static_cast<float>(s.re), static_cast<float>(s.im)};
            const IQBlock b{0, &one, 1, 125000.0};
            if (d.detect(b).detected) { found = true; break; }
        }
        if (found)
            return 4;
    }

    // Noise (silence) must not trigger D-Star.
    {
        dstar::DStarDetector d;
        const ComplexFloat zero{0.0F, 0.0F};
        for (int i = 0; i < 8192; ++i) {
            const IQBlock b{0, &zero, 1, 125000.0};
            if (d.detect(b).detected)
                return 5;
        }
    }

    return 0;
}

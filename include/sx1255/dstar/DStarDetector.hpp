#pragma once

#include "sx1255/Types.hpp"
#include "sx1255/m17/M17DSP.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sx1255::dstar {

// Detection-only D-Star layer.  It FM-discriminates the shared 125 kSa/s IQ
// stream, runs the GMSK matched filter + PLL, hunts for the 24-bit frame sync
// and validates the FEC header (descramble + deinterleave + Viterbi + CCITT).
// It deliberately contains no AMBE/voice/gateway/network code.
class DStarDetector {
public:
    DStarDetector();

    Detection detect(const IQBlock& block);
    void reset();

    // Feed one already-demodulated D-Star bit directly (test seam for the
    // sync + header-validation state machine, bypassing the RF front end).
    bool feedBit(bool bit);

    std::uint64_t frameSyncsSeen() const { return m_frameSyncs; }

private:
    enum class State { None, Header };

    bool processBit(bool bit);
    bool validateHeader();
    unsigned int countBits(std::uint32_t value) const;

    void viterbiDecode(int data1, int data0);
    void acs(const std::array<int, 8>& metric);
    void traceBack();

    // RX front end (reused, not reimplemented: m17 FMDemodulator + resampler).
    m17::FMDemodulator m_fm;
    m17::RationalResampler m_resampler{48, 125};  // 125 kSa/s -> 48 kSa/s

    // GMSK matched filter (43-tap Gaussian, BT=0.5).
    std::array<float, 43> m_filterTaps;
    std::array<float, 43> m_filterBuf{};
    int m_filterPtr = 0;

    // PLL symbol timing (DSTAR_RADIO_BIT_LENGTH = 10 samples/bit).
    std::uint32_t m_pll = 0;
    bool m_prevBit = false;
    std::uint32_t m_inc = 0;  // set to INC_UNLOCK in the constructor
    bool m_locked = false;
    float m_offset = 0.0F;
    float m_accum = 0.0F;
    std::uint32_t m_count = 0;

    // Sync + header state machine.
    State m_state = State::None;
    std::uint32_t m_pattern = 0;
    std::array<std::uint8_t, 83> m_headerBits{};
    std::uint32_t m_headerBitsCount = 0;
    std::uint64_t m_frameSyncs = 0;

    // Viterbi state.
    std::array<int, 4> m_pathMetric{};
    std::array<std::uint32_t, 42> m_pathMemory0{};
    std::array<std::uint32_t, 42> m_pathMemory1{};
    std::array<std::uint32_t, 42> m_pathMemory2{};
    std::array<std::uint32_t, 42> m_pathMemory3{};
    std::array<std::uint8_t, 42> m_fecOutput{};
    std::uint32_t m_mar = 0;
};

// Test/build helpers: D-Star TX side needed to generate a valid header.
const std::array<float, 43>& dstarGaussianTaps();
std::uint16_t dstarHeaderCrc(const std::uint8_t* data, std::size_t length);
// Encodes a 41-byte header into the 83-byte FEC block (convolve + interleave +
// scramble), as txHeader() does in DStarRepeater.
std::array<std::uint8_t, 83> dstarFecEncode(const std::array<std::uint8_t, 41>& header);

}  // namespace sx1255::dstar

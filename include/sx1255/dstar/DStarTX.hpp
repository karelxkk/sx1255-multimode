#pragma once

#include "sx1255/Types.hpp"
#include "sx1255/dstar/DStarDetector.hpp"
#include "sx1255/m17/M17DSP.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sx1255::dstar {

// ---------------------------------------------------------------------------
// DStarTXModulator
//
// Streaming D-Star GMSK transmit chain.  It mirrors DStarRepeater's
// SoundCardController writeBits(): one NRZ bit becomes 10 baseband samples
// through the 43-tap Gaussian filter (BT=0.5), the 48 kSa/s baseband is
// fractionally resampled to 125 kSa/s and FM-modulated with 1200 Hz peak
// deviation.  All filter/resampler/FM state persists between calls so frames
// are shaped with the filter history they need.
// ---------------------------------------------------------------------------
class DStarTXModulator {
public:
    DStarTXModulator();

    void reset();

    std::vector<ComplexFloat> modulateBytes(const std::uint8_t* data, std::size_t count);

    // Preamble + frame sync + FEC header, exactly like DStarRepeater.
    std::vector<ComplexFloat> modulateHeader(const DStarHeader& header,
                                             unsigned int preambleBytes = 210);

    std::vector<ComplexFloat> modulateFrame(const DStarFrame& frame);

    // The first six bytes of END_PATTERN_BYTES repeated `tailBlocks` times.
    std::vector<ComplexFloat> modulateEnd(unsigned int tailBlocks = 8);

private:
    std::vector<ComplexFloat> modulateBits(const std::vector<bool>& bits);

    std::array<float, 43> m_filterBuf{};
    int m_filterPtr = 0;
    m17::BatchResampler m_resampler{125, 48};
    m17::BatchFMModulator m_fm{125000.0};
};

// Build the header a duplex repeater puts on air from a decoded RF header:
// RPT1/RPT2 are replaced by the repeater callsign and the repeater flag is
// cleared (DStarRepeater's processRadioHeader() behaviour), then the CCITT
// checksum is recomputed.
DStarHeader dstarRepeaterHeader(const DStarHeader& rx, const std::string& callsign);

}  // namespace sx1255::dstar

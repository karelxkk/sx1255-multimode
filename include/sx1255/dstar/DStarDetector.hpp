#pragma once

#include "sx1255/Types.hpp"
#include "sx1255/m17/M17DSP.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace sx1255::dstar {

// One decoded on-air radio header.  `bytes` is the 41-byte wire format
// (flags, RPT2/RPT1/YOUR/MY1/MY2, CCITT checksum), including the checksum.
struct DStarHeader {
    std::array<std::uint8_t, 41> bytes{};
};

// One decoded DV frame: 9 AMBE voice bytes + 3 slow-data/sync bytes.
struct DStarFrame {
    std::array<std::uint8_t, 12> bytes{};
};

enum class DStarRxEvent {
    None,
    Header,
    Data,
    Eot,
    Lost,
};

// Streaming D-Star receive layer.  It FM-discriminates the shared 125 kSa/s
// IQ stream, runs the GMSK matched filter + PLL, hunts for the frame sync,
// validates the FEC header (descramble + deinterleave + Viterbi + CCITT) and
// then follows the DV data stream to the end-of-transmission marker.  Decoded
// events are queued and drained through nextEvent(), which makes this class
// usable both for mode detection and for a local RX->TX repeater.
//
// It deliberately contains no AMBE/voice/gateway/network code.
class DStarDetector {
public:
    DStarDetector();

    Detection detect(const IQBlock& block);
    void reset();

    // Feed one already-demodulated D-Star bit directly (test seam for the
    // sync + header-validation state machine, bypassing the RF front end).
    bool feedBit(bool bit);

    // Pop the oldest queued decode event.  On Header/Data the matching payload
    // is written to `header`/`frame` when the pointer is non-null.  Returns
    // false when the queue is empty.
    bool nextEvent(DStarRxEvent& event, DStarHeader* header = nullptr,
                   DStarFrame* frame = nullptr);

    std::uint64_t frameSyncsSeen() const { return m_frameSyncs; }
    bool inData() const { return m_state == State::Data; }

private:
    enum class State { None, Header, Data };

    bool processBit(bool bit);
    bool validateHeader();
    void processDataBit(bool bit);
    void pushEvent(DStarRxEvent event, const DStarHeader* header = nullptr,
                   const DStarFrame* frame = nullptr);
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

    // Sync + header + data state machine.
    State m_state = State::None;
    std::uint32_t m_pattern = 0;
    std::array<std::uint8_t, 83> m_headerBits{};
    std::uint32_t m_headerBitsCount = 0;
    std::uint64_t m_frameSyncs = 0;
    DStarHeader m_lastHeader{};

    // DV data frame collection.
    std::array<std::uint8_t, 12> m_dataFrame{};
    std::uint32_t m_dataFrameBits = 0;
    std::uint32_t m_dataBits = 0;

    // Decoded event queue (drained by the plugin on the dispatch thread).
    std::deque<DStarRxEvent> m_events;
    std::deque<DStarHeader> m_headerQueue;
    std::deque<DStarFrame> m_frameQueue;

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

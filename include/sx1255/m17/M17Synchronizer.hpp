#pragma once

#include "sx1255/m17/M17Constants.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace sx1255::m17 {

// Detection thresholds.  The defaults are m17-gateway's; they can be loosened
// in the configuration when signals are marginal.
struct SyncThresholds {
    float lsf = 4.5F;
    float packet = 5.0F;
    float stream = 5.0F;
    float eot = 4.5F;
    // Single-sync-word threshold used while a packet transmission is already
    // running (see setExpectedType).  The double window is worth sqrt(2) more,
    // so this is the equivalent single-window value.
    float follow = 3.5F;
};

// SyncMetrics holds the individual euclidean distances of one sync search
// window.  Ideal symbols are +-3, so lower is better; "pair" distances look at
// the following frame as well (m17-gateway's trick to tell sync words from
// noise), while the "only" distances look at the sync word alone.
struct SyncMetrics {
    float lsf = 0.0F;         // 16 symbols: preamble + LSF sync word
    float packetPair = 0.0F;  // 16 symbols: packet sync + next frame's sync
    float packetEnd = 0.0F;   // 16 symbols: packet sync + EOT one frame later
    float streamPair = 0.0F;  // 16 symbols: stream sync + next frame's sync
    float streamEnd = 0.0F;   // 16 symbols: stream sync + EOT one frame later
    float eotPair = 0.0F;     // 16 symbols: EOT pattern, repeated
    float packetOnly = 0.0F;  // 8 symbols: packet sync word alone
    float streamOnly = 0.0F;  // 8 symbols: stream sync word alone
};

SyncMetrics syncMetrics(const std::vector<Symbol>& symbols, int offset, int sps);

// SyncDistance port (codec.go): returns (euclidean distance, sync type) of the
// closest of the six classic candidates, without applying any threshold.
std::pair<float, std::uint16_t> syncDistance(const std::vector<Symbol>& symbols,
                                             int offset, int sps);

// Port of processSymbolStream + extractPayload (modem/modem.go).  Feeds the
// same sliding-window detection loop as m17-gateway and emits soft bits for
// every detected sync burst via the frame sink.
class M17Synchronizer {
public:
    using FrameSink = std::function<void(std::uint16_t typ, const std::vector<SoftBit>& softBits)>;

    M17Synchronizer(int sps, FrameSink sink, SyncThresholds thresholds = {});

    void push(float sample);
    void reset();

    // setExpectedType tells the synchronizer which frame type to expect while
    // a transmission is already being received (PacketSync after a packet-mode
    // LSF).  The following frames are then recognized by their sync word alone,
    // so the last frame of a packet is not lost when the sync word that would
    // follow it (another frame or the EOT) is missing, weak or truncated.
    void setExpectedType(std::uint16_t typ) { m_expectedType = typ; }
    std::uint16_t expectedType() const { return m_expectedType; }
    const SyncThresholds& thresholds() const { return m_thresholds; }

    std::size_t bufferedSamples() const { return m_symbols.size(); }

private:
    void extractPayload(float dist, std::uint16_t typ);

    int m_sps;
    FrameSink m_sink;
    SyncThresholds m_thresholds;
    std::vector<Symbol> m_symbols;
    std::size_t m_bufSize;
    std::uint16_t m_expectedType = 0;
};

}  // namespace sx1255::m17

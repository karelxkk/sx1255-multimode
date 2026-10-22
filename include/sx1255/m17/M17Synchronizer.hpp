#pragma once

#include "sx1255/m17/M17Constants.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace sx1255::m17 {

// Detection thresholds.  Measured on a real radio (OpenRTX): real LSFs land at
// 1.4-4.4 and real frames at 2.7-3.8, while anything that is not a sync word
// stays above ~11.5.  The defaults sit in that gap with margin on both sides.
struct SyncThresholds {
    float lsf = 7.5F;
    float packet = 5.0F;
    float stream = 5.0F;
    float eot = 4.5F;
    // The LSF sync word alone, used for transmissions that do not repeat the
    // preamble (a radio retrying a packet sends the header without it, and the
    // frames were heard while the header was not).
    float lsfSync = 4.5F;
    // Single-sync-word threshold used while a transmission is already running
    // (see setExpectedType).  A real frame matches its sync word at ~3, so 4.5
    // keeps a margin while staying far below the ~9 of a non-match.
    float follow = 4.5F;
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
    float lsfOnly = 0.0F;     // 8 symbols: LSF sync word alone, no preamble
};

// How a burst was recognised: by the classic best-of-six search, by the sync
// word of a frame inside a running transmission, or by a header's sync word
// alone.
enum class SyncKind { Blind, Follow, SyncOnly };

// Diagnostic result of one synchronizer search step.  This records what the
// existing detector evaluated; it is not an additional detection decision.
struct SyncEvaluation {
    float distance = 0.0F;
    float threshold = 0.0F;
    std::uint16_t type = 0;
    bool accepted = false;
    SyncKind kind = SyncKind::Blind;
    SyncMetrics metrics{};
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
    using EvaluationSink = std::function<void(const SyncEvaluation&)>;

    using Kind = SyncKind;

    M17Synchronizer(int sps, FrameSink sink, SyncThresholds thresholds = {});

    void push(float sample);
    void reset();

    // Diagnostic observer.  It is called for every emitted sync burst and for
    // rejected search steps whose best distance is below maxDistance.  Setting
    // the observer does not alter thresholds or symbol extraction.
    void setEvaluationSink(float maxDistance, EvaluationSink sink)
    {
        m_evaluationMaxDistance = maxDistance;
        m_evaluationSink = std::move(sink);
    }

    // Undo the payload extraction of the last burst.  A sync word that turns
    // out to carry nothing valid (a false header, a header whose CRC fails)
    // must not swallow the signal that follows it - the frames behind it are
    // real.  restoreSyncWindow keeps the sync symbols available as well; that
    // is needed only while a real stream is already established.
    void retract(bool restoreSyncWindow = true);
    bool canRetract() const { return m_canRetract; }

    // setExpectedType tells the synchronizer which frame type to expect while
    // a transmission is already being received (PacketSync after a packet-mode
    // LSF).  The following frames are then recognized by their sync word alone,
    // so the last frame of a packet is not lost when the sync word that would
    // follow it (another frame or the EOT) is missing, weak or truncated.
    void setExpectedType(std::uint16_t typ) { m_expectedType = typ; }
    std::uint16_t expectedType() const { return m_expectedType; }
    const SyncThresholds& thresholds() const { return m_thresholds; }
    // Distance of the sync burst emitted last (diagnostics).
    float lastDistance() const { return m_lastDistance; }
    const SyncEvaluation& lastEvaluation() const { return m_lastEvaluation; }
    // Metrics of the sync burst emitted last (diagnostics).
    const SyncMetrics& lastMetrics() const { return m_lastMetrics; }
    // True when the last burst was recognized by its sync word alone, because
    // the receiver already knew a packet transmission was running.
    bool lastFollow() const { return m_lastFollow; }
    Kind lastKind() const { return m_lastKind; }
    // Number of samples pushed since start; the difference between two bursts
    // is their spacing in samples (diagnostics).
    std::uint64_t samples() const { return m_samples; }
    const std::vector<Symbol>& lastPayloadSymbols() const { return m_lastPayloadSymbols; }

    std::size_t bufferedSamples() const { return m_symbols.size(); }

private:
    void extractPayload(float dist, std::uint16_t typ);

    int m_sps;
    FrameSink m_sink;
    SyncThresholds m_thresholds;
    std::vector<Symbol> m_symbols;
    std::size_t m_bufSize;
    std::uint16_t m_expectedType = 0;
    float m_lastDistance = 0.0F;
    SyncMetrics m_lastMetrics{};
    bool m_lastFollow = false;
    Kind m_lastKind = Kind::Blind;
    std::uint64_t m_samples = 0;
    std::vector<Symbol> m_retractPrefix; // candidate sync window and alignment
    std::vector<Symbol> m_extracted;   // payload samples of the last burst
    std::vector<Symbol> m_lastPayloadSymbols;
    bool m_canRetract = false;
    std::uint16_t m_lastExtractedType = 0;
    std::uint16_t m_suppressedType = 0;
    std::uint64_t m_suppressUntilSample = 0;
    SyncEvaluation m_lastEvaluation{};
    float m_evaluationMaxDistance = 7.0F;
    EvaluationSink m_evaluationSink;
};

}  // namespace sx1255::m17

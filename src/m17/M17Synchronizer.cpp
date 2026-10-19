#include "sx1255/m17/M17Synchronizer.hpp"

#include "sx1255/m17/M17FrameDecoder.hpp"

#include <algorithm>
#include <cmath>

namespace sx1255::m17 {

SyncMetrics syncMetrics(const std::vector<Symbol>& symbols, int offset, int sps)
{
    double lsf = 0, lsfOnly = 0, pkt = 0, pkte = 0, pkta = 0, pktb = 0;
    double str = 0, stre = 0, stra = 0, strb = 0, eote = 0, eot = 0;

    const int frameStride = static_cast<int>(SymbolsPerFrame) * sps;

    for (int i = 0; i < 16 * sps; ++i) {
        if (i % sps == 0) {
            const double v = static_cast<double>(symbols[static_cast<std::size_t>(offset + i)]);
            const int sym = i / sps;
            const double dl = v - ExtLSFSyncSymbols[static_cast<std::size_t>(sym)];
            lsf += dl * dl;
            const double de = v - EOTMarkerSymbols[static_cast<std::size_t>(sym % 8)];
            eot += de * de;
            if (sym > 7) {
                const double ds = v - StreamSyncSymbols[static_cast<std::size_t>(sym - 8)];
                stra += ds * ds;
                const double dp = v - PacketSyncSymbols[static_cast<std::size_t>(sym - 8)];
                pkta += dp * dp;
                const double dl2 = v - LSFSyncSymbols[static_cast<std::size_t>(sym - 8)];
                lsfOnly += dl2 * dl2;
            }
        }
    }

    for (int i = 0; i < 16 * sps; ++i) {
        if (i % sps == 0) {
            const double v = static_cast<double>(symbols[static_cast<std::size_t>(frameStride + offset + i)]);
            const int sym = i / sps;
            if (sym > 7) {
                const double ds = v - StreamSyncSymbols[static_cast<std::size_t>(sym - 8)];
                strb += ds * ds;
                const double de = v - EOTMarkerSymbols[static_cast<std::size_t>(sym - 8)];
                eote += de * de;
                const double dp = v - PacketSyncSymbols[static_cast<std::size_t>(sym - 8)];
                pktb += dp * dp;
            }
        }
    }

    lsf = std::sqrt(lsf);
    pkt = std::sqrt(pkta + pktb);
    pkte = std::sqrt(pkta + eote);
    eot = std::sqrt(eot);
    str = std::sqrt(stra + strb);
    stre = std::sqrt(stra + eote);

    SyncMetrics metrics;
    metrics.lsf = static_cast<float>(lsf);
    metrics.packetPair = static_cast<float>(pkt);
    metrics.packetEnd = static_cast<float>(pkte);
    metrics.streamPair = static_cast<float>(str);
    metrics.streamEnd = static_cast<float>(stre);
    metrics.eotPair = static_cast<float>(eot);
    metrics.packetOnly = static_cast<float>(std::sqrt(pkta));
    metrics.streamOnly = static_cast<float>(std::sqrt(stra));
    metrics.lsfOnly = static_cast<float>(std::sqrt(lsfOnly));
    return metrics;
}

namespace {

// syncWordDistance is the metric that identifies a burst of this type by its
// sync word alone.  The payload only depends on where the sync word sits, so
// this is also the metric used to fine-tune the sampling offset.
float syncWordDistance(const SyncMetrics& m, std::uint16_t typ)
{
    switch (typ) {
        case LSFSync:    return m.lsfOnly;
        case PacketSync: return m.packetOnly;
        case StreamSync: return m.streamOnly;
        default:         return m.eotPair;
    }
}

}  // namespace

std::pair<float, std::uint16_t> syncDistance(const std::vector<Symbol>& symbols,
                                             int offset, int sps)
{
    const SyncMetrics metrics = syncMetrics(symbols, offset, sps);
    const float distances[6] = {
        metrics.lsf, metrics.packetPair, metrics.packetEnd,
        metrics.streamPair, metrics.streamEnd, metrics.eotPair};
    int mi = 0;
    for (int i = 1; i < 6; ++i)
        if (distances[i] < distances[mi])
            mi = i;

    const float m = distances[mi];
    switch (mi) {
        case 0:  return {m, LSFSync};
        case 1:
        case 2:  return {m, PacketSync};
        case 3:
        case 4:  return {m, StreamSync};
        default: return {m, EOTMarker};
    }
}

M17Synchronizer::M17Synchronizer(int sps, FrameSink sink, SyncThresholds thresholds)
    : m_sps(sps), m_sink(std::move(sink)), m_thresholds(thresholds)
{
    m_bufSize = static_cast<std::size_t>(
        8 * sps + 2 * (8 * sps + static_cast<int>(SymbolsPerFrame) * sps) + sps / 2 + 256);
}

void M17Synchronizer::reset()
{
    m_symbols.clear();
}

void M17Synchronizer::extractPayload(float dist, std::uint16_t typ)
{
    m_canRetract = false;
    int offset = 0;
    for (int i = 0; i < m_sps / 2; ++i) {
        const float d = syncWordDistance(syncMetrics(m_symbols, i + 1, m_sps), typ);
        if (d < dist) {
            dist = d;
            offset = i + 1;
        }
    }

    m_symbols.erase(m_symbols.begin(), m_symbols.begin() + offset);
    m_symbols.erase(m_symbols.begin(), m_symbols.begin() + 16 * m_sps);

    std::vector<Symbol> pld(SymbolsPerPayload);
    for (std::size_t i = 0; i < SymbolsPerPayload; ++i)
        pld[i] = m_symbols[i * static_cast<std::size_t>(m_sps)];
    m_lastPayloadSymbols = pld;

    const std::vector<SoftBit> softBits = calcSoftbits(pld);

    const std::size_t skip = static_cast<std::size_t>(
        (static_cast<int>(SymbolsPerPayload) - offset - 16) * m_sps);
    // Keep what is about to be consumed, so a rejected burst can be undone.
    if (skip <= m_symbols.size()) {
        m_extracted.assign(m_symbols.begin(), m_symbols.begin() + static_cast<std::ptrdiff_t>(skip));
        m_canRetract = true;
    }
    m_symbols.erase(m_symbols.begin(), m_symbols.begin() + static_cast<std::ptrdiff_t>(skip));

    m_sink(typ, softBits);
}

void M17Synchronizer::retract()
{
    if (!m_canRetract)
        return;
    m_symbols.insert(m_symbols.begin(), m_extracted.begin(), m_extracted.end());
    m_extracted.clear();
    m_canRetract = false;
}

void M17Synchronizer::push(float sample)
{
    ++m_samples;
    m_symbols.push_back(static_cast<Symbol>(sample));
    if (m_symbols.size() < m_bufSize)
        return;

    const SyncMetrics metrics = syncMetrics(m_symbols, 0, m_sps);

    // Blind detection: the classic best-of-six candidates, each against its own
    // threshold (m17-gateway compares the winner against its limit).
    const struct {
        float distance;
        std::uint16_t type;
        float limit;
    } candidates[6] = {
        {metrics.lsf, LSFSync, m_thresholds.lsf},
        {metrics.packetPair, PacketSync, m_thresholds.packet},
        {metrics.packetEnd, PacketSync, m_thresholds.packet},
        {metrics.streamPair, StreamSync, m_thresholds.stream},
        {metrics.streamEnd, StreamSync, m_thresholds.stream},
        {metrics.eotPair, EOTMarker, m_thresholds.eot},
    };
    int best = 0;
    for (int i = 1; i < 6; ++i)
        if (candidates[i].distance < candidates[best].distance)
            best = i;

    float dist = candidates[best].distance;
    std::uint16_t typ = 0;
    m_lastKind = Kind::Blind;
    if (dist < candidates[best].limit)
        typ = candidates[best].type;

    SyncEvaluation evaluation;
    evaluation.metrics = metrics;
    evaluation.distance = candidates[best].distance;
    evaluation.threshold = candidates[best].limit;
    evaluation.type = candidates[best].type;
    evaluation.kind = Kind::Blind;

    // Follow mode: while a packet transmission is already being received, the
    // packet sync word alone identifies its frames.  This must not depend on
    // the blind best-of-six result: in a real transmission the pair windows
    // (which look one frame ahead) can be much worse than the sync word
    // itself, which used to leave the detection sitting right at the
    // threshold.  An LSF wins over it - that is a 16 symbol match and means a
    // new transmission starts.
    m_lastFollow = false;
    if (m_expectedType == PacketSync && metrics.packetOnly < m_thresholds.follow &&
        !(typ == LSFSync && dist < m_thresholds.lsf)) {
        typ = PacketSync;
        dist = metrics.packetOnly;
        m_lastFollow = true;
        m_lastKind = Kind::Follow;
        evaluation.distance = dist;
        evaluation.threshold = m_thresholds.follow;
        evaluation.type = typ;
        evaluation.kind = Kind::Follow;
    }

    // A header whose preamble was not repeated: the LSF sync word alone decides.
    // Radios that retry a packet send the header without the preamble again,
    // and without this the frames were heard while the message had no header.
    if (typ == 0 && metrics.lsfOnly < m_thresholds.lsfSync) {
        typ = LSFSync;
        dist = metrics.lsfOnly;
        m_lastKind = Kind::SyncOnly;
        evaluation.distance = dist;
        evaluation.threshold = m_thresholds.lsfSync;
        evaluation.type = typ;
        evaluation.kind = Kind::SyncOnly;
    }

    evaluation.accepted = typ != 0;
    evaluation.type = typ == 0 ? candidates[best].type : typ;
    m_lastEvaluation = evaluation;
    if (m_evaluationSink &&
        (evaluation.accepted || evaluation.distance < m_evaluationMaxDistance)) {
        m_evaluationSink(evaluation);
    }

    if (typ == 0) {
        m_symbols.erase(m_symbols.begin());
        return;
    }

    if (typ == EOTMarker) {
        m_lastDistance = dist;
        m_lastMetrics = metrics;
        m_symbols.erase(m_symbols.begin(), m_symbols.begin() + 16 * m_sps);
        m_sink(typ, {});
        return;
    }

    m_lastDistance = dist;
    m_lastMetrics = metrics;
    extractPayload(dist, typ);
}

}  // namespace sx1255::m17

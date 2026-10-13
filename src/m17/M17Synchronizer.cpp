#include "sx1255/m17/M17Synchronizer.hpp"

#include "sx1255/m17/M17FrameDecoder.hpp"

#include <algorithm>
#include <cmath>

namespace sx1255::m17 {

std::pair<float, std::uint16_t> syncDistance(const std::vector<Symbol>& symbols,
                                             int offset, int sps)
{
    double lsf = 0, pkt = 0, pkte = 0, pkta = 0, pktb = 0;
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

    const double vals[6] = {lsf, pkt, pkte, str, stre, eot};
    int mi = 0;
    for (int i = 1; i < 6; ++i)
        if (vals[i] < vals[mi])
            mi = i;

    const float m = static_cast<float>(vals[mi]);
    switch (mi) {
        case 0:  return {m, LSFSync};
        case 1:
        case 2:  return {m, PacketSync};
        case 3:
        case 4:  return {m, StreamSync};
        default: return {m, EOTMarker};
    }
}

M17Synchronizer::M17Synchronizer(int sps, FrameSink sink)
    : m_sps(sps), m_sink(std::move(sink))
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
    int offset = 0;
    for (int i = 0; i < m_sps / 2; ++i) {
        const auto [d, t] = syncDistance(m_symbols, i + 1, m_sps);
        if (t == typ && d < dist) {
            dist = d;
            offset = i + 1;
        }
    }

    m_symbols.erase(m_symbols.begin(), m_symbols.begin() + offset);
    m_symbols.erase(m_symbols.begin(), m_symbols.begin() + 16 * m_sps);

    std::vector<Symbol> pld(SymbolsPerPayload);
    for (std::size_t i = 0; i < SymbolsPerPayload; ++i)
        pld[i] = m_symbols[i * static_cast<std::size_t>(m_sps)];

    const std::vector<SoftBit> softBits = calcSoftbits(pld);

    const std::size_t skip = static_cast<std::size_t>(
        (static_cast<int>(SymbolsPerPayload) - offset - 16) * m_sps);
    m_symbols.erase(m_symbols.begin(), m_symbols.begin() + static_cast<std::ptrdiff_t>(skip));

    m_sink(typ, softBits);
}

void M17Synchronizer::push(float sample)
{
    m_symbols.push_back(static_cast<Symbol>(sample));
    if (m_symbols.size() < m_bufSize)
        return;

    const auto [dist, typ] = syncDistance(m_symbols, 0, m_sps);
    bool handled = false;

    switch (typ) {
        case LSFSync:
            if (dist < 4.5F) {
                extractPayload(dist, typ);
                handled = true;
            }
            break;
        case PacketSync:
            if (dist < 5.0F) {
                extractPayload(dist, typ);
                handled = true;
            }
            break;
        case StreamSync:
            if (dist < 5.0F) {
                extractPayload(dist, typ);
                handled = true;
            }
            break;
        case EOTMarker:
            if (dist < 4.5F) {
                m_symbols.erase(m_symbols.begin(), m_symbols.begin() + 16 * m_sps);
                m_sink(typ, {});
                handled = true;
            }
            break;
        default:
            break;
    }

    if (!handled)
        m_symbols.erase(m_symbols.begin());
}

}  // namespace sx1255::m17

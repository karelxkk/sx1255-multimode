#pragma once

#include "sx1255/m17/M17Constants.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace sx1255::m17 {

// SyncDistance port (codec.go): returns (euclidean distance, sync type).
std::pair<float, std::uint16_t> syncDistance(const std::vector<Symbol>& symbols,
                                             int offset, int sps);

// Port of processSymbolStream + extractPayload (modem/modem.go).  Feeds the
// same sliding-window detection loop as m17-gateway and emits soft bits for
// every detected sync burst via the frame sink.
class M17Synchronizer {
public:
    using FrameSink = std::function<void(std::uint16_t typ, const std::vector<SoftBit>& softBits)>;

    M17Synchronizer(int sps, FrameSink sink);

    void push(float sample);
    void reset();

    std::size_t bufferedSamples() const { return m_symbols.size(); }

private:
    void extractPayload(float dist, std::uint16_t typ);

    int m_sps;
    FrameSink m_sink;
    std::vector<Symbol> m_symbols;
    std::size_t m_bufSize;
};

}  // namespace sx1255::m17

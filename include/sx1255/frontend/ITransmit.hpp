#pragma once

#include "sx1255/Types.hpp"

#include <cstddef>

namespace sx1255 {

// ---------------------------------------------------------------------------
// ITransmit
//
// The only transmit API plugins are allowed to use.  It deliberately exposes
// frequency, PTT and raw IQ output -- and nothing that is mode-specific.
// Each plugin builds its own modulation chain on top of these primitives.
// ---------------------------------------------------------------------------
class ITransmit {
public:
    virtual ~ITransmit() = default;

    virtual bool setTxFrequency(Hertz hz) = 0;
    virtual bool setPtt(bool on) = 0;

    // Queue interleaved IQ samples for transmission.  May block until space
    // is available in the hardware buffer.
    virtual bool writeIQ(const ComplexFloat* samples, std::size_t count) = 0;

    // Wait until all queued TX samples have actually played out.  Default:
    // no-op for transmit sinks that have no hardware buffer.
    virtual bool drainTx() { return true; }

    virtual bool isReady() const = 0;
};

}  // namespace sx1255

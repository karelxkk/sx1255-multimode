#pragma once

#include "sx1255/Types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace sx1255 {

// ---------------------------------------------------------------------------
// ISX1255
//
// Thin hardware abstraction for the SX1255 HAT.  It knows about RF control
// (frequency, power, PTT, RSSI, squelch) and raw IQ transport -- and nothing
// about any protocol.  Concrete implementations can talk to real hardware,
// to a file for offline replay, or to a synthetic source for tests.
// ---------------------------------------------------------------------------
class ISX1255 {
public:
    virtual ~ISX1255() = default;

    virtual std::string name() const = 0;

    virtual bool open()  = 0;
    virtual void close() = 0;

    virtual bool setRxFrequency(Hertz hz) = 0;
    virtual bool setTxFrequency(Hertz hz) = 0;
    virtual bool setPower(dBm power)       = 0;

    virtual bool setPtt(bool on) = 0;

    virtual double readRssi()               = 0;  // dBm
    virtual bool   setSquelch(dBm level)    = 0;

    // Receive: append up to maxSamples samples into out and return the number
    // of samples appended.  Returns false on a non-recoverable error.
    virtual bool readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples) = 0;

    // Transmit: queue interleaved IQ samples.
    virtual bool writeIQ(const ComplexFloat* samples, std::size_t count) = 0;

    // Wait until queued TX samples have played out (hardware drain).
    virtual bool drainTx() { return true; }

    // True once a finite source (e.g. an IQ replay file) is exhausted.  Live
    // hardware and synthetic sources return false, so the capture loop keeps
    // running.
    virtual bool eof() const { return false; }
};

}  // namespace sx1255

#pragma once

#include "sx1255/frontend/ISX1255.hpp"

#include <atomic>

namespace sx1255 {

// ---------------------------------------------------------------------------
// NullSX1255
//
// A silent synthetic frontend used by tests, by the simulator, and as the
// default device when no SX1255 hardware is present.  It produces interleaved
// zero IQ (no carrier) and accepts (discards) transmit samples.
// ---------------------------------------------------------------------------
class NullSX1255 final : public ISX1255 {
public:
    NullSX1255() = default;

    std::string name() const override;

    bool open() override;
    void close() override;

    bool setRxFrequency(Hertz hz) override;
    bool setTxFrequency(Hertz hz) override;
    bool setPower(dBm power) override;

    bool setPtt(bool on) override;

    double readRssi() override;
    bool   setSquelch(dBm level) override;

    bool readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples) override;
    bool writeIQ(const ComplexFloat* samples, std::size_t count) override;

    // Test/observability helpers.
    bool   ptt() const;
    Hertz  rxFrequency() const;
    Hertz  txFrequency() const;
    dBm    power() const;

private:
    std::atomic<bool>  m_open{false};
    std::atomic<bool>  m_ptt{false};
    std::atomic<Hertz> m_rxFreq{0};
    std::atomic<Hertz> m_txFreq{0};
    std::atomic<dBm>   m_power{0};
    std::atomic<dBm>   m_squelch{-120};
};

}  // namespace sx1255

#pragma once

#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/frontend/ISX1255.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace sx1255::debug {

// ---------------------------------------------------------------------------
// IQReplay
//
// An ISX1255 backend that replays a captured IQ file without any radio
// hardware.  RF control methods are accepted and remembered but have no
// physical effect; the receive path streams the file at whatever speed the
// consumer drains it, which makes replay fully deterministic (sample order
// is preserved and no wall-clock timing influences detection).
// ---------------------------------------------------------------------------
class IQReplay final : public ISX1255 {
public:
    explicit IQReplay(std::string file);
    ~IQReplay() override;

    IQReplay(const IQReplay&) = delete;
    IQReplay& operator=(const IQReplay&) = delete;

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

    bool eof() const override;

    std::uint32_t sampleRateHz() const;
    std::uint32_t frequencyHz() const;
    std::uint64_t totalSamples() const;
    std::uint64_t replayedSamples() const;
    const std::string& file() const;

private:
    std::string   m_file;
    IQFileHeader  m_header;
    std::ifstream m_in;
    std::uint64_t m_total = 0;
    std::uint64_t m_replayed = 0;
    bool          m_eof = false;
    Hertz         m_rxFreq = 0;
    Hertz         m_txFreq = 0;
};

}  // namespace sx1255::debug

#pragma once

#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/frontend/ISX1255.hpp"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

namespace sx1255::debug {

// ---------------------------------------------------------------------------
// IQRecorder
//
// A transparent ISX1255 decorator that records every receive sample flowing
// through readIQ() into an IQ file while delegating all RF control to the
// wrapped hardware.  It contains no protocol knowledge and does not alter the
// stream: whatever the dispatcher sees is exactly what lands in the file.
// ---------------------------------------------------------------------------
class IQRecorder final : public ISX1255 {
public:
    IQRecorder(std::unique_ptr<ISX1255> source, std::string file);
    ~IQRecorder() override;

    IQRecorder(const IQRecorder&) = delete;
    IQRecorder& operator=(const IQRecorder&) = delete;

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

    bool         isRecording() const;
    std::uint64_t capturedSamples() const;
    const std::string& file() const;

private:
    std::unique_ptr<ISX1255> m_source;
    std::string              m_file;
    IQFileHeader             m_header;
    std::ofstream            m_out;
    std::uint64_t            m_captured = 0;
};

}  // namespace sx1255::debug

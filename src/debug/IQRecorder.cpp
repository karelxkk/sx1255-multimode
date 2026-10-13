#include "sx1255/debug/IQRecorder.hpp"

#include <chrono>
#include <utility>

namespace sx1255::debug {

IQRecorder::IQRecorder(std::unique_ptr<ISX1255> source, std::string file)
    : m_source(std::move(source))
    , m_file(std::move(file))
{
}

IQRecorder::~IQRecorder()
{
    close();
}

std::string IQRecorder::name() const
{
    return m_source->name() + "+recorder";
}

bool IQRecorder::open()
{
    if (!m_source->open())
        return false;

    m_header = IQFileHeader{};
    m_header.sampleRateHz = 125000;
    m_header.frequencyHz  = 0;
    m_header.timestampSec = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    m_header.sampleCount = 0;
    m_captured = 0;

    m_out.open(m_file, std::ios::binary | std::ios::trunc);
    if (!m_out.is_open() || !writeIQHeader(m_out, m_header)) {
        m_out.close();
        m_source->close();
        return false;
    }
    return true;
}

void IQRecorder::close()
{
    if (m_out.is_open()) {
        // Patch the final RX frequency and sample count into their fixed
        // header locations (both may change after the header was first
        // written).
        m_out.seekp(static_cast<std::streamoff>(kIqFrequencyOffset), std::ios::beg);
        writeU32Le(m_out, m_header.frequencyHz);
        m_out.seekp(static_cast<std::streamoff>(kIqSampleCountOffset), std::ios::beg);
        writeU64Le(m_out, m_captured);
        m_out.flush();
        m_out.close();
    }
    m_source->close();
}

bool IQRecorder::setRxFrequency(Hertz hz)
{
    m_header.frequencyHz = hz;
    return m_source->setRxFrequency(hz);
}

bool IQRecorder::setTxFrequency(Hertz hz)
{
    return m_source->setTxFrequency(hz);
}

bool IQRecorder::setPower(dBm power)
{
    return m_source->setPower(power);
}

bool IQRecorder::setPtt(bool on)
{
    return m_source->setPtt(on);
}

double IQRecorder::readRssi()
{
    return m_source->readRssi();
}

bool IQRecorder::setSquelch(dBm level)
{
    return m_source->setSquelch(level);
}

bool IQRecorder::readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples)
{
    const std::size_t before = out.size();
    const bool ok = m_source->readIQ(out, maxSamples);
    const std::size_t added = out.size() - before;

    if (added > 0 && m_out.is_open()) {
        if (!writeIQSamples(m_out, out.data() + before, added)) {
            m_out.close();
            return false;
        }
        m_captured += added;
    }

    return ok;
}

bool IQRecorder::writeIQ(const ComplexFloat* samples, std::size_t count)
{
    return m_source->writeIQ(samples, count);
}

bool IQRecorder::isRecording() const
{
    return m_out.is_open();
}

std::uint64_t IQRecorder::capturedSamples() const
{
    return m_captured;
}

const std::string& IQRecorder::file() const
{
    return m_file;
}

}  // namespace sx1255::debug

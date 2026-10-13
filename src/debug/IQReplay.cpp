#include "sx1255/debug/IQReplay.hpp"

#include <utility>

namespace sx1255::debug {

IQReplay::IQReplay(std::string file)
    : m_file(std::move(file))
{
}

IQReplay::~IQReplay()
{
    close();
}

std::string IQReplay::name() const
{
    return "iq-replay";
}

bool IQReplay::open()
{
    m_in.open(m_file, std::ios::binary);
    if (!m_in.is_open())
        return false;

    if (!readIQHeader(m_in, m_header) || !validateIQHeader(m_header)) {
        m_in.close();
        return false;
    }

    // A streamed recording may have sampleCount == 0 in the header; derive the
    // count from the remaining file length instead.
    if (m_header.sampleCount != 0) {
        m_total = m_header.sampleCount;
    } else {
        const auto pos = m_in.tellg();
        m_in.seekg(0, std::ios::end);
        const auto end = m_in.tellg();
        m_in.seekg(pos, std::ios::beg);
        const std::streamoff bytes = end - pos;
        m_total = bytes > 0
            ? static_cast<std::uint64_t>(bytes) / (2 * sizeof(float))
            : 0;
    }

    m_replayed = 0;
    m_eof = false;
    return true;
}

void IQReplay::close()
{
    if (m_in.is_open())
        m_in.close();
    m_eof = true;
}

bool IQReplay::setRxFrequency(Hertz hz)
{
    m_rxFreq = hz;
    return true;
}

bool IQReplay::setTxFrequency(Hertz hz)
{
    m_txFreq = hz;
    return true;
}

bool IQReplay::setPower(dBm)
{
    return true;
}

bool IQReplay::setPtt(bool)
{
    return true;
}

double IQReplay::readRssi()
{
    return 0.0;
}

bool IQReplay::setSquelch(dBm)
{
    return true;
}

bool IQReplay::readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples)
{
    if (!m_in.is_open() || m_eof)
        return false;

    const std::size_t before = out.size();
    out.resize(before + maxSamples);
    const std::size_t n = readIQSamples(m_in, out.data() + before, maxSamples);
    out.resize(before + n);
    m_replayed += n;

    if (n == 0)
        m_eof = true;
    return n > 0;
}

bool IQReplay::writeIQ(const ComplexFloat*, std::size_t)
{
    return true;  // offline replay: TX is discarded
}

bool IQReplay::eof() const
{
    return m_eof;
}

std::uint32_t IQReplay::sampleRateHz() const
{
    return m_header.sampleRateHz;
}

std::uint32_t IQReplay::frequencyHz() const
{
    return m_header.frequencyHz;
}

std::uint64_t IQReplay::totalSamples() const
{
    return m_total;
}

std::uint64_t IQReplay::replayedSamples() const
{
    return m_replayed;
}

const std::string& IQReplay::file() const
{
    return m_file;
}

}  // namespace sx1255::debug

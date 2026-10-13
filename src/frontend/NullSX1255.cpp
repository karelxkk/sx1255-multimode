#include "sx1255/frontend/NullSX1255.hpp"

namespace sx1255 {

std::string NullSX1255::name() const
{
    return "null-sx1255";
}

bool NullSX1255::open()
{
    m_open = true;
    return true;
}

void NullSX1255::close()
{
    m_open = false;
    m_ptt = false;
}

bool NullSX1255::setRxFrequency(Hertz hz)
{
    m_rxFreq = hz;
    return true;
}

bool NullSX1255::setTxFrequency(Hertz hz)
{
    m_txFreq = hz;
    return true;
}

bool NullSX1255::setPower(dBm power)
{
    m_power = power;
    return true;
}

bool NullSX1255::setPtt(bool on)
{
    m_ptt = on;
    return true;
}

double NullSX1255::readRssi()
{
    return -120.0;
}

bool NullSX1255::setSquelch(dBm level)
{
    m_squelch = level;
    return true;
}

bool NullSX1255::readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples)
{
    if (!m_open.load())
        return false;

    const std::size_t before = out.size();
    out.resize(before + maxSamples, ComplexFloat{0.0F, 0.0F});
    return true;
}

bool NullSX1255::writeIQ(const ComplexFloat*, std::size_t)
{
    return true;  // discard
}

bool NullSX1255::ptt() const
{
    return m_ptt.load();
}

Hertz NullSX1255::rxFrequency() const
{
    return m_rxFreq.load();
}

Hertz NullSX1255::txFrequency() const
{
    return m_txFreq.load();
}

dBm NullSX1255::power() const
{
    return m_power.load();
}

}  // namespace sx1255

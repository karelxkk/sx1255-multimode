#include "sx1255/fm/FMDsp.hpp"

#include <cmath>

namespace sx1255::fm {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

float FmDiscriminator::process(ComplexFloat sample)
{
    if (!m_hasPrev) {
        m_prevI = sample.i;
        m_prevQ = sample.q;
        m_hasPrev = true;
        return 0.0F;
    }

    const float dot   = sample.i * m_prevI + sample.q * m_prevQ;
    const float cross = sample.q * m_prevI - sample.i * m_prevQ;
    m_prevI = sample.i;
    m_prevQ = sample.q;
    return static_cast<float>(std::atan2(cross, dot));
}

void FmDiscriminator::reset()
{
    m_hasPrev = false;
    m_prevI = 0.0F;
    m_prevQ = 0.0F;
}

DeEmphasis::DeEmphasis(double tauSec, double sampleRateHz)
{
    const double dt = 1.0 / sampleRateHz;
    m_alpha = dt / (tauSec + dt);
}

float DeEmphasis::process(float x)
{
    m_y = m_y + m_alpha * (static_cast<double>(x) - m_y);
    return static_cast<float>(m_y);
}

void DeEmphasis::reset()
{
    m_y = 0.0;
}

PreEmphasis::PreEmphasis(double tauSec, double sampleRateHz)
{
    m_a = std::exp(-1.0 / (tauSec * sampleRateHz));
}

float PreEmphasis::process(float x)
{
    const double y = static_cast<double>(x) - m_a * m_xPrev;
    m_xPrev = static_cast<double>(x);
    return static_cast<float>(y);
}

void PreEmphasis::reset()
{
    m_xPrev = 0.0;
}

DcBlocker::DcBlocker(double r)
    : m_r(r)
{
}

float DcBlocker::process(float x)
{
    const double y = static_cast<double>(x) - m_xPrev + m_r * m_yPrev;
    m_xPrev = static_cast<double>(x);
    m_yPrev = y;
    return static_cast<float>(y);
}

void DcBlocker::reset()
{
    m_xPrev = 0.0;
    m_yPrev = 0.0;
}

FmModulator::FmModulator(double sensitivity)
    : m_sensitivity(sensitivity)
{
}

ComplexFloat FmModulator::process(float audio)
{
    m_phase += m_sensitivity * static_cast<double>(audio);
    if (m_phase > kPi)
        m_phase -= 2.0 * kPi;
    else if (m_phase < -kPi)
        m_phase += 2.0 * kPi;
    return {static_cast<float>(std::cos(m_phase)),
            static_cast<float>(std::sin(m_phase))};
}

void FmModulator::reset()
{
    m_phase = 0.0;
}

CtcssGenerator::CtcssGenerator(double toneHz, double deviationHz, double sampleRateHz)
{
    m_step = 2.0 * kPi * toneHz / sampleRateHz;
    m_amplitude = 2.0 * kPi * deviationHz / sampleRateHz;
}

float CtcssGenerator::process()
{
    m_phase += m_step;
    if (m_phase > kPi)
        m_phase -= 2.0 * kPi;
    return static_cast<float>(m_amplitude * std::sin(m_phase));
}

void CtcssGenerator::reset()
{
    m_phase = 0.0;
}

}  // namespace sx1255::fm

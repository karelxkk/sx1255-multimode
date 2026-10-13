#include "sx1255/fm/Resampler.hpp"

namespace sx1255::fm {

LinearResampler::LinearResampler(double outputRate, double inputRate)
    : m_step(inputRate / outputRate)
{
}

std::size_t LinearResampler::process(const float* input, std::size_t count,
                                     std::vector<float>& output)
{
    // Streaming linear interpolation.  m_inPos tracks the absolute position
    // of the last consumed input sample; m_outPos is the position of the next
    // output sample (both measured in input samples).  This works for both
    // batch and one-sample-at-a-time feeding.
    std::size_t added = 0;
    for (std::size_t i = 0; i < count; ++i) {
        m_inPos += 1.0;
        const double leftPos = m_inPos - 1.0;

        while (m_outPos <= m_inPos) {
            const double raw = m_outPos - leftPos;
            const double frac = raw < 0.0 ? 0.0 : (raw > 1.0 ? 1.0 : raw);
            const float sample = m_prev + static_cast<float>((input[i] - m_prev) * frac);
            output.push_back(sample);
            ++added;
            m_outPos += m_step;
        }
        m_prev = input[i];
    }
    return added;
}

void LinearResampler::reset()
{
    m_outPos = 0.0;
    m_inPos = 0.0;
    m_prev = 0.0F;
}

}  // namespace sx1255::fm

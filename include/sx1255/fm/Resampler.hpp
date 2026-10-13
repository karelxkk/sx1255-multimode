#pragma once

#include <cstddef>
#include <vector>

namespace sx1255::fm {

// Streaming linear-interpolation sample-rate converter used for the SVXLink
// audio bridge (125 kSa/s <-> 16 kSa/s).  Deterministic and allocation-free.
class LinearResampler {
public:
    LinearResampler(double outputRate, double inputRate);

    // Resample `input` and append the result to `output`; returns the number
    // of output samples appended.
    std::size_t process(const float* input, std::size_t count, std::vector<float>& output);

    void reset();

private:
    double m_step;
    double m_outPos = 0.0;   // position (input samples) of next output sample
    double m_inPos  = 0.0;   // position (input samples) of last input sample
    float  m_prev = 0.0F;
};

}  // namespace sx1255::fm

#include "sx1255/dsp/Goertzel.hpp"

#include <cmath>

namespace sx1255 {

double goertzelPower(const float* samples, std::size_t count,
                     double targetHz, double sampleRateHz)
{
    if (samples == nullptr || count == 0)
        return 0.0;

    constexpr double kPi = 3.14159265358979323846;
    const double omega = 2.0 * kPi * targetHz / sampleRateHz;
    const double coeff = 2.0 * std::cos(omega);

    double sPrev = 0.0;
    double sPrev2 = 0.0;

    for (std::size_t n = 0; n < count; ++n) {
        const double s = static_cast<double>(samples[n]) + coeff * sPrev - sPrev2;
        sPrev2 = sPrev;
        sPrev = s;
    }

    return sPrev * sPrev + sPrev2 * sPrev2 - coeff * sPrev * sPrev2;
}

}  // namespace sx1255

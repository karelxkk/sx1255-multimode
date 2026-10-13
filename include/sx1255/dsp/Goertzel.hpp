#pragma once

#include <cstddef>

namespace sx1255 {

// Power of a single DFT bin at targetHz, computed with the Goertzel
// algorithm.  Used by the FM plugin for CTCSS tone detection.
double goertzelPower(const float* samples, std::size_t count,
                     double targetHz, double sampleRateHz);

}  // namespace sx1255

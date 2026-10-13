#pragma once

#include "sx1255/Types.hpp"

#include <complex>
#include <cstddef>
#include <vector>

namespace sx1255::spectrum {

// ---------------------------------------------------------------------------
// Minimal, dependency-free power-of-two FFT used by the spectrum monitor.
// The SX1255 IQ stream is complex, so the transform handles complex input
// directly (no real-input packing tricks).
// ---------------------------------------------------------------------------

// In-place radix-2 Cooley-Tukey FFT.  n must be a power of two.
void fftInPlace(std::vector<std::complex<float>>& data);

// Multiply a real buffer by a Hann window (periodic form, symmetric enough
// for spectral monitoring).
void applyHann(std::vector<float>& samples);

// Compute the one-sided power spectral density of an IQ window.  Returns
// bins 0..n/2 with power in linear units (peak ≈ A^2/4 for a tone of
// amplitude A).  n must be a power of two.
std::vector<float> powerSpectrum(const ComplexFloat* iq, std::size_t n,
                                 bool windowed = true);

// Center frequency (Hz) of the given FFT bin for the given sample rate.
double binFrequencyHz(std::size_t bin, double sampleRateHz, std::size_t fftSize);

// Power (linear) to dB, clamped to a minimum noise floor.
float powerToDb(float linearPower);

}  // namespace sx1255::spectrum

#pragma once

#include "sx1255/Types.hpp"

#include <cstddef>

namespace sx1255::fm {

// Standard narrowband FM pre/de-emphasis time constants (ported from the
// original sx1255-fm-repeater defaults).
inline constexpr double kDeemphTau   = 75e-6;    //  75 us RX de-emphasis
inline constexpr double kPreemphTau  = 75e-6;    //  75 us TX pre-emphasis
inline constexpr double kCtcssDevHz  = 500.0;    // CTCSS deviation on TX

// ---------------------------------------------------------------------------
// FmDiscriminator
//
// Instantaneous frequency estimator: atan2(cross, dot) between consecutive IQ
// samples.  Output is in radians per sample (proportional to RF deviation).
// ---------------------------------------------------------------------------
class FmDiscriminator {
public:
    float process(ComplexFloat sample);
    void reset();

private:
    bool  m_hasPrev = false;
    float m_prevI = 0.0F;
    float m_prevQ = 0.0F;
};

// One-pole low-pass used as the FM de-emphasis filter.
class DeEmphasis {
public:
    DeEmphasis(double tauSec, double sampleRateHz);
    float process(float x);
    void reset();

private:
    double m_alpha;
    double m_y = 0.0;
};

// First-order high-pass used as the FM pre-emphasis filter.
class PreEmphasis {
public:
    PreEmphasis(double tauSec, double sampleRateHz);
    float process(float x);
    void reset();

private:
    double m_a;
    double m_xPrev = 0.0;
};

// First-order DC blocker for the TX audio path.
class DcBlocker {
public:
    explicit DcBlocker(double r = 0.995);
    float process(float x);
    void reset();

private:
    double m_r;
    double m_xPrev = 0.0;
    double m_yPrev = 0.0;
};

// ---------------------------------------------------------------------------
// FmModulator
//
// Direct phase-accumulator FM modulator.  The input is in radians/sample and
// sensitivity defaults to 1.0, so a transparent discriminator->modulator
// bridge preserves the received deviation.
// ---------------------------------------------------------------------------
class FmModulator {
public:
    explicit FmModulator(double sensitivity = 1.0);
    ComplexFloat process(float audio);
    void reset();

private:
    double m_sensitivity;
    double m_phase = 0.0;
};

// CTCSS sub-audio tone generator.  Output is in radians/sample.
class CtcssGenerator {
public:
    CtcssGenerator(double toneHz, double deviationHz, double sampleRateHz);
    float process();
    void reset();

private:
    double m_step;
    double m_amplitude;
    double m_phase = 0.0;
};

}  // namespace sx1255::fm

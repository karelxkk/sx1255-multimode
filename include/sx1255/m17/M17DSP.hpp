#pragma once

#include "sx1255/m17/M17Constants.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace sx1255::m17 {

// Double-precision complex sample, matching Go's complex128.
struct CD {
    double re = 0.0;
    double im = 0.0;
};

std::vector<double> designLowpassFIR(int numTaps, double cutoffNorm, int interpFactor);
std::vector<double> designChannelFilter(double channelBWHz, double sampleRate, int numTaps);

// 1:1 ports of the Go RX blocks (modem/sx1255_dsp.go, modem/transform.go).
class ComplexDCRemoval {
public:
    explicit ComplexDCRemoval(double alpha);
    CD process(CD sample);
private:
    double m_avgI = 0.0, m_avgQ = 0.0, m_alpha;
};

class PolyphaseDecimator {
public:
    PolyphaseDecimator(std::vector<double> taps, int decimFactor);
    // Returns a view into an internal fixed buffer (valid until the next
    // process() call).  Avoids per-sample heap allocations on the RX path.
    std::span<const CD> process(CD sample);
private:
    int m_decimFactor;
    std::vector<std::vector<double>> m_phases;
    std::vector<CD> m_buffer;
    int m_bufIdx = 0;
    int m_count = 0;
    std::array<CD, 4> m_out{};
    std::size_t m_outCount = 0;
};

class FMDemodulator {
public:
    FMDemodulator();
    double process(CD sample);
private:
    CD m_prev{1.0, 0.0};
};

class FMDemodAFC {
public:
    FMDemodAFC(int avgCnt, double fastN, double slowN);
    double process(CD sample);
private:
    CD m_prev{1.0, 0.0};
    double m_fast = 0.0, m_slow = 0.0, m_before = 0.0, m_warm = 0.0;
    double m_offset = 0.0;
    bool m_carrier = false;
    int m_n = 0, m_avgCnt;
    double m_fastN, m_slowN;
};

class RationalResampler {
public:
    RationalResampler(int interpFactor, int decimFactor);
    std::span<const double> process(double sample);
private:
    int m_interpFactor, m_decimFactor;
    std::vector<std::vector<double>> m_phases;
    std::vector<double> m_buffer;
    int m_bufIdx = 0, m_outPhase = 0;
    std::array<double, 16> m_out{};
    std::size_t m_outCount = 0;
};

// RRC matched filter (SampleToSymbol).  Output is float32, as in Go.
class SampleToSymbol {
public:
    SampleToSymbol(std::vector<double> taps, double scalingCoeff);
    float process(double sample);
private:
    std::vector<double> m_taps;
    double m_scalingCoeff;
    std::vector<double> m_buffer;
    int m_idx = 0;
};

// --- Batch TX blocks (sx1255_dsp.go) ---
class TXPulseShaper {
public:
    TXPulseShaper(std::vector<double> taps, int samplesPerSymbol);
    void reset();
    std::vector<double> process(const std::vector<Symbol>& symbols);
private:
    std::vector<double> m_taps;
    int m_sps;
    std::vector<double> m_delay;
    int m_delayIdx = 0;
};

class BatchResampler {
public:
    BatchResampler(int interpFactor, int decimFactor);
    void reset();
    std::vector<double> process(const std::vector<double>& input);
private:
    int m_interpFactor, m_decimFactor;
    std::vector<std::vector<double>> m_phases;
    std::vector<double> m_buffer;
    int m_bufIdx = 0, m_outPhase = 0;
};

class BatchFMModulator {
public:
    explicit BatchFMModulator(double sampleRate);
    void reset();
    std::vector<CD> modulate(const std::vector<double>& baseband, double deviationHz);
private:
    double m_phase = 0.0, m_sampleRate;
};

// Convenience wrappers used by tests and the plugin.
// Full RX chain: 125 kSa/s IQ -> RRC matched-filter samples (5 sps).
std::vector<float> runRXChain(const std::vector<CD>& iq);
// Full TX chain: M17 symbols -> 125 kSa/s IQ.
std::vector<CD> modulateSymbols(const std::vector<Symbol>& symbols);

}  // namespace sx1255::m17

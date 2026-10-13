#include "sx1255/m17/M17DSP.hpp"

#include <algorithm>
#include <cmath>

namespace sx1255::m17 {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

std::vector<double> designLowpassFIR(int numTaps, double cutoffNorm, int interpFactor)
{
    if (numTaps % interpFactor != 0)
        numTaps += interpFactor - numTaps % interpFactor;

    std::vector<double> taps(static_cast<std::size_t>(numTaps));
    const double center = static_cast<double>(numTaps - 1) / 2.0;
    const double wc = cutoffNorm * kPi;
    const double denom = static_cast<double>(numTaps - 1);

    for (int i = 0; i < numTaps; ++i) {
        const double n = static_cast<double>(i) - center;
        const double h = std::abs(n) < 1e-10 ? wc / kPi : std::sin(wc * n) / (kPi * n);
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / denom)
                               + 0.08 * std::cos(4.0 * kPi * static_cast<double>(i) / denom);
        taps[static_cast<std::size_t>(i)] = h * w * static_cast<double>(interpFactor);
    }
    return taps;
}

std::vector<double> designChannelFilter(double channelBWHz, double sampleRate, int numTaps)
{
    return designLowpassFIR(numTaps, channelBWHz / sampleRate, 1);
}

ComplexDCRemoval::ComplexDCRemoval(double alpha) : m_alpha(alpha) {}

CD ComplexDCRemoval::process(CD sample)
{
    const double i = sample.re;
    const double q = sample.im;
    m_avgI = m_alpha * m_avgI + (1.0 - m_alpha) * i;
    m_avgQ = m_alpha * m_avgQ + (1.0 - m_alpha) * q;
    return {i - m_avgI, q - m_avgQ};
}

PolyphaseDecimator::PolyphaseDecimator(std::vector<double> taps, int decimFactor)
    : m_decimFactor(decimFactor)
{
    if (static_cast<int>(taps.size()) % decimFactor != 0) {
        const int pad = decimFactor - static_cast<int>(taps.size()) % decimFactor;
        taps.resize(taps.size() + static_cast<std::size_t>(pad), 0.0);
    }

    const int tapsPerPhase = static_cast<int>(taps.size()) / decimFactor;
    m_phases.resize(static_cast<std::size_t>(decimFactor));
    for (int p = 0; p < decimFactor; ++p) {
        m_phases[static_cast<std::size_t>(p)].resize(static_cast<std::size_t>(tapsPerPhase));
        for (int t = 0; t < tapsPerPhase; ++t)
            m_phases[static_cast<std::size_t>(p)][static_cast<std::size_t>(t)] =
                taps[static_cast<std::size_t>(t * decimFactor + p)];
    }
    m_buffer.resize(taps.size(), CD{});
}

std::span<const CD> PolyphaseDecimator::process(CD sample)
{
    const int L = static_cast<int>(m_buffer.size());
    m_buffer[static_cast<std::size_t>(m_bufIdx)] = sample;
    m_bufIdx = (m_bufIdx + 1) % L;
    m_count++;

    if (m_count % m_decimFactor != 0) {
        m_outCount = 0;
        return {m_out.data(), m_outCount};
    }

    const int M = m_decimFactor;
    const int tapsPerPhase = static_cast<int>(m_phases[0].size());
    CD acc{};

    for (int p = 0; p < M; ++p) {
        int startIdx = m_bufIdx - M + p;
        if (startIdx < 0)
            startIdx += L;
        for (int t = 0; t < tapsPerPhase; ++t) {
            int idx = startIdx - t * M;
            while (idx < 0)
                idx += L;
            acc.re += m_buffer[static_cast<std::size_t>(idx)].re
                      * m_phases[static_cast<std::size_t>(p)][static_cast<std::size_t>(t)];
            acc.im += m_buffer[static_cast<std::size_t>(idx)].im
                      * m_phases[static_cast<std::size_t>(p)][static_cast<std::size_t>(t)];
        }
    }

    m_out[0] = acc;
    m_outCount = 1;
    return {m_out.data(), m_outCount};
}

FMDemodulator::FMDemodulator() = default;

double FMDemodulator::process(CD sample)
{
    // sample * conj(prev)
    const double re = sample.re * m_prev.re + sample.im * m_prev.im;
    const double im = sample.im * m_prev.re - sample.re * m_prev.im;
    m_prev = sample;
    return std::atan2(im, re);
}

FMDemodAFC::FMDemodAFC(int avgCnt, double fastN, double slowN)
    : m_avgCnt(avgCnt), m_fastN(fastN), m_slowN(slowN)
{
}

double FMDemodAFC::process(CD sample)
{
    const double re = sample.re * m_prev.re + sample.im * m_prev.im;
    const double im = sample.im * m_prev.re - sample.re * m_prev.im;
    const double d = std::atan2(im, re);
    m_prev = sample;

    if (m_avgCnt < 2)
        return d;

    const double p = sample.re * sample.re + sample.im * sample.im;
    m_fast += (p - m_fast) / m_fastN;
    m_slow += (p - m_slow) / m_slowN;
    if (m_warm < m_slowN)
        m_warm++;

    if (!m_carrier && m_warm >= m_slowN && m_fast > carrierRiseSX1255 * m_slow) {
        m_carrier = true;
        m_before = m_slow;
        m_offset = 0.0;
        m_n = 0;
    } else if (m_carrier && m_fast < carrierFallSX1255 * m_before) {
        m_carrier = false;
    }

    m_n++;
    m_offset += (d - m_offset) / static_cast<double>(std::min(m_n, m_avgCnt));
    return d - m_offset;
}

RationalResampler::RationalResampler(int interpFactor, int decimFactor)
    : m_interpFactor(interpFactor), m_decimFactor(decimFactor)
{
    const double cutoffNorm = 1.0 / static_cast<double>(std::max(interpFactor, decimFactor));
    int tapsPerPhase = 24;
    std::vector<double> taps = designLowpassFIR(interpFactor * tapsPerPhase, cutoffNorm, interpFactor);
    tapsPerPhase = static_cast<int>(taps.size()) / interpFactor;

    m_phases.resize(static_cast<std::size_t>(interpFactor));
    for (int p = 0; p < interpFactor; ++p) {
        m_phases[static_cast<std::size_t>(p)].resize(static_cast<std::size_t>(tapsPerPhase));
        for (int t = 0; t < tapsPerPhase; ++t)
            m_phases[static_cast<std::size_t>(p)][static_cast<std::size_t>(t)] =
                taps[static_cast<std::size_t>(t * interpFactor + p)];
    }
    m_buffer.resize(static_cast<std::size_t>(tapsPerPhase), 0.0);
}

std::span<const double> RationalResampler::process(double sample)
{
    const int L = static_cast<int>(m_buffer.size());
    m_buffer[static_cast<std::size_t>(m_bufIdx)] = sample;
    m_bufIdx = (m_bufIdx + 1) % L;

    m_outCount = 0;
    while (m_outPhase < m_interpFactor) {
        const int phase = m_outPhase;
        double acc = 0.0;
        int idx = m_bufIdx;
        for (int t = 0; t < L; ++t) {
            idx--;
            if (idx < 0)
                idx = L - 1;
            acc += m_buffer[static_cast<std::size_t>(idx)]
                   * m_phases[static_cast<std::size_t>(phase)][static_cast<std::size_t>(t)];
        }
        if (m_outCount < m_out.size())
            m_out[m_outCount++] = acc;
        m_outPhase += m_decimFactor;
    }
    m_outPhase -= m_interpFactor;
    return {m_out.data(), m_outCount};
}

SampleToSymbol::SampleToSymbol(std::vector<double> taps, double scalingCoeff)
    : m_taps(std::move(taps)), m_scalingCoeff(scalingCoeff), m_buffer(m_taps.size(), 0.0)
{
}

float SampleToSymbol::process(double sample)
{
    const int L = static_cast<int>(m_buffer.size());
    m_buffer[static_cast<std::size_t>(m_idx)] = sample;
    m_idx = (m_idx + 1) % L;

    double acc = 0.0;
    int p = m_idx;
    for (int t = 0; t < L; ++t) {
        acc += m_taps[static_cast<std::size_t>(t)] * m_buffer[static_cast<std::size_t>(p)];
        p = (p + 1) % L;
    }
    acc *= m_scalingCoeff;
    return static_cast<float>(acc);
}

TXPulseShaper::TXPulseShaper(std::vector<double> taps, int samplesPerSymbol)
    : m_taps(std::move(taps)), m_sps(samplesPerSymbol), m_delay(m_taps.size(), 0.0)
{
}

void TXPulseShaper::reset()
{
    std::fill(m_delay.begin(), m_delay.end(), 0.0);
    m_delayIdx = 0;
}

std::vector<double> TXPulseShaper::process(const std::vector<Symbol>& symbols)
{
    const int L = static_cast<int>(m_delay.size());
    std::vector<double> out;
    out.reserve(symbols.size() * static_cast<std::size_t>(m_sps));

    for (const Symbol sym : symbols) {
        for (int j = 0; j < m_sps; ++j) {
            m_delay[static_cast<std::size_t>(m_delayIdx)] = (j == 0) ? static_cast<double>(sym) : 0.0;
            m_delayIdx = (m_delayIdx + 1) % L;

            double acc = 0.0;
            int idx = m_delayIdx;
            for (int t = 0; t < L; ++t) {
                idx--;
                if (idx < 0)
                    idx = L - 1;
                acc += m_delay[static_cast<std::size_t>(idx)] * m_taps[static_cast<std::size_t>(t)];
            }
            out.push_back(acc);
        }
    }
    return out;
}

BatchResampler::BatchResampler(int interpFactor, int decimFactor)
    : m_interpFactor(interpFactor), m_decimFactor(decimFactor)
{
    const double cutoffNorm = 1.0 / static_cast<double>(std::max(interpFactor, decimFactor));
    const int tapsPerPhase = 24;
    std::vector<double> taps = designLowpassFIR(interpFactor * tapsPerPhase, cutoffNorm, interpFactor);
    const int tpp = static_cast<int>(taps.size()) / interpFactor;

    m_phases.resize(static_cast<std::size_t>(interpFactor));
    for (int p = 0; p < interpFactor; ++p) {
        m_phases[static_cast<std::size_t>(p)].resize(static_cast<std::size_t>(tpp));
        for (int t = 0; t < tpp; ++t)
            m_phases[static_cast<std::size_t>(p)][static_cast<std::size_t>(t)] =
                taps[static_cast<std::size_t>(t * interpFactor + p)];
    }
    m_buffer.resize(static_cast<std::size_t>(tpp), 0.0);
}

void BatchResampler::reset()
{
    std::fill(m_buffer.begin(), m_buffer.end(), 0.0);
    m_bufIdx = 0;
    m_outPhase = 0;
}

std::vector<double> BatchResampler::process(const std::vector<double>& input)
{
    const int L = static_cast<int>(m_buffer.size());
    std::vector<double> out;
    out.reserve(input.size() * static_cast<std::size_t>(m_interpFactor / m_decimFactor + 1));

    for (const double sample : input) {
        m_buffer[static_cast<std::size_t>(m_bufIdx)] = sample;
        m_bufIdx = (m_bufIdx + 1) % L;

        while (m_outPhase < m_interpFactor) {
            const int phase = m_outPhase;
            double acc = 0.0;
            int idx = m_bufIdx;
            for (int t = 0; t < L; ++t) {
                idx--;
                if (idx < 0)
                    idx = L - 1;
                acc += m_buffer[static_cast<std::size_t>(idx)]
                       * m_phases[static_cast<std::size_t>(phase)][static_cast<std::size_t>(t)];
            }
            out.push_back(acc);
            m_outPhase += m_decimFactor;
        }
        m_outPhase -= m_interpFactor;
    }
    return out;
}

BatchFMModulator::BatchFMModulator(double sampleRate) : m_sampleRate(sampleRate) {}

void BatchFMModulator::reset()
{
    m_phase = 0.0;
}

std::vector<CD> BatchFMModulator::modulate(const std::vector<double>& baseband, double deviationHz)
{
    std::vector<CD> out(baseband.size());
    const double phaseScale = 2.0 * kPi * deviationHz / m_sampleRate;

    for (std::size_t i = 0; i < baseband.size(); ++i) {
        m_phase += baseband[i] * phaseScale;
        if (m_phase > kPi)
            m_phase -= 2.0 * kPi;
        else if (m_phase < -kPi)
            m_phase += 2.0 * kPi;
        out[i] = {std::cos(m_phase), std::sin(m_phase)};
    }
    return out;
}

std::vector<float> runRXChain(const std::vector<CD>& iq)
{
    ComplexDCRemoval dcr(0.9999);
    PolyphaseDecimator decimator(designChannelFilter(channelBWSX1255, sampleRateSX1255, channelTapsSX1255),
                                 rxDecimSX1255);
    FMDemodAFC afc(basebandDCAvgCntSX1255, rxFastNSX1255, rxSlowNSX1255);
    RationalResampler resampler(24, 25);
    SampleToSymbol s2s(std::vector<double>(rrcTaps5.begin(), rrcTaps5.end()), rxScalingCoeffSX1255);

    std::vector<float> out;
    for (const CD sample : iq) {
        for (const CD dc : decimator.process(dcr.process(sample))) {
            const double dem = afc.process(dc);
            for (const double r : resampler.process(dem))
                out.push_back(s2s.process(r));
        }
    }
    return out;
}

std::vector<CD> modulateSymbols(const std::vector<Symbol>& symbols)
{
    TXPulseShaper shaper(std::vector<double>(rrcTaps5.begin(), rrcTaps5.end()), 5);
    std::vector<double> baseband = shaper.process(symbols);

    // Restore the sqrt(sps) gain: zero-stuffing + convolution divides out the
    // gain baked into rrcTaps5, so symbol +-3 must map back to +-2400 Hz.
    for (double& b : baseband)
        b *= std::sqrt(5.0);

    BatchResampler resampler(125, 24);
    const std::vector<double> resampled = resampler.process(baseband);

    BatchFMModulator fm(static_cast<double>(sampleRateSX1255));
    return fm.modulate(resampled, 800.0);
}

}  // namespace sx1255::m17

#include "sx1255/soak/SoakTester.hpp"

#include "sx1255/core/IQDispatcher.hpp"
#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/dstar/DStarDetector.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/plugins/PluginFactory.hpp"
#include "sx1255/util/SystemInfo.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

namespace sx1255::soak {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 125000.0;

std::vector<ComplexFloat> fmIq()
{
    constexpr std::size_t kCount = 16384;
    std::vector<ComplexFloat> iq(kCount);
    double phase = 0.0;
    for (std::size_t n = 0; n < kCount; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        const double freq = 500.0 * std::sin(2.0 * kPi * 88.5 * t);
        phase += 2.0 * kPi * freq / kSampleRate;
        iq[n] = {static_cast<float>(std::cos(phase)),
                 static_cast<float>(std::sin(phase))};
    }
    return iq;
}

std::vector<bool> byteToBits(const std::vector<std::uint8_t>& bytes)
{
    std::vector<bool> bits;
    bits.reserve(bytes.size() * 8);
    for (const std::uint8_t b : bytes)
        for (int j = 0; j < 8; ++j)
            bits.push_back((b >> j) & 1);
    return bits;
}

std::vector<ComplexFloat> dstarIq()
{
    std::array<std::uint8_t, 41> header{};
    header[0] = 0x0A; header[1] = 0x0B; header[2] = 0x0C;
    const std::uint16_t crc = dstar::dstarHeaderCrc(header.data(), 39);
    header[39] = static_cast<std::uint8_t>(crc & 0xFF);
    header[40] = static_cast<std::uint8_t>(crc >> 8);

    const std::array<std::uint8_t, 83> fec = dstar::dstarFecEncode(header);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(200 + 2 + 83 + 50);
    for (int i = 0; i < 200; ++i) bytes.push_back(0xAA);
    bytes.push_back(0xEA);
    bytes.push_back(0xA6);
    bytes.insert(bytes.end(), fec.begin(), fec.end());
    for (int i = 0; i < 50; ++i) bytes.push_back(0x55);
    const auto bits = byteToBits(bytes);

    const auto& taps = dstar::dstarGaussianTaps();
    std::array<float, 43> buf{};
    int idx = 0;
    std::vector<double> baseband;
    baseband.reserve(bits.size() * 10);
    for (const bool bit : bits) {
        const float in = bit ? -0.5F : 0.5F;
        for (int i = 0; i < 10; ++i) {
            buf[static_cast<std::size_t>(idx)] = in;
            idx = (idx + 1) % 43;
            float out = 0.0F;
            int p = idx;
            for (int t = 0; t < 43; ++t) {
                out += taps[static_cast<std::size_t>(t)] * buf[static_cast<std::size_t>(p)];
                p = (p + 1) % 43;
            }
            baseband.push_back(out);
        }
    }

    m17::BatchResampler resampler(125, 48);
    const std::vector<double> up = resampler.process(baseband);
    m17::BatchFMModulator fm(125000.0);
    const std::vector<m17::CD> iq = fm.modulate(up, 2400.0);

    std::vector<ComplexFloat> out(iq.size());
    for (std::size_t i = 0; i < iq.size(); ++i)
        out[i] = {static_cast<float>(iq[i].re), static_cast<float>(iq[i].im)};
    return out;
}

std::vector<ComplexFloat> m17Iq(std::size_t targetSamples)
{
    const m17::LSF txLsf = m17::makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
    std::vector<m17::Symbol> symbols = m17::appendPreamble({}, false);
    const std::vector<m17::Symbol> lsfSyms = m17::generateLSFSymbols(txLsf);
    symbols.insert(symbols.end(), lsfSyms.begin(), lsfSyms.end());

    // Fill to ~2 seconds of symbols at 4800 sym/s.
    const std::size_t targetSymbols = 9600;
    for (std::size_t i = symbols.size(); i < targetSymbols; ++i)
        symbols.push_back(i % 2 ? +3.0F : -3.0F);

    const std::vector<m17::CD> iq = m17::modulateSymbols(symbols);
    std::vector<ComplexFloat> out;
    out.reserve(std::min(iq.size(), targetSamples));
    for (std::size_t i = 0; i < iq.size() && i < targetSamples; ++i)
        out.push_back({static_cast<float>(iq[i].re), static_cast<float>(iq[i].im)});
    return out;
}

std::vector<ComplexFloat> noiseIq(std::size_t count)
{
    std::vector<ComplexFloat> out(count);
    std::uint32_t state = 0x12345678u;
    for (std::size_t i = 0; i < count; ++i) {
        state = state * 1664525u + 1013904223u;
        const float v = (static_cast<float>((state >> 8) & 0xFFFF) / 32768.0F) - 1.0F;
        out[i] = {v * 0.05F, 0.0F};
    }
    return out;
}

std::vector<ComplexFloat> bufferFor(std::size_t phase, std::size_t targetSamples)
{
    switch (phase) {
        case 0: return fmIq();
        case 1: return dstarIq();
        case 2: return m17Iq(targetSamples);
        default: return noiseIq(targetSamples);
    }
}

}  // namespace

SoakTester::SoakTester(Config config, std::chrono::seconds duration)
    : m_config(std::move(config))
    , m_duration(duration)
{
}

bool SoakTester::run()
{
    SystemStatistics stats;

    PluginRegistry registry;
    if (m_config.fm.enabled)
        registry.registerPlugin(createFmPlugin(m_config.fm, &stats));
    if (m_config.dstar.enabled)
        registry.registerPlugin(createDStarPlugin(m_config.dstar, &stats));
    if (m_config.m17.enabled)
        registry.registerPlugin(createM17Plugin(m_config.m17, &stats));

    SessionManager sessions;
    sessions.setStatistics(&stats);
    IQDispatcher dispatcher(registry, sessions, nullptr);

    const std::size_t blockSize = m_config.frontend.blockSize > 0
        ? m_config.frontend.blockSize : 2048;
    const std::size_t phaseSamples = static_cast<std::size_t>(kSampleRate * 2.0);  // 2 s phases

    const double cpuStart = util::readProcessInfo().cpuSeconds;
    const auto wallStart = std::chrono::steady_clock::now();
    double rssSum = 0.0;
    std::uint64_t rssSamples = 0;

    std::size_t phase = 0;
    std::size_t phaseOffset = 0;
    auto phaseStart = wallStart;

    while (std::chrono::steady_clock::now() - wallStart < m_duration) {
        const auto buffer = bufferFor(phase, phaseSamples);
        if (buffer.empty()) {
            m_metrics.errors++;
            break;
        }

        phaseOffset = 0;
        while (std::chrono::steady_clock::now() - phaseStart < std::chrono::seconds(2)) {
            const std::size_t remaining = buffer.size() - phaseOffset;
            if (remaining == 0)
                phaseOffset = 0;  // loop a short mode buffer
            const std::size_t count = std::min(blockSize, buffer.size() - phaseOffset);

            IQBlock block;
            block.samples = buffer.data() + phaseOffset;
            block.count = count;
            block.sampleRateHz = kSampleRate;

            try {
                dispatcher.dispatch(block);
                m_metrics.samplesProcessed += count;
                m_metrics.blocksProcessed++;
            } catch (...) {
                m_metrics.errors++;
            }

            phaseOffset += count;

            if (m_metrics.blocksProcessed % 64 == 0) {
                const double rss = static_cast<double>(util::readProcessInfo().rssKb);
                m_metrics.peakRssKb = std::max(m_metrics.peakRssKb, rss);
                rssSum += rss;
                ++rssSamples;
            }
        }

        sessions.finishSession();
        phase = (phase + 1) % 4;
        phaseStart = std::chrono::steady_clock::now();
    }

    m_metrics.durationSeconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(m_duration).count());
    m_metrics.cpuSeconds = util::readProcessInfo().cpuSeconds - cpuStart;
    m_metrics.avgRssKb = rssSamples ? rssSum / static_cast<double>(rssSamples) : 0.0;
    m_metrics.fmSessions = stats.fmSessions();
    m_metrics.dstarSessions = stats.dstarSessions();
    m_metrics.m17Sessions = stats.m17Sessions();
    m_metrics.sessionsCompleted = m_metrics.fmSessions + m_metrics.dstarSessions + m_metrics.m17Sessions;
    if (m_allocationCounter)
        m_metrics.allocations = m_allocationCounter();
    return true;
}

const SoakMetrics& SoakTester::metrics() const
{
    return m_metrics;
}

std::string SoakTester::toJson() const
{
    char buf[640];
    std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"duration_seconds\": %llu,\n"
        "  \"samples_processed\": %llu,\n"
        "  \"blocks_processed\": %llu,\n"
        "  \"sessions_completed\": %llu,\n"
        "  \"fm_sessions\": %llu,\n"
        "  \"dstar_sessions\": %llu,\n"
        "  \"m17_sessions\": %llu,\n"
        "  \"errors\": %llu,\n"
        "  \"peak_rss_kb\": %.1f,\n"
        "  \"avg_rss_kb\": %.1f,\n"
        "  \"cpu_seconds\": %.3f,\n"
        "  \"allocations\": %llu\n"
        "}\n",
        static_cast<unsigned long long>(m_metrics.durationSeconds),
        static_cast<unsigned long long>(m_metrics.samplesProcessed),
        static_cast<unsigned long long>(m_metrics.blocksProcessed),
        static_cast<unsigned long long>(m_metrics.sessionsCompleted),
        static_cast<unsigned long long>(m_metrics.fmSessions),
        static_cast<unsigned long long>(m_metrics.dstarSessions),
        static_cast<unsigned long long>(m_metrics.m17Sessions),
        static_cast<unsigned long long>(m_metrics.errors),
        m_metrics.peakRssKb,
        m_metrics.avgRssKb,
        m_metrics.cpuSeconds,
        static_cast<unsigned long long>(m_metrics.allocations));
    return buf;
}

void SoakTester::writeReport(const std::string& path) const
{
    std::ofstream file(path, std::ios::trunc);
    if (file.is_open())
        file << toJson();
}

void SoakTester::setAllocationCounter(std::function<std::uint64_t()> counter)
{
    m_allocationCounter = std::move(counter);
}

}  // namespace sx1255::soak

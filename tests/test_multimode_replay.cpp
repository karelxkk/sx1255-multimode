#include "sx1255/config/Config.hpp"
#include "sx1255/core/IQDispatcher.hpp"
#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/debug/IQReplay.hpp"
#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/dstar/DStarDetector.hpp"
#include "sx1255/frontend/RadioFrontend.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace sx1255;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 125000.0;

// The radio frontend now runs the SX1255 25 kHz low-IF (LO = channel - 25 kHz)
// and RadioFrontend undoes that with an NCO before dispatch.  A replay file
// therefore contains the raw low-IF IQ, i.e. the wanted channel sits at +25 kHz.
std::vector<ComplexFloat> toLowIf(std::vector<ComplexFloat> iq, double shiftHz)
{
    double phase = 0.0;
    const double inc = 2.0 * kPi * shiftHz / kSampleRate;
    for (ComplexFloat& s : iq) {
        const double c = std::cos(phase);
        const double q = std::sin(phase);
        const double i = static_cast<double>(s.i);
        const double qq = static_cast<double>(s.q);
        s = {static_cast<float>(i * c - qq * q),
             static_cast<float>(i * q + qq * c)};
        phase += inc;
    }
    return iq;
}

std::string tempFile(const char* tag)
{
    return "/tmp/sx1255_mm_" + std::string(tag) + "_" + std::to_string(::getpid()) + ".iq";
}

void writeFile(const std::string& path, const std::vector<ComplexFloat>& iq,
               std::uint32_t frequencyHz)
{
    debug::IQFileHeader header;
    header.sampleRateHz = static_cast<std::uint32_t>(kSampleRate);
    header.frequencyHz  = frequencyHz;
    header.sampleCount  = iq.size();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        throw std::runtime_error("cannot open " + path);
    if (!debug::writeIQHeader(out, header) || !debug::writeIQSamples(out, iq.data(), iq.size()))
        throw std::runtime_error("cannot write " + path);
}

// ---------------------------------------------------------------------------
// FM + CTCSS: FM-modulated 88.5 Hz sub-audio tone.
// ---------------------------------------------------------------------------
std::vector<ComplexFloat> fmIq(std::size_t count = 32768)
{
    std::vector<ComplexFloat> iq(count);
    double phase = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        const double freq = 500.0 * std::sin(2.0 * kPi * 88.5 * t);
        phase += 2.0 * kPi * freq / kSampleRate;
        iq[n] = {static_cast<float>(std::cos(phase)),
                 static_cast<float>(std::sin(phase))};
    }
    return iq;
}

// ---------------------------------------------------------------------------
// D-Star: GMSK header (preamble + frame sync + FEC) at 125 kSa/s.
// ---------------------------------------------------------------------------
std::vector<bool> byteToBits(const std::vector<std::uint8_t>& bytes)
{
    std::vector<bool> bits;
    bits.reserve(bytes.size() * 8);
    for (const std::uint8_t b : bytes)
        for (int j = 0; j < 8; ++j)
            bits.push_back((b >> j) & 1);
    return bits;
}

std::vector<bool> dstarHeaderBits()
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
    // Keep the DV stream alive long enough for the higher-priority D-Star
    // detector to confirm after the header and preempt an FM noise-squelch
    // claim, but shorter than the detector's 1 s data-sync timeout so the
    // session is still ActiveDStar when the replay reaches EOF.
    for (int i = 0; i < 120; ++i) bytes.push_back(0x55);
    return byteToBits(bytes);
}

std::vector<ComplexFloat> dstarIq()
{
    const auto bits = dstarHeaderBits();
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

// ---------------------------------------------------------------------------
// M17: preamble + LSF + filler at 125 kSa/s.
// ---------------------------------------------------------------------------
std::vector<ComplexFloat> m17Iq(int fillerSymbols = 600)
{
    const m17::LSF txLsf = m17::makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
    std::vector<m17::Symbol> symbols = m17::appendPreamble({}, false);
    const std::vector<m17::Symbol> lsfSyms = m17::generateLSFSymbols(txLsf);
    symbols.insert(symbols.end(), lsfSyms.begin(), lsfSyms.end());
    // Valid stream frames so the plugin can latch M17 mode after several
    // useful syncs (matching the stricter min_sync_burst claim logic).
    const int frames = std::max(5, fillerSymbols / 200);
    for (int fn = 0; fn < frames; ++fn) {
        const std::vector<std::uint8_t> payload(16, static_cast<std::uint8_t>(fn));
        const std::vector<m17::Symbol> frame =
            m17::generateStreamSymbols(txLsf, static_cast<std::uint16_t>(fn), payload);
        symbols.insert(symbols.end(), frame.begin(), frame.end());
    }

    const std::vector<m17::CD> iq = m17::modulateSymbols(symbols);
    std::vector<ComplexFloat> out(iq.size());
    for (std::size_t i = 0; i < iq.size(); ++i)
        out[i] = {static_cast<float>(iq[i].re), static_cast<float>(iq[i].im)};
    return out;
}

std::vector<ComplexFloat> unmodulatedCarrier(std::size_t count)
{
    std::vector<ComplexFloat> out(count, ComplexFloat{1.0F, 0.0F});
    return out;
}

std::vector<ComplexFloat> scaled(std::vector<ComplexFloat> iq, float gain)
{
    for (ComplexFloat& s : iq) {
        s.i *= gain;
        s.q *= gain;
    }
    return iq;
}

// ---------------------------------------------------------------------------
// Full pipeline: file -> IQReplay -> RadioFrontend -> IQDispatcher -> plugins.
// ---------------------------------------------------------------------------
SessionManager::State runReplay(const std::string& path)
{
    SystemStatistics stats;
    Config config;
    config.fm.enabled = true;
    config.dstar.enabled = true;
    config.m17.enabled = true;

    PluginRegistry registry;
    registry.registerPlugin(createFmPlugin(config.fm, &stats));
    registry.registerPlugin(createDStarPlugin(config.dstar, &stats));
    registry.registerPlugin(createM17Plugin(config.m17, &stats));

    SessionManager sessions;
    sessions.setStatistics(&stats);
    IQDispatcher dispatcher(registry, sessions, nullptr);

    auto device = std::make_unique<debug::IQReplay>(path);
    RadioFrontend frontend(std::move(device), dispatcher);
    dispatcher.setTransmitter(&frontend);

    RadioFrontend::Settings settings;
    settings.blockSize = 2048;
    if (!frontend.start(settings))
        return SessionManager::State::Idle;

    for (int i = 0; i < 20000 && frontend.running(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto state = sessions.state();
    sessions.reset();  // release the owner before the frontend is destroyed
    frontend.stop();
    return state;
}

}  // namespace

int main()
{
    const std::string fmFile    = tempFile("fm");
    const std::string dstarFile = tempFile("dstar");
    const std::string m17File   = tempFile("m17");
    const std::string mixedFile = tempFile("mixed");

    // FM needs 300 ms of carrier before it claims the channel (digital modes
    // must win when they are on the air); generate one continuous fixture so
    // the noise-squelch confirmation window never crosses a phase jump.
    std::vector<ComplexFloat> fmLong = fmIq(131072);
    writeFile(fmFile, toLowIf(fmLong, 25000.0), 430950000);
    writeFile(dstarFile, toLowIf(dstarIq(), 25000.0), 430950000);
    writeFile(m17File, toLowIf(m17Iq(2000), 25000.0), 430950000);

    // A 400 ms unmodulated carrier lets RSSI-based FM claim the channel first;
    // the following M17 LSF must then preempt FM and finish as ActiveM17.
    std::vector<ComplexFloat> mixed = unmodulatedCarrier(50000);
    const std::vector<ComplexFloat> m17AfterFm = m17Iq(2000);
    mixed.insert(mixed.end(), m17AfterFm.begin(), m17AfterFm.end());
    writeFile(mixedFile, toLowIf(mixed, 25000.0), 430950000);

    const auto fmState = runReplay(fmFile);
    if (fmState != SessionManager::State::ActiveFm) {
        std::printf("FM replay state: %s\n", SessionManager::toString(fmState));
        return 1;
    }

    const auto dstarState = runReplay(dstarFile);
    if (dstarState != SessionManager::State::ActiveDStar) {
        std::printf("dstar replay state: %s\n",
                    SessionManager::toString(dstarState));
        return 2;
    }

    const auto m17State = runReplay(m17File);
    if (m17State != SessionManager::State::ActiveM17)
        return 3;

    const auto mixedState = runReplay(mixedFile);
    if (mixedState != SessionManager::State::ActiveM17) {
        std::printf("mixed replay state: %s\n",
                    SessionManager::toString(mixedState));
        return 4;
    }

    // RSSI gate: the same valid M17 signal is rejected at the noise floor and
    // accepted when strong enough.  Decoding still runs below the gate.
    {
        M17Config m17Config;
        m17Config.bridge.enabled = false;
        m17Config.reflector.enabled = false;
        const auto strong = m17Iq(2000);
        const auto weak = scaled(strong, 0.001F);   // approx -60 dBFS

        auto feedAll = [&](const std::vector<ComplexFloat>& iq) {
            auto plugin = createM17Plugin(m17Config);
            bool detected = false;
            for (std::size_t off = 0; off < iq.size(); off += 2048) {
                const IQBlock block{off, iq.data() + off,
                                    std::min<std::size_t>(2048, iq.size() - off),
                                    kSampleRate};
                detected = detected || plugin->detect(block).detected;
            }
            return detected;
        };

        if (!feedAll(strong))
            return 5;
        if (feedAll(weak))
            return 6;
    }

    std::remove(fmFile.c_str());
    std::remove(dstarFile.c_str());
    std::remove(m17File.c_str());
    std::remove(mixedFile.c_str());
    return 0;
}

#include "sx1255/frontend/SX1255Hardware.hpp"

#include <alsa/asoundlib.h>

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace sx1255 {

namespace {

// SX1255 register map and mode bits (modem/sx1255_modem.go, sx1255_spi.go).
constexpr std::uint8_t kRegControl     = 0x00;
constexpr std::uint8_t kRegRXFreqMSB   = 0x01;
constexpr std::uint8_t kRegRXFreqMid   = 0x02;
constexpr std::uint8_t kRegRXFreqLSB   = 0x03;
constexpr std::uint8_t kRegTXFreqMSB   = 0x04;
constexpr std::uint8_t kRegTXFreqMid   = 0x05;
constexpr std::uint8_t kRegTXFreqLSB   = 0x06;
constexpr std::uint8_t kRegVersion     = 0x07;
constexpr std::uint8_t kRegTXDACGain   = 0x08;
constexpr std::uint8_t kRegTXPLLFilter = 0x0A;
constexpr std::uint8_t kRegTXDACTaps   = 0x0B;
constexpr std::uint8_t kRegRXLNAPGASX  = 0x0C;
constexpr std::uint8_t kRegRXBWFilter  = 0x0D;
constexpr std::uint8_t kRegRXPLLBW     = 0x0E;
constexpr std::uint8_t kRegI2SRate0    = 0x12;
constexpr std::uint8_t kRegI2SRate1    = 0x13;
constexpr std::uint8_t kRegPLLStatus   = 0x11;
constexpr std::uint8_t kPllTxLock      = 0x01;

constexpr std::uint8_t kModeRefEnable    = 0x01;
constexpr std::uint8_t kModeRXEnable     = 0x02;
constexpr std::uint8_t kModeTXEnable     = 0x04;
constexpr std::uint8_t kModeDriverEnable = 0x08;
constexpr std::uint8_t kModeMask         = 0x0F;

constexpr std::uint8_t kExpectedVersion = 0x11;
constexpr int kSampleRate = 125000;
// ~65 ms of silent carrier written at PTT-on to prime the TX playback ring.
constexpr snd_pcm_uframes_t kTxPrefillFrames = 8192;

std::uint8_t lnaCode(int db)
{
    const int gains[6] = {48, 42, 36, 24, 12, 0};
    const std::uint8_t codes[6] = {1, 2, 3, 4, 5, 6};
    int best = 0;
    for (int i = 1; i < 6; ++i)
        if (std::abs(db - gains[i]) < std::abs(db - gains[best]))
            best = i;
    return codes[best];
}

std::uint8_t dacCode(int db)
{
    const int gains[4] = {-9, -6, -3, 0};
    const std::uint8_t codes[4] = {0, 1, 2, 3};
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (std::abs(db - gains[i]) < std::abs(db - gains[best]))
            best = i;
    return codes[best];
}

// Nominal mapping from requested output power (0..10 dBm) to the SX1255 DAC
// attenuation (0, -3, -6, -9 dB relative to full scale).  The chip has no PA,
// so the DAC gain in TXFE1 (0x08) is the only TX level control.
int dacAttenuationForPower(dBm power)
{
    if (power >= 10) return 0;
    if (power >= 7)  return -3;
    if (power >= 4)  return -6;
    return -9;
}

}  // namespace

std::uint32_t sx1255FreqToReg(std::uint32_t hz)
{
    return static_cast<std::uint32_t>(std::round(static_cast<double>(hz) * 1048576.0 / 32.0e6));
}

SX1255Hardware::SX1255Hardware(SX1255HardwareConfig config,
                               std::unique_ptr<ISX1255SPI> spi,
                               std::unique_ptr<ISX1255GPIO> gpio)
    : m_config(std::move(config))
    , m_spi(std::move(spi))
    , m_gpio(std::move(gpio))
    , m_rxQueue(m_config.rxQueueSize)
{
}

SX1255Hardware::~SX1255Hardware()
{
    close();
}

bool SX1255Hardware::writeReg(std::uint8_t addr, std::uint8_t value)
{
    if (!m_spi || !m_spi->writeRegister(addr, value)) {
        m_spiErrors++;
        return false;
    }
    return true;
}

bool SX1255Hardware::readReg(std::uint8_t addr, std::uint8_t& value)
{
    if (!m_spi || !m_spi->readRegister(addr, value)) {
        m_spiErrors++;
        return false;
    }
    return true;
}

bool SX1255Hardware::updateReg(std::uint8_t addr, std::uint8_t mask, std::uint8_t value)
{
    std::uint8_t reg = 0;
    if (!readReg(addr, reg))
        return false;
    const std::uint8_t want = static_cast<std::uint8_t>((reg & ~mask) | (value & mask));
    if (!writeReg(addr, want))
        return false;
    std::uint8_t got = 0;
    if (!readReg(addr, got))
        return false;
    return ((got ^ want) & mask) == 0;
}

bool SX1255Hardware::setMode(std::uint8_t set, std::uint8_t clear)
{
    std::uint8_t reg = 0;
    if (!readReg(kRegControl, reg))
        return false;
    const std::uint8_t want = static_cast<std::uint8_t>((reg & ~clear) | set | kModeRefEnable);
    if (!writeReg(kRegControl, want))
        return false;
    std::uint8_t got = 0;
    if (!readReg(kRegControl, got))
        return false;
    return ((got ^ want) & kModeMask) == 0;
}

bool SX1255Hardware::enableRx(bool on)
{
    return on ? setMode(kModeRXEnable, 0) : setMode(0, kModeRXEnable);
}

bool SX1255Hardware::enableTx(bool on)
{
    const std::uint8_t txBits = static_cast<std::uint8_t>(kModeTXEnable | kModeDriverEnable);
    return on ? setMode(txBits, 0) : setMode(0, txBits);
}

bool SX1255Hardware::setRate()
{
    // Reg 0x13 before 0x12 (libsx1255 init order).
    if (!writeReg(kRegI2SRate1, 0x2C))
        return false;
    return writeReg(kRegI2SRate0, 0x22);
}

bool SX1255Hardware::setGains()
{
    bool ok = true;
    ok = updateReg(kRegRXLNAPGASX, 0xE0, static_cast<std::uint8_t>(lnaCode(m_config.lnaGain) << 5)) && ok;
    ok = updateReg(kRegRXLNAPGASX, 0x1E, static_cast<std::uint8_t>((m_config.pgaGain / 2 & 0x0F) << 1)) && ok;
    ok = updateReg(kRegTXDACGain, 0x70, static_cast<std::uint8_t>(dacCode(m_config.dacGain) << 4)) && ok;
    const int mixerStep = std::clamp(static_cast<int>(std::round((m_config.mixerGain + 37.5) / 2.0)), 0, 15);
    ok = updateReg(kRegTXDACGain, 0x0F, static_cast<std::uint8_t>(mixerStep)) && ok;
    return ok;
}

bool SX1255Hardware::initChip()
{
    if (m_gpio) {
        m_gpio->setReset(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        m_gpio->setReset(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (!writeReg(kRegRXBWFilter, static_cast<std::uint8_t>((0x01 << 5) | (0x05 << 2) | 0x03)))
        return false;
    if (!writeReg(kRegTXDACTaps, 5))
        return false;
    // These two PLL registers are part of the original libsx1255 init
    // sequence.  Without them the RX/TX PLLs stay at reset defaults after a
    // hardware reset and never lock, so the I2S master clock never starts.
    if (!writeReg(kRegRXPLLBW, 0x00))
        return false;
    if (!writeReg(kRegTXPLLFilter, 0x00))
        return false;

    std::uint8_t version = 0;
    if (!readReg(kRegVersion, version))
        return false;
    if (version != kExpectedVersion) {
        std::fprintf(stderr, "SX1255 unexpected chip version 0x%02X (expected 0x%02X)\n",
                     version, kExpectedVersion);
        return false;
    }
    return true;
}

bool SX1255Hardware::setRxFrequency(Hertz hz)
{
    const std::uint32_t val = sx1255FreqToReg(hz);
    if (!writeReg(kRegRXFreqMSB, static_cast<std::uint8_t>(val >> 16))) return false;
    if (!writeReg(kRegRXFreqMid, static_cast<std::uint8_t>(val >> 8))) return false;
    return writeReg(kRegRXFreqLSB, static_cast<std::uint8_t>(val));
}

bool SX1255Hardware::setTxFrequency(Hertz hz)
{
    const std::uint32_t val = sx1255FreqToReg(hz);
    if (!writeReg(kRegTXFreqMSB, static_cast<std::uint8_t>(val >> 16))) return false;
    if (!writeReg(kRegTXFreqMid, static_cast<std::uint8_t>(val >> 8))) return false;
    return writeReg(kRegTXFreqLSB, static_cast<std::uint8_t>(val));
}

bool SX1255Hardware::setPower(dBm power)
{
    m_power = power;
    const int db = dacAttenuationForPower(power);
    return updateReg(kRegTXDACGain, 0x70, static_cast<std::uint8_t>(dacCode(db) << 4));
}

bool SX1255Hardware::setPtt(bool on)
{
    m_ptt = on;
    if (m_gpio) {
        m_gpio->setPtt(on);
        m_gpio->setTxEnable(on);
    }
    if (on) {
        // Mirror m17-gateway's recoverALSA(): return the playback stream to a
        // clean PREPARED state before every transmission so the preamble/LSF
        // is never clipped by a stale XRUN state from a previous session.
        std::lock_guard<std::mutex> lock(m_alsaMutex);
        if (m_playback) {
            snd_pcm_drop(m_playback);
            snd_pcm_prepare(m_playback);
            // Pre-fill the playback ring with a short silent carrier so the
            // stream cannot underrun (XRUN) in the window between enabling the
            // TX clock and the plugin's first IQ block.  A start-of-over
            // underrun is audible as a periodic digital-noise burst.
            std::vector<std::int32_t> prefill(kTxPrefillFrames * 2, 0);
            snd_pcm_writei(m_playback, prefill.data(), kTxPrefillFrames);
        }
        if (!enableTx(true))
            return false;
        // Let the TX PLL lock before IQ is written, like m17-gateway's
        // startTX: writing immediately after the PA switches on corrupts the
        // beginning of the waveform while the I2S master clock stabilizes.
        for (int i = 0; i < 100; ++i) {
            std::uint8_t status = 0;
            if (readReg(kRegPLLStatus, status) && (status & kPllTxLock)) {
                std::fprintf(stderr, "SX1255 TX PLL locked after %d ms\n", i + 1);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::fprintf(stderr, "SX1255 TX PLL lock timeout (100 ms)\n");
        return true;   // best effort if the lock bit never appears
    }
    return enableTx(false);
}

double SX1255Hardware::readRssi()
{
    return m_rssi.load();
}

bool SX1255Hardware::setSquelch(dBm level)
{
    m_squelch = level;
    return true;
}

bool SX1255Hardware::openAlsa()
{
    auto setParams = [](snd_pcm_t* pcm, snd_pcm_stream_t stream) -> bool {
        snd_pcm_hw_params_t* params = nullptr;
        snd_pcm_hw_params_alloca(&params);
        snd_pcm_hw_params_any(pcm, params);
        if (snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
            return false;
        if (snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S32_LE) < 0)
            return false;
        if (snd_pcm_hw_params_set_channels(pcm, params, 2) < 0)
            return false;
        unsigned int rate = kSampleRate;
        if (snd_pcm_hw_params_set_rate_near(pcm, params, &rate, nullptr) < 0)
            return false;
        snd_pcm_uframes_t period = 4096;
        snd_pcm_hw_params_set_period_size_near(pcm, params, &period, nullptr);
        snd_pcm_uframes_t buffer = 65536;
        snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer);
        if (snd_pcm_hw_params(pcm, params) < 0)
            return false;
        snd_pcm_hw_params_t* actual = nullptr;
        snd_pcm_hw_params_alloca(&actual);
        snd_pcm_hw_params_current(pcm, actual);
        unsigned int gotRate = 0;
        unsigned int gotChannels = 0;
        snd_pcm_uframes_t gotPeriod = 0;
        snd_pcm_uframes_t gotBuffer = 0;
        snd_pcm_hw_params_get_rate(actual, &gotRate, nullptr);
        snd_pcm_hw_params_get_channels(actual, &gotChannels);
        snd_pcm_hw_params_get_period_size(actual, &gotPeriod, nullptr);
        snd_pcm_hw_params_get_buffer_size(actual, &gotBuffer);
        std::fprintf(stderr,
                     "SX1255 ALSA %s: rate=%u, channels=%u, period=%lu, buffer=%lu\n",
                     stream == SND_PCM_STREAM_CAPTURE ? "capture" : "playback",
                     gotRate, gotChannels,
                     static_cast<unsigned long>(gotPeriod),
                     static_cast<unsigned long>(gotBuffer));
        (void)stream;
        return snd_pcm_prepare(pcm) >= 0;
    };

    const std::string cap = m_config.alsaCapture.empty() ? "default" : m_config.alsaCapture;
    const std::string play = m_config.alsaPlayback.empty() ? "default" : m_config.alsaPlayback;

    if (snd_pcm_open(&m_capture, cap.c_str(), SND_PCM_STREAM_CAPTURE, 0) < 0)
        return false;
    if (!setParams(m_capture, SND_PCM_STREAM_CAPTURE)) {
        snd_pcm_close(m_capture);
        m_capture = nullptr;
        return false;
    }

    if (snd_pcm_open(&m_playback, play.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        snd_pcm_close(m_capture);
        m_capture = nullptr;
        return false;
    }
    if (!setParams(m_playback, SND_PCM_STREAM_PLAYBACK)) {
        snd_pcm_close(m_capture);
        snd_pcm_close(m_playback);
        m_capture = nullptr;
        m_playback = nullptr;
        return false;
    }
    return true;
}

void SX1255Hardware::closeAlsa()
{
    if (m_capture) {
        snd_pcm_close(m_capture);
        m_capture = nullptr;
    }
    if (m_playback) {
        snd_pcm_close(m_playback);
        m_playback = nullptr;
    }
}

bool SX1255Hardware::open()
{
    if (m_running.load())
        return true;

    if (!m_spi || !m_spi->open()) {
        std::fprintf(stderr, "SX1255: SPI open failed\n");
        return false;
    }
    if (m_gpio && !m_gpio->open()) {
        // Reset is best effort: the chip may already be initialised.
        std::fprintf(stderr, "SX1255 GPIO open failed; continuing without GPIO control\n");
    }

    if (!initChip()) {
        std::fprintf(stderr, "SX1255: chip init failed\n");
        m_spi->close();
        return false;
    }
    if (!setRate()) {
        std::fprintf(stderr, "SX1255: set rate failed\n");
        m_spi->close();
        return false;
    }
    if (!enableRx(true)) {
        std::fprintf(stderr, "SX1255: enable RX failed\n");
        m_spi->close();
        return false;
    }
    if (m_gpio)
        m_gpio->setRxEnable(true);
    setGains();       // gain failures are non-fatal
    enableTx(false);  // TX PA off until keyed

    if (!openAlsa()) {
        std::fprintf(stderr, "SX1255: ALSA open failed\n");
        m_spi->close();
        return false;
    }

    m_running = true;
    m_captureThread = std::thread([this] { captureThread(); });
    return true;
}

void SX1255Hardware::close()
{
    if (!m_running.exchange(false))
        return;

    if (m_captureThread.joinable())
        m_captureThread.join();

    closeAlsa();
    enableTx(false);
    if (m_spi)
        m_spi->close();
    if (m_gpio)
        m_gpio->close();
}

void SX1255Hardware::captureThread()
{
    const std::size_t frames = m_config.blockSize;
    std::vector<std::int32_t> buf(frames * 2);

    while (m_running.load()) {
        snd_pcm_sframes_t got = snd_pcm_readi(m_capture, buf.data(), static_cast<snd_pcm_uframes_t>(frames));
        if (got == -EPIPE) {
            m_alsaErrors++;
            snd_pcm_prepare(m_capture);
            continue;
        }
        if (got < 0) {
            m_alsaErrors++;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        double power = 0.0;
        for (snd_pcm_sframes_t i = 0; i < got; ++i) {
            const float fi = static_cast<float>(buf[static_cast<std::size_t>(i) * 2]) / 2147483648.0F;
            const float fq = static_cast<float>(buf[static_cast<std::size_t>(i) * 2 + 1]) / 2147483648.0F;
            power += static_cast<double>(fi) * fi + static_cast<double>(fq) * fq;
            if (!m_rxQueue.push(ComplexFloat{fi, fq}))
                m_rxOverruns++;
        }
        power /= static_cast<double>(got);
        m_rssi = 10.0 * std::log10(power + 1e-12);
        m_rxSamples += static_cast<std::uint64_t>(got);
    }
}

bool SX1255Hardware::readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples)
{
    for (std::size_t i = 0; i < maxSamples; ++i) {
        ComplexFloat sample{};
        if (!m_rxQueue.pop(sample))
            break;
        out.push_back(sample);
    }
    return true;
}

bool SX1255Hardware::writeIQ(const ComplexFloat* samples, std::size_t count)
{
    if (count == 0)
        return true;
    if (!m_running.load() || !m_playback) {
        m_txUnderruns += count;   // not opened / not running
        return true;
    }

    // Write directly to the ALSA playback stream, blocking and paced by the
    // SX1255 I2S clock exactly like m17-gateway.  A queue + separate thread
    // here underran on the Pi Zero 2 W and corrupted M17 frames.
    std::lock_guard<std::mutex> lock(m_alsaMutex);

    std::vector<std::int32_t> buf(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        buf[i * 2]     = static_cast<std::int32_t>(samples[i].i * 2147483647.0F);
        buf[i * 2 + 1] = static_cast<std::int32_t>(samples[i].q * 2147483647.0F);
    }

    std::size_t off = 0;
    int recoveries = 0;
    while (off < count) {
        const snd_pcm_sframes_t written = snd_pcm_writei(
            m_playback, buf.data() + off * 2,
            static_cast<snd_pcm_uframes_t>(count - off));
        if (written >= 0) {
            off += static_cast<std::size_t>(written);
            m_txSamples += static_cast<std::uint64_t>(written);
            recoveries = 0;
            continue;
        }

        m_alsaErrors++;
        if (written == -EAGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        // Recoverable stream-state errors: underrun/overrun (-EPIPE), resume
        // (-ESTRPIPE), a stream left in SETUP/OPEN after a drain or a dropped
        // descriptor (-EBADFD), or a temporarily gone device (-ENODEV).  Always
        // re-prepare and retry a bounded number of times so a single glitch in
        // the TX chain cannot silently kill the FM/ M17 transmitter.
        if (written == -EPIPE || written == -ESTRPIPE ||
            written == -EBADFD || written == -ENODEV) {
            if (recoveries++ < 16) {
                std::fprintf(stderr, "SX1255 playback recover: %s (state=%d)\n",
                             snd_strerror(static_cast<int>(written)),
                             static_cast<int>(snd_pcm_state(m_playback)));
                snd_pcm_prepare(m_playback);
                continue;
            }
        }
        std::fprintf(stderr, "SX1255 playback write failed: %s (%ld)\n",
                     snd_strerror(static_cast<int>(written)),
                     static_cast<long>(written));
        m_txUnderruns += (count - off);
        break;
    }
    return true;
}

bool SX1255Hardware::drainTx()
{
    std::lock_guard<std::mutex> lock(m_alsaMutex);
    if (m_playback)
        snd_pcm_drain(m_playback);
    return true;
}

FrontendStats SX1255Hardware::stats() const
{
    FrontendStats s;
    s.rxSamples = m_rxSamples.load();
    s.txSamples = m_txSamples.load();
    s.rssiDbm = m_rssi.load();
    s.txPowerDbm = m_power.load();
    s.rxOverruns = m_rxOverruns.load();
    s.txUnderruns = m_txUnderruns.load();
    s.spiErrors = m_spiErrors.load();
    s.alsaErrors = m_alsaErrors.load();
    return s;
}

}  // namespace sx1255

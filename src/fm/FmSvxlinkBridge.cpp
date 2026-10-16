#include "sx1255/fm/FmSvxlinkBridge.hpp"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace sx1255::fm {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kAudioPeriod = 512;
constexpr double kRfRate = 125000.0;

// Ported from sx1255-fm-repeater (src/sx1255_repeater.c).  The SX1255 is run
// zero-IF in this project, so the -25 kHz rotator is intentionally absent.
// Decimation, IF filter and gain match the original; the audio low-pass
// corners are config-driven so the channel can be tuned for narrow (12.5 kHz)
// or wide (25 kHz) FM.  De-emphasis and pre-emphasis are deliberately absent:
// this path hands the raw discriminator audio to SVXLink and transmits what
// SVXLink produces, so SVXLink owns both filters ([Rx1] DEEMPHASIS, [Tx1]
// PREEMPHASIS), which is what those settings are for.
constexpr int kRxIfDecim = 5;
constexpr int kRxIfTaps = 61;
constexpr int kAudioFirTaps = 81;
constexpr float kRxIfFc = 7500.0F;

void designLowpass(float* taps, int ntaps, float fs, float fc)
{
    const float normCut = fc / fs;
    const int m = ntaps - 1;
    float sum = 0.0F;
    for (int n = 0; n < ntaps; ++n) {
        const float t = static_cast<float>(n) - static_cast<float>(m) / 2.0F;
        const float h = std::fabs(t) < 1e-6F
            ? 2.0F * normCut
            : std::sin(2.0F * static_cast<float>(kPi) * normCut * t) /
                  (static_cast<float>(kPi) * t);
        const float w = 0.54F - 0.46F * std::cos(2.0F * static_cast<float>(kPi) *
                                                 static_cast<float>(n) / static_cast<float>(m));
        taps[n] = h * w;
        sum += taps[n];
    }
    for (int n = 0; n < ntaps; ++n)
        taps[n] /= sum;
}

class FirFilter {
public:
    FirFilter(float fs, float fc)
    {
        m_taps.fill(0.0F);
        m_delay.fill(0.0F);
        designLowpass(m_taps.data(), kAudioFirTaps, fs, fc);
    }

    float process(float x)
    {
        m_delay[static_cast<std::size_t>(m_idx)] = x;
        m_idx = (m_idx + 1) % kAudioFirTaps;

        float y = 0.0F;
        int idx = m_idx;
        for (int n = 0; n < kAudioFirTaps; ++n) {
            if (--idx < 0)
                idx = kAudioFirTaps - 1;
            y += m_taps[static_cast<std::size_t>(n)] * m_delay[static_cast<std::size_t>(idx)];
        }
        return y;
    }

    void reset()
    {
        m_delay.fill(0.0F);
        m_idx = 0;
    }

private:
    std::array<float, kAudioFirTaps> m_taps;
    std::array<float, kAudioFirTaps> m_delay;
    int m_idx = 0;
};

class FirDecimator {
public:
    FirDecimator(float fs, float fc)
    {
        m_taps.fill(0.0F);
        m_delay.fill(ComplexFloat{0.0F, 0.0F});
        designLowpass(m_taps.data(), kRxIfTaps, fs, fc);
    }

    void push(ComplexFloat x)
    {
        m_delay[static_cast<std::size_t>(m_idx)] = x;
        m_idx = (m_idx + 1) % kRxIfTaps;
    }

    ComplexFloat compute() const
    {
        ComplexFloat y{0.0F, 0.0F};
        int idx = m_idx;
        for (int n = 0; n < kRxIfTaps; ++n) {
            if (--idx < 0)
                idx = kRxIfTaps - 1;
            y.i += m_taps[static_cast<std::size_t>(n)] * m_delay[static_cast<std::size_t>(idx)].i;
            y.q += m_taps[static_cast<std::size_t>(n)] * m_delay[static_cast<std::size_t>(idx)].q;
        }
        return y;
    }

    void reset()
    {
        m_delay.fill(ComplexFloat{0.0F, 0.0F});
        m_idx = 0;
    }

private:
    std::array<float, kRxIfTaps> m_taps;
    std::array<ComplexFloat, kRxIfTaps> m_delay;
    int m_idx = 0;
};

class ComplexDcBlocker {
public:
    explicit ComplexDcBlocker(float r)
        : m_r(r)
    {
    }

    ComplexFloat process(ComplexFloat x)
    {
        const ComplexFloat y{
            x.i - m_xPrev.i + m_r * m_yPrev.i,
            x.q - m_xPrev.q + m_r * m_yPrev.q};
        m_xPrev = x;
        m_yPrev = y;
        return y;
    }

    void reset()
    {
        m_xPrev = ComplexFloat{0.0F, 0.0F};
        m_yPrev = ComplexFloat{0.0F, 0.0F};
    }

private:
    float m_r;
    ComplexFloat m_xPrev{0.0F, 0.0F};
    ComplexFloat m_yPrev{0.0F, 0.0F};
};

// 2nd order high-pass (biquad).  Removes the sub-audible CTCSS tone and the
// low harmonics of a clipped tone generator without touching the rest of the
// voice band - unlike a bank of notches on every harmonic, which left the audio
// full of holes.  A corner of 0 turns it into a passthrough.
class HighPass {
public:
    HighPass(double sampleRateHz, double freqHz, double q = 0.707)
    {
        if (freqHz <= 0.0) {
            m_b0 = 1.0;
            return;
        }
        const double w0 = 2.0 * kPi * freqHz / sampleRateHz;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        const double cosw0 = std::cos(w0);

        m_b0 = (1.0 + cosw0) / 2.0 / a0;
        m_b1 = -(1.0 + cosw0) / a0;
        m_b2 = m_b0;
        m_a1 = -2.0 * cosw0 / a0;
        m_a2 = (1.0 - alpha) / a0;
    }

    float process(float x)
    {
        const double y = m_b0 * x + m_b1 * m_x1 + m_b2 * m_x2
                       - m_a1 * m_y1 - m_a2 * m_y2;
        m_x2 = m_x1;
        m_x1 = x;
        m_y2 = m_y1;
        m_y1 = y;
        return static_cast<float>(y);
    }

private:
    double m_b0 = 1.0, m_b1 = 0.0, m_b2 = 0.0, m_a1 = 0.0, m_a2 = 0.0;
    double m_x1 = 0.0, m_x2 = 0.0, m_y1 = 0.0, m_y2 = 0.0;
};

bool openPcm(snd_pcm_t*& pcm, const std::string& device, snd_pcm_stream_t stream,
             unsigned int rate, bool nonBlocking)
{
    if (snd_pcm_open(&pcm, device.c_str(), stream, 0) < 0)
        return false;

    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_any(pcm, params) < 0)
        return false;
    if (snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
        return false;
    if (snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE) < 0)
        return false;
    if (snd_pcm_hw_params_set_channels(pcm, params, 1) < 0)
        return false;
    unsigned int r = rate;
    if (snd_pcm_hw_params_set_rate_near(pcm, params, &r, nullptr) < 0)
        return false;
    snd_pcm_uframes_t period = kAudioPeriod;
    snd_pcm_hw_params_set_period_size_near(pcm, params, &period, nullptr);
    snd_pcm_uframes_t buffer = period * 8;
    snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer);
    if (snd_pcm_hw_params(pcm, params) < 0)
        return false;
    if (snd_pcm_prepare(pcm) < 0)
        return false;
    if (nonBlocking)
        snd_pcm_nonblock(pcm, 1);
    // snd-aloop capture can stay PREPARED forever in non-blocking mode, with
    // readi() then returning EIO even while the paired playback is RUNNING.
    // The original sx1255-fm-repeater starts capture explicitly; EIO is
    // expected until a playback client connects.
    if (stream == SND_PCM_STREAM_CAPTURE) {
        const int rc = snd_pcm_start(pcm);
        if (rc < 0 && rc != -EIO)
            return false;
    }
    return true;
}

}  // namespace

FmSvxlinkBridge::FmSvxlinkBridge(SvxlinkBridgeConfig config)
    : m_config(std::move(config))
    , m_rxQueue(m_config.queueSamples)
    , m_ptt(m_config.ptyPath, [this](bool on) { onPtt(on); })
{
}

FmSvxlinkBridge::~FmSvxlinkBridge()
{
    stop();
}

bool FmSvxlinkBridge::start(ITransmit* tx)
{
    if (m_running.load())
        return true;

    m_tx = tx;

    auto* capture = reinterpret_cast<snd_pcm_t*>(m_capture);
    auto* playback = reinterpret_cast<snd_pcm_t*>(m_playback);
    if (!openPcm(capture, m_config.audioCapture, SND_PCM_STREAM_CAPTURE,
                 static_cast<unsigned int>(m_config.audioRate), true) ||
        !openPcm(playback, m_config.audioPlayback, SND_PCM_STREAM_PLAYBACK,
                 static_cast<unsigned int>(m_config.audioRate), true)) {
        std::fprintf(stderr, "SVXLink FM bridge: ALSA open failed (capture=%s playback=%s)\n",
                     m_config.audioCapture.c_str(), m_config.audioPlayback.c_str());
        if (capture) { snd_pcm_close(capture); capture = nullptr; }
        if (playback) { snd_pcm_close(playback); playback = nullptr; }
        m_capture = nullptr;
        m_playback = nullptr;
        m_tx = nullptr;
        return false;
    }
    m_capture = capture;
    m_playback = playback;

    m_running = true;
    m_ptt.start();
    m_rxThread = std::thread([this] { rxThread(); });
    m_txThread = std::thread([this] { txThread(); });
    std::fprintf(stderr, "SVXLink FM bridge started (rate=%d)\n", m_config.audioRate);
    return true;
}

void FmSvxlinkBridge::stop()
{
    if (!m_running.load())
        return;
    m_running = false;
    m_ptt.stop();
    if (m_playback)
        snd_pcm_drop(reinterpret_cast<snd_pcm_t*>(m_playback));
    if (m_capture)
        snd_pcm_abort(reinterpret_cast<snd_pcm_t*>(m_capture));
    if (m_rxThread.joinable())
        m_rxThread.join();
    if (m_txThread.joinable())
        m_txThread.join();

    {
        std::lock_guard<std::mutex> lock(m_pttMutex);
        if (m_pttReserved && m_arbiter.load())
            m_arbiter.load()->releaseTx();
        m_pttReserved = false;
        m_pttKeyed = false;
    }
    if (m_capture) { snd_pcm_close(reinterpret_cast<snd_pcm_t*>(m_capture)); m_capture = nullptr; }
    if (m_playback) { snd_pcm_close(reinterpret_cast<snd_pcm_t*>(m_playback)); m_playback = nullptr; }
    if (m_tx) {
        m_tx->setPtt(false);
        m_tx = nullptr;
    }
    std::fprintf(stderr, "SVXLink FM bridge stopped\n");
}

void FmSvxlinkBridge::pushRxIq(const ComplexFloat* samples, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        if (!m_rxQueue.push(samples[i]))
            m_rxDrops++;
}

void FmSvxlinkBridge::onPtt(bool on)
{
    std::fprintf(stderr, "SVXLink PTT %s\n", on ? "ON" : "OFF");
    m_transmitting = on;
    {
        std::lock_guard<std::mutex> lock(m_pttMutex);
        m_pttWant = on;
    }
    updatePtt();
}

void FmSvxlinkBridge::updatePtt()
{
    std::lock_guard<std::mutex> lock(m_pttMutex);

    const bool want = m_pttWant;
    const bool sessionOwned = m_sessionActive.load();
    ITxArbiter* arb = m_arbiter.load();

    if (!want) {
        // Physical TX-off and the arbiter release are done by the TX worker
        // (the single owner of the SX1255 TX stream).
        if (m_pttKeyed)
            m_pttKeyed = false;
        m_pttPending = false;
        return;
    }

    if (!m_pttKeyed) {
        const bool granted = sessionOwned || !arb || arb->acquireTx(Mode::FM);
        if (granted) {
            if (!sessionOwned && arb)
                m_pttReserved = true;
            m_pttKeyed = true;
            m_pttPending = false;
        } else {
            m_pttPending = true;
        }
        return;
    }

    // Already keyed.  If we keyed directly during an FM session and the
    // session has now ended while PTT is still on, move onto the arbiter.
    if (!m_pttReserved && !sessionOwned && arb) {
        if (arb->acquireTx(Mode::FM)) {
            m_pttReserved = true;
        } else {
            m_pttKeyed = false;
            m_pttPending = true;
        }
    }
}

void FmSvxlinkBridge::rxThread()
{
    auto* playback = reinterpret_cast<snd_pcm_t*>(m_playback);
    const double ifRate = kRfRate / static_cast<double>(kRxIfDecim);   // 25 kHz
    const double demodScale = ifRate / (2.0 * kPi * m_config.rxMaxDevHz);
    const double rxResampStep = ifRate / static_cast<double>(m_config.audioRate);

    ComplexDcBlocker dc(0.999F);   // original RX_IF DC blocker
    FirDecimator ifLpf(static_cast<float>(kRfRate), kRxIfFc);
    FmDiscriminator discriminator;
    FirFilter audioLpf(static_cast<float>(m_config.audioRate),
                       static_cast<float>(m_config.rxAudioLpHz));
    HighPass audioHpf(static_cast<double>(m_config.audioRate), m_config.rxHighPassHz);
    int decPhase = 0;
    double rxInT = 0.0;
    double rxNextOutT = 0.0;
    float rxPrevIf = 0.0F;
    bool rxHavePrev = false;

    std::vector<float> audio;
    std::vector<std::int16_t> pcm;
    std::uint64_t rxProcessed = 0;
    auto rxLastLog = std::chrono::steady_clock::now();

    while (m_running.load()) {
        ComplexFloat sample{};
        if (!m_rxQueue.pop(sample)) {
            const auto now = std::chrono::steady_clock::now();
            if (now - rxLastLog >= std::chrono::seconds(1)) {
                std::fprintf(stderr,
                             "SVXLink RX: %llu IQ/s, drops=%llu\n",
                             static_cast<unsigned long long>(rxProcessed),
                             static_cast<unsigned long long>(m_rxDrops.load()));
                rxProcessed = 0;
                rxLastLog = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        rxProcessed++;

        const ComplexFloat x = dc.process(sample);
        ifLpf.push(x);
        if (++decPhase < kRxIfDecim)
            continue;
        decPhase = 0;

        const ComplexFloat xIf = ifLpf.compute();
        const float aIf = discriminator.process(xIf) * static_cast<float>(demodScale);

        if (!rxHavePrev) {
            rxPrevIf = aIf;
            rxHavePrev = true;
            continue;
        }

        rxInT += 1.0;
        while (rxNextOutT <= rxInT) {
            const double frac = rxNextOutT - (rxInT - 1.0);
            if (frac < 0.0) {
                rxNextOutT += rxResampStep;
                continue;
            }
            if (frac > 1.0)
                break;

            float a = rxPrevIf + static_cast<float>(frac) * (aIf - rxPrevIf);
            a = audioLpf.process(a);
            a = audioHpf.process(a);
            a = std::clamp(a, -1.0F, 1.0F);
            audio.push_back(a);

            rxNextOutT += rxResampStep;
        }
        rxPrevIf = aIf;

        // Write whole 512-frame chunks; never drop audio on -EAGAIN.
        while (audio.size() >= kAudioPeriod && m_running.load()) {
            const std::size_t frames = (audio.size() / kAudioPeriod) * kAudioPeriod;
            pcm.resize(frames);
            for (std::size_t i = 0; i < frames; ++i) {
                pcm[i] = static_cast<std::int16_t>(
                    std::lround(audio[i] * 30000.0F));   // original int16 scale
            }

            std::size_t off = 0;
            while (off < frames && m_running.load()) {
                const snd_pcm_sframes_t written = snd_pcm_writei(
                    playback, pcm.data() + off,
                    static_cast<snd_pcm_uframes_t>(frames - off));
                if (written == -EPIPE) {
                    snd_pcm_prepare(playback);
                    continue;
                }
                if (written == -EAGAIN) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                if (written < 0)
                    break;
                off += static_cast<std::size_t>(written);
            }
            audio.erase(audio.begin(),
                        audio.begin() + static_cast<std::ptrdiff_t>(off));
        }
    }
}

void FmSvxlinkBridge::txThread()
{
    auto* capture = reinterpret_cast<snd_pcm_t*>(m_capture);
    const bool dumpTx = std::getenv("SX1255_DUMP_FM_TX") != nullptr;
    FILE* dumpIq = nullptr;
    FILE* dumpAud = nullptr;
    if (dumpTx) {
        dumpIq = std::fopen("/var/lib/sx1255-multimode/fm_tx.iq", "wb");
        dumpAud = std::fopen("/var/lib/sx1255-multimode/fm_tx_audio.s16", "wb");
    }
    // Original: fm_mod_init(&mod, TX_GAIN * TX_AUDIO_SR / RF_SR).
    // tx_dev_hz is the deviation reference: 2500 Hz keeps the original NFM
    // 12.5 kHz tuning, 5000 Hz doubles it for 25 kHz wide FM.  tx_gain stays
    // as a fine trim around that reference.
    constexpr double kTxDevReferenceHz = 2500.0;
    const double deviationScale = m_config.txDevHz / kTxDevReferenceHz;
    const double sensitivity = m_config.txGain *
        static_cast<double>(m_config.audioRate) / kRfRate * deviationScale;

    DcBlocker dc(0.995F);                       // original TX DC blocker
    FirFilter audioLpf(static_cast<float>(m_config.audioRate),
                       static_cast<float>(m_config.txAudioLpHz));
    FmModulator mod(sensitivity);

    std::vector<std::int16_t> pcm(kAudioPeriod);
    std::vector<ComplexFloat> iq;
    std::uint64_t framesRead = 0;
    double        audioSumSq = 0.0;
    int           audioPeak = 0;
    auto lastLog = std::chrono::steady_clock::now();
    auto pttLastRetry = std::chrono::steady_clock::now();
    bool          prevKeyed = false;

    while (m_running.load()) {
        const auto now2 = std::chrono::steady_clock::now();
        if (m_pttPending.load() &&
            now2 - pttLastRetry >= std::chrono::milliseconds(20)) {
            updatePtt();
            pttLastRetry = now2;
        }

        const bool keyed = m_pttKeyed.load();
        if (keyed != prevKeyed) {
            // The TX worker is the single owner of the SX1255 TX stream: PTT
            // is switched only here, between writes, so no IQ is ever clocked
            // into the I2S while the chip transitions RX<->TX.
            if (m_tx)
                m_tx->setPtt(keyed);
            if (!keyed) {
                // Release the arbiter only after the physical TX is off.
                std::lock_guard<std::mutex> lock(m_pttMutex);
                if (m_pttReserved) {
                    if (ITxArbiter* arb = m_arbiter.load())
                        arb->releaseTx();
                    m_pttReserved = false;
                }
                m_pttPending = false;
            }
            prevKeyed = keyed;
        }

        const snd_pcm_sframes_t got = snd_pcm_readi(
            capture, pcm.data(), static_cast<snd_pcm_uframes_t>(pcm.size()));
        if (got == -EAGAIN) {
            // While keyed, SVXLink may not have audio ready yet (start of
            // transmission or a silence gap in the tail).  The SX1255
            // playback stream must be fed continuously or the ALSA ring
            // underruns and the DAC repeats stale samples, which sounds like
            // regular digital noise on air.  Emit a silent carrier instead.
            if (m_pttKeyed.load() && m_tx) {
                const std::size_t chunk =
                    static_cast<std::size_t>(kRfRate / 1000.0);   // 1 ms
                std::vector<ComplexFloat> silence(chunk, mod.process(0.0F));
                m_tx->writeIQ(silence.data(), silence.size());
                if (dumpIq)
                    std::fwrite(silence.data(), sizeof(ComplexFloat), silence.size(), dumpIq);
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (got == -EPIPE) {
            snd_pcm_prepare(capture);
            continue;
        }
        if (got == -EIO) {
            // snd-aloop returns EIO while its paired playback side is not
            // connected (e.g. after an SVXLink restart).  Reopen the capture
            // side, exactly like the original sx1255-fm-repeater TX worker.
            snd_pcm_close(capture);
            capture = nullptr;
            snd_pcm_t* reopened = nullptr;
            if (openPcm(reopened, m_config.audioCapture, SND_PCM_STREAM_CAPTURE,
                        static_cast<unsigned int>(m_config.audioRate), true)) {
                capture = reopened;
                m_capture = reopened;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        if (got < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        framesRead += static_cast<std::uint64_t>(got);

        for (snd_pcm_sframes_t i = 0; i < got; ++i) {
            const std::int16_t pcmSample = pcm[static_cast<std::size_t>(i)];
            const int magnitude = pcmSample < 0 ? -pcmSample : pcmSample;
            if (magnitude > audioPeak)
                audioPeak = magnitude;
            audioSumSq += (static_cast<double>(pcmSample) / 32768.0) *
                          (static_cast<double>(pcmSample) / 32768.0);
        }

        if (keyed) {
            int upAcc = 0;
            for (snd_pcm_sframes_t i = 0; i < got; ++i) {
                float x = static_cast<float>(pcm[static_cast<std::size_t>(i)]) / 32768.0F;
                if (dumpAud)
                    std::fwrite(&pcm[static_cast<std::size_t>(i)], sizeof(std::int16_t), 1, dumpAud);
                x = dc.process(x);
                x = audioLpf.process(x);
                x = std::clamp(x, -1.0F, 1.0F);

                // Sample-and-hold 16 kHz -> 125 kHz, exactly like the original
                // tx_worker (GNU Radio generated at RF_SR directly).
                upAcc += static_cast<int>(kRfRate);
                while (upAcc >= m_config.audioRate) {
                    upAcc -= m_config.audioRate;
                    iq.push_back(mod.process(x));
                }
            }

            if (m_tx && !iq.empty())
                m_tx->writeIQ(iq.data(), iq.size());
            if (dumpIq && !iq.empty())
                std::fwrite(iq.data(), sizeof(ComplexFloat), iq.size(), dumpIq);
            iq.clear();
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(1)) {
            const double rms = framesRead > 0
                ? std::sqrt(audioSumSq / static_cast<double>(framesRead))
                : 0.0;
            std::fprintf(stderr,
                         "SVXLink TX audio%s: %llu frames/s, last got=%ld, "
                         "peak=%d (%.1f dBFS), rms=%.6f (%.1f dBFS)\n",
                         m_pttKeyed.load() ? "" : " (PTT not keyed)",
                         static_cast<unsigned long long>(framesRead),
                         static_cast<long>(got), audioPeak,
                         audioPeak > 0 ? 20.0 * std::log10(static_cast<double>(audioPeak) / 32768.0) : -120.0,
                         rms, rms > 0.0 ? 20.0 * std::log10(rms) : -120.0);
            framesRead = 0;
            audioSumSq = 0.0;
            audioPeak = 0;
            lastLog = now;
        }
    }
    if (dumpIq)
        std::fclose(dumpIq);
    if (dumpAud)
        std::fclose(dumpAud);
}

}  // namespace sx1255::fm

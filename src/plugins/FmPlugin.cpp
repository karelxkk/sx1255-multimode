#include "sx1255/plugins/PluginFactory.hpp"

#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/dsp/Goertzel.hpp"
#include "sx1255/fm/FMDsp.hpp"
#include "sx1255/fm/FmSvxlinkBridge.hpp"
#include "sx1255/fm/SqlPty.hpp"
#include "sx1255/frontend/ITransmit.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

namespace sx1255 {

namespace {

// ---------------------------------------------------------------------------
// FmPlugin
//
// NFM repeater.  While monitoring, it FM-discriminates the shared IQ stream
// and looks for the configured RX CTCSS tone (Goertzel).  Once a session is
// granted it becomes a full-duplex bridge:
//
//   RX IQ -> discriminator -> de-emphasis -> DC block
//        -> (+ regenerated CTCSS) -> pre-emphasis -> FM modulator -> TX IQ
//
// PTT is asserted for the whole session and held for hang_time_ms after the
// RX carrier drops, then the session is released.
// ---------------------------------------------------------------------------
class FmPlugin final : public IModePlugin {
public:
    FmPlugin(FmConfig config, SystemStatistics* stats)
        : m_config(config)
        , m_stats(stats)
        , m_useSvxlink(config.backend == "svxlink")
        , m_deemph(fm::kDeemphTau, kSampleRate)
        , m_preemph(fm::kPreemphTau, kSampleRate)
        , m_ctcssGen(m_config.ctcssTx, fm::kCtcssDevHz, kSampleRate)
    {
        if (m_useSvxlink) {
            m_sqlPty = std::make_unique<fm::SqlPty>(
                m_config.svxlink.sqlPty, m_config.svxlink.sqlPtyOwner);
            m_sqlPty->open();
        }
    }

    ~FmPlugin() override
    {
        if (m_bridge)
            m_bridge->stop();
    }

    std::string_view id() const noexcept override { return "fm"; }
    Mode mode() const noexcept override { return Mode::FM; }
    int priority() const noexcept override { return 10; }

    void setTransmitter(ITransmit* tx) override
    {
        m_tx = tx;
        if (m_useSvxlink)
            ensureBridge(tx);
    }

    void setTxArbiter(ITxArbiter* arbiter) override
    {
        m_arbiter = arbiter;
        if (m_bridge)
            m_bridge->setArbiter(arbiter);
    }

    Detection detect(const IQBlock& block) override
    {
        // Keep the RF -> SVXLink audio path alive even while we are only
        // monitoring (no FM session yet).  The dispatcher calls detect() both
        // in the idle monitoring phase and during an arbiter-only TX state
        // (ACTIVE_SVXLINK), so SVXLink always hears the receiver.
        if (m_useSvxlink && m_bridge)
            m_bridge->pushRxIq(block.samples, block.count);

        const double presence = ingest(block, false);
        if (presence < 0.0) {
            m_confirm = 0;
            return {false, 0.0F};
        }

        m_confirm = presence > kToneThreshold ? m_confirm + 1 : 0;
        const bool detected = m_confirm >= kConfirmBlocks;
        const float confidence = static_cast<float>(
            std::clamp(presence / (2.0 * kToneThreshold), 0.0, 1.0));

        // Drive the SVXLink squelch from the local carrier even outside an FM
        // session, so a local station can key up during SVXLink's hang tail
        // (state ACTIVE_SVXLINK) without waiting for a new session.
        if (m_useSvxlink)
            driveSql(detected, std::chrono::steady_clock::now());

        if (detected && m_stats)
            m_stats->recordDetection(Mode::FM);
        m_lastConfidence = confidence;
        return {detected, confidence};
    }

    void startSession(const SessionContext& ctx) override
    {
        m_tx = ctx.tx;
        m_active = true;
        m_hangActive = false;
        m_audio.clear();
        m_deemph.reset();
        m_dcBlock.reset();
        m_preemph.reset();
        m_fmMod.reset();
        m_ctcssGen.reset();

        if (m_useSvxlink) {
            ensureBridge(ctx.tx);
            m_bridge->setSessionActive(true);
            if (m_sqlPty) {
                m_sqlPty->set(true);   // FM detected: open SVXLink squelch
                m_sqlOpen = true;
            }
            m_carrierLostAt = {};
            m_carrierLostBlocks = 0;
        } else if (m_tx) {
            m_tx->setPtt(true);
        }
        if (m_stats)
            m_stats->recordSession(Mode::FM);
    }

    bool process(const IQBlock& block) override
    {
        const double presence = ingest(block, !m_useSvxlink);

        const auto now = std::chrono::steady_clock::now();
        const bool carrier = presence >= 0.0 && presence > kToneThreshold;

        if (m_useSvxlink) {
            // presence < 0 means the Goertzel window is still filling right
            // after startSession; keep the session for that brief settle.
            if (presence < 0.0) {
                if (m_bridge)
                    m_bridge->pushRxIq(block.samples, block.count);
                return true;
            }

            if (carrier && m_bridge)
                m_bridge->pushRxIq(block.samples, block.count);

            // Squelch must follow the RF carrier, not SVXLink's PTT.  Writing
            // 'Z' only after SVXLink releases PTT deadlocks the two: SVXLink
            // waits for the squelch to close before dropping PTT.
            driveSql(carrier, now);
            if (carrier)
                return true;

            // Squelch is closed: stop feeding receiver noise into SVXLink so
            // the hang tail stays quiet instead of producing a noise burst.

            // Keep the RF<->audio bridge alive for SVXLink's hang tail, but
            // release the session once SVXLink has dropped PTT too.
            if (m_bridge && m_bridge->transmitting())
                return true;
            return false;
        }

        if (carrier) {
            m_hangActive = false;
        } else if (!m_hangActive) {
            m_hangActive = true;
            m_hangStart = now;
        }

        // Keep the session (and PTT) during the hang tail.
        if (m_hangActive &&
            now - m_hangStart >= std::chrono::milliseconds(m_config.hangTimeMs))
            return false;
        return true;
    }

    void stopSession() override
    {
        m_active = false;
        m_hangActive = false;
        m_audio.clear();
        m_disc.reset();
        m_deemph.reset();
        m_dcBlock.reset();
        m_preemph.reset();
        m_fmMod.reset();
        m_ctcssGen.reset();
        if (m_useSvxlink) {
            m_bridge->setSessionActive(false);
            // The RF<->SVXLink audio bridge stays alive across sessions (like
            // the original duplex fm-repeater): SVXLink owns the tail, and the
            // bridge must keep forwarding its TX audio while it is keyed.
            if (m_sqlPty && m_sqlOpen)
                m_sqlPty->set(false);  // FM session over: close squelch
            m_sqlOpen = false;
            m_carrierLostAt = {};
            m_carrierLostBlocks = 0;
        } else if (m_tx) {
            m_tx->setPtt(false);
        }
    }

    PluginTelemetry telemetry() const override
    {
        PluginTelemetry t;
        t.mode = Mode::FM;
        t.metrics["ctcss_rx_hz"] = m_config.ctcssRx;
        t.metrics["ctcss_tx_hz"] = m_config.ctcssTx;
        t.metrics["ctcss_dev_hz"] = m_lastCtcssDev;
        t.metrics["confidence"] = m_lastConfidence;
        return t;
    }

private:
    void ensureBridge(ITransmit* tx)
    {
        if (m_bridge)
            return;
        if (!tx)
            return;

        fm::SvxlinkBridgeConfig bridgeCfg;
        bridgeCfg.audioCapture  = m_config.svxlink.audioCapture;
        bridgeCfg.audioPlayback = m_config.svxlink.audioPlayback;
        bridgeCfg.ptyPath       = m_config.svxlink.ptyPath;
        bridgeCfg.audioRate     = m_config.svxlink.audioRate;
        bridgeCfg.rxShiftHz     = m_config.svxlink.rxShiftHz;
        bridgeCfg.rxMaxDevHz    = m_config.svxlink.rxMaxDevHz;
        bridgeCfg.txDevHz       = m_config.svxlink.txDevHz;
        bridgeCfg.txGain        = m_config.svxlink.txGain;
        bridgeCfg.deemphTau     = m_config.svxlink.rxDeemphUs * 1e-6;
        bridgeCfg.preemphTau    = m_config.svxlink.txPreemphUs * 1e-6;
        bridgeCfg.rxAudioLpHz   = m_config.svxlink.rxAudioLpfHz;
        bridgeCfg.txAudioLpHz   = m_config.svxlink.txAudioLpfHz;
        bridgeCfg.rxHighPassHz  = m_config.svxlink.rxHighPassHz;
        m_bridge = std::make_unique<fm::FmSvxlinkBridge>(bridgeCfg);
        m_bridge->start(tx);
        m_bridge->setArbiter(m_arbiter.load());
        m_bridge->setSessionActive(m_active);
    }

    // Drive the SVXLink squelch (SQL PTY) from the local RF carrier.  Called
    // from detect() (monitoring and arbiter-only states such as the hang tail)
    // and from process() (active session) so a local station can always be
    // received, even while SVXLink holds the transmitter.
    void driveSql(bool carrier, std::chrono::steady_clock::time_point now)
    {
        if (!m_sqlPty)
            return;

        if (carrier) {
            m_carrierLostAt = {};
            m_carrierLostBlocks = 0;
            if (!m_sqlOpen) {
                m_sqlPty->set(true);
                m_sqlOpen = true;
            }
            return;
        }

        if (!m_sqlOpen)
            return;
        if (m_carrierLostAt == std::chrono::steady_clock::time_point{})
            m_carrierLostAt = now;
        const bool lostLongEnough =
            now - m_carrierLostAt >= std::chrono::milliseconds(kSquelchCloseDelayMs);
        const bool confirmed = ++m_carrierLostBlocks >= kSquelchCloseBlocks;
        if (lostLongEnough && confirmed) {
            m_sqlPty->set(false);
            m_sqlOpen = false;
        }
    }

    // Feed one IQ block through the discriminator.  When tx is true, bridge
    // the audio into the TX chain and emit IQ via ITransmit.  Returns the
    // CTCSS presence ratio over the most recent window, or -1 if not enough
    // audio has accumulated yet.
    double ingest(const IQBlock& block, bool tx)
    {
        std::vector<ComplexFloat> txIq;
        if (tx)
            txIq.reserve(block.count);

        for (std::size_t n = 0; n < block.count; ++n) {
            const float raw = m_disc.process(block.samples[n]);
            m_audio.push_back(raw);

            if (tx) {
                const float flat   = m_deemph.process(raw);
                const float dcFree = m_dcBlock.process(flat);
                const float ctcss  = m_config.ctcssTx > 0.0 ? m_ctcssGen.process() : 0.0F;
                const float pre    = m_preemph.process(dcFree + ctcss);
                txIq.push_back(m_fmMod.process(pre));
            }
        }

        // Bounded history for the Goertzel window.
        if (m_audio.size() > kWindow * 4)
            m_audio.erase(m_audio.begin(), m_audio.end() - kWindow * 2);

        if (m_audio.size() < kWindow)
            return -1.0;

        const auto begin = m_audio.end() - static_cast<std::ptrdiff_t>(kWindow);

        // Remove the discriminator DC component (carrier offset) so it cannot
        // leak into the CTCSS Goertzel bin.
        double mean = 0.0;
        for (auto it = begin; it != m_audio.end(); ++it)
            mean += static_cast<double>(*it);
        mean /= static_cast<double>(kWindow);

        std::vector<float> centered(kWindow);
        double totalEnergy = 0.0;
        std::size_t idx = 0;
        for (auto it = begin; it != m_audio.end(); ++it, ++idx) {
            const double v = static_cast<double>(*it) - mean;
            centered[idx] = static_cast<float>(v);
            totalEnergy += v * v;
        }

        const double tonePower = goertzelPower(centered.data(), kWindow,
                                               m_config.ctcssRx, block.sampleRateHz);

        // CTCSS level as deviation: the Goertzel magnitude of a sine of
        // amplitude A over N samples is A*N/2, so A = 2*sqrt(power)/N; the
        // discriminator output is radians per sample, hence the conversion.
        // Spec for a 12.5 kHz NFM system: 175-375 Hz (-23 to -16.5 dB).
        m_lastCtcssDev = 2.0 * std::sqrt(tonePower) / static_cast<double>(kWindow) *
                         (kSampleRate / (2.0 * 3.14159265358979323846));
        const auto nowTone = std::chrono::steady_clock::now();
        if (m_lastCtcssDev > 50.0 &&
            nowTone - m_lastToneLog > std::chrono::seconds(1)) {
            m_lastToneLog = nowTone;
            std::fprintf(stderr, "FM CTCSS: %.0f Hz deviation (%.1f dB rel. 2.5 kHz)\n",
                         m_lastCtcssDev, 20.0 * std::log10(m_lastCtcssDev / 2500.0));
        }

        if (tx && m_tx && !txIq.empty())
            m_tx->writeIQ(txIq.data(), txIq.size());

        return totalEnergy > 1e-12 ? tonePower / totalEnergy : 0.0;
    }

    static constexpr double      kSampleRate   = 125000.0;
    static constexpr std::size_t kWindow       = 4096;
    // CTCSS gate.  Keep the original values: a stronger threshold made the
    // squelch flap on air because real CTCSS levels hover around it.
    // Deliberately slower than M17/D-Star: a false CTCSS blip on a digital
    // signal must not grab the channel before the digital plugin wins.  The
    // tone threshold stays low (50), so real CTCSS still opens reliably.
    static constexpr std::size_t kConfirmBlocks = 10;
    static constexpr double      kToneThreshold = 50.0;
    // A single Goertzel window (16 ms at the dispatch block size) is far too
    // short to decide the carrier is gone: real CTCSS levels hover right
    // around the threshold and one weak block used to slam the squelch shut,
    // which chopped the repeated audio and made SVXLink's tail sound like
    // digital garbage.  Require several consecutive weak blocks plus a short
    // grace period before closing, mirroring the confirmation used for open.
    static constexpr long        kSquelchCloseDelayMs = 150;
    static constexpr std::size_t kSquelchCloseBlocks  = 5;

    FmConfig m_config;
    SystemStatistics* m_stats;
    ITransmit* m_tx = nullptr;
    std::atomic<ITxArbiter*> m_arbiter{nullptr};
    bool m_useSvxlink = false;
    std::unique_ptr<fm::FmSvxlinkBridge> m_bridge;
    std::unique_ptr<fm::SqlPty> m_sqlPty;

    fm::FmDiscriminator m_disc;
    fm::DeEmphasis      m_deemph;
    fm::DcBlocker       m_dcBlock;
    fm::PreEmphasis     m_preemph;
    fm::FmModulator     m_fmMod;
    fm::CtcssGenerator  m_ctcssGen;

    std::vector<float> m_audio;
    bool  m_active = false;
    bool  m_hangActive = false;
    bool  m_sqlOpen = false;
    std::chrono::steady_clock::time_point m_carrierLostAt{};
    unsigned m_carrierLostBlocks = 0;
    float m_lastConfidence = 0.0F;
    double m_lastCtcssDev = 0.0;
    std::chrono::steady_clock::time_point m_lastToneLog{};
    unsigned m_confirm = 0;
    std::chrono::steady_clock::time_point m_hangStart{};
};

}  // namespace

std::shared_ptr<IModePlugin> createFmPlugin(const FmConfig& config, SystemStatistics* stats)
{
    return std::make_shared<FmPlugin>(config, stats);
}

}  // namespace sx1255

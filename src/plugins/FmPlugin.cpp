#include "sx1255/plugins/PluginFactory.hpp"

#include "sx1255/diag/SystemStatistics.hpp"
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
// NFM repeater.  While monitoring it measures the shared IQ power (RSSI) and
// opens the FM squelch once that level stays above the configured threshold.
// Once a session is
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
        , m_confirmSamplesNeeded(static_cast<std::uint64_t>(m_config.squelchHoldMs) * 125U)
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

        const double rssi = blockRssiDb(block);
        m_lastRssiDb = rssi;

        // The carrier must hold for a while before FM claims the channel: the
        // digital modes see the same carrier and they have higher priority, so
        // a too eager FM would steal e.g. a D-Star or M17 transmission before
        // their detector confirms.  A time based hold keeps this independent
        // of the dispatch block size.
        if (rssi >= m_config.rssiSquelchDb) {
            m_confirmSamples += static_cast<std::uint64_t>(block.count);
        } else {
            m_confirmSamples = 0;
        }
        const bool detected = m_confirmSamples >= m_confirmSamplesNeeded;
        const float confidence = static_cast<float>(
            std::clamp(0.5 + (rssi - m_config.rssiSquelchDb) / 20.0, 0.0, 1.0));

        const auto now = std::chrono::steady_clock::now();
        if (rssi >= m_config.rssiSquelchDb &&
            now - m_lastRssiLog > std::chrono::seconds(2)) {
            m_lastRssiLog = now;
            std::fprintf(stderr, "FM squelch: carrier, RSSI %.1f dB\n", rssi);
        }

        if (detected) {
            if (m_countDetections && !m_detectionReported) {
                m_detectionReported = true;
                if (m_stats)
                    m_stats->recordDetection(Mode::FM);
            }
        } else {
            m_detectionReported = false;
        }
        m_lastConfidence = confidence;
        return {detected, confidence};
    }

    void startSession(const SessionContext& ctx) override
    {
        m_tx = ctx.tx;
        m_active = true;
        m_hangActive = false;
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
        const double rssi = blockRssiDb(block);
        m_lastRssiDb = rssi;
        const auto now = std::chrono::steady_clock::now();
        const bool carrier = rssi >= m_config.rssiSquelchDb;

        // Track the hang tail for both backends.  In the SVXLink backend this
        // keeps the FM session (and its immediate carrier handling) alive over
        // SVXLink's own IDLE_TIMEOUT, so re-keying during that window does not
        // have to go through the slower RSSI confirmation.
        if (carrier) {
            m_hangActive = false;
        } else if (!m_hangActive) {
            m_hangActive = true;
            m_hangStart = now;
        }

        if (m_useSvxlink) {
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
            if (m_bridge && (m_bridge->transmitting() || m_bridge->pttKeyed()))
                return true;
            if (m_hangActive &&
                now - m_hangStart < std::chrono::milliseconds(m_config.hangTimeMs))
                return true;
            return false;
        }

        processLocalTx(block);
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

    // Another mode won the channel: drop the RSSI confirmation window so FM
    // cannot immediately take over when that session ends.
    void clearPendingDetection() override
    {
        m_confirmSamples = 0;
    }

    void setDetectionCounting(bool on) override
    {
        m_countDetections = on;
    }

    PluginTelemetry telemetry() const override
    {
        PluginTelemetry t;
        t.mode = Mode::FM;
        t.metrics["ctcss_tx_hz"] = m_config.ctcssTx;
        t.metrics["rssi_db"] = m_lastRssiDb;
        t.metrics["rssi_squelch_db"] = m_config.rssiSquelchDb;
        t.metrics["squelch_open"] = m_sqlOpen ? 1.0 : 0.0;
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
    // only from process(), i.e. once FM already owns the channel, so a
    // digital carrier cannot make SVXLink key up before arbitration.
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

    // Local-backend TX: demodulate, de-emphasise, re-insert the optional
    // CTCSS, pre-emphasise and re-modulate the block.
    void processLocalTx(const IQBlock& block)
    {
        std::vector<ComplexFloat> txIq;
        txIq.reserve(block.count);

        for (std::size_t n = 0; n < block.count; ++n) {
            const float raw = m_disc.process(block.samples[n]);
            const float flat   = m_deemph.process(raw);
            const float dcFree = m_dcBlock.process(flat);
            const float ctcss  = m_config.ctcssTx > 0.0 ? m_ctcssGen.process() : 0.0F;
            const float pre    = m_preemph.process(dcFree + ctcss);
            txIq.push_back(m_fmMod.process(pre));
        }

        if (m_tx && !txIq.empty())
            m_tx->writeIQ(txIq.data(), txIq.size());
    }

    static double blockRssiDb(const IQBlock& block)
    {
        if (block.count == 0)
            return -120.0;
        double power = 0.0;
        for (std::size_t n = 0; n < block.count; ++n) {
            const double i = static_cast<double>(block.samples[n].i);
            const double q = static_cast<double>(block.samples[n].q);
            power += i * i + q * q;
        }
        power /= static_cast<double>(block.count);
        return 10.0 * std::log10(power + 1e-12);
    }

    static constexpr double kSampleRate = 125000.0;
    // A single noise window (16 ms at the dispatch block size) is far too
    // short to decide the carrier is gone: one noisy block used to slam the
    // squelch shut,
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

    bool  m_active = false;
    bool  m_hangActive = false;
    bool  m_sqlOpen = false;
    std::chrono::steady_clock::time_point m_carrierLostAt{};
    unsigned m_carrierLostBlocks = 0;
    float m_lastConfidence = 0.0F;
    double m_lastRssiDb = -120.0;
    std::chrono::steady_clock::time_point m_lastRssiLog{};
    std::uint64_t m_confirmSamples = 0;
    std::uint64_t m_confirmSamplesNeeded = 87500;
    bool m_countDetections = true;
    bool m_detectionReported = false;
    std::chrono::steady_clock::time_point m_hangStart{};
};

}  // namespace

std::shared_ptr<IModePlugin> createFmPlugin(const FmConfig& config, SystemStatistics* stats)
{
    return std::make_shared<FmPlugin>(config, stats);
}

}  // namespace sx1255

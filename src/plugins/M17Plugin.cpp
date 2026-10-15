#include "sx1255/plugins/PluginFactory.hpp"

#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/frontend/ITransmit.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Network.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sx1255 {

namespace {

// ---------------------------------------------------------------------------
// M17Plugin
//
// Pure adapter.  It owns no DSP, no synchronization and no decoding logic:
// those live in m17::M17DSP, m17::M17Synchronizer and m17::M17FrameDecoder,
// which are 1:1 ports of m17-gateway.  The plugin only wires IQDispatcher
// IQ blocks into the RX chain and forwards decoded frames.
// ---------------------------------------------------------------------------
class M17Plugin final : public IModePlugin {
public:
    M17Plugin(M17Config config, SystemStatistics* stats)
        : m_dcr(0.9999)
        , m_decimator(m17::designChannelFilter(m17::channelBWSX1255, m17::sampleRateSX1255,
                                                m17::channelTapsSX1255), m17::rxDecimSX1255)
        , m_afc(m17::basebandDCAvgCntSX1255, m17::rxFastNSX1255, m17::rxSlowNSX1255)
        , m_resampler(24, 25)
        , m_s2s(std::vector<double>(m17::rrcTaps5.begin(), m17::rrcTaps5.end()),
                m17::rxScalingCoeffSX1255)
        , m_synchronizer(5, [this](std::uint16_t typ, const std::vector<m17::SoftBit>& softBits) {
              const auto now = std::chrono::steady_clock::now();
              m_lastActivity = now;
              // Noise produces the occasional stray correlation.  Require a
              // burst of several syncs inside a short window before declaring
              // M17; a real stream emits them every frame (~25/s).
              if (m_syncBurst == 0 || now - m_syncBurstStart > std::chrono::milliseconds(500)) {
                  m_syncBurst = 1;
                  m_syncBurstStart = now;
              } else {
                  ++m_syncBurst;
              }
              if (m_syncBurst >= kMinSyncBurst) {
                  m_detected = true;
                  m_syncConfidence = 1.0;
              }
              m_frameCount++;
              if (typ == m17::LSFSync)
                  m_lsfCount++;
              if (typ == m17::EOTMarker)
                  m_eotSeen = true;
              m_decoder.decodeFrame(typ, softBits);
          })
        , m_decoder(m17::Decoder::Callbacks{
              [this](const m17::LSF&, double) {       // onLSF
                  m_lsfDecoded = true;
                  m_lastActivity = std::chrono::steady_clock::now();
                  std::fprintf(stderr, "M17: LSF decoded\n");
              },
              [this](const m17::LSF& lsf, const std::vector<std::uint8_t>& payload,
                     std::uint16_t sid, std::uint16_t fn, double) {   // onStream
                  m_lastActivity = std::chrono::steady_clock::now();
                  forwardStream(lsf, sid, fn, payload, false);
              },
              [this](const m17::LSF&, double) {        // onStreamLICH
                  m_lastActivity = std::chrono::steady_clock::now();
              },
              [this](const m17::LSF& lsf, std::uint16_t sid, std::uint16_t fn, double) {  // onStreamEOT
                  m_lastActivity = std::chrono::steady_clock::now();
                  forwardStream(lsf, sid, fn, {}, true);
              },
              [this](const m17::LSF&, const std::vector<std::uint8_t>&, double) {  // onPacket
                  m_lastActivity = std::chrono::steady_clock::now();
              }})
        , m_m17Config(std::move(config))
        , m_stats(stats)
    {
        if (m_m17Config.reflector.enabled && !m_m17Config.reflector.host.empty()) {
            m17::M17ReflectorClient::Config net;
            net.host     = m_m17Config.reflector.host;
            net.port     = m_m17Config.reflector.port;
            net.module   = m_m17Config.reflector.module;
            net.callsign = m_m17Config.callsign;
            m_reflector = std::make_unique<m17::M17ReflectorClient>(
                net,
                [this](m17::ReflectorState state) { m_reflectorState = state; },
                [this](const m17::StreamDatagram& dg) {
                    {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        m_txQueue.push_back(dg);
                    }
                    m_txCv.notify_one();
                },
                m17::M17ReflectorClient::PacketHandler{});
            m_reflector->start();
            m_networkTxThread = std::thread([this] {
                for (;;) {
                    m17::StreamDatagram dg;
                    {
                        std::unique_lock<std::mutex> lock(m_queueMutex);
                        m_txCv.wait(lock, [this] {
                            return !m_txQueue.empty() || m_watchdogStop.load();
                        });
                        if (m_watchdogStop.load())
                            return;
                        dg = std::move(m_txQueue.front());
                        m_txQueue.pop_front();
                    }
                    processNetworkStream(dg);
                }
            });
            m_watchdog = std::thread([this] {
                while (!m_watchdogStop.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    ITransmit* tx = m_tx.load();
                    ITxArbiter* arbiter = m_arbiter.load();
                    std::lock_guard<std::mutex> lock(m_txMutex);
                    if (m_txActive &&
                        std::chrono::steady_clock::now() - m_txLastFrameTime >
                            std::chrono::milliseconds(kNetTxTimeoutMs)) {
                        if (tx)
                            tx->setPtt(false);
                        m_txActive = false;
                        m_txStreamId = 0xFFFF;
                        if (m_netTxGranted && arbiter)
                            arbiter->releaseTx();
                        m_netTxGranted = false;
                        std::fprintf(stderr, "M17 NET TX: timeout, released\n");
                    }
                }
            });
        }
    }

    ~M17Plugin() override
    {
        m_watchdogStop = true;
        m_txCv.notify_all();
        if (m_watchdog.joinable())
            m_watchdog.join();
        if (m_networkTxThread.joinable())
            m_networkTxThread.join();
    }

    std::string_view id() const noexcept override { return "m17"; }
    Mode mode() const noexcept override { return Mode::M17; }
    int priority() const noexcept override { return 30; }

    void setTransmitter(ITransmit* tx) override
    {
        m_tx = tx;
    }

    void setTxArbiter(ITxArbiter* arbiter) override
    {
        m_arbiter = arbiter;
    }

    Detection detect(const IQBlock& block) override
    {
        m_detected = false;
        m_lsfDecoded = false;
        feed(block);
        // A valid LSF (Golay + CRC) is trusted immediately; otherwise require
        // two syncs inside a short window.  A single noise correlation used to
        // grab the channel and starve FM for the whole inactivity timeout.
        const bool detected = m_detected || m_lsfDecoded;
        if (detected && m_stats)
            m_stats->recordDetection(Mode::M17);
        return {detected, detected ? 1.0F : 0.0F};
    }

    void startSession(const SessionContext&) override
    {
        m_active = true;
        m_eotSeen = false;
        m_detected = false;
        m_syncBurst = 0;
        m_lastActivity = std::chrono::steady_clock::now();
        if (m_stats)
            m_stats->recordSession(Mode::M17);
        m_synchronizer.reset();
    }

    bool process(const IQBlock& block) override
    {
        feed(block);
        // Release the channel once the M17 end-of-transmission marker is seen.
        if (m_eotSeen)
            return false;
        // A false sync in noise must not hold the channel forever: if no valid
        // M17 frame is decoded for this long, drop the session.
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastActivity > std::chrono::milliseconds(kIdleTimeoutMs))
            return false;
        return true;
    }

    void stopSession() override
    {
        m_active = false;
        m_eotSeen = false;
        m_syncBurst = 0;
        m_synchronizer.reset();
        m_decoder.reset();
    }

    PluginTelemetry telemetry() const override
    {
        PluginTelemetry t;
        t.mode = Mode::M17;
        t.metrics["sync_confidence"] = m_syncConfidence;
        t.metrics["lsf_count"] = static_cast<double>(m_lsfCount);
        t.metrics["frame_count"] = static_cast<double>(m_frameCount);
        if (m_reflector) {
            const auto net = m_reflector->telemetry();
            t.strings["reflector_host"] = net.host;
            t.metrics["reflector_connected"] = net.connected ? 1.0 : 0.0;
            t.metrics["reflector_last_heartbeat"] = static_cast<double>(net.lastHeartbeatSec);
            t.metrics["reflector_reconnects"] = static_cast<double>(net.reconnectCount);
            t.metrics["reflector_rx_packets"] = static_cast<double>(net.rxStreams);
            t.metrics["reflector_tx_packets"] = static_cast<double>(net.txStreams);
            t.strings["reflector_state"] = m17::toString(net.state);
        }
        return t;
    }

private:
    void processNetworkStream(const m17::StreamDatagram& sd)
    {
        // Half-duplex: hold reflector audio while a local RF stream is active.
        if (m_active.load())
            return;
        ITransmit* tx = m_tx.load();
        if (!tx)
            return;
        ITxArbiter* arbiter = m_arbiter.load();

        std::lock_guard<std::mutex> lock(m_txMutex);

        m17::StreamDatagram dg = sd;
        dg.lsf.src = m17::encodeCallsign(m_m17Config.callsign);
        dg.lsf.calcCRC();

        const bool first = dg.streamId != m_txStreamId;
        if (first) {
            // Ask the global arbiter before keying.  If denied, hold the
            // stream and skip this frame; a later frame can retry.
            if (arbiter && !arbiter->acquireTx(Mode::M17)) {
                std::fprintf(stderr, "M17 NET TX: blocked (channel busy)\n");
                return;
            }
            m_netTxGranted = true;
            m_txStreamId = dg.streamId;
            std::fprintf(stderr, "M17 NET TX: acquired sid=%04x\n", dg.streamId);
            m_txShaper.reset();
            m_txResampler.reset();
            m_txFMMod.reset();
        } else if (!m_netTxGranted) {
            return;
        }

        std::vector<m17::Symbol> symbols;
        if (first) {
            std::vector<m17::Symbol> pre = m17::appendPreamble({}, false);
            symbols.insert(symbols.end(), pre.begin(), pre.end());
            std::vector<m17::Symbol> lsf = m17::generateLSFSymbols(dg.lsf);
            symbols.insert(symbols.end(), lsf.begin(), lsf.end());
        }

        std::vector<std::uint8_t> payload(dg.payload.begin(), dg.payload.end());
        const std::uint16_t txFn = static_cast<std::uint16_t>(
            dg.frameNumber | (dg.lastFrame ? 0x8000 : 0));
        std::vector<m17::Symbol> frame =
            m17::generateStreamSymbols(dg.lsf, txFn, payload);
        symbols.insert(symbols.end(), frame.begin(), frame.end());
        if (dg.lastFrame) {
            std::vector<m17::Symbol> eot = m17::appendEOT({});
            symbols.insert(symbols.end(), eot.begin(), eot.end());
        }

        std::fprintf(stderr,
                     "M17 NET TX: sid=%04x fn=%04x last=%d first=%d symbols=%zu\n",
                     dg.streamId, dg.frameNumber, dg.lastFrame ? 1 : 0,
                     first ? 1 : 0, symbols.size());

        // Continuous TX DSP state, exactly like m17-gateway: RRC shaping,
        // 24k -> 125k resampling and FM modulation persist across frames so
        // the waveform is continuous and the frame sync words are shaped
        // with the filter history they need.
        std::vector<double> baseband = m_txShaper.process(symbols);
        for (double& b : baseband)
            b *= std::sqrt(5.0);
        const std::vector<double> resampled = m_txResampler.process(baseband);
        const std::vector<m17::CD> iq = m_txFMMod.modulate(resampled, 800.0);
        std::vector<ComplexFloat> out(iq.size());
        for (std::size_t i = 0; i < iq.size(); ++i)
            out[i] = {static_cast<float>(iq[i].re), static_cast<float>(iq[i].im)};

        if (!m_txActive) {
            tx->setPtt(true);
            m_txActive = true;
        }
        if (std::getenv("SX1255_DUMP_M17_TX")) {
            if (FILE* f = std::fopen("/var/lib/sx1255-multimode/m17_tx.iq", "ab")) {
                for (const ComplexFloat& s : out) {
                    std::fwrite(&s.i, sizeof(float), 1, f);
                    std::fwrite(&s.q, sizeof(float), 1, f);
                }
                std::fclose(f);
            }
        }
        tx->writeIQ(out.data(), out.size());
        m_txLastFrameTime = std::chrono::steady_clock::now();

        if (dg.lastFrame) {
            // Drain the ALSA playback so the EOT symbols actually reach the
            // RF before TX is disabled, then leave a short TX tail, exactly
            // like m17-gateway's stopTX (Drain + txTail).
            tx->drainTx();
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            tx->setPtt(false);
            m_txActive = false;
            m_txStreamId = 0xFFFF;
            if (m_netTxGranted && arbiter)
                arbiter->releaseTx();
            m_netTxGranted = false;
        }
    }

    void forwardStream(const m17::LSF& lsf, std::uint16_t sid, std::uint16_t fn,
                       const std::vector<std::uint8_t>& payload, bool lastFrame)
    {
        if (!m_reflector)
            return;
        m17::StreamDatagram dg;
        dg.streamId = sid;
        dg.frameNumber = fn & 0x7FFF;
        dg.lastFrame = lastFrame;
        dg.lsf = lsf;
        const std::size_t n = std::min(payload.size(), dg.payload.size());
        std::copy_n(payload.begin(), n, dg.payload.begin());
        std::fprintf(stderr,
                     "M17 RX->reflector: sid=%04x fn=%04x dst=%s src=%s last=%d\n",
                     sid, fn, lsf.dstCallsign().c_str(), lsf.srcCallsign().c_str(),
                     lastFrame ? 1 : 0);
        m_reflector->sendStream(dg);
    }

    void feed(const IQBlock& block)
    {
        // The RF frontend supplies float IQ; m17-gateway DSP works in
        // float64 (Go complex128), so widen without rescaling.
        for (std::size_t n = 0; n < block.count; ++n) {
            const m17::CD sample{double(block.samples[n].i), double(block.samples[n].q)};
            for (const m17::CD dc : m_decimator.process(m_dcr.process(sample))) {
                const double dem = m_afc.process(dc);
                for (const double r : m_resampler.process(dem))
                    m_synchronizer.push(m_s2s.process(r));
            }
        }
    }

    m17::ComplexDCRemoval   m_dcr;
    m17::PolyphaseDecimator m_decimator;
    m17::FMDemodAFC         m_afc;
    m17::RationalResampler  m_resampler;
    m17::SampleToSymbol     m_s2s;
    m17::M17Synchronizer    m_synchronizer;
    m17::Decoder            m_decoder;
    m17::TXPulseShaper      m_txShaper{
        std::vector<double>(m17::rrcTaps5.begin(), m17::rrcTaps5.end()), 5};
    m17::BatchResampler     m_txResampler{125, 24};
    m17::BatchFMModulator   m_txFMMod{125000.0};
    M17Config               m_m17Config;
    std::unique_ptr<m17::M17ReflectorClient> m_reflector;
    std::atomic<m17::ReflectorState> m_reflectorState{m17::ReflectorState::Disconnected};
    std::atomic<ITransmit*> m_tx{nullptr};
    std::atomic<ITxArbiter*> m_arbiter{nullptr};
    std::mutex               m_txMutex;
    std::mutex               m_queueMutex;
    std::condition_variable  m_txCv;
    std::deque<m17::StreamDatagram> m_txQueue;
    std::uint16_t            m_txStreamId = 0xFFFF;
    bool                     m_txActive = false;
    bool                     m_netTxGranted = false;
    std::chrono::steady_clock::time_point m_txLastFrameTime{};
    std::atomic<bool>        m_watchdogStop{false};
    std::thread              m_watchdog;
    std::thread              m_networkTxThread;
    SystemStatistics*       m_stats;
    std::atomic<bool> m_active{false};
    bool m_detected = false;
    bool m_lsfDecoded = false;
    bool m_eotSeen = false;
    std::chrono::steady_clock::time_point m_lastActivity{};
    std::uint64_t m_lsfCount = 0;
    std::uint64_t m_frameCount = 0;
    double m_syncConfidence = 0.0;
    int m_syncBurst = 0;
    std::chrono::steady_clock::time_point m_syncBurstStart{};

    static constexpr long kIdleTimeoutMs = 1500;
    static constexpr int  kMinSyncBurst = 5;
    static constexpr long kNetTxTimeoutMs = 1000;
};

}  // namespace

std::shared_ptr<IModePlugin> createM17Plugin(const M17Config& config, SystemStatistics* stats)
{
    return std::make_shared<M17Plugin>(config, stats);
}

}  // namespace sx1255

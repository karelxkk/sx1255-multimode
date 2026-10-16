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

// syncThresholds turns the M17 configuration into synchronizer thresholds;
// the defaults are m17-gateway's values.
m17::SyncThresholds syncThresholds(const M17Config& config)
{
    m17::SyncThresholds t;
    t.lsf    = static_cast<float>(config.syncLsfThreshold);
    t.packet = static_cast<float>(config.syncPacketThreshold);
    t.stream = static_cast<float>(config.syncStreamThreshold);
    t.eot    = static_cast<float>(config.syncEotThreshold);
    t.lsfSync = static_cast<float>(config.syncLsfSyncThreshold);
    t.follow = static_cast<float>(config.syncFollowThreshold);
    return t;
}

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
              // M17; a real stream emits them every frame (~25/s).  This only
              // decides who owns the channel: decoding below runs regardless,
              // so a short packet is never lost because it did not raise the
              // burst count.
              if (m_syncBurst == 0 || now - m_syncBurstStart > std::chrono::milliseconds(500)) {
                  m_syncBurst = 1;
                  m_syncBurstStart = now;
              } else {
                  ++m_syncBurst;
              }
              if (m_syncBurst >= m_m17Config.minSyncBurst) {
                  m_detected = true;
                  m_syncConfidence = 1.0;
              }
              m_frameCount++;
              m_syncCount++;
              if (typ == m17::LSFSync)
                  m_lsfCount++;
              if (m_m17Config.logSync) {
                  // Burst level diagnostics: what the synchronizer actually
                  // hears, how close the match was and in which state the
                  // decoder is.  Packet relevant bursts (LSF, packet frames)
                  // are always logged; stream bursts are rate limited so a
                  // voice transmission cannot flood the journal.
                  const m17::SyncMetrics metrics = m_synchronizer.lastMetrics();
                  char components[160];
                  std::snprintf(components, sizeof(components),
                                "only=%.2f pair=%.2f end=%.2f lsf=%.2f eot=%.2f",
                                static_cast<double>(typ == m17::PacketSync ? metrics.packetOnly
                                                   : (typ == m17::StreamSync ? metrics.streamOnly
                                                                             : metrics.lsf)),
                                static_cast<double>(typ == m17::StreamSync ? metrics.streamPair
                                                                            : metrics.packetPair),
                                static_cast<double>(typ == m17::StreamSync ? metrics.streamEnd
                                                                            : metrics.packetEnd),
                                static_cast<double>(metrics.lsf),
                                static_cast<double>(metrics.eotPair));
                  const std::string line = syncName(typ) +
                      " dist=" + std::to_string(static_cast<double>(m_synchronizer.lastDistance())) +
                      " " + components +
                      " follow=" + (m_synchronizer.lastFollow() ? "1" : "0") +
                      " samples=" + std::to_string(m_synchronizer.samples()) +
                      " decoder=" + (m_decoder.syncedType() == m17::PacketSync ? "packet"
                                      : (m_decoder.syncedType() == m17::StreamSync ? "stream" : "idle"));
                  // LSF and packet frames are logged as they come (there are
                  // only a handful per transmission); stream bursts are rate
                  // limited, otherwise a voice transmission floods the journal.
                  const bool everyBurst = typ == m17::LSFSync || typ == m17::PacketSync;
                  logDiag(m_diagSync, "sync", line, !everyBurst);
              }
              m_decoder.decodeFrame(typ, softBits);
              // Follow the decoder: while a packet transmission is running, a
              // single packet sync word identifies its frames, so the last
              // frame is not lost when the sync word that would follow it is
              // missing or truncated.
              m_synchronizer.setExpectedType(m_decoder.syncedType() == m17::PacketSync
                                                 ? m17::PacketSync
                                                 : 0);
          },
          syncThresholds(config))
        , m_decoder(m17::Decoder::Callbacks{
              [this](const m17::LSF& lsf, double) {   // onLSF
                  m_lsfDecoded = true;
                  m_lastActivity = std::chrono::steady_clock::now();
                  std::fprintf(stderr, "M17: LSF decoded (%s %s>%s)\n",
                               lsf.lsfType() == 0 ? "packet" : "stream",
                               lsf.srcCallsign().c_str(), lsf.dstCallsign().c_str());
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
              [this](const m17::LSF& lsf, const std::vector<std::uint8_t>& payload, double ber) {  // onPacket
                  m_lastActivity = std::chrono::steady_clock::now();
                  m_rfPackets++;
                  std::vector<std::uint8_t> packetBytes = lsf.toBytes();
                  packetBytes.insert(packetBytes.end(), payload.begin(), payload.end());
                  bool forwarded = false;
                  if (m_reflector && m_reflector->sendPacket(packetBytes))
                      forwarded = true;
                  if (m_bridge && m_bridge->sendPacket(packetBytes))
                      forwarded = true;
                  if (forwarded)
                      ++m_rfPacketsForwarded;
                  else
                      ++m_rfPacketsNotForwarded;
                  std::fprintf(stderr,
                               "M17 RF->NET packet: dst=%s src=%s payload=%zu bytes ber=%.1f%% %s\n",
                               lsf.dstCallsign().c_str(), lsf.srcCallsign().c_str(),
                               payload.size(), ber, forwarded ? "forwarded" : "NOT forwarded");
              },
              [this](const m17::LSF& lsf, double ber) {   // onLSFBadCRC
                  ++m_lsfBadCrc;
                  logDiag(m_diagLsf, "LSF CRC fail",
                          lsf.dstCallsign() + ">" + lsf.srcCallsign() +
                              " ber=" + std::to_string(ber));
              },
              [this](const m17::LSF& lsf, const std::vector<std::uint8_t>& payload, double ber) {  // onPacketBadCRC
                  ++m_rfPacketsBadCrc;
                  std::fprintf(stderr,
                               "M17 RF->NET packet CRC fail: dst=%s src=%s len=%zu ber=%.1f%% data=",
                               lsf.dstCallsign().c_str(), lsf.srcCallsign().c_str(),
                               payload.size(), ber);
                  for (const std::uint8_t b : payload)
                      std::fprintf(stderr, "%02x", b);
                  std::fprintf(stderr, "\n");
              },
              [this](std::uint16_t typ, const char* reason) {   // onFrameDropped
                  ++m_framesDropped;
                  logDiag(m_diagFrame, "frame not usable", std::string(reason) +
                                                            " (sync=0x" +
                                                            hexSync(typ) + ")");
              },
              [this]() {   // onEOT: the decoder saw the end of a transmission
                  m_eotSeen = true;
                  m_lastActivity = std::chrono::steady_clock::now();
              },
              [this](const m17::LSF& lsf) {   // onPacketReattached
                  m_lastActivity = std::chrono::steady_clock::now();
                  std::fprintf(stderr,
                               "M17 RF->NET: frame re-attached to the previous packet LSF (%s>%s)\n",
                               lsf.srcCallsign().c_str(), lsf.dstCallsign().c_str());
              },
              [this](std::uint8_t field, bool lastFrame, int errors) {   // onPacketFrame
                  if (!m_m17Config.logSync)
                      return;
                  std::fprintf(stderr, "M17 packet frame: %s=%u errors=%d\n",
                               lastFrame ? "bytes" : "frame", field, errors);
              }})
        , m_m17Config(std::move(config))
        , m_stats(stats)
    {
        auto streamHandler = [this](const m17::StreamDatagram& dg) {
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_txQueue.push_back(dg);
            }
            m_txCv.notify_one();
        };
        auto packetHandler = [this](const std::vector<std::uint8_t>& packet) {
            processNetworkPacket(packet);
        };

        if (m_m17Config.reflector.enabled && !m_m17Config.reflector.host.empty()) {
            m17::M17ReflectorClient::Config net;
            net.host     = m_m17Config.reflector.host;
            net.port     = m_m17Config.reflector.port;
            net.module   = m_m17Config.reflector.module;
            net.callsign = m_m17Config.callsign;
            m_reflector = std::make_unique<m17::M17ReflectorClient>(
                net,
                [this](m17::ReflectorState state) { m_reflectorState = state; },
                streamHandler,
                packetHandler);
            m_reflector->start();
        }

        if (m_m17Config.bridge.enabled && !m_m17Config.bridge.host.empty()) {
            m17::M17ReflectorClient::Config net;
            net.host     = m_m17Config.bridge.host;
            net.port     = m_m17Config.bridge.port;
            net.module   = m_m17Config.bridge.module;
            net.callsign = m_m17Config.bridge.callsign.empty()
                ? m_m17Config.callsign
                : m_m17Config.bridge.callsign;
            m_bridge = std::make_unique<m17::M17ReflectorClient>(
                net,
                [this](m17::ReflectorState state) { m_bridgeState = state; },
                streamHandler,
                packetHandler);
            m_bridge->start();
        }

        if (m_reflector || m_bridge) {
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
                    {
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

                    // Packets that waited for a free channel: forget the ones
                    // that waited too long, retry the rest.
                    const auto now = std::chrono::steady_clock::now();
                    const auto ttl = std::chrono::milliseconds(
                        m_m17Config.networkPacketTtlMs > 0 ? m_m17Config.networkPacketTtlMs : 1);
                    std::size_t expired = 0;
                    {
                        std::lock_guard<std::mutex> lock(m_pendingMutex);
                        while (!m_pendingPackets.empty() &&
                               now - m_pendingPackets.front().received > ttl) {
                            m_pendingPackets.pop_front();
                            ++expired;
                        }
                    }
                    if (expired > 0) {
                        m_netPacketsDropped += expired;
                        std::fprintf(stderr,
                                     "M17 NET RX: %zu packet(s) expired after %d ms without a free channel\n",
                                     expired, m_m17Config.networkPacketTtlMs);
                    }
                    transmitPendingPacket();
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
        // A quiet channel must not leave a stale packet header behind that a
        // much later frame could be attached to.
        if (std::chrono::steady_clock::now() - m_lastActivity >
            std::chrono::milliseconds(m_m17Config.idleTimeoutMs))
            m_decoder.expireRetention();
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
        // The synchronizer buffer is deliberately not cleared: it already
        // holds samples of the transmission being received, and discarding
        // them used to lose the packet frame that follows the LSF.
    }

    // monitor keeps the M17 receive path alive while another plugin owns the
    // session (FM, D-Star or their hang time).  Packet mode carries data that
    // has nothing to do with the other session, so it must not be ignored.
    void monitor(const IQBlock& block) override
    {
        if (!m_m17Config.decodeDuringOtherSessions)
            return;
        feed(block);
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
        if (now - m_lastActivity > std::chrono::milliseconds(m_m17Config.idleTimeoutMs))
            return false;
        return true;
    }

    void stopSession() override
    {
        m_active = false;
        m_eotSeen = false;
        m_syncBurst = 0;
        // A packet that is still being reassembled must survive the session
        // end; only an open stream has to go, because its LSF is stale once
        // the session is over.
        if (m_decoder.syncedType() == m17::StreamSync)
            m_decoder.reset();
    }

    PluginTelemetry telemetry() const override
    {
        PluginTelemetry t;
        t.mode = Mode::M17;
        t.metrics["sync_confidence"] = m_syncConfidence;
        t.metrics["lsf_count"] = static_cast<double>(m_lsfCount);
        t.metrics["frame_count"] = static_cast<double>(m_frameCount);
        t.metrics["sync_count"] = static_cast<double>(m_syncCount);
        // Packet statistics: an operator can see that nothing was lost and,
        // when something was, why.
        const m17::Decoder::Counters counters = m_decoder.counters();
        t.metrics["lsf_bad_crc"] = static_cast<double>(m_lsfBadCrc);
        t.metrics["resync_count"] = static_cast<double>(counters.resync);
        t.metrics["packet_frames"] = static_cast<double>(counters.packetFrames);
        t.metrics["packets_rx"] = static_cast<double>(counters.packets);
        t.metrics["packets_bad_crc"] = static_cast<double>(counters.packetsBadCRC);
        t.metrics["frames_dropped"] = static_cast<double>(m_framesDropped);
        t.metrics["frames_reattached"] = static_cast<double>(counters.reattached);
        t.metrics["frames_improved"] = static_cast<double>(counters.framesImproved);
        t.metrics["packets_duplicate"] = static_cast<double>(counters.duplicates);
        t.metrics["rf_packets"] = static_cast<double>(m_rfPackets);
        t.metrics["rf_packets_forwarded"] = static_cast<double>(m_rfPacketsForwarded);
        t.metrics["rf_packets_not_forwarded"] = static_cast<double>(m_rfPacketsNotForwarded);
        t.metrics["net_packets_queued"] = static_cast<double>(m_netPacketsQueued);
        t.metrics["net_packets_sent"] = static_cast<double>(m_netPacketsSent);
        t.metrics["net_packets_dropped"] = static_cast<double>(m_netPacketsDropped);
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            t.metrics["net_packets_pending"] = static_cast<double>(m_pendingPackets.size());
        }
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
        if (m_bridge) {
            const auto net = m_bridge->telemetry();
            t.strings["bridge_host"] = net.host;
            t.metrics["bridge_connected"] = net.connected ? 1.0 : 0.0;
            t.metrics["bridge_rx_packets"] = static_cast<double>(net.rxPackets);
            t.metrics["bridge_tx_packets"] = static_cast<double>(net.txPackets);
            t.strings["bridge_state"] = m17::toString(m_bridgeState.load());
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

    // processNetworkPacket queues a packet coming from the network and hands it
    // to the transmitter as soon as the channel is free.  A packet is never
    // thrown away just because the channel happened to be busy: it waits (up to
    // networkPacketTtlMs) and the watchdog retries it.
    void processNetworkPacket(const std::vector<std::uint8_t>& packetBytes)
    {
        if (packetBytes.size() < m17::LSFLen + 3) {
            ++m_netPacketsDropped;
            std::fprintf(stderr, "M17 NET RX packet too short (%zu bytes), discarded\n",
                         packetBytes.size());
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            const std::size_t limit = m_m17Config.networkPacketQueue > 0
                ? static_cast<std::size_t>(m_m17Config.networkPacketQueue)
                : 1;
            while (m_pendingPackets.size() >= limit) {
                m_pendingPackets.pop_front();
                ++m_netPacketsDropped;
                std::fprintf(stderr, "M17 NET RX queue is full, dropping the oldest packet\n");
            }
            m_pendingPackets.push_back(PendingPacket{packetBytes, std::chrono::steady_clock::now()});
            ++m_netPacketsQueued;
        }
        transmitPendingPacket();
    }

    // transmitPendingPacket sends the oldest queued packet if the channel is
    // free; otherwise it leaves it in the queue for a later attempt.
    void transmitPendingPacket()
    {
        std::vector<std::uint8_t> packetBytes;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            if (m_pendingPackets.empty())
                return;
            packetBytes = m_pendingPackets.front().bytes;
        }

        // Half duplex: hold the packet while a local RF session is running.
        if (m_active.load())
            return;
        ITransmit* tx = m_tx.load();
        ITxArbiter* arbiter = m_arbiter.load();
        if (!tx || !arbiter)
            return;

        std::lock_guard<std::mutex> lock(m_txMutex);
        // A continuous network stream owns the TX chain; do not inject a
        // packet into the middle of it.
        if (m_txActive || m_netTxGranted)
            return;
        // A packet is data for M17 and must reach the radio even when the
        // network audio source is currently another mode.
        if (!arbiter->acquireTxPacket(Mode::M17))
            return;

        const m17::LSF lsf = m17::LSF::newFromBytes(packetBytes.data());
        const std::vector<std::uint8_t> packetPayload(
            packetBytes.begin() + static_cast<std::ptrdiff_t>(m17::LSFLen),
            packetBytes.end());
        const std::vector<m17::Symbol> symbols =
            m17::generatePacketSymbols(lsf, packetPayload);

        m_txShaper.reset();
        m_txResampler.reset();
        m_txFMMod.reset();
        std::vector<double> baseband = m_txShaper.process(symbols);
        for (double& b : baseband)
            b *= std::sqrt(5.0);
        const std::vector<double> resampled = m_txResampler.process(baseband);
        const std::vector<m17::CD> iq = m_txFMMod.modulate(resampled, 800.0);

        std::vector<ComplexFloat> out(iq.size());
        for (std::size_t i = 0; i < iq.size(); ++i)
            out[i] = {static_cast<float>(iq[i].re), static_cast<float>(iq[i].im)};

        tx->setPtt(true);
        tx->writeIQ(out.data(), out.size());
        tx->drainTx();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        tx->setPtt(false);
        arbiter->releaseTx();

        {
            std::lock_guard<std::mutex> pendingLock(m_pendingMutex);
            if (!m_pendingPackets.empty())
                m_pendingPackets.pop_front();
        }
        ++m_netPacketsSent;
        std::fprintf(stderr,
                     "M17 NET TX packet: dst=%s src=%s payload=%zu bytes\n",
                     lsf.dstCallsign().c_str(), lsf.srcCallsign().c_str(),
                     packetPayload.size());
    }

    void forwardStream(const m17::LSF& lsf, std::uint16_t sid, std::uint16_t fn,
                       const std::vector<std::uint8_t>& payload, bool lastFrame)
    {
        if (!m_reflector && !m_bridge)
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
        if (m_reflector)
            m_reflector->sendStream(dg);
        if (m_bridge)
            m_bridge->sendStream(dg);
    }

    // Diagnostics are rate limited: every event is counted (telemetry), but a
    // marginal signal must not flood the journal - a stream whose LSF was
    // missed rejects frames 25 times per second.
    struct DiagState {
        std::chrono::steady_clock::time_point last{};
        std::uint64_t suppressed = 0;
    };

    static std::string hexSync(std::uint16_t typ)
    {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04x", typ);
        return buf;
    }

    static std::string syncName(std::uint16_t typ)
    {
        switch (typ) {
            case m17::LSFSync:    return "LSF";
            case m17::PacketSync: return "packet";
            case m17::StreamSync: return "stream";
            case m17::EOTMarker:  return "EOT";
            default:              return hexSync(typ);
        }
    }

    // Called from the receive thread only.  With limit == false every event is
    // printed, otherwise at most one line per second (the counter still keeps
    // track of everything).
    void logDiag(DiagState& state, const char* kind, const std::string& message, bool limit = true)
    {
        const auto now = std::chrono::steady_clock::now();
        if (limit && state.last != std::chrono::steady_clock::time_point{} &&
            now - state.last < std::chrono::seconds(1)) {
            ++state.suppressed;
            return;
        }
        const std::uint64_t suppressed = state.suppressed;
        state.suppressed = 0;
        state.last = now;
        if (suppressed != 0)
            std::fprintf(stderr, "M17 %s: %s (+%llu suppressed)\n", kind, message.c_str(),
                         static_cast<unsigned long long>(suppressed));
        else
            std::fprintf(stderr, "M17 %s: %s\n", kind, message.c_str());
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
    std::unique_ptr<m17::M17ReflectorClient> m_bridge;
    std::atomic<m17::ReflectorState> m_reflectorState{m17::ReflectorState::Disconnected};
    std::atomic<m17::ReflectorState> m_bridgeState{m17::ReflectorState::Disconnected};
    std::atomic<ITransmit*> m_tx{nullptr};
    std::atomic<ITxArbiter*> m_arbiter{nullptr};
    std::mutex               m_txMutex;
    std::mutex               m_queueMutex;
    std::condition_variable  m_txCv;
    std::deque<m17::StreamDatagram> m_txQueue;

    // Network packets waiting for a free transmit slot.
    struct PendingPacket {
        std::vector<std::uint8_t> bytes;
        std::chrono::steady_clock::time_point received;
    };
    mutable std::mutex m_pendingMutex;
    std::deque<PendingPacket> m_pendingPackets;

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
    std::uint64_t m_syncCount = 0;
    std::uint64_t m_lsfBadCrc = 0;
    std::uint64_t m_framesDropped = 0;
    std::uint64_t m_rfPackets = 0;
    std::uint64_t m_rfPacketsForwarded = 0;
    std::uint64_t m_rfPacketsNotForwarded = 0;
    std::uint64_t m_rfPacketsBadCrc = 0;
    std::uint64_t m_netPacketsQueued = 0;
    std::uint64_t m_netPacketsSent = 0;
    std::uint64_t m_netPacketsDropped = 0;
    DiagState m_diagLsf;
    DiagState m_diagFrame;
    DiagState m_diagSync;
    double m_syncConfidence = 0.0;
    int m_syncBurst = 0;
    std::chrono::steady_clock::time_point m_syncBurstStart{};

    static constexpr long kNetTxTimeoutMs = 1000;
};

}  // namespace

std::shared_ptr<IModePlugin> createM17Plugin(const M17Config& config, SystemStatistics* stats)
{
    return std::make_shared<M17Plugin>(config, stats);
}

}  // namespace sx1255

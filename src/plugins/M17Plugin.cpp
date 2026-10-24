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
#include <ctime>
#include <deque>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
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
    struct Candidate {
        std::string timestamp;
        double rssiDbfs = -120.0;
        double afcHz = 0.0;
        float syncDistance = 0.0F;
        std::string syncType;
        std::string result;
    };
    struct FrameAuditEntry {
        std::string timestamp;
        double rssiDbfs = -120.0;
        double afcHz = 0.0;
        float syncDistance = 0.0F;
        double softbitRms = 0.0;
        double merDb = -1.0;
        double berPercent = -1.0;
        int channelErrors = -1;
        int channelBits = 0;
        int viterbiMetric = -1;
        bool viterbiOk = false;
        int golayOk = 0;
        int golayFail = 0;
        int crcStatus = -1;
        int lsfStatus = -1;
        int frameStatus = -1;
        std::string syncType;
        std::string result;
    };
    struct SoftbitAggregate {
        std::uint64_t failedFrames = 0;
        std::uint64_t softbits = 0;
        std::uint64_t saturatedLow = 0;
        std::uint64_t saturatedHigh = 0;
        std::uint64_t nearCentre = 0;
        std::array<std::uint64_t, 16> histogram{};
        m17::SoftbitStats last;
    };

    static bool validatedBer(const FrameAuditEntry& entry)
    {
        return entry.crcStatus == 1 && entry.frameStatus == 1 &&
               entry.berPercent >= 0.0;
    }

    static bool measuredMer(const FrameAuditEntry& entry)
    {
        return entry.result == "M17" && entry.frameStatus != 0 &&
               entry.merDb >= 0.0;
    }

    static double symbolMerDb(const std::vector<m17::Symbol>& symbols)
    {
        if (symbols.empty())
            return -1.0;

        static constexpr std::array<m17::Symbol, 4> levels = {
            -3.0F, -1.0F, 1.0F, 3.0F
        };
        double signalPower = 0.0;
        double errorPower = 0.0;
        for (const m17::Symbol symbol : symbols) {
            m17::Symbol nearest = levels[0];
            for (const m17::Symbol level : levels) {
                if (std::abs(symbol - level) < std::abs(symbol - nearest))
                    nearest = level;
            }
            const double error = static_cast<double>(symbol - nearest);
            signalPower += static_cast<double>(nearest) * nearest;
            errorPower += error * error;
        }
        if (signalPower < 1e-12)
            return -1.0;
        if (errorPower < 1e-12)
            return 40.0;
        return 10.0 * std::log10(signalPower / errorPower);
    }

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
              m_frameCount++;
              m_syncCount++;
              if (typ == m17::LSFSync)
                  m_lsfCount++;
              // Noise produces the occasional stray correlation, and a header
              // that matched by accident must not claim the channel.  A burst
              // only counts towards detection when it carried something the
              // decoder could use: it was already inside a transmission, or it
              // just produced a valid header.  Decoding below runs regardless,
              // so a short packet is never lost because it did not raise the
              // burst count.
              const std::uint16_t syncedBefore = m_decoder.syncedType();
              const m17::SyncEvaluation evaluation = m_synchronizer.lastEvaluation();
              m_lsfWasSyncOnly =
                  m_synchronizer.lastKind() == m17::M17Synchronizer::Kind::SyncOnly;
              m_frameRejected = false;
              m_decoder.decodeFrame(typ, softBits);
              const m17::FrameAudit frameAudit = m_decoder.lastFrameAudit();
              const double frameMerDb = typ == m17::EOTMarker
                  ? -1.0 : symbolMerDb(m_synchronizer.lastPayloadSymbols());
              // Being inside a stream is not enough to make an LSF burst
              // useful: random payload can contain an LSF-like 16-symbol
              // pattern.  Only a CRC-valid LSF may claim the transmission.
              const bool carrierFrame = typ != m17::LSFSync &&
                  frameAudit.frameStatus != 0 &&
                  (syncedBefore != 0 || m_decoder.syncedType() != 0);
              const bool useful = m_lsfDecoded || carrierFrame;
              const char* result = typ == m17::EOTMarker
                  ? "EOT" : (useful ? "M17" : "Rejected");
              recordFrameAudit(evaluation, frameAudit, result, frameMerDb);
              recordSyncBurst(evaluation, result, useful);
              if (useful) {
                  if (m_syncBurst == 0 ||
                      now - m_syncBurstStart > std::chrono::milliseconds(500)) {
                      m_syncBurst = 1;
                      m_syncBurstStart = now;
                  } else {
                      ++m_syncBurst;
                  }
                  if (m_syncBurst >= m_m17Config.minSyncBurst) {
                      m_detected = true;
                      m_syncConfidence = 1.0;
                  }
              }
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
              // A header that carried nothing valid must not swallow the
              // frames behind it: hand the signal back to the synchronizer.
              if (m_lsfRejected) {
                  m_synchronizer.retract(syncedBefore != 0);
                  m_lsfRejected = false;
              } else if (m_frameRejected && syncedBefore != 0) {
                  m_synchronizer.retract(true);
              }
              m_frameRejected = false;
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
                  // A header recognised by its sync word alone matches in data
                  // regions many times per second; count those apart so the CRC
                  // counter still means "a real header failed".
                  if (m_lsfWasSyncOnly)
                      ++m_lsfSyncRejects;
                  else
                      ++m_lsfBadCrc;
                  m_lsfRejected = true;
                  if (!m_lsfWasSyncOnly)
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
                  m_frameRejected = true;
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
                  // A packet frame alone must not keep the session alive: a
                  // false packet LSF can otherwise be followed by endless
                  // noise frames and never time out.  Valid LSF/stream/packet
                  // events update m_lastActivity.
                  if (!m_m17Config.logSync)
                      return;
                  std::fprintf(stderr, "M17 packet frame: %s=%u errors=%d\n",
                               lastFrame ? "bytes" : "frame", field, errors);
              }})
        , m_m17Config(std::move(config))
        , m_stats(stats)
    {
        m_synchronizer.setEvaluationSink(
            static_cast<float>(m_m17Config.diagnostics.nearMissMaxDistance),
            [this](const m17::SyncEvaluation& evaluation) {
                onSyncEvaluation(evaluation);
            });

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
                [this](m17::ReflectorState state) {
                    m_reflectorState = state;
                    std::fprintf(stderr, "M17 reflector: %s\n",
                                 m17::toString(state));
                },
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
                [this](m17::ReflectorState state) {
                    m_bridgeState = state;
                    std::fprintf(stderr, "M17 bridge: %s\n",
                                 m17::toString(state));
                },
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
        std::lock_guard<std::mutex> lock(m_diagMutex);
        if (m_frameAudits.size() > 0)
            writeSoftbitStatsLocked();
        if (m_candidateOut.is_open())
            m_candidateOut.close();
        if (m_frameAuditOut.is_open())
            m_frameAuditOut.close();
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

    void setNearMissHandler(std::function<void()> handler) override
    {
        m_nearMissHandler = std::move(handler);
    }

    Detection detect(const IQBlock& block) override
    {
        const double rssi = blockRssiDb(block);
        m_lastRssiDb = rssi;
        m_detected = false;
        m_lsfDecoded = false;
        // A quiet channel must not leave a stale packet header behind that a
        // much later frame could be attached to.
        if (std::chrono::steady_clock::now() - m_lastActivity >
            std::chrono::milliseconds(m_m17Config.idleTimeoutMs))
            m_decoder.expireRetention();
        feed(block);
        // A valid LSF is the acquisition trigger.  Do not make M17 wait for
        // several stream syncs: a short packet or a brief voice burst would
        // otherwise lose the channel to FM.  The RSSI gate is the protection
        // against noise-floor false headers.
        const bool raw = m_lsfDecoded || m_detected ||
                         m_decoder.syncedType() != 0;
        const bool detected = raw && rssi >= m_m17Config.rssiGateDbfs;
        m_modeDetected = detected;
        {
            std::lock_guard<std::mutex> lock(m_diagMutex);
            m_diagAfcHz = m_afc.offsetHz(25000.0);
            m_averageSymbolLevel = m_symbolLevel;
        }
        if (detected) {
            if (m_countDetections && !m_detectionReported) {
                m_detectionReported = true;
                if (m_stats)
                    m_stats->recordDetection(Mode::M17);
            }
        } else {
            m_detectionReported = false;
        }
        return {detected, detected ? 1.0F : 0.0F};
    }

    void setDetectionCounting(bool on) override
    {
        m_countDetections = on;
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
        std::lock_guard<std::mutex> lock(m_diagMutex);
        t.metrics["afc_correction_hz"] = m_diagAfcHz;
        t.metrics["sync_distance"] = m_diagSyncDistance;
        t.metrics["sync_confidence"] = m_syncConfidence;
        t.metrics["rssi_db"] = m_lastRssiDb;
        t.metrics["rssi_gate_db"] = m_m17Config.rssiGateDbfs;
        t.metrics["lsf_count"] = static_cast<double>(m_lsfCount);
        t.metrics["frame_count"] = static_cast<double>(m_frameCount);
        t.metrics["sync_count"] = static_cast<double>(m_syncCount);
        t.metrics["sync_hits"] = static_cast<double>(m_syncHits);
        t.metrics["sync_misses"] = static_cast<double>(m_syncMisses);
        t.metrics["average_symbol_level"] = m_averageSymbolLevel;
        t.metrics["rssi_at_sync"] = m_rssiAtSync;
        double berSum = 0.0;
        int berCount = 0;
        double lastBer = 0.0;
        double merSum = 0.0;
        int merCount = 0;
        double lastMer = -1.0;
        for (const FrameAuditEntry& entry : m_frameAudits) {
            if (validatedBer(entry)) {
                berSum += entry.berPercent;
                ++berCount;
            }
            if (measuredMer(entry)) {
                merSum += entry.merDb;
                ++merCount;
            }
        }
        for (auto it = m_frameAudits.rbegin(); it != m_frameAudits.rend(); ++it) {
            if (validatedBer(*it)) {
                lastBer = it->berPercent;
                break;
            }
        }
        for (auto it = m_frameAudits.rbegin(); it != m_frameAudits.rend(); ++it) {
            if (measuredMer(*it)) {
                lastMer = it->merDb;
                break;
            }
        }
        t.metrics["ber_percent"] = lastBer;
        t.metrics["current_ber_percent"] = m_decoder.currentBerPercent();
        t.metrics["last_frame_ber_percent"] = lastBer;
        t.metrics["average_ber_percent"] =
            berCount > 0 ? berSum / berCount : 0.0;
        t.metrics["mer_db"] = lastMer;
        t.metrics["last_frame_mer_db"] = lastMer;
        t.metrics["average_mer_db"] =
            merCount > 0 ? merSum / merCount : -1.0;
        const m17::LayerCountersSnapshot layers = m_decoder.layerCounters();
        t.metrics["sync_detected"] = static_cast<double>(layers.syncDetected);
        t.metrics["lsf_ok"] = static_cast<double>(layers.lsfOk);
        t.metrics["lsf_fail"] = static_cast<double>(layers.lsfFail);
        t.metrics["crc_ok"] = static_cast<double>(layers.crcOk);
        t.metrics["crc_fail"] = static_cast<double>(layers.crcFail);
        t.metrics["golay_ok"] = static_cast<double>(layers.golayOk);
        t.metrics["golay_fail"] = static_cast<double>(layers.golayFail);
        t.metrics["viterbi_ok"] = static_cast<double>(layers.viterbiOk);
        t.metrics["viterbi_fail"] = static_cast<double>(layers.viterbiFail);
        t.metrics["frame_ok"] = static_cast<double>(layers.frameOk);
        t.metrics["frame_fail"] = static_cast<double>(layers.frameFail);
        // Packet statistics: an operator can see that nothing was lost and,
        // when something was, why.
        const m17::Decoder::Counters counters = m_decoder.counters();
        t.metrics["lsf_bad_crc"] = static_cast<double>(m_lsfBadCrc);
        t.metrics["lsf_sync_rejects"] = static_cast<double>(m_lsfSyncRejects);
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
        t.metrics["rf_streams"] = static_cast<double>(m_rfStreams);
        t.metrics["rf_streams_forwarded"] =
            static_cast<double>(m_rfStreamsForwarded);
        t.metrics["rf_streams_not_forwarded"] =
            static_cast<double>(m_rfStreamsNotForwarded);
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

    std::string diagnosticsJson() const override
    {
        std::lock_guard<std::mutex> lock(m_diagMutex);
        const m17::LayerCountersSnapshot layers = m_decoder.layerCounters();
        double berSum = 0.0;
        int berCount = 0;
        double lastBer = 0.0;
        double merSum = 0.0;
        int merCount = 0;
        double lastMer = -1.0;
        for (const FrameAuditEntry& entry : m_frameAudits) {
            if (validatedBer(entry)) {
                berSum += entry.berPercent;
                ++berCount;
            }
            if (measuredMer(entry)) {
                merSum += entry.merDb;
                ++merCount;
            }
        }
        for (auto it = m_frameAudits.rbegin(); it != m_frameAudits.rend(); ++it) {
            if (validatedBer(*it)) {
                lastBer = it->berPercent;
                break;
            }
        }
        for (auto it = m_frameAudits.rbegin(); it != m_frameAudits.rend(); ++it) {
            if (measuredMer(*it)) {
                lastMer = it->merDb;
                break;
            }
        }
        std::ostringstream out;
        out << std::fixed << std::setprecision(3);
        out << "{\"afc_correction_hz\":" << m_diagAfcHz
            << ",\"sync_distance\":" << m_diagSyncDistance
            << ",\"sync_confidence\":" << m_syncConfidence
            << ",\"frame_count\":" << m_frameCount
            << ",\"sync_hits\":" << m_syncHits
            << ",\"sync_misses\":" << m_syncMisses
            << ",\"average_symbol_level\":" << m_averageSymbolLevel
            << ",\"rssi_at_sync\":" << m_rssiAtSync
            << ",\"ber_percent\":" << lastBer
            << ",\"current_ber_percent\":" << m_decoder.currentBerPercent()
            << ",\"last_frame_ber_percent\":" << lastBer
            << ",\"average_ber_percent\":"
            << (berCount > 0 ? berSum / berCount : 0.0)
            << ",\"mer_db\":" << lastMer
            << ",\"last_frame_mer_db\":" << lastMer
            << ",\"average_mer_db\":"
            << (merCount > 0 ? merSum / merCount : -1.0)
            << ",\"sync_detected\":" << layers.syncDetected
            << ",\"lsf_ok\":" << layers.lsfOk
            << ",\"lsf_fail\":" << layers.lsfFail
            << ",\"crc_ok\":" << layers.crcOk
            << ",\"crc_fail\":" << layers.crcFail
            << ",\"golay_ok\":" << layers.golayOk
            << ",\"golay_fail\":" << layers.golayFail
            << ",\"viterbi_ok\":" << layers.viterbiOk
            << ",\"viterbi_fail\":" << layers.viterbiFail
            << ",\"frame_ok\":" << layers.frameOk
            << ",\"frame_fail\":" << layers.frameFail
            << ",\"softbit_stats\":{\"failed_frames\":" << m_softbits.failedFrames
            << ",\"softbits\":" << m_softbits.softbits
            << ",\"saturated_low\":" << m_softbits.saturatedLow
            << ",\"saturated_high\":" << m_softbits.saturatedHigh
            << ",\"near_centre\":" << m_softbits.nearCentre
            << ",\"last_rms\":" << m_softbits.last.rms
            << "},\"frames\":[";
        bool firstFrame = true;
        for (const FrameAuditEntry& entry : m_frameAudits) {
            if (!firstFrame)
                out << ',';
            firstFrame = false;
            out << "{\"timestamp\":\"" << entry.timestamp
                << "\",\"sync_type\":\"" << entry.syncType
                << "\",\"rssi_dbfs\":" << entry.rssiDbfs
                << ",\"afc_correction_hz\":" << entry.afcHz
                << ",\"sync_distance\":" << entry.syncDistance
                << ",\"softbit_rms\":" << entry.softbitRms
                     << ",\"mer_db\":" << entry.merDb
                << ",\"ber_percent\":" << entry.berPercent
                << ",\"channel_errors\":" << entry.channelErrors
                << ",\"channel_bits\":" << entry.channelBits
                << ",\"viterbi_metric\":" << entry.viterbiMetric
                << ",\"viterbi_ok\":" << (entry.viterbiOk ? "true" : "false")
                << ",\"golay_ok\":" << entry.golayOk
                << ",\"golay_fail\":" << entry.golayFail
                << ",\"crc\":\"" << statusName(entry.crcStatus)
                << "\",\"lsf\":\"" << statusName(entry.lsfStatus)
                << "\",\"frame\":\"" << statusName(entry.frameStatus)
                << "\",\"result\":\"" << entry.result << "\"}";
        }
        out << "],\"candidates\":[";
        bool first = true;
        for (const Candidate& candidate : m_candidates) {
            if (!first)
                out << ',';
            first = false;
            out << "{\"timestamp\":\"" << candidate.timestamp
                << "\",\"rssi_dbfs\":" << candidate.rssiDbfs
                << ",\"afc_correction_hz\":" << candidate.afcHz
                << ",\"sync_distance\":" << candidate.syncDistance
                << ",\"sync_type\":\"" << candidate.syncType
                << "\",\"result\":\"" << candidate.result << "\"}";
        }
        out << "]}";
        return out.str();
    }

    void resetDiagnostics() override
    {
        m_decoder.resetLayerCounters();
        std::lock_guard<std::mutex> lock(m_diagMutex);
        m_candidates.clear();
        m_frameAudits.clear();
        m_softbits = SoftbitAggregate{};
        m_syncHits = 0;
        m_syncMisses = 0;
        m_lastRejectedCandidate = {};
        m_lastFrameArtifact = {};
        m_lastSoftbitWrite = {};
        writeSoftbitStatsLocked();
    }

private:
    static std::string candidateTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t value = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&value, &tm);
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &tm);
        return text;
    }

    static std::string syncTypeName(std::uint16_t type)
    {
        switch (type) {
            case m17::LSFSync:    return "LSF";
            case m17::PacketSync: return "Packet";
            case m17::StreamSync: return "Stream";
            case m17::EOTMarker:  return "EOT";
            default:              return "None";
        }
    }

    static std::string statusName(int status)
    {
        if (status == 1) return "OK";
        if (status == 0) return "FAIL";
        return "N/A";
    }

    static std::string auditTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t value = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm{};
        localtime_r(&value, &tm);
        char text[40];
        std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &tm);
        std::ostringstream out;
        out << text << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return out.str();
    }

    static std::string hexBytes(const std::vector<std::uint8_t>& bytes)
    {
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (const std::uint8_t value : bytes)
            out << std::setw(2) << static_cast<unsigned int>(value);
        return out.str();
    }

    static std::string hardBitString(const std::vector<m17::SoftBit>& bits)
    {
        std::string out;
        out.reserve(bits.size());
        for (const m17::SoftBit bit : bits)
            out += bit > m17::SoftMaybe ? '1' : '0';
        return out;
    }

    void recordFrameAudit(const m17::SyncEvaluation& evaluation,
                          const m17::FrameAudit& audit,
                          const char* result,
                          double merDb)
    {
        FrameAuditEntry entry;
        entry.timestamp = auditTimestamp();
        entry.rssiDbfs = m_lastRssiDb;
        entry.afcHz = m_afc.offsetHz(25000.0);
        entry.syncDistance = evaluation.distance;
        entry.softbitRms = audit.softbitStats.rms;
        entry.merDb = merDb;
        entry.channelErrors = audit.channelErrors;
        entry.channelBits = audit.channelBits;
        if (audit.channelErrors >= 0 && audit.channelBits > 0)
            entry.berPercent = 100.0 * audit.channelErrors / audit.channelBits;
        entry.viterbiMetric = audit.viterbiMetric;
        entry.viterbiOk = audit.viterbiOk;
        entry.golayOk = audit.golayOk;
        entry.golayFail = audit.golayFail;
        entry.crcStatus = audit.crcStatus;
        entry.lsfStatus = audit.lsfStatus;
        entry.frameStatus = audit.frameStatus;
        entry.syncType = syncTypeName(audit.syncType);
        entry.result = result;

        const bool failed =
            audit.frameStatus == 0 || audit.crcStatus == 0 || audit.lsfStatus == 0;
        std::string artifact;
        bool requestCapture = false;

        {
            std::lock_guard<std::mutex> lock(m_diagMutex);
            m_frameAudits.push_back(entry);
            if (m_frameAudits.size() > 100)
                m_frameAudits.pop_front();

            if (failed) {
                ++m_softbits.failedFrames;
                m_softbits.softbits += audit.softbitStats.count;
                m_softbits.saturatedLow += audit.softbitStats.saturated_low;
                m_softbits.saturatedHigh += audit.softbitStats.saturated_high;
                m_softbits.nearCentre += audit.softbitStats.near_centre;
                for (std::size_t i = 0; i < m_softbits.histogram.size(); ++i)
                    m_softbits.histogram[i] += audit.softbitStats.histogram[i];
                m_softbits.last = audit.softbitStats;
            }

            writeFrameAuditLog(entry);

            const auto now = std::chrono::steady_clock::now();
            if (m_softbits.failedFrames > 0 &&
                (m_lastSoftbitWrite == std::chrono::steady_clock::time_point{} ||
                 now - m_lastSoftbitWrite >= std::chrono::seconds(2))) {
                writeSoftbitStatsLocked();
                m_lastSoftbitWrite = now;
            }

            const auto& d = m_m17Config.diagnostics;
            if (audit.crcStatus == 0 && evaluation.distance < 4.0F &&
                m_lastRssiDb >= d.nearMissRssiDbfs &&
                (m_lastFrameArtifact == std::chrono::steady_clock::time_point{} ||
                 now - m_lastFrameArtifact >= std::chrono::milliseconds(500))) {
                m_lastFrameArtifact = now;
                std::ostringstream json;
                json << std::fixed << std::setprecision(3);
                json << "{\"timestamp\":\"" << entry.timestamp
                     << "\",\"sync_type\":\"" << entry.syncType
                     << "\",\"rssi_dbfs\":" << entry.rssiDbfs
                     << ",\"afc_correction_hz\":" << entry.afcHz
                     << ",\"sync_distance\":" << entry.syncDistance
                     << ",\"softbit_rms\":" << entry.softbitRms
                << ",\"mer_db\":" << entry.merDb
                     << ",\"ber_percent\":" << entry.berPercent
                     << ",\"channel_errors\":" << entry.channelErrors
                     << ",\"channel_bits\":" << entry.channelBits
                     << ",\"softbit_stats\":{\"count\":" << audit.softbitStats.count
                     << ",\"mean\":" << audit.softbitStats.mean
                     << ",\"rms\":" << audit.softbitStats.rms
                     << ",\"saturated_low\":" << audit.softbitStats.saturated_low
                     << ",\"saturated_high\":" << audit.softbitStats.saturated_high
                     << ",\"near_centre\":" << audit.softbitStats.near_centre
                     << "},\"viterbi_metric\":" << entry.viterbiMetric
                     << ",\"viterbi_ok\":" << (entry.viterbiOk ? "true" : "false")
                     << ",\"golay_ok\":" << entry.golayOk
                     << ",\"golay_fail\":" << entry.golayFail
                     << ",\"crc\":\"" << statusName(entry.crcStatus)
                     << "\",\"lsf\":\"" << statusName(entry.lsfStatus)
                     << "\",\"viterbi_output_hex\":\"" << hexBytes(audit.viterbiOutput)
                     << "\",\"deinterleaved_bits\":\""
                     << hardBitString(audit.deinterleavedSoftbits)
                     << "\",\"softbits\":[";
                for (std::size_t i = 0; i < audit.rawSoftbits.size(); ++i) {
                    if (i)
                        json << ',';
                    json << audit.rawSoftbits[i];
                }
                json << "]}";
                artifact = json.str();
                requestCapture = true;
            }
        }

        if (!artifact.empty()) {
            namespace fs = std::filesystem;
            const fs::path dir(m_m17Config.diagnostics.auditDirectory);
            std::error_code ec;
            fs::create_directories(dir, ec);
            const fs::path path = dir /
                ("frame_" + entry.timestamp + "_" +
                 std::to_string(m_frameAuditSequence.fetch_add(1)) + ".json");
            std::ofstream out(path, std::ios::trunc);
            if (out.is_open())
                out << artifact << '\n';
        }
        if (requestCapture && m_nearMissHandler)
            m_nearMissHandler();
    }

    void writeFrameAuditLog(const FrameAuditEntry& entry)
    {
        const auto& d = m_m17Config.diagnostics;
        if (!d.frameAuditLog)
            return;
        namespace fs = std::filesystem;
        const fs::path path(d.frameAuditLogFile);
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        if (!m_frameAuditOut.is_open()) {
            m_frameAuditOut.open(path, std::ios::app);
            if (!m_frameAuditOut.is_open())
                return;
        }
        std::ostringstream line;
        line << entry.timestamp
             << " RSSI=" << std::fixed << std::setprecision(1) << entry.rssiDbfs
             << " AFC=" << std::showpos << std::setprecision(0) << entry.afcHz
             << std::noshowpos << "Hz SyncDistance=" << std::setprecision(2)
             << entry.syncDistance << " SoftbitRMS=" << std::setprecision(3)
             << entry.softbitRms << " MER=" << std::setprecision(2)
             << entry.merDb << "dB BER=" << std::setprecision(2)
             << entry.berPercent << "% ViterbiMetric=" << entry.viterbiMetric
             << " Viterbi=" << (entry.viterbiOk ? "OK" : "FAIL")
             << " Golay=" << (entry.golayOk + entry.golayFail == 0
                                  ? "N/A"
                                  : (entry.golayFail == 0 ? "OK" : "FAIL"))
             << " CRC=" << statusName(entry.crcStatus)
             << " LSF=" << statusName(entry.lsfStatus)
             << " Frame=" << statusName(entry.frameStatus)
             << " Type=" << entry.syncType << " Result=" << entry.result << '\n';
        const std::string text = line.str();
        const std::uintmax_t current = fs::file_size(path, ec);
        if (!ec && current > 0 && current + text.size() > d.frameAuditLogMaxBytes) {
            m_frameAuditOut.close();
            for (int i = d.frameAuditLogBackups; i >= 1; --i) {
                const fs::path from = i == 1
                    ? path : fs::path(path.string() + "." + std::to_string(i - 1));
                const fs::path to = fs::path(path.string() + "." + std::to_string(i));
                fs::rename(from, to, ec);
                ec.clear();
            }
            m_frameAuditOut.open(path, std::ios::trunc);
            if (!m_frameAuditOut.is_open())
                return;
        }
        m_frameAuditOut << text;
        m_frameAuditOut.flush();
    }

    void writeSoftbitStatsLocked()
    {
        namespace fs = std::filesystem;
        const fs::path dir(m_m17Config.diagnostics.auditDirectory);
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::ofstream out(dir / "softbit_stats.json", std::ios::trunc);
        if (!out.is_open())
            return;
        out << std::fixed << std::setprecision(3)
            << "{\"failed_frames\":" << m_softbits.failedFrames
            << ",\"softbits\":" << m_softbits.softbits
            << ",\"saturated_low\":" << m_softbits.saturatedLow
            << ",\"saturated_high\":" << m_softbits.saturatedHigh
            << ",\"near_centre\":" << m_softbits.nearCentre
            << ",\"histogram16\":[";
        for (std::size_t i = 0; i < m_softbits.histogram.size(); ++i) {
            if (i)
                out << ',';
            out << m_softbits.histogram[i];
        }
        out << "],\"last\":{\"count\":" << m_softbits.last.count
            << ",\"mean\":" << m_softbits.last.mean
            << ",\"rms\":" << m_softbits.last.rms
            << ",\"minimum\":" << m_softbits.last.minimum
            << ",\"maximum\":" << m_softbits.last.maximum
            << ",\"saturated_low\":" << m_softbits.last.saturated_low
            << ",\"saturated_high\":" << m_softbits.last.saturated_high
            << ",\"near_centre\":" << m_softbits.last.near_centre << "}}\n";
    }

    void onSyncEvaluation(const m17::SyncEvaluation& evaluation)
    {
        if (evaluation.accepted)
            return;
        const auto& d = m_m17Config.diagnostics;
        if (evaluation.distance < d.nearMissMinDistance ||
            evaluation.distance > d.nearMissMaxDistance ||
            m_lastRssiDb < d.nearMissRssiDbfs) {
            return;
        }
        recordCandidate(evaluation, "Rejected", true);
    }

    void recordSyncBurst(const m17::SyncEvaluation& evaluation,
                         const char* result,
                         bool useful)
    {
        recordCandidate(evaluation, result, !useful);
    }

    void recordCandidate(const m17::SyncEvaluation& evaluation,
                         const char* result,
                         bool rejected)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto& d = m_m17Config.diagnostics;
        bool requestCapture = false;

        {
            std::lock_guard<std::mutex> lock(m_diagMutex);
            if (rejected && d.nearMissIntervalMs > 0 &&
                m_lastRejectedCandidate != std::chrono::steady_clock::time_point{} &&
                now - m_lastRejectedCandidate <
                    std::chrono::milliseconds(d.nearMissIntervalMs)) {
                return;
            }
            if (rejected)
                m_lastRejectedCandidate = now;

            m_diagAfcHz = m_afc.offsetHz(25000.0);
            m_diagSyncDistance = evaluation.distance;
            m_rssiAtSync = m_lastRssiDb;
            if (!rejected)
                ++m_syncHits;
            else
                ++m_syncMisses;

            Candidate candidate;
            candidate.timestamp = candidateTimestamp();
            candidate.rssiDbfs = m_lastRssiDb;
            candidate.afcHz = m_diagAfcHz;
            candidate.syncDistance = evaluation.distance;
            candidate.syncType = syncTypeName(evaluation.type);
            candidate.result = result;
            m_candidates.push_back(std::move(candidate));
            if (m_candidates.size() > 100)
                m_candidates.pop_front();

            writeCandidateLog(m_candidates.back());

            const bool inWindow =
                evaluation.distance >= d.nearMissMinDistance &&
                evaluation.distance <= d.nearMissMaxDistance;
            requestCapture = rejected && inWindow &&
                m_lastRssiDb >= d.nearMissRssiDbfs;
        }

        if (requestCapture && m_nearMissHandler)
            m_nearMissHandler();
    }

    void writeCandidateLog(const Candidate& candidate)
    {
        const auto& d = m_m17Config.diagnostics;
        if (!d.candidateLog)
            return;

        namespace fs = std::filesystem;
        const fs::path path(d.candidateLogFile);
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);

        if (!m_candidateOut.is_open()) {
            m_candidateOut.open(path, std::ios::app);
            if (!m_candidateOut.is_open())
                return;
        }

        std::ostringstream line;
        line << candidate.timestamp
             << " RSSI=" << std::fixed << std::setprecision(1) << candidate.rssiDbfs
             << " AFC=" << std::showpos << std::setprecision(0) << candidate.afcHz
             << std::noshowpos << " Hz SyncDistance=" << std::setprecision(2)
             << candidate.syncDistance << " Type=" << candidate.syncType
             << " Result=" << candidate.result << '\n';
        const std::string text = line.str();

        const std::uintmax_t current = fs::file_size(path, ec);
        if (!ec && current > 0 &&
            current + text.size() > d.candidateLogMaxBytes) {
            m_candidateOut.close();
            for (int i = d.candidateLogBackups; i >= 1; --i) {
                const fs::path from = i == 1
                    ? path : fs::path(path.string() + "." + std::to_string(i - 1));
                const fs::path to = fs::path(path.string() + "." + std::to_string(i));
                fs::rename(from, to, ec);
                ec.clear();
            }
            m_candidateOut.open(path, std::ios::trunc);
            if (!m_candidateOut.is_open())
                return;
        }

        m_candidateOut << text;
        m_candidateOut.flush();
    }

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
        bool reflectorOk = false;
        if (m_reflector) {
            ++m_rfStreams;
            reflectorOk = m_reflector->sendStream(dg);
            if (reflectorOk)
                ++m_rfStreamsForwarded;
            else
                ++m_rfStreamsNotForwarded;
        }
        std::fprintf(stderr,
                     "M17 RX->reflector: sid=%04x fn=%04x dst=%s src=%s last=%d forwarded=%d\n",
                     sid, fn, lsf.dstCallsign().c_str(), lsf.srcCallsign().c_str(),
                     lastFrame ? 1 : 0, reflectorOk ? 1 : 0);
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

    void feed(const IQBlock& block)
    {
        // The RF frontend supplies float IQ; m17-gateway DSP works in
        // float64 (Go complex128), so widen without rescaling.
        for (std::size_t n = 0; n < block.count; ++n) {
            const m17::CD sample{double(block.samples[n].i), double(block.samples[n].q)};
            for (const m17::CD dc : m_decimator.process(m_dcr.process(sample))) {
                const double dem = m_afc.process(dc);
                for (const double r : m_resampler.process(dem)) {
                    const float symbol = m_s2s.process(r);
                    m_symbolLevel += (std::fabs(symbol) - m_symbolLevel) / 512.0F;
                    m_synchronizer.push(symbol);
                }
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
    bool m_modeDetected = false;
    bool m_countDetections = true;
    bool m_detectionReported = false;
    bool m_eotSeen = false;
    std::chrono::steady_clock::time_point m_lastActivity{};
    std::atomic<std::uint64_t> m_lsfCount{0};
    std::atomic<std::uint64_t> m_frameCount{0};
    std::atomic<std::uint64_t> m_syncCount{0};
    std::atomic<std::uint64_t> m_lsfBadCrc{0};
    std::atomic<std::uint64_t> m_framesDropped{0};
    std::atomic<std::uint64_t> m_rfPackets{0};
    std::atomic<std::uint64_t> m_rfPacketsForwarded{0};
    std::atomic<std::uint64_t> m_rfPacketsNotForwarded{0};
    std::atomic<std::uint64_t> m_rfPacketsBadCrc{0};
    std::atomic<std::uint64_t> m_rfStreams{0};
    std::atomic<std::uint64_t> m_rfStreamsForwarded{0};
    std::atomic<std::uint64_t> m_rfStreamsNotForwarded{0};
    std::atomic<std::uint64_t> m_netPacketsQueued{0};
    std::atomic<std::uint64_t> m_netPacketsSent{0};
    std::atomic<std::uint64_t> m_netPacketsDropped{0};
    DiagState m_diagLsf;
    DiagState m_diagFrame;
    DiagState m_diagSync;
    bool m_lsfRejected = false;
    bool m_frameRejected = false;
    bool m_lsfWasSyncOnly = false;
    std::uint64_t m_lsfSyncRejects = 0;
    std::atomic<double> m_syncConfidence{0.0};
    std::atomic<double> m_lastRssiDb{-120.0};
    int m_syncBurst = 0;
    std::chrono::steady_clock::time_point m_syncBurstStart{};

    mutable std::mutex m_diagMutex;
    std::deque<Candidate> m_candidates;
    std::deque<FrameAuditEntry> m_frameAudits;
    SoftbitAggregate m_softbits;
    std::chrono::steady_clock::time_point m_lastRejectedCandidate{};
    std::chrono::steady_clock::time_point m_lastFrameArtifact{};
    std::chrono::steady_clock::time_point m_lastSoftbitWrite{};
    double m_diagAfcHz = 0.0;
    float m_diagSyncDistance = 0.0F;
    double m_rssiAtSync = -120.0;
    std::uint64_t m_syncHits = 0;
    std::uint64_t m_syncMisses = 0;
    float m_averageSymbolLevel = 0.0F;
    float m_symbolLevel = 0.0F;
    std::ofstream m_candidateOut;
    std::ofstream m_frameAuditOut;
    std::atomic<std::uint64_t> m_frameAuditSequence{0};
    std::function<void()> m_nearMissHandler;

    static constexpr long kNetTxTimeoutMs = 1000;
};

}  // namespace

std::shared_ptr<IModePlugin> createM17Plugin(const M17Config& config, SystemStatistics* stats)
{
    return std::make_shared<M17Plugin>(config, stats);
}

}  // namespace sx1255

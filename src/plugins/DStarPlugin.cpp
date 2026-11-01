#include "sx1255/dstar/DStarPlugin.hpp"

#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/dstar/DStarGateway.hpp"
#include "sx1255/dstar/DStarTX.hpp"
#include "sx1255/frontend/ITransmit.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace sx1255 {

namespace {

unsigned int tailBlocksFor(unsigned int tailMs)
{
    // DStarRepeater: 3 fixed blocks plus one block for every full 10 ms of
    // configured TX tail.
    return 3U + tailMs / 10U;
}

// Two seconds of 125 kSa/s TX is enough for the ~0.5 s header, the DV frame
// stream and the end pattern while still bounding memory if the hardware ever
// stalls.  Header and end blocks bypass the limit.
constexpr std::size_t kMaxTxQueueSamples = 250000;

}  // namespace

DStarPlugin::DStarPlugin(DStarConfig config, SystemStatistics* stats)
    : m_txMod(std::make_unique<dstar::DStarTXModulator>())
    , m_netTxMod(std::make_unique<dstar::DStarTXModulator>())
    , m_config(std::move(config))
    , m_stats(stats)
{
    if (m_config.gateway.enabled) {
        dstar::DStarGatewayConfig net;
        net.enabled       = true;
        net.gatewayAddress = m_config.gateway.address;
        net.gatewayPort   = m_config.gateway.port;
        net.localAddress  = m_config.gateway.localAddress;
        net.localPort     = m_config.gateway.localPort;
        net.name          = m_config.gateway.name;

        m_gateway = std::make_unique<dstar::DStarGatewayClient>(
            net,
            [this](const dstar::DStarHeader& header) { processNetworkHeader(header); },
            [this](const std::uint8_t* data, std::size_t length, bool end) {
                processNetworkData(data, length, end);
            });
        if (!m_gateway->start())
            std::fprintf(stderr, "D-Star gateway: UDP start failed\n");
        m_watchdog = std::thread([this] { networkThread(); });
    }
}

DStarPlugin::~DStarPlugin()
{
    m_watchdogStop = true;
    if (m_watchdog.joinable())
        m_watchdog.join();
    if (m_gateway)
        m_gateway->stop();
}

void DStarPlugin::setTransmitter(ITransmit* tx)
{
    m_tx = tx;
}

void DStarPlugin::setTxArbiter(ITxArbiter* arbiter)
{
    m_arbiter = arbiter;
}

Detection DStarPlugin::detect(const IQBlock& block)
{
    const Detection d = m_detector.detect(block);
    // Once a header has been validated the detector stays in the DV stream.
    // Treat that follow state as a persistent detection: the dispatcher uses
    // repeated detection to debounce a hand-over from a wrong FM owner, and
    // after the session is dropped this is also what lets D-Star win the next
    // monitoring pass without having to decode a second header.
    const bool detected = d.detected || m_detector.inData();
    m_lastConfidence = detected ? 1.0F : 0.0F;
    if (detected) {
        if (m_countDetections && !m_detectionReported) {
            m_detectionReported = true;
            if (m_stats)
                m_stats->recordDetection(Mode::DStar);
        }
    } else {
        m_detectionReported = false;
    }
    return {detected, detected ? 1.0F : 0.0F};
}

void DStarPlugin::clearPendingDetection()
{
    m_detector.reset();
}

void DStarPlugin::setDetectionCounting(bool on)
{
    m_countDetections = on;
}

void DStarPlugin::startSession(const SessionContext& ctx)
{
    m_active = true;
    m_endQueued = false;
    m_txQueue.clear();
    m_txMod->reset();
    m_tx = ctx.tx;

    dstar::DStarRxEvent event{};
    dstar::DStarHeader header{};
    dstar::DStarFrame frame{};

    // The dispatcher granted this session because detect() decoded a header,
    // so the queued Header event should be the first one we drain.  Any frames
    // decoded in the same IQ block follow it immediately.
    while (m_detector.nextEvent(event, &header, &frame)) {
        if (event == dstar::DStarRxEvent::Header) {
            if (m_gateway)
                m_gateway->sendHeader(dstar::dstarGatewayHeader(header));
            const dstar::DStarHeader txHeader =
                dstar::dstarRepeaterHeader(header, m_config.callsign);
            queueIq(m_txMod->modulateHeader(txHeader, m_config.txPreambleBytes), true);
            startTx();
        } else if (event == dstar::DStarRxEvent::Data) {
            if (m_gateway)
                m_gateway->sendData(frame.bytes.data(), frame.bytes.size(), false);
            if (!m_txOn)
                startTx();
            queueIq(m_txMod->modulateFrame(frame));
        } else if (event == dstar::DStarRxEvent::Eot ||
                   event == dstar::DStarRxEvent::Lost) {
            if (m_gateway) {
                const auto end = dstar::dstarGatewayEndData();
                m_gateway->sendData(end.data(), end.size(), true);
            }
            if (!m_txOn)
                startTx();
            queueIq(m_txMod->modulateEnd(tailBlocksFor(m_config.txTailMs)), true);
            m_endQueued = true;
        }
    }

    if (m_stats)
        m_stats->recordSession(Mode::DStar);
}

bool DStarPlugin::process(const IQBlock& block)
{
    if (!m_active.load())
        return false;

    m_detector.detect(block);

    dstar::DStarRxEvent event{};
    dstar::DStarHeader header{};
    dstar::DStarFrame frame{};
    while (m_detector.nextEvent(event, &header, &frame)) {
        if (event == dstar::DStarRxEvent::Header) {
            if (m_gateway)
                m_gateway->sendHeader(dstar::dstarGatewayHeader(header));
            if (!m_txOn)
                startTx();
            const dstar::DStarHeader txHeader =
                dstar::dstarRepeaterHeader(header, m_config.callsign);
            queueIq(m_txMod->modulateHeader(txHeader, m_config.txPreambleBytes), true);
        } else if (event == dstar::DStarRxEvent::Data) {
            if (m_gateway)
                m_gateway->sendData(frame.bytes.data(), frame.bytes.size(), false);
            if (!m_txOn)
                startTx();
            queueIq(m_txMod->modulateFrame(frame));
        } else if (event == dstar::DStarRxEvent::Eot ||
                   event == dstar::DStarRxEvent::Lost) {
            if (m_gateway) {
                const auto end = dstar::dstarGatewayEndData();
                m_gateway->sendData(end.data(), end.size(), true);
            }
            if (!m_txOn)
                startTx();
            queueIq(m_txMod->modulateEnd(tailBlocksFor(m_config.txTailMs)), true);
            m_endQueued = true;
        }
    }

    pumpTx(block);

    // Keep the session alive until the queued TX (including the end pattern)
    // has actually played out.
    if (m_endQueued && m_txQueue.empty())
        return false;
    return true;
}

void DStarPlugin::stopSession()
{
    stopTx();
    m_active = false;
    m_endQueued = false;
    m_txQueue.clear();
    m_detector.reset();
    m_txMod->reset();
}

PluginTelemetry DStarPlugin::telemetry() const
{
    PluginTelemetry t;
    t.mode = Mode::DStar;
    t.metrics["sync_confidence"] = m_lastConfidence;
    t.metrics["frame_count"] = static_cast<double>(m_detector.frameSyncsSeen());
    t.metrics["tx_queue_samples"] = static_cast<double>(m_txQueue.size());
    t.metrics["repeater_active"] = m_active.load() ? 1.0 : 0.0;
    if (m_gateway) {
        const dstar::DStarGatewayTelemetry net = m_gateway->telemetry();
        t.strings["gateway_state"] = net.running ? "running" : "stopped";
        t.metrics["gateway_rx_headers"] = static_cast<double>(net.rxHeaders);
        t.metrics["gateway_rx_frames"] = static_cast<double>(net.rxData);
        t.metrics["gateway_tx_headers"] = static_cast<double>(net.txHeaders);
        t.metrics["gateway_tx_frames"] = static_cast<double>(net.txData);
        t.metrics["gateway_invalid_packets"] = static_cast<double>(net.invalidPackets);
        t.metrics["gateway_input_stream_resets"] =
            static_cast<double>(net.inputStreamResets);
    }
    t.metrics["net_data_calls"] = static_cast<double>(m_netDataCalls.load());
    t.metrics["net_data_ignored_active"] =
        static_cast<double>(m_netDataIgnoredActive.load());
    t.metrics["net_data_short"] = static_cast<double>(m_netDataShort.load());
    t.metrics["net_data_queued"] = static_cast<double>(m_netDataQueued.load());
    t.metrics["net_tx_active"] = m_netTxActive.load() ? 1.0 : 0.0;
    t.metrics["net_tx_ended"] = static_cast<double>(m_netTxEnded.load());
    t.metrics["net_tx_timeouts"] = static_cast<double>(m_netTxTimeouts.load());
    {
        std::lock_guard<std::mutex> lock(m_netMutex);
        t.metrics["net_queue_samples"] = static_cast<double>(m_netTxQueue.size());
    }
    return t;
}

unsigned int DStarPlugin::gatewayBoundPort() const
{
    return m_gateway ? m_gateway->boundPort() : 0;
}

void DStarPlugin::queueIq(const std::vector<ComplexFloat>& iq, bool force)
{
    if (!force && m_txQueue.size() + iq.size() > kMaxTxQueueSamples) {
        std::fprintf(stderr, "D-Star TX: queue full, dropping %zu samples\n",
                     iq.size());
        return;
    }
    m_txQueue.insert(m_txQueue.end(), iq.begin(), iq.end());
}

void DStarPlugin::enqueueNetworkIq(const std::vector<ComplexFloat>& iq)
{
    std::lock_guard<std::mutex> lock(m_netMutex);
    if (m_netTxQueue.size() + iq.size() > kMaxNetTxQueueSamples) {
        std::fprintf(stderr, "D-Star NET TX: queue full, dropping %zu samples\n",
                     iq.size());
        return;
    }
    m_netTxQueue.insert(m_netTxQueue.end(), iq.begin(), iq.end());
}

void DStarPlugin::startTx()
{
    ITransmit* tx = m_tx.load();
    if (!tx)
        return;
    std::lock_guard<std::mutex> lock(m_txMutex);
    if (m_txOn)
        return;

    // The SX1255 hardware already implements the "no I2S data during RX/TX
    // switching" rule: setPtt(true) reopens an empty playback stream, waits
    // for the TX PLL and prefills a silent carrier before any IQ is written.
    if (!tx->setPtt(true)) {
        std::fprintf(stderr, "D-Star TX: PTT on failed\n");
        return;
    }
    m_txOn = true;
}

void DStarPlugin::stopTx()
{
    if (!m_txOn)
        return;
    ITransmit* tx = m_tx.load();
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        if (tx)
            tx->setPtt(false);
        m_txOn = false;
    }
}

void DStarPlugin::pumpTx(const IQBlock& block)
{
    ITransmit* tx = m_tx.load();
    if (!tx || m_txQueue.empty() || block.count == 0)
        return;

    const std::size_t count = std::min(m_txQueue.size(), block.count);
    std::vector<ComplexFloat> out(m_txQueue.begin(), m_txQueue.begin() + static_cast<std::ptrdiff_t>(count));
    m_txQueue.erase(m_txQueue.begin(), m_txQueue.begin() + static_cast<std::ptrdiff_t>(count));

    if (!m_txOn)
        startTx();
    if (!m_txOn) {
        // TX could not be started; drop this chunk so the queue cannot grow
        // without bound while the frontend is unavailable.
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        tx->writeIQ(out.data(), out.size());

        if (m_endQueued && m_txQueue.empty()) {
            // Drain the ALSA playback so the end pattern reaches the RF before
            // TX is disabled, then switch back to RX (the hardware drops the
            // stream before turning the TX off).
            tx->drainTx();
            tx->setPtt(false);
            m_txOn = false;
        }
    }
}

void DStarPlugin::processNetworkHeader(const dstar::DStarHeader& header)
{
    if (m_active.load())
        return;

    ITransmit* tx = m_tx.load();
    ITxArbiter* arbiter = m_arbiter.load();
    if (!tx || !arbiter)
        return;

    if (m_netTxActive.load())
        return;
    if (!arbiter->acquireTx(Mode::DStar)) {
        std::fprintf(stderr, "D-Star NET TX: blocked (channel busy or wrong mode)\n");
        return;
    }

    m_netTxGranted.store(true);
    m_netTxMod->reset();
    const std::vector<ComplexFloat> iq =
        m_netTxMod->modulateHeader(header, m_config.txPreambleBytes);
    {
        std::lock_guard<std::mutex> lock(m_netMutex);
        m_netEndQueued = false;
        m_netTxQueue.clear();
        m_netTxLastFrameTime = std::chrono::steady_clock::now();
    }
    enqueueNetworkIq(iq);
    m_netTxActive.store(true);
    std::fprintf(stderr, "D-Star NET TX: header acquired\n");
}

void DStarPlugin::processNetworkData(const std::uint8_t* data, std::size_t length, bool end)
{
    m_netDataCalls.fetch_add(1, std::memory_order_relaxed);
    if (m_active.load())
    {
        m_netDataIgnoredActive.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (!m_netTxGranted.load() || !m_netTxActive.load())
        return;

    if (end) {
        const std::vector<ComplexFloat> iq =
            m_netTxMod->modulateEnd(tailBlocksFor(m_config.txTailMs));
        enqueueNetworkIq(iq);
        {
            std::lock_guard<std::mutex> lock(m_netMutex);
            m_netEndQueued = true;
            m_netTxLastFrameTime = std::chrono::steady_clock::now();
        }
        return;
    }

    if (length < 12) {
        m_netDataShort.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    dstar::DStarFrame frame;
    std::copy_n(data, frame.bytes.size(), frame.bytes.begin());
    const std::vector<ComplexFloat> iq = m_netTxMod->modulateFrame(frame);
    enqueueNetworkIq(iq);
    {
        std::lock_guard<std::mutex> lock(m_netMutex);
        m_netTxLastFrameTime = std::chrono::steady_clock::now();
    }
    m_netDataQueued.fetch_add(1, std::memory_order_relaxed);
}

void DStarPlugin::networkThread()
{
    if (m_gateway)
        m_gateway->sendRegister();
    auto lastRegister = std::chrono::steady_clock::now();
    auto lastPoll = lastRegister;

    while (!m_watchdogStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        if (m_gateway) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRegister >= std::chrono::seconds(30)) {
                m_gateway->sendRegister();
                lastRegister = now;
            }
            if (now - lastPoll >= std::chrono::seconds(60)) {
                m_gateway->sendPoll();
                lastPoll = now;
            }
        }

        if (!m_netTxActive.load())
            continue;

        std::vector<ComplexFloat> chunk;
        bool endQueued = false;
        std::chrono::steady_clock::time_point lastNetworkFrame;
        {
            std::lock_guard<std::mutex> lock(m_netMutex);
            if (!m_netTxQueue.empty()) {
                constexpr std::size_t kChunkSamples = 2500;
                const std::size_t count = std::min(kChunkSamples, m_netTxQueue.size());
                chunk.assign(m_netTxQueue.begin(),
                             m_netTxQueue.begin() + static_cast<std::ptrdiff_t>(count));
                m_netTxQueue.erase(
                    m_netTxQueue.begin(),
                    m_netTxQueue.begin() + static_cast<std::ptrdiff_t>(count));
            }
            endQueued = m_netEndQueued && m_netTxQueue.empty();
            lastNetworkFrame = m_netTxLastFrameTime;
        }

        ITransmit* tx = m_tx.load();
        if (tx && !chunk.empty()) {
            std::lock_guard<std::mutex> lock(m_txMutex);
            if (!m_netPttOn) {
                if (!tx->setPtt(true)) {
                    std::fprintf(stderr, "D-Star NET TX: PTT on failed\n");
                } else {
                    m_netPttOn = true;
                }
            }
            if (m_netPttOn)
                tx->writeIQ(chunk.data(), chunk.size());
        }

        if (endQueued) {
            ITxArbiter* arbiter = m_arbiter.load();
            if (tx) {
                std::lock_guard<std::mutex> lock(m_txMutex);
                tx->drainTx();
                if (m_netPttOn) {
                    tx->setPtt(false);
                    m_netPttOn = false;
                }
            }
            m_netTxActive.store(false);
            m_netTxGranted.store(false);
            {
                std::lock_guard<std::mutex> lock(m_netMutex);
                m_netEndQueued = false;
            }
            if (arbiter)
                arbiter->releaseTx();
            m_netTxEnded.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "D-Star NET TX: ended\n");
            continue;
        }

        if (std::chrono::steady_clock::now() - lastNetworkFrame <
            std::chrono::milliseconds(kNetTxTimeoutMs))
            continue;

        ITxArbiter* arbiter = m_arbiter.load();
        {
            std::lock_guard<std::mutex> lock(m_txMutex);
            if (tx && m_netPttOn) {
                tx->setPtt(false);
                m_netPttOn = false;
            }
        }
        m_netTxActive.store(false);
        m_netTxGranted.store(false);
        {
            std::lock_guard<std::mutex> lock(m_netMutex);
            m_netTxQueue.clear();
            m_netEndQueued = false;
        }
        if (arbiter)
            arbiter->releaseTx();
        if (m_gateway)
            m_gateway->resetInputStream();
        m_netTxTimeouts.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "D-Star NET TX: timeout, released\n");
    }
}

std::shared_ptr<IModePlugin> createDStarPlugin(const DStarConfig& config, SystemStatistics* stats)
{
    return std::make_shared<DStarPlugin>(config, stats);
}

}  // namespace sx1255

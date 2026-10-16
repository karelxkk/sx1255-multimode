#pragma once

#include "sx1255/config/Config.hpp"
#include "sx1255/dstar/DStarDetector.hpp"
#include "sx1255/plugin/IModePlugin.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <string_view>

namespace sx1255 {

class SystemStatistics;
namespace dstar {
class DStarTXModulator;
class DStarGatewayClient;
}

// D-Star mode plugin.  While monitoring it uses DStarDetector for mode
// detection; once a valid header grants it the channel it becomes a local
// RX->TX repeater: the decoded header is retransmitted with the repeater
// callsign and every decoded DV frame is queued and re-modulated, followed by
// the D-Star end-of-transmission pattern.
class DStarPlugin final : public IModePlugin {
public:
    explicit DStarPlugin(DStarConfig config, SystemStatistics* stats = nullptr);
    ~DStarPlugin() override;

    std::string_view id() const noexcept override { return "dstar"; }
    Mode mode() const noexcept override { return Mode::DStar; }
    int priority() const noexcept override { return 20; }

    void setTransmitter(ITransmit* tx) override;
    void setTxArbiter(ITxArbiter* arbiter) override;

    Detection detect(const IQBlock& block) override;
    void startSession(const SessionContext& ctx) override;
    bool process(const IQBlock& block) override;
    void stopSession() override;
    PluginTelemetry telemetry() const override;
    unsigned int gatewayBoundPort() const;

private:
    void queueIq(const std::vector<ComplexFloat>& iq, bool force = false);
    void pumpTx(const IQBlock& block);
    void startTx();
    void stopTx();
    void processNetworkHeader(const dstar::DStarHeader& header);
    void processNetworkData(const std::uint8_t* data, std::size_t length, bool end);
    void networkWatchdog();

    dstar::DStarDetector m_detector;
    std::unique_ptr<dstar::DStarTXModulator> m_txMod;
    std::unique_ptr<dstar::DStarTXModulator> m_netTxMod;
    std::unique_ptr<dstar::DStarGatewayClient> m_gateway;
    DStarConfig m_config;
    SystemStatistics* m_stats;
    std::atomic<ITransmit*> m_tx{nullptr};
    std::atomic<ITxArbiter*> m_arbiter{nullptr};
    std::deque<ComplexFloat> m_txQueue;
    std::atomic<bool> m_active{false};
    bool m_txOn = false;
    bool m_endQueued = false;
    bool m_netTxActive = false;
    bool m_netTxGranted = false;
    std::chrono::steady_clock::time_point m_netTxLastFrameTime{};
    std::mutex m_txMutex;
    std::thread m_watchdog;
    std::atomic<bool> m_watchdogStop{false};
    float m_lastConfidence = 0.0F;

    static constexpr long kNetTxTimeoutMs = 1000;
};

}  // namespace sx1255

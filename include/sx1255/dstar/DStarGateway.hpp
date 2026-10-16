#pragma once

#include "sx1255/dstar/DStarDetector.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace sx1255::dstar {

struct DStarGatewayConfig {
    bool          enabled       = false;
    std::string   gatewayAddress = "127.0.0.1";
    unsigned int  gatewayPort   = 20010;
    std::string   localAddress  = "127.0.0.1";
    unsigned int  localPort     = 20011;
    std::string   name          = "sx1255-multimode";
};

struct DStarGatewayTelemetry {
    bool         running = false;
    std::uint64_t rxHeaders = 0;
    std::uint64_t rxData    = 0;
    std::uint64_t txHeaders = 0;
    std::uint64_t txData    = 0;
    std::uint64_t invalidPackets = 0;
    std::string  lastText;
};

// Header the repeater sends toward the gateway for an RF reception:
// RPT1/RPT2 are swapped and the repeater flag is cleared, matching
// DStarRepeater's processRadioHeader().  sendHeader() then performs the
// air-order -> DSRP-order conversion.
DStarHeader dstarGatewayHeader(const DStarHeader& rx);

// 15-byte DSRP end payload: NULL AMBE voice + END_PATTERN slow data.
std::array<std::uint8_t, 15> dstarGatewayEndData();

// Minimal D-Star Repeater Protocol (DSRP) client used by the repeater side.
// It talks to ircDDBGateway/DStarGateway on localhost exactly like
// DStarRepeater's CRepeaterProtocolHandler: UDP, "DSRP" magic, header/data
// packets, and register/poll keepalives.
class DStarGatewayClient {
public:
    using HeaderHandler = std::function<void(const DStarHeader&)>;
    using DataHandler = std::function<void(const std::uint8_t* data,
                                           std::size_t length, bool end)>;

    DStarGatewayClient(DStarGatewayConfig config,
                       HeaderHandler headerHandler = {},
                       DataHandler dataHandler = {});
    ~DStarGatewayClient();

    DStarGatewayClient(const DStarGatewayClient&) = delete;
    DStarGatewayClient& operator=(const DStarGatewayClient&) = delete;

    bool start();
    void stop();

    bool sendHeader(const DStarHeader& header);
    bool sendData(const std::uint8_t* data, std::size_t length, bool end);
    bool sendPoll();
    bool sendRegister();

    // Actual local UDP port after bind (useful with localPort == 0 in tests).
    unsigned int boundPort() const;

    DStarGatewayTelemetry telemetry() const;

private:
    void run();
    void handlePacket(const std::uint8_t* data, std::size_t length);
    bool openSocket();
    bool sendPacket(const std::uint8_t* data, std::size_t length);

    DStarGatewayConfig m_config;
    HeaderHandler m_headerHandler;
    DataHandler m_dataHandler;

    int m_fd = -1;
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    // DSRP stream state (repeater side).
    std::uint16_t m_outId = 0;
    std::uint8_t  m_outSeq = 0;
    std::uint16_t m_inId = 0;

    mutable std::atomic<std::uint64_t> m_rxHeaders{0};
    mutable std::atomic<std::uint64_t> m_rxData{0};
    mutable std::atomic<std::uint64_t> m_txHeaders{0};
    mutable std::atomic<std::uint64_t> m_txData{0};
    mutable std::atomic<std::uint64_t> m_invalidPackets{0};
    mutable std::atomic<bool> m_runningTelemetry{false};
    mutable std::string m_lastText;
    mutable std::mutex m_textMutex;
    std::mutex m_sendMutex;
};

}  // namespace sx1255::dstar

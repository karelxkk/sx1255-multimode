#pragma once

#include "sx1255/m17/M17FrameDecoder.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace sx1255::m17 {

// M17_inet datagram magics (github.com/jancona/m17 stream.go).
inline constexpr std::size_t MagicLen = 4;
inline constexpr char MagicACKN[4]      = {'A', 'C', 'K', 'N'};
inline constexpr char MagicCONN[4]      = {'C', 'O', 'N', 'N'};
inline constexpr char MagicDISC[4]      = {'D', 'I', 'S', 'C'};
inline constexpr char MagicNACK[4]      = {'N', 'A', 'C', 'K'};
inline constexpr char MagicPING[4]      = {'P', 'I', 'N', 'G'};
inline constexpr char MagicPONG[4]      = {'P', 'O', 'N', 'G'};
inline constexpr char MagicM17Stream[4] = {'M', '1', '7', ' '};
inline constexpr char MagicM17Packet[4] = {'M', '1', '7', 'P'};

// M17_inet stream datagram: 4-byte magic, then a 50-byte payload ending in
// a CRC-16.  Total length is 54 bytes.
inline constexpr std::size_t StreamDatagramLen = 54;
inline constexpr std::size_t StreamPayloadLen  = 16;

struct StreamDatagram {
    std::uint16_t streamId = 0;
    std::uint16_t frameNumber = 0;
    bool          lastFrame = false;
    LSF           lsf;
    std::array<std::uint8_t, StreamPayloadLen> payload{};

    std::vector<std::uint8_t> toBytes() const;
    static bool fromBytes(const std::uint8_t* buf, std::size_t len, StreamDatagram& out);
};

enum class ReflectorState {
    Disconnected,
    Resolving,
    Connecting,
    Connected,
    Streaming,
    Reconnecting,
};

const char* toString(ReflectorState state);

// ---------------------------------------------------------------------------
// M17ReflectorClient
//
// UDP client for the M17_inet reflector protocol, ported 1:1 from
// github.com/jancona/m17 inet.Client: CONN/ACKN/NACK/DISC handshake, PING/PONG
// heartbeat, exponential backoff on unanswered CONN, and stream forwarding.
// ---------------------------------------------------------------------------
class M17ReflectorClient {
public:
    struct Config {
        std::string host;
        int         port = 17000;
        std::string module = "A";
        std::string callsign = "N0CALL";
    };

    struct Telemetry {
        ReflectorState state = ReflectorState::Disconnected;
        std::string    host;
        bool           connected = false;
        std::int64_t   lastHeartbeatSec = 0;   // Unix seconds of last PING received
        std::uint64_t  reconnectCount = 0;
        std::uint64_t  rxStreams = 0;
        std::uint64_t  txStreams = 0;
        std::uint64_t  rxPackets = 0;
        std::uint64_t  txPackets = 0;
    };

    using StateHandler  = std::function<void(ReflectorState)>;
    using StreamHandler = std::function<void(const StreamDatagram&)>;
    using PacketHandler = std::function<void(const std::vector<std::uint8_t>&)>;

    M17ReflectorClient(Config config,
                       StateHandler stateHandler = {},
                       StreamHandler streamHandler = {},
                       PacketHandler packetHandler = {});
    ~M17ReflectorClient();

    M17ReflectorClient(const M17ReflectorClient&) = delete;
    M17ReflectorClient& operator=(const M17ReflectorClient&) = delete;

    bool start();
    void stop();

    bool sendStream(const StreamDatagram& datagram);
    // Send an M17 packet datagram: LSF + packet type + payload + CRC.
    bool sendPacket(const std::vector<std::uint8_t>& packetBytes);

    Telemetry telemetry() const;

private:
    void run();
    bool connectSocket();
    void sendConn();
    void sendPong();
    void sendDisc();
    void setState(ReflectorState state);

    Config m_config;
    StateHandler  m_stateHandler;
    StreamHandler m_streamHandler;
    PacketHandler m_packetHandler;

    std::array<std::uint8_t, EncodedCallsignLen> m_encodedCallsign{};
    std::array<std::uint8_t, EncodedCallsignLen> m_encodedModule{};
    std::uint8_t m_moduleByte = 'A';

    int m_fd = -1;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<ReflectorState> m_state{ReflectorState::Disconnected};

    std::atomic<std::int64_t> m_lastHeartbeatSec{0};
    std::atomic<std::uint64_t> m_reconnectCount{0};
    std::atomic<std::uint64_t> m_rxStreams{0};
    std::atomic<std::uint64_t> m_txStreams{0};
    std::atomic<std::uint64_t> m_rxPackets{0};
    std::atomic<std::uint64_t> m_txPackets{0};
    std::atomic<std::int64_t>  m_lastStreamMs{0};
};

}  // namespace sx1255::m17

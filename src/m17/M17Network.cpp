#include "sx1255/m17/M17Network.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <stdexcept>

namespace sx1255::m17 {

namespace {

constexpr int kMaxRetries = 10;
constexpr int kConnRetryMs = 5000;
constexpr int kMaxConnRetryMs = 60000;
constexpr int kHeartbeatTimeoutSec = 30;

std::int64_t nowSec()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void putU16be(std::vector<std::uint8_t>& buf, std::uint16_t v)
{
    buf.push_back(static_cast<std::uint8_t>(v >> 8));
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint16_t getU16be(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

bool magicEquals(const std::uint8_t* buf, const char (&magic)[4])
{
    return std::memcmp(buf, magic, 4) == 0;
}

}  // namespace

const char* toString(ReflectorState state)
{
    switch (state) {
        case ReflectorState::Disconnected:  return "DISCONNECTED";
        case ReflectorState::Resolving:     return "RESOLVING";
        case ReflectorState::Connecting:    return "CONNECTING";
        case ReflectorState::Connected:     return "CONNECTED";
        case ReflectorState::Streaming:     return "STREAMING";
        case ReflectorState::Reconnecting:  return "RECONNECTING";
    }
    return "UNKNOWN";
}

std::vector<std::uint8_t> StreamDatagram::toBytes() const
{
    std::vector<std::uint8_t> buf;
    buf.reserve(StreamDatagramLen);
    buf.insert(buf.end(), MagicM17Stream, MagicM17Stream + 4);
    putU16be(buf, streamId);

    const auto lsfBytes = lsf.toBytes();
    // LSD = the LSF without its trailing 2-byte CRC.
    buf.insert(buf.end(), lsfBytes.begin(), lsfBytes.begin() + (LSFLen - CRCLen));

    const std::uint16_t fn = lastFrame
        ? static_cast<std::uint16_t>(frameNumber | 0x8000)
        : frameNumber;
    putU16be(buf, fn);
    buf.insert(buf.end(), payload.begin(), payload.end());

    const std::uint16_t crc = crc16(buf.data(), buf.size());
    putU16be(buf, crc);
    return buf;
}

bool StreamDatagram::fromBytes(const std::uint8_t* buf, std::size_t len, StreamDatagram& out)
{
    if (len != StreamDatagramLen || !magicEquals(buf, MagicM17Stream))
        return false;
    if (crc16(buf, StreamDatagramLen) != 0)
        return false;

    out.streamId = getU16be(buf + 4);
    out.lsf = LSF::newFromBytes(buf + 6);
    const std::uint16_t fn = getU16be(buf + 6 + (LSFLen - CRCLen));
    out.frameNumber = fn & 0x7FFF;
    out.lastFrame = (fn & 0x8000) != 0;
    std::memcpy(out.payload.data(), buf + 6 + (LSFLen - CRCLen) + 2, StreamPayloadLen);
    return true;
}

M17ReflectorClient::M17ReflectorClient(Config config,
                                       StateHandler stateHandler,
                                       StreamHandler streamHandler,
                                       PacketHandler packetHandler)
    : m_config(std::move(config))
    , m_stateHandler(std::move(stateHandler))
    , m_streamHandler(std::move(streamHandler))
    , m_packetHandler(std::move(packetHandler))
{
    m_encodedCallsign = encodeCallsign(m_config.callsign);
    m_moduleByte = m_config.module.empty() ? 0
                  : static_cast<std::uint8_t>(m_config.module[0]);
}

M17ReflectorClient::~M17ReflectorClient()
{
    stop();
}

bool M17ReflectorClient::start()
{
    if (m_running.load())
        return true;
    m_running = true;
    m_thread = std::thread([this] { run(); });
    return true;
}

void M17ReflectorClient::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_fd >= 0) {
        sendDisc();
        ::shutdown(m_fd, SHUT_RDWR);
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_thread.joinable())
        m_thread.join();
    m_connected = false;
    setState(ReflectorState::Disconnected);
}

bool M17ReflectorClient::sendStream(const StreamDatagram& datagram)
{
    if (m_fd < 0 || !m_connected.load())
        return false;
    const auto bytes = datagram.toBytes();
    if (::send(m_fd, bytes.data(), bytes.size(), 0) != static_cast<ssize_t>(bytes.size()))
        return false;
    m_txStreams++;
    m_lastStreamMs = nowMs();
    setState(ReflectorState::Streaming);
    return true;
}

bool M17ReflectorClient::sendPacket(const std::vector<std::uint8_t>& packetBytes)
{
    if (m_fd < 0 || !m_connected.load())
        return false;
    if (packetBytes.size() < 4)
        return false;

    std::vector<std::uint8_t> bytes;
    bytes.reserve(MagicLen + packetBytes.size());
    bytes.insert(bytes.end(), MagicM17Packet, MagicM17Packet + MagicLen);
    bytes.insert(bytes.end(), packetBytes.begin(), packetBytes.end());

    if (::send(m_fd, bytes.data(), bytes.size(), 0) != static_cast<ssize_t>(bytes.size()))
        return false;
    m_txPackets++;
    return true;
}

M17ReflectorClient::Telemetry M17ReflectorClient::telemetry() const
{
    Telemetry t;
    t.state = m_state.load();
    t.host = m_config.host;
    t.connected = m_connected.load();
    t.lastHeartbeatSec = m_lastHeartbeatSec.load();
    t.reconnectCount = m_reconnectCount.load();
    t.rxStreams = m_rxStreams.load();
    t.txStreams = m_txStreams.load();
    t.rxPackets = m_rxPackets.load();
    t.txPackets = m_txPackets.load();
    return t;
}

bool M17ReflectorClient::connectSocket()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }

    setState(ReflectorState::Resolving);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    const std::string port = std::to_string(m_config.port);
    addrinfo* result = nullptr;
    if (::getaddrinfo(m_config.host.c_str(), port.c_str(), &hints, &result) != 0)
        return false;

    for (addrinfo* rp = result; rp; rp = rp->ai_next) {
        m_fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (m_fd < 0)
            continue;
        if (::connect(m_fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        ::close(m_fd);
        m_fd = -1;
    }
    ::freeaddrinfo(result);
    return m_fd >= 0;
}

void M17ReflectorClient::sendConn()
{
    std::uint8_t cmd[11] = {'C', 'O', 'N', 'N'};
    std::memcpy(cmd + 4, m_encodedCallsign.data(), EncodedCallsignLen);
    cmd[10] = m_moduleByte;
    ::send(m_fd, cmd, sizeof(cmd), 0);
}

void M17ReflectorClient::sendPong()
{
    std::uint8_t cmd[10] = {'P', 'O', 'N', 'G'};
    std::memcpy(cmd + 4, m_encodedCallsign.data(), EncodedCallsignLen);
    ::send(m_fd, cmd, sizeof(cmd), 0);
}

void M17ReflectorClient::sendDisc()
{
    if (m_fd < 0)
        return;
    std::uint8_t cmd[10] = {'D', 'I', 'S', 'C'};
    std::memcpy(cmd + 4, m_encodedCallsign.data(), EncodedCallsignLen);
    ::send(m_fd, cmd, sizeof(cmd), 0);
}

void M17ReflectorClient::setState(ReflectorState state)
{
    if (m_state.exchange(state) == state)
        return;
    if (m_stateHandler)
        m_stateHandler(state);
}

void M17ReflectorClient::run()
{
    int retries = 0;
    while (m_running.load()) {
        if (!connectSocket()) {
            m_reconnectCount++;
            setState(ReflectorState::Reconnecting);
            std::this_thread::sleep_for(std::chrono::milliseconds(kConnRetryMs));
            continue;
        }

        setState(ReflectorState::Connecting);
        sendConn();
        m_connected = false;

        int connBackoffMs = kConnRetryMs;
        auto lastPing = std::chrono::steady_clock::now();
        bool gaveUp = false;

        while (m_running.load() && m_fd >= 0) {
            pollfd pfd{m_fd, POLLIN, 0};
            const int pr = ::poll(&pfd, 1, 1000);
            if (pr < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            if (pr == 0) {
                if (!m_connected.load()) {
                    // Unanswered CONN: resend with exponential backoff.
                    sendConn();
                    connBackoffMs = std::min(connBackoffMs * 2, kMaxConnRetryMs);
                } else if (now - lastPing > std::chrono::seconds(kHeartbeatTimeoutSec)) {
                    m_connected = false;
                    m_reconnectCount++;
                    setState(ReflectorState::Reconnecting);
                    break;
                }
                if (m_state.load() == ReflectorState::Streaming &&
                    nowMs() - m_lastStreamMs.load() > 1000) {
                    setState(ReflectorState::Connected);
                }
                continue;
            }

            std::uint8_t buffer[1024];
            const ssize_t n = ::recv(m_fd, buffer, sizeof(buffer), 0);
            if (n < 4)
                continue;

            if (magicEquals(buffer, MagicACKN)) {
                m_connected = true;
                retries = 0;
                connBackoffMs = kConnRetryMs;
                lastPing = now;
                setState(ReflectorState::Connected);
            } else if (magicEquals(buffer, MagicNACK)) {
                m_connected = false;
                setState(ReflectorState::Disconnected);
                gaveUp = true;
                break;
            } else if (magicEquals(buffer, MagicDISC)) {
                m_connected = false;
                m_reconnectCount++;
                setState(ReflectorState::Reconnecting);
                break;
            } else if (magicEquals(buffer, MagicPING)) {
                sendPong();
                lastPing = now;
                m_lastHeartbeatSec = nowSec();
            } else if (magicEquals(buffer, MagicM17Stream)) {
                StreamDatagram dg;
                if (StreamDatagram::fromBytes(buffer, static_cast<std::size_t>(n), dg)) {
                    m_rxStreams++;
                    m_lastStreamMs = nowMs();
                    setState(ReflectorState::Streaming);
                    if (m_streamHandler)
                        m_streamHandler(dg);
                }
            } else if (magicEquals(buffer, MagicM17Packet)) {
                m_rxPackets++;
                if (m_packetHandler)
                    m_packetHandler(std::vector<std::uint8_t>(buffer + 4, buffer + n));
            }
        }

        if (gaveUp || !m_running.load())
            break;

        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        retries++;
        if (retries > kMaxRetries) {
            m_connected = false;
            setState(ReflectorState::Disconnected);
            break;
        }
        const int backoff = std::min(connBackoffMs * (1 << std::min(retries, 4)), kMaxConnRetryMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
    }

    m_connected = false;
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

}  // namespace sx1255::m17

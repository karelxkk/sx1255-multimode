#include "sx1255/dstar/DStarGateway.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>

namespace sx1255::dstar {

namespace {

constexpr std::array<char, 4> kMagic = {'D', 'S', 'R', 'P'};
constexpr std::uint8_t kTypeHeader = 0x20;
constexpr std::uint8_t kTypeData = 0x21;
constexpr std::uint8_t kTypeBusyHeader = 0x22;
constexpr std::uint8_t kTypeBusyData = 0x23;
constexpr std::uint8_t kTypePoll = 0x0A;
constexpr std::uint8_t kTypeRegister = 0x0B;
constexpr std::array<std::uint8_t, 3> kDataSyncBytes = {0x55, 0x2D, 0x16};
constexpr std::array<std::uint8_t, 3> kScrambledSync = {0x70, 0x4F, 0x93};
constexpr std::size_t kHeaderPayloadOffset = 8;

std::array<std::uint8_t, 41> makeDsrpHeaderPayload(const DStarHeader& air)
{
    std::array<std::uint8_t, 41> out{};
    std::copy_n(air.bytes.begin(), 3, out.begin());

    // On-air order is RPT2,RPT1; DSRP order is RPT1,RPT2.
    std::copy_n(air.bytes.begin() + 11, 8, out.begin() + 3);  // RPT1
    std::copy_n(air.bytes.begin() + 3, 8, out.begin() + 11);  // RPT2
    std::copy_n(air.bytes.begin() + 19, 8, out.begin() + 19); // YOUR
    std::copy_n(air.bytes.begin() + 27, 8, out.begin() + 27); // MY1
    std::copy_n(air.bytes.begin() + 35, 4, out.begin() + 35); // MY2

    const std::uint16_t crc = dstarHeaderCrc(out.data(), 39);
    out[39] = static_cast<std::uint8_t>(crc & 0xFF);
    out[40] = static_cast<std::uint8_t>(crc >> 8);
    return out;
}

DStarHeader makeAirHeader(const std::uint8_t* dsrp)
{
    DStarHeader out;
    std::copy_n(dsrp, 3, out.bytes.begin());
    std::copy_n(dsrp + 3, 8, out.bytes.begin() + 3);    // DSRP RPT1 -> air RPT2
    std::copy_n(dsrp + 11, 8, out.bytes.begin() + 11);  // DSRP RPT2 -> air RPT1
    std::copy_n(dsrp + 19, 8, out.bytes.begin() + 19);
    std::copy_n(dsrp + 27, 8, out.bytes.begin() + 27);
    std::copy_n(dsrp + 35, 4, out.bytes.begin() + 35);
    std::copy_n(dsrp + 39, 2, out.bytes.begin() + 39);
    return out;
}

bool resolveAddress(const std::string& host, sockaddr_in& out)
{
    std::memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    if (::inet_pton(AF_INET, host.c_str(), &out.sin_addr) == 1)
        return true;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr)
        return false;
    out.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    ::freeaddrinfo(res);
    return true;
}

}  // namespace

DStarHeader dstarGatewayHeader(const DStarHeader& rx)
{
    DStarHeader out = rx;
    // air: RPT2 is bytes 3..10, RPT1 is bytes 11..18.
    std::array<std::uint8_t, 8> rpt1{};
    std::copy_n(rx.bytes.begin() + 11, 8, rpt1.begin());
    std::copy_n(rx.bytes.begin() + 3, 8, out.bytes.begin() + 11);
    std::copy_n(rpt1.begin(), 8, out.bytes.begin() + 3);
    out.bytes[0] &= static_cast<std::uint8_t>(~0x40);
    const std::uint16_t crc = dstarHeaderCrc(out.bytes.data(), 39);
    out.bytes[39] = static_cast<std::uint8_t>(crc & 0xFF);
    out.bytes[40] = static_cast<std::uint8_t>(crc >> 8);
    return out;
}

std::array<std::uint8_t, 15> dstarGatewayEndData()
{
    constexpr std::array<std::uint8_t, 9> nullAmbe = {
        0x9E, 0x8D, 0x32, 0x88, 0x26, 0x1A, 0x3F, 0x61, 0xE8};
    constexpr std::array<std::uint8_t, 6> endPattern = {
        0x55, 0x55, 0x55, 0x55, 0xC8, 0x7A};
    std::array<std::uint8_t, 15> out{};
    std::copy(nullAmbe.begin(), nullAmbe.end(), out.begin());
    std::copy(endPattern.begin(), endPattern.end(), out.begin() + 9);
    return out;
}

DStarGatewayClient::DStarGatewayClient(DStarGatewayConfig config,
                                       HeaderHandler headerHandler,
                                       DataHandler dataHandler)
    : m_config(std::move(config))
    , m_headerHandler(std::move(headerHandler))
    , m_dataHandler(std::move(dataHandler))
{
}

DStarGatewayClient::~DStarGatewayClient()
{
    stop();
}

bool DStarGatewayClient::openSocket()
{
    m_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd < 0)
        return false;

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<std::uint16_t>(m_config.localPort));
    if (m_config.localAddress.empty())
        local.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (::inet_pton(AF_INET, m_config.localAddress.c_str(), &local.sin_addr) != 1) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    int reuse = 1;
    ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (::bind(m_fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    return true;
}

bool DStarGatewayClient::start()
{
    if (m_running.load())
        return true;
    if (!openSocket())
        return false;
    m_running = true;
    m_runningTelemetry = true;
    m_thread = std::thread([this] { run(); });
    return true;
}

void DStarGatewayClient::stop()
{
    if (!m_running.exchange(false)) {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        return;
    }

    if (m_fd >= 0)
        ::shutdown(m_fd, SHUT_RDWR);
    if (m_thread.joinable())
        m_thread.join();
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_runningTelemetry = false;
}

void DStarGatewayClient::run()
{
    std::array<std::uint8_t, 512> buffer{};

    while (m_running.load()) {
        pollfd pfd{};
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, 50);
        if (ready <= 0)
            continue;

        sockaddr_in source{};
        socklen_t sourceLen = sizeof(source);
        const ssize_t got = ::recvfrom(m_fd, buffer.data(), buffer.size(), 0,
                                       reinterpret_cast<sockaddr*>(&source), &sourceLen);
        if (got <= 0)
            continue;
        handlePacket(buffer.data(), static_cast<std::size_t>(got));
    }
}

void DStarGatewayClient::handlePacket(const std::uint8_t* data, std::size_t length)
{
    if (length < 5 || std::memcmp(data, kMagic.data(), kMagic.size()) != 0) {
        m_invalidPackets.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const std::uint8_t type = data[4];
    if (type == kTypeHeader) {
        if (length < 49)
            return;
        const std::uint16_t id = static_cast<std::uint16_t>((data[5] << 8) | data[6]);
        if (m_inId != 0)
            return;

        const std::uint8_t* payload = data + kHeaderPayloadOffset;
        if (payload[39] != 0xFF || payload[40] != 0xFF) {
            const std::uint16_t crc = dstarHeaderCrc(payload, 39);
            if (static_cast<std::uint8_t>(crc) != payload[39] ||
                static_cast<std::uint8_t>(crc >> 8) != payload[40]) {
                m_invalidPackets.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }

        m_inId = id;
        m_rxHeaders.fetch_add(1, std::memory_order_relaxed);
        if (m_headerHandler)
            m_headerHandler(makeAirHeader(payload));
        return;
    }

    if (type == kTypeData) {
        if (length < 9)
            return;
        const std::uint16_t id = static_cast<std::uint16_t>((data[5] << 8) | data[6]);
        if (id != m_inId)
            return;

        const bool end = (data[7] & 0x40) != 0;
        const std::uint8_t seq = data[7] & 0x1F;
        const std::size_t dataLength = length - 9;
        if (end)
            m_inId = 0;

        std::array<std::uint8_t, 15> frame{};
        const std::size_t n = std::min(dataLength, frame.size());
        std::copy_n(data + 9, n, frame.begin());

        if (seq == 0) {
            frame[9] = kDataSyncBytes[0];
            frame[10] = kDataSyncBytes[1];
            frame[11] = kDataSyncBytes[2];
        } else if (std::equal(kDataSyncBytes.begin(), kDataSyncBytes.end(), frame.begin() + 9)) {
            frame[9] = kScrambledSync[0];
            frame[10] = kScrambledSync[1];
            frame[11] = kScrambledSync[2];
        }

        m_rxData.fetch_add(1, std::memory_order_relaxed);
        if (m_dataHandler)
            m_dataHandler(frame.data(), n, end);
        return;
    }

    // Text/status/DD packets are not used by the plugin; keep lastText for
    // diagnostics without interpreting the gateway link-state fields.
    if (type == 0x00 && length > 5) {
        std::lock_guard<std::mutex> lock(m_textMutex);
        const std::size_t n = std::min<std::size_t>(length - 5, 20);
        m_lastText.assign(reinterpret_cast<const char*>(data + 5), n);
    }
}

bool DStarGatewayClient::sendPacket(const std::uint8_t* data, std::size_t length)
{
    if (m_fd < 0)
        return false;
    sockaddr_in gateway{};
    if (!resolveAddress(m_config.gatewayAddress, gateway))
        return false;
    gateway.sin_port = htons(static_cast<std::uint16_t>(m_config.gatewayPort));
    const ssize_t sent = ::sendto(m_fd, data, length, 0,
                                  reinterpret_cast<sockaddr*>(&gateway), sizeof(gateway));
    return sent == static_cast<ssize_t>(length);
}

bool DStarGatewayClient::sendHeader(const DStarHeader& header)
{
    std::lock_guard<std::mutex> lock(m_sendMutex);

    std::array<std::uint8_t, 49> packet{};
    std::copy(kMagic.begin(), kMagic.end(), packet.begin());
    packet[4] = kTypeHeader;

    std::random_device rd;
    m_outId = static_cast<std::uint16_t>((rd() % 65535) + 1);
    packet[5] = static_cast<std::uint8_t>(m_outId >> 8);
    packet[6] = static_cast<std::uint8_t>(m_outId & 0xFF);
    packet[7] = 0;

    const auto payload = makeDsrpHeaderPayload(header);
    std::copy(payload.begin(), payload.end(), packet.begin() + kHeaderPayloadOffset);
    m_outSeq = 0;

    for (int i = 0; i < 2; ++i)
        if (!sendPacket(packet.data(), packet.size()))
            return false;
    m_txHeaders.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool DStarGatewayClient::sendData(const std::uint8_t* data, std::size_t length, bool end)
{
    std::lock_guard<std::mutex> lock(m_sendMutex);

    if (length < 9 || length > 15)
        return false;

    std::array<std::uint8_t, 24> packet{};
    std::copy(kMagic.begin(), kMagic.end(), packet.begin());
    packet[4] = kTypeData;
    packet[5] = static_cast<std::uint8_t>(m_outId >> 8);
    packet[6] = static_cast<std::uint8_t>(m_outId & 0xFF);

    if (length >= 12 && data[9] == kDataSyncBytes[0] &&
        data[10] == kDataSyncBytes[1] && data[11] == kDataSyncBytes[2])
        m_outSeq = 0;

    std::uint8_t seq = m_outSeq;
    if (end)
        seq |= 0x40;
    packet[7] = seq;
    packet[8] = 0;  // errors
    std::copy_n(data, length, packet.begin() + 9);

    ++m_outSeq;
    if (m_outSeq > 0x14)
        m_outSeq = 0;

    if (!sendPacket(packet.data(), length + 9))
        return false;
    m_txData.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool DStarGatewayClient::sendPoll()
{
    std::array<std::uint8_t, 40> packet{};
    std::copy(kMagic.begin(), kMagic.end(), packet.begin());
    packet[4] = kTypePoll;
    const std::size_t n = std::min<std::size_t>(m_config.name.size(), 34);
    std::copy_n(m_config.name.begin(), n, packet.begin() + 5);
    packet[5 + n] = 0;
    return sendPacket(packet.data(), 6 + n);
}

bool DStarGatewayClient::sendRegister()
{
    std::array<std::uint8_t, 40> packet{};
    std::copy(kMagic.begin(), kMagic.end(), packet.begin());
    packet[4] = kTypeRegister;
    const std::size_t n = std::min<std::size_t>(m_config.name.size(), 34);
    std::copy_n(m_config.name.begin(), n, packet.begin() + 5);
    packet[5 + n] = 0;
    return sendPacket(packet.data(), 6 + n);
}

unsigned int DStarGatewayClient::boundPort() const
{
    if (m_fd < 0)
        return 0;
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (::getsockname(m_fd, reinterpret_cast<sockaddr*>(&local), &len) < 0)
        return 0;
    return ntohs(local.sin_port);
}

DStarGatewayTelemetry DStarGatewayClient::telemetry() const
{
    DStarGatewayTelemetry t;
    t.running = m_runningTelemetry.load();
    t.rxHeaders = m_rxHeaders.load();
    t.rxData = m_rxData.load();
    t.txHeaders = m_txHeaders.load();
    t.txData = m_txData.load();
    t.invalidPackets = m_invalidPackets.load();
    std::lock_guard<std::mutex> lock(m_textMutex);
    t.lastText = m_lastText;
    return t;
}

}  // namespace sx1255::dstar

#include "sx1255/dstar/DStarGateway.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace sx1255;

namespace {

struct Received {
    bool haveHeader = false;
    bool haveData = false;
    bool end = false;
    dstar::DStarHeader header{};
    std::vector<std::uint8_t> data;
};

struct Harness {
    std::mutex mutex;
    std::condition_variable cv;
    Received rx;
};

void putCallsign(dstar::DStarHeader& header, std::size_t offset,
                 const std::string& callsign, std::size_t width)
{
    for (std::size_t i = 0; i < width; ++i) {
        const char c = i < callsign.size() ? callsign[i] : ' ';
        header.bytes[offset + i] = static_cast<std::uint8_t>(c);
    }
}

dstar::DStarHeader makeHeader()
{
    dstar::DStarHeader header;
    header.bytes.fill(0);
    putCallsign(header, 3,  "OK1LBC", 8);
    putCallsign(header, 11, "DIRECT", 8);
    putCallsign(header, 19, "CQCQCQ", 8);
    putCallsign(header, 27, "OK2ABC", 8);
    putCallsign(header, 35, "  A", 4);
    const std::uint16_t crc = dstar::dstarHeaderCrc(header.bytes.data(), 39);
    header.bytes[39] = static_cast<std::uint8_t>(crc & 0xFF);
    header.bytes[40] = static_cast<std::uint8_t>(crc >> 8);
    return header;
}

bool recvWithTimeout(int fd, std::uint8_t* data, std::size_t maxLen,
                     std::size_t& got, int timeoutMs = 1000)
{
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, timeoutMs);
    if (ready <= 0)
        return false;
    const ssize_t n = ::recv(fd, data, maxLen, 0);
    if (n <= 0)
        return false;
    got = static_cast<std::size_t>(n);
    return true;
}

int makeFakeGateway(int& outFd, std::uint16_t& outPort)
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return 77;   // sandbox has no network sockets

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return 77;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        ::close(fd);
        return 77;
    }
    outFd = fd;
    outPort = ntohs(addr.sin_port);
    return 0;
}

}  // namespace

int main()
{
    int fakeFd = -1;
    std::uint16_t fakePort = 0;
    const int fakeResult = makeFakeGateway(fakeFd, fakePort);
    if (fakeResult != 0)
        return fakeResult;

    Harness harness;
    dstar::DStarGatewayConfig cfg;
    cfg.enabled = true;
    cfg.gatewayAddress = "127.0.0.1";
    cfg.gatewayPort = fakePort;
    cfg.localAddress = "127.0.0.1";
    cfg.localPort = 0;
    cfg.name = "test";

    dstar::DStarGatewayClient client(
        cfg,
        [&](const dstar::DStarHeader& header) {
            std::lock_guard<std::mutex> lock(harness.mutex);
            harness.rx.header = header;
            harness.rx.haveHeader = true;
            harness.cv.notify_one();
        },
        [&](const std::uint8_t* data, std::size_t length, bool end) {
            std::lock_guard<std::mutex> lock(harness.mutex);
            harness.rx.data.assign(data, data + length);
            harness.rx.end = end;
            harness.rx.haveData = true;
            harness.cv.notify_one();
        });

    if (!client.start())
        return 2;
    const unsigned int boundPort = client.boundPort();
    if (boundPort == 0)
        return 3;

    // Repeater -> gateway header.
    const dstar::DStarHeader air = makeHeader();
    if (!client.sendHeader(air))
        return 4;

    std::array<std::uint8_t, 64> pkt{};
    std::size_t got = 0;
    if (!recvWithTimeout(fakeFd, pkt.data(), pkt.size(), got))
        return 5;
    if (got != 49 || pkt[4] != 0x20 ||
        std::memcmp(pkt.data(), "DSRP", 4) != 0)
        return 6;
    const std::uint16_t streamId = static_cast<std::uint16_t>((pkt[5] << 8) | pkt[6]);
    if (streamId == 0)
        return 7;
    // sendHeader() transmits the header twice for UDP-loss resilience.
    if (!recvWithTimeout(fakeFd, pkt.data(), pkt.size(), got))
        return 8;
    if (got != 49 || pkt[4] != 0x20)
        return 8;

    // Gateway -> repeater header (DSRP order is RPT1,RPT2; air order is
    // RPT2,RPT1).
    std::array<std::uint8_t, 64> send{};
    std::memcpy(send.data(), "DSRP", 4);
    send[4] = 0x20;
    send[5] = static_cast<std::uint8_t>(streamId >> 8);
    send[6] = static_cast<std::uint8_t>(streamId & 0xFF);
    send[7] = 0;
    send[8] = air.bytes[0];
    send[9] = air.bytes[1];
    send[10] = air.bytes[2];
    std::copy_n(air.bytes.begin() + 11, 8, send.begin() + 11);  // RPT1
    std::copy_n(air.bytes.begin() + 3, 8, send.begin() + 19);   // RPT2
    std::copy_n(air.bytes.begin() + 19, 8, send.begin() + 27);  // YOUR
    std::copy_n(air.bytes.begin() + 27, 8, send.begin() + 35);  // MY1
    std::copy_n(air.bytes.begin() + 35, 4, send.begin() + 43);  // MY2
    const std::uint16_t crc = dstar::dstarHeaderCrc(send.data() + 8, 39);
    send[47] = static_cast<std::uint8_t>(crc & 0xFF);
    send[48] = static_cast<std::uint8_t>(crc >> 8);

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<std::uint16_t>(boundPort));
    ::inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    if (::sendto(fakeFd, send.data(), 49, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0)
        return 9;

    {
        std::unique_lock<std::mutex> lock(harness.mutex);
        if (!harness.cv.wait_for(lock, std::chrono::seconds(1),
                                 [&] { return harness.rx.haveHeader; }))
            return 10;
    }
    // DStarRepeater's DSRP layer swaps RPT1/RPT2 between the on-air and DSRP
    // wire orders, so the round-trip must reproduce that exact behaviour.
    if (!std::equal(harness.rx.header.bytes.begin(),
                    harness.rx.header.bytes.begin() + 3,
                    air.bytes.begin()))
        return 11;
    if (!std::equal(harness.rx.header.bytes.begin() + 3,
                    harness.rx.header.bytes.begin() + 11,
                    air.bytes.begin() + 11))
        return 11;
    if (!std::equal(harness.rx.header.bytes.begin() + 11,
                    harness.rx.header.bytes.begin() + 19,
                    air.bytes.begin() + 3))
        return 11;
    if (!std::equal(harness.rx.header.bytes.begin() + 19,
                    harness.rx.header.bytes.begin() + 39,
                    air.bytes.begin() + 19))
        return 10;

    // Repeater -> gateway data frame.
    std::array<std::uint8_t, 12> frame{};
    frame.fill(0x9E);
    frame[9] = 0x55; frame[10] = 0x2D; frame[11] = 0x16;
    if (!client.sendData(frame.data(), frame.size(), false))
        return 12;
    if (!recvWithTimeout(fakeFd, pkt.data(), pkt.size(), got))
        return 13;
    if (got != 21 || pkt[4] != 0x21 || pkt[5] != send[5] || pkt[6] != send[6])
        return 14;

    // Gateway -> repeater data frame (seq 0, sync bytes are regenerated).
    std::memset(send.data(), 0, send.size());
    std::memcpy(send.data(), "DSRP", 4);
    send[4] = 0x21;
    send[5] = static_cast<std::uint8_t>(streamId >> 8);
    send[6] = static_cast<std::uint8_t>(streamId & 0xFF);
    send[7] = 0;
    send[8] = 0;
    for (int i = 0; i < 12; ++i)
        send[9 + i] = static_cast<std::uint8_t>(0x10 + i);
    if (::sendto(fakeFd, send.data(), 21, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0)
        return 14;

    {
        std::unique_lock<std::mutex> lock(harness.mutex);
        if (!harness.cv.wait_for(lock, std::chrono::seconds(1),
                                 [&] { return harness.rx.haveData; }))
            return 15;
        if (harness.rx.end)
            return 16;
        if (harness.rx.data.size() != 12)
            return 17;
        if (harness.rx.data[9] != 0x55 || harness.rx.data[10] != 0x2D ||
            harness.rx.data[11] != 0x16)
            return 18;
    }

    // Gateway -> repeater end packet.
    harness.rx.haveData = false;
    std::memset(send.data(), 0, send.size());
    std::memcpy(send.data(), "DSRP", 4);
    send[4] = 0x21;
    send[5] = static_cast<std::uint8_t>(streamId >> 8);
    send[6] = static_cast<std::uint8_t>(streamId & 0xFF);
    send[7] = 0x40;
    send[8] = 0;
    for (int i = 0; i < 12; ++i)
        send[9 + i] = 0x55;
    if (::sendto(fakeFd, send.data(), 21, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0)
        return 19;
    {
        std::unique_lock<std::mutex> lock(harness.mutex);
        if (!harness.cv.wait_for(lock, std::chrono::seconds(1),
                                 [&] { return harness.rx.haveData && harness.rx.end; }))
            return 20;
    }

    client.stop();
    ::close(fakeFd);
    return 0;
}

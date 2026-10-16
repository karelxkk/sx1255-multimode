#include "sx1255/dstar/DStarPlugin.hpp"
#include "sx1255/frontend/ITransmit.hpp"
#include "sx1255/plugin/IModePlugin.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

using namespace sx1255;
using namespace sx1255::dstar;

namespace {

class FakeTx final : public ITransmit {
public:
    bool setTxFrequency(Hertz) override { return true; }

    bool setPtt(bool on) override
    {
        ptt = on;
        pttOnCount += on ? 1 : 0;
        pttOffCount += on ? 0 : 1;
        return true;
    }

    bool writeIQ(const ComplexFloat*, std::size_t count) override
    {
        written += count;
        return true;
    }

    bool drainTx() override
    {
        drained = true;
        return true;
    }

    bool isReady() const override { return true; }

    std::atomic<bool> ptt{false};
    std::atomic<std::size_t> written{0};
    std::atomic<std::size_t> pttOnCount{0};
    std::atomic<std::size_t> pttOffCount{0};
    std::atomic<bool> drained{false};
};

class FakeArbiter final : public ITxArbiter {
public:
    bool acquireTx(Mode mode) override
    {
        if (mode != Mode::DStar || acquired.exchange(true))
            return false;
        return true;
    }

    void releaseTx() override
    {
        acquired = false;
    }

    std::atomic<bool> acquired{false};
};

int makeFakeGateway(int& fd, std::uint16_t& port)
{
    fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return 77;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        return 77;
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0)
        return 77;
    port = ntohs(addr.sin_port);
    return 0;
}

void putCallsign(DStarHeader& header, std::size_t offset,
                 const std::string& callsign, std::size_t width)
{
    for (std::size_t i = 0; i < width; ++i) {
        const char c = i < callsign.size() ? callsign[i] : ' ';
        header.bytes[offset + i] = static_cast<std::uint8_t>(c);
    }
}

DStarHeader makeHeader()
{
    DStarHeader header;
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

}  // namespace

int main()
{
    int fakeFd = -1;
    std::uint16_t fakePort = 0;
    const int fakeResult = makeFakeGateway(fakeFd, fakePort);
    if (fakeResult != 0)
        return fakeResult;

    DStarConfig config;
    config.gateway.enabled = true;
    config.gateway.address = "127.0.0.1";
    config.gateway.port = fakePort;
    config.gateway.localAddress = "127.0.0.1";
    config.gateway.localPort = 0;
    config.gateway.name = "test";

    DStarPlugin plugin(config, nullptr);
    FakeTx tx;
    FakeArbiter arbiter;
    plugin.setTransmitter(&tx);
    plugin.setTxArbiter(&arbiter);

    unsigned int boundPort = 0;
    for (int i = 0; i < 100 && boundPort == 0; ++i) {
        boundPort = plugin.gatewayBoundPort();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (boundPort == 0)
    {
        std::fprintf(stderr, "gateway_state=%s\n",
                     plugin.telemetry().strings["gateway_state"].c_str());
        std::fflush(stderr);
        return 1;
    }

    const DStarHeader air = makeHeader();
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<std::uint16_t>(boundPort));
    ::inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);

    // DSRP header: flags + RPT1 + RPT2 + YOUR + MY1 + MY2 + CRC.
    std::array<std::uint8_t, 49> headerPacket{};
    std::memcpy(headerPacket.data(), "DSRP", 4);
    headerPacket[4] = 0x20;
    headerPacket[5] = 0x12;
    headerPacket[6] = 0x34;
    headerPacket[7] = 0;
    headerPacket[8] = air.bytes[0];
    headerPacket[9] = air.bytes[1];
    headerPacket[10] = air.bytes[2];
    std::copy_n(air.bytes.begin() + 11, 8, headerPacket.begin() + 11);  // RPT1
    std::copy_n(air.bytes.begin() + 3, 8, headerPacket.begin() + 19);   // RPT2
    std::copy_n(air.bytes.begin() + 19, 8, headerPacket.begin() + 27);
    std::copy_n(air.bytes.begin() + 27, 8, headerPacket.begin() + 35);
    std::copy_n(air.bytes.begin() + 35, 4, headerPacket.begin() + 43);
    const std::uint16_t crc = dstar::dstarHeaderCrc(headerPacket.data() + 8, 39);
    headerPacket[47] = static_cast<std::uint8_t>(crc & 0xFF);
    headerPacket[48] = static_cast<std::uint8_t>(crc >> 8);

    if (::sendto(fakeFd, headerPacket.data(), headerPacket.size(), 0,
                 reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0)
        return 2;

    for (int i = 0; i < 200 && tx.pttOnCount.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (tx.pttOnCount.load() == 0 || tx.written.load() == 0)
        return 3;

    std::array<std::uint8_t, 21> dataPacket{};
    std::memcpy(dataPacket.data(), "DSRP", 4);
    dataPacket[4] = 0x21;
    dataPacket[5] = 0x12;
    dataPacket[6] = 0x34;
    dataPacket[7] = 0;
    dataPacket[8] = 0;
    for (int i = 0; i < 12; ++i)
        dataPacket[9 + i] = static_cast<std::uint8_t>(0x10 + i);
    if (::sendto(fakeFd, dataPacket.data(), dataPacket.size(), 0,
                 reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0)
        return 4;

    std::array<std::uint8_t, 21> endPacket{};
    std::memcpy(endPacket.data(), "DSRP", 4);
    endPacket[4] = 0x21;
    endPacket[5] = 0x12;
    endPacket[6] = 0x34;
    endPacket[7] = 0x40;
    endPacket[8] = 0;
    for (int i = 0; i < 12; ++i)
        endPacket[9 + i] = 0x55;
    if (::sendto(fakeFd, endPacket.data(), endPacket.size(), 0,
                 reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0)
        return 5;

    for (int i = 0; i < 200 && tx.pttOffCount.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (tx.pttOffCount.load() == 0 || !tx.drained.load())
        return 6;
    if (arbiter.acquired.load())
        return 7;

    ::close(fakeFd);
    return 0;
}

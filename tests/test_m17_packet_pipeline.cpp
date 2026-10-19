// End to end receive path of the M17 plugin: IQ blocks in, M17 packet on the
// bridge connection out.  The blocks are deliberately small so that the LSF and
// the packet frames that follow it arrive in different calls - exactly the
// situation in which the plugin used to throw the frames away when it started
// the session.
#include "sx1255/config/Config.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

namespace {

std::vector<std::uint8_t> packetPayload(const std::string& text)
{
    std::vector<std::uint8_t> payload;
    payload.push_back(0x05);   // SMS
    payload.insert(payload.end(), text.begin(), text.end());
    const std::uint16_t crc = crc16(payload.data(), payload.size());
    payload.push_back(static_cast<std::uint8_t>(crc >> 8));
    payload.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    return payload;
}

std::vector<Symbol> filler(std::size_t count)
{
    std::vector<Symbol> syms;
    syms.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        syms.push_back(i % 2 ? +3.0F : -3.0F);
    return syms;
}

// Fake m17-bridge: answers the inet handshake and collects M17 packets.
struct FakeBridge {
    int fd = -1;
    int port = 0;
    std::atomic<int> packets{0};
    std::atomic<bool> connected{false};
    std::vector<std::uint8_t> lastPacket;
    std::thread thread;

    ~FakeBridge() { stop(); }

    bool start()
    {
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0)
            return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            return false;
        socklen_t len = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0)
            return false;
        port = ntohs(addr.sin_port);
        thread = std::thread([this] { run(); });
        return true;
    }

    void stop()
    {
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
            fd = -1;
        }
        if (thread.joinable())
            thread.join();
    }

    void run()
    {
        sockaddr_in client{};
        socklen_t clen = sizeof(client);
        std::uint8_t buf[2048];
        for (;;) {
            const ssize_t n = ::recvfrom(fd, buf, sizeof(buf), 0,
                                         reinterpret_cast<sockaddr*>(&client), &clen);
            if (n < 0)
                return;
            if (n >= 4 && std::memcmp(buf, "CONN", 4) == 0) {
                connected = true;
                const char ackn[4] = {'A', 'C', 'K', 'N'};
                ::sendto(fd, ackn, 4, 0, reinterpret_cast<sockaddr*>(&client), clen);
            } else if (n >= 4 && std::memcmp(buf, "PING", 4) == 0) {
                const char pong[4] = {'P', 'O', 'N', 'G'};
                ::sendto(fd, pong, 4, 0, reinterpret_cast<sockaddr*>(&client), clen);
            } else if (n > 4 && std::memcmp(buf, "M17P", 4) == 0) {
                lastPacket.assign(buf + 4, buf + n);
                ++packets;
            }
        }
    }
};

}  // namespace

int main()
{
    FakeBridge bridge;
    if (!bridge.start()) {
        std::printf("packet pipeline: cannot open a UDP socket, skipping\n");
        return 77;
    }

    M17Config config;
    config.enabled = true;
    config.callsign = "OK1LBC";
    config.bridge.enabled = true;
    config.bridge.host = "127.0.0.1";
    config.bridge.port = bridge.port;
    config.bridge.module = "A";

    const auto plugin = createM17Plugin(config, nullptr);

    for (int i = 0; i < 200 && !bridge.connected.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!bridge.connected.load()) {
        std::printf("packet pipeline: FAIL the plugin did not connect to the bridge\n");
        return 1;
    }

    const std::string text = "Zprava pres dva ramce z radia";
    const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 0, 1, 0);   // packet, data
    std::vector<Symbol> symbols = generatePacketSymbols(lsf, packetPayload(text));
    const std::vector<Symbol> tail = filler(1000);
    symbols.insert(symbols.end(), tail.begin(), tail.end());
    const std::vector<CD> iqDouble = modulateSymbols(symbols);
    std::vector<ComplexFloat> iq(iqDouble.size());
    for (std::size_t i = 0; i < iqDouble.size(); ++i)
        iq[i] = {static_cast<float>(iqDouble[i].re), static_cast<float>(iqDouble[i].im)};

    // Drive the plugin the way IQDispatcher does.
    constexpr std::size_t kBlock = 512;
    bool session = false;
    std::uint64_t sequence = 0;
    for (std::size_t off = 0; off < iq.size(); off += kBlock) {
        const std::size_t count = std::min<std::size_t>(kBlock, iq.size() - off);
        const IQBlock block{sequence++, iq.data() + off, count, 125000.0};
        if (session) {
            if (!plugin->process(block)) {
                plugin->stopSession();
                session = false;
            }
        } else if (plugin->detect(block).detected) {
            plugin->startSession(SessionContext{});
            session = true;
        }
    }
    // Give the network thread a moment to flush the packet.
    for (int i = 0; i < 100 && bridge.packets.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (session)
        plugin->stopSession();

    if (bridge.packets.load() != 1) {
        std::printf("packet pipeline: FAIL %d packet(s) reached the bridge, want 1\n",
                    bridge.packets.load());
        return 1;
    }
    // Payload: 30 byte LSF, type byte, text, CRC.
    const std::vector<std::uint8_t>& packet = bridge.lastPacket;
    const std::string received(packet.begin() + 31, packet.end() - 2);
    if (received != text) {
        std::printf("packet pipeline: FAIL payload \"%s\", want \"%s\"\n", received.c_str(), text.c_str());
        return 1;
    }

    const PluginTelemetry telemetry = plugin->telemetry();
    for (const char* key : {"afc_correction_hz", "sync_distance", "sync_confidence",
                            "frame_count", "sync_hits", "sync_misses",
                            "average_symbol_level", "rssi_at_sync"}) {
        if (telemetry.metrics.find(key) == telemetry.metrics.end()) {
            std::printf("packet pipeline: FAIL telemetry key %s is missing\n", key);
            return 1;
        }
    }
    if (telemetry.metrics.at("sync_hits") < 1.0 ||
        telemetry.metrics.at("average_symbol_level") <= 0.0) {
        std::printf("packet pipeline: FAIL sync diagnostics were not populated\n");
        return 1;
    }
    const std::string diagnostics = plugin->diagnosticsJson();
    if (diagnostics.find("\"candidates\"") == std::string::npos ||
        diagnostics.find("\"result\":\"M17\"") == std::string::npos) {
        std::printf("packet pipeline: FAIL M17 candidate history is empty\n");
        return 1;
    }

    std::printf("packet pipeline: ok (%zu byte packet, text \"%s\")\n", packet.size(), text.c_str());
    return 0;
}

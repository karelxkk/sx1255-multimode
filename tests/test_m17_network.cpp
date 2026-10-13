#include "sx1255/m17/M17Network.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

using namespace sx1255;

namespace {

struct FakeReflector {
    int fd = -1;
    int port = 0;
    std::atomic<bool> gotConn{false};
    std::atomic<bool> gotPong{false};
    std::atomic<int>  streams{0};
    std::thread thread;

    ~FakeReflector() { stop(); }

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
        std::uint8_t buf[1024];
        for (;;) {
            const ssize_t n = ::recvfrom(fd, buf, sizeof(buf), 0,
                                         reinterpret_cast<sockaddr*>(&client), &clen);
            if (n < 0)
                break;
            if (n >= 4 && std::memcmp(buf, "CONN", 4) == 0) {
                gotConn = true;
                const char ackn[4] = {'A', 'C', 'K', 'N'};
                ::sendto(fd, ackn, 4, 0, reinterpret_cast<sockaddr*>(&client), clen);
            } else if (n >= 4 && std::memcmp(buf, "PONG", 4) == 0) {
                gotPong = true;
            } else if (n >= 4 && std::memcmp(buf, "M17 ", 4) == 0) {
                streams++;
                // Echo the stream datagram back, as a current reflector does.
                ::sendto(fd, buf, static_cast<std::size_t>(n), 0,
                         reinterpret_cast<sockaddr*>(&client), clen);
            }
        }
    }
};

}  // namespace

int main()
{
    FakeReflector reflector;
    if (!reflector.start())
        return 77;   // sandbox has no network sockets

    m17::M17ReflectorClient::Config cfg;
    cfg.host = "127.0.0.1";
    cfg.port = reflector.port;
    cfg.module = "A";
    cfg.callsign = "N0CALL";

    m17::M17ReflectorClient client(cfg);
    if (!client.start())
        return 2;

    // Wait for the CONN/ACKN handshake.
    auto connected = [&] { return client.telemetry().connected; };
    for (int i = 0; i < 40 && !connected(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!connected())
        return 3;
    if (!reflector.gotConn.load())
        return 4;

    m17::StreamDatagram dg;
    dg.streamId = 0x1234;
    dg.frameNumber = 0;
    dg.lastFrame = false;
    dg.lsf = m17::makeLSF("N0CALL", "N0CALL", 1, 2, 0);
    for (int i = 0; i < 16; ++i)
        dg.payload[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);

    if (!client.sendStream(dg))
        return 5;

    for (int i = 0; i < 40 && reflector.streams.load() < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (reflector.streams.load() < 1)
        return 6;

    // The reflector echoed the datagram; wait for the client to receive it.
    for (int i = 0; i < 40 && client.telemetry().rxStreams < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (client.telemetry().rxStreams < 1)
        return 7;

    client.stop();
    return 0;
}

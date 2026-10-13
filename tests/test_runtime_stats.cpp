#include "sx1255/config/Config.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/diag/HttpStatusServer.hpp"
#include "sx1255/diag/SystemStatistics.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <cerrno>
#include <string>

using namespace sx1255;

namespace {

std::string httpGet(std::uint16_t port, const char* path)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return {};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return {};
    }
    const std::string req = std::string("GET ") + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);
    std::string out;
    char buf[2048];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        out += buf;
    }
    ::close(fd);
    return out;
}

}  // namespace

int main()
{
    SystemStatistics stats;
    stats.recordDetection(Mode::FM);
    stats.recordDetection(Mode::FM);
    stats.recordDetection(Mode::DStar);
    stats.recordDetection(Mode::M17);
    stats.recordSession(Mode::M17);
    stats.recordSession(Mode::M17);
    stats.recordFalsePositive();

    if (stats.fmDetected() != 2) return 1;
    if (stats.dstarDetected() != 1) return 2;
    if (stats.m17Detected() != 1) return 3;
    if (stats.m17Sessions() != 2) return 4;
    if (stats.falsePositives() != 1) return 5;

    const std::string json = stats.statusJson();
    if (json.find("\"fm_detected\":2") == std::string::npos) return 6;
    if (json.find("\"dstar_detected\":1") == std::string::npos) return 7;
    if (json.find("\"m17_sessions\":2") == std::string::npos) return 8;
    if (json.find("\"false_positives\":1") == std::string::npos) return 9;
    if (json.find("\"state\":") == std::string::npos) return 10;

    // Socket creation is disallowed in sandboxed CI (EPERM/EACCES); skip the
    // live HTTP round-trip there rather than failing a valid build.
    const int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    if (probe < 0 && (errno == EPERM || errno == EACCES))
        return 77;
    if (probe >= 0)
        ::close(probe);

    Config config;
    config.fm.enabled = true;
    config.dstar.enabled = true;
    config.m17.enabled = true;
    SessionManager sessions;
    HttpStatusServer server(18080, stats, sessions, config, [] { return -82.0; });
    server.setIqStatusProvider([] {
        IQStatusSnapshot snapshot;
        snapshot.recording = true;
        snapshot.capturedSamples = 123;
        snapshot.file = "capture.iq";
        return snapshot;
    });
    if (!server.start())
        return 11;

    const std::string status = httpGet(18080, "/api/status");
    if (status.find("\"state\":\"IDLE\"") == std::string::npos) return 12;
    if (status.find("\"rssi\":-82") == std::string::npos) return 13;

    const std::string plugins = httpGet(18080, "/api/plugins");
    if (plugins.find("\"name\":\"DSTAR\",\"enabled\":true") == std::string::npos) return 14;
    if (plugins.find("\"name\":\"M17\",\"enabled\":true") == std::string::npos) return 15;

    const std::string iq = httpGet(18080, "/api/iq");
    if (iq.find("\"recording\":true") == std::string::npos) return 16;
    if (iq.find("\"replay\":false") == std::string::npos) return 17;
    if (iq.find("\"captured_samples\":123") == std::string::npos) return 18;
    if (iq.find("\"file\":\"capture.iq\"") == std::string::npos) return 19;

    server.stop();
    return 0;
}

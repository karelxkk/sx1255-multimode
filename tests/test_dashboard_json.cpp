#include "sx1255/config/Config.hpp"
#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/diag/HttpStatusServer.hpp"
#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
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
    char buf[4096];
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
    const int probe = ::socket(AF_INET, SOCK_STREAM, 0);
    if (probe < 0 && (errno == EPERM || errno == EACCES))
        return 77;
    if (probe >= 0)
        ::close(probe);

    SystemStatistics stats;
    Config config;
    config.fm.enabled = true;
    config.dstar.enabled = true;
    config.m17.enabled = true;

    PluginRegistry registry;
    registry.registerPlugin(createFmPlugin(config.fm, &stats));
    registry.registerPlugin(createDStarPlugin(config.dstar, &stats));
    registry.registerPlugin(createM17Plugin(config.m17, &stats));

    SessionManager sessions;
    HttpStatusServer server(18081, stats, sessions, config, [] { return -82.0; });
    server.setPluginRegistry(&registry);
    server.setSpectrumProvider([] {
        return std::string("{\"fft_size\":4096,\"spectrum\":[1,2,3],\"peak_hold\":[1,2,3]}");
    });
    server.setWaterfallProvider([](std::uint64_t) {
        return std::string("{\"bins\":3,\"max_rows\":500,\"rows\":1,\"data\":[[1,2,3]]}");
    });
    server.setHealthProvider([] {
        return std::string("{\"healthy\":true,\"components\":[{\"name\":\"frontend\",\"healthy\":true}]}");
    });
    server.setSystemProvider([] {
        return std::string("{\"memory_rss_kb\":123,\"cpu_seconds\":1.5,\"threads\":4,\"frontend_state\":\"running\"}");
    });

    if (!server.start())
        return 1;

    const std::string html = httpGet(18081, "/");
    if (html.find("<html") == std::string::npos)
        return 2;
    if (html.find("spectrum") == std::string::npos)
        return 3;

    const std::string spectrum = httpGet(18081, "/api/spectrum");
    if (spectrum.find("\"fft_size\":4096") == std::string::npos)
        return 4;

    const std::string waterfall = httpGet(18081, "/api/waterfall");
    if (waterfall.find("\"max_rows\":500") == std::string::npos)
        return 5;

    const std::string plugins = httpGet(18081, "/api/plugins");
    if (plugins.find("\"name\":\"FM\"") == std::string::npos)
        return 6;
    if (plugins.find("\"rssi_db\"") == std::string::npos)
        return 7;
    if (plugins.find("\"sync_confidence\"") == std::string::npos)
        return 8;
    if (plugins.find("\"lsf_count\"") == std::string::npos)
        return 9;

    const std::string status = httpGet(18081, "/api/status");
    if (status.find("\"state\":") == std::string::npos)
        return 10;

    const std::string health = httpGet(18081, "/api/health");
    if (health.find("\"healthy\":true") == std::string::npos)
        return 11;
    if (health.find("\"name\":\"frontend\"") == std::string::npos)
        return 12;

    const std::string system = httpGet(18081, "/api/system");
    if (system.find("\"memory_rss_kb\":123") == std::string::npos)
        return 13;
    if (system.find("\"frontend_state\":\"running\"") == std::string::npos)
        return 14;

    server.stop();
    return 0;
}

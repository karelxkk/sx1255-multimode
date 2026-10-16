#include "sx1255/diag/HttpStatusServer.hpp"

#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/web/WebDashboard.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <cstdio>
#include <string>

namespace sx1255 {

namespace {

// Extract the "since=N" query parameter from an HTTP request line.
std::uint64_t parseSince(const char* request)
{
    const char* p = std::strstr(request, "since=");
    if (!p)
        return 0;
    p += 6;
    std::uint64_t value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + static_cast<std::uint64_t>(*p - '0');
        ++p;
    }
    return value;
}

}  // namespace

HttpStatusServer::HttpStatusServer(std::uint16_t port,
                                   SystemStatistics& stats,
                                   SessionManager& sessions,
                                   const Config& config,
                                   std::function<double()> rssiGetter)
    : m_port(port), m_stats(stats), m_sessions(sessions), m_config(config), m_rssiGetter(std::move(rssiGetter))
{
}

HttpStatusServer::~HttpStatusServer()
{
    stop();
}

bool HttpStatusServer::start()
{
    if (m_running.load())
        return true;

    m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0)
        return false;

    int reuse = 1;
    ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(m_fd, 4) < 0) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_running = true;
    m_thread = std::thread([this] { run(); });
    return true;
}

void HttpStatusServer::setIqStatusProvider(std::function<IQStatusSnapshot()> provider)
{
    m_iqStatusProvider = std::move(provider);
}

void HttpStatusServer::setSpectrumProvider(std::function<std::string()> provider)
{
    m_spectrumProvider = std::move(provider);
}

void HttpStatusServer::setWaterfallProvider(std::function<std::string(std::uint64_t)> provider)
{
    m_waterfallProvider = std::move(provider);
}

void HttpStatusServer::setHealthProvider(std::function<std::string()> provider)
{
    m_healthProvider = std::move(provider);
}

void HttpStatusServer::setSystemProvider(std::function<std::string()> provider)
{
    m_systemProvider = std::move(provider);
}

void HttpStatusServer::setPluginRegistry(PluginRegistry* registry)
{
    m_registry = registry;
}

void HttpStatusServer::stop()
{
    m_running = false;
    if (m_fd >= 0) {
        ::shutdown(m_fd, SHUT_RDWR);
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_thread.joinable())
        m_thread.join();
}

std::string HttpStatusServer::handleStatus()
{
    m_stats.setRssi(m_rssiGetter());
    m_stats.setCurrentMode(SessionManager::toString(m_sessions.state()));
    switch (m_sessions.networkMode()) {
        case Mode::M17:   m_stats.setNetworkMode("M17"); break;
        case Mode::DStar: m_stats.setNetworkMode("D-Star"); break;
        default:          m_stats.setNetworkMode("FM / SVXLink"); break;
    }
    return m_stats.statusJson();
}

std::string HttpStatusServer::handlePlugins() const
{
    if (m_registry) {
        std::string out = "[";
        bool first = true;
        for (const auto& plugin : m_registry->plugins()) {
            if (!plugin)
                continue;
            if (!first)
                out += ',';
            first = false;

            const char* name = "?";
            switch (plugin->mode()) {
                case Mode::FM:    name = "FM"; break;
                case Mode::DStar: name = "DSTAR"; break;
                case Mode::M17:   name = "M17"; break;
                default:          name = "UNKNOWN"; break;
            }

            const PluginTelemetry t = plugin->telemetry();
            out += "{\"name\":\"";
            out += name;
            out += "\",\"enabled\":true,\"mode\":\"";
            out += name;
            out += "\",\"priority\":";
            out += std::to_string(plugin->priority());
            out += ",\"active\":";
            out += (m_sessions.owner() == plugin ? "true" : "false");
            out += ",\"telemetry\":{";
            bool firstMetric = true;
            for (const auto& [key, value] : t.metrics) {
                if (!firstMetric)
                    out += ',';
                firstMetric = false;
                out += '"';
                out += key;
                out += "\":";
                char valueBuf[32];
                std::snprintf(valueBuf, sizeof(valueBuf), "%.6g", value);
                out += valueBuf;
            }
            for (const auto& [key, value] : t.strings) {
                if (!firstMetric)
                    out += ',';
                firstMetric = false;
                out += '"';
                out += key;
                out += "\":\"";
                out += value;
                out += '"';
            }
            out += "}}";
        }
        out += ']';
        return out;
    }

    return std::string("[") +
           "{\"name\":\"FM\",\"enabled\":" + (m_config.fm.enabled ? "true" : "false") + "}," +
           "{\"name\":\"DSTAR\",\"enabled\":" + (m_config.dstar.enabled ? "true" : "false") + "}," +
           "{\"name\":\"M17\",\"enabled\":" + (m_config.m17.enabled ? "true" : "false") + "}" +
           "]";
}

std::string HttpStatusServer::handleIq() const
{
    IQStatusSnapshot snapshot;
    if (m_iqStatusProvider)
        snapshot = m_iqStatusProvider();

    // Minimal JSON string escaping for filesystem paths.
    std::string file;
    for (const char c : snapshot.file) {
        if (c == '"' || c == '\\')
            file += '\\';
        file += c;
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "{\"recording\":%s,\"replay\":%s,\"captured_samples\":%llu,\"file\":\"%s\"}",
        snapshot.recording ? "true" : "false",
        snapshot.replay ? "true" : "false",
        static_cast<unsigned long long>(snapshot.capturedSamples),
        file.c_str());
    return buf;
}

std::string HttpStatusServer::handleSpectrum() const
{
    return m_spectrumProvider ? m_spectrumProvider() : "{\"spectrum\":[],\"peak_hold\":[]}";
}

std::string HttpStatusServer::handleWaterfall(std::uint64_t sinceSeq) const
{
    return m_waterfallProvider
        ? m_waterfallProvider(sinceSeq)
        : "{\"bins\":0,\"max_rows\":0,\"rows\":0,\"seq\":0,\"data\":[]}";
}

std::string HttpStatusServer::handleHealth() const
{
    return m_healthProvider ? m_healthProvider() : "{\"healthy\":true,\"components\":[]}";
}

std::string HttpStatusServer::handleSystem() const
{
    return m_systemProvider ? m_systemProvider() : "{\"memory_rss_kb\":0,\"cpu_seconds\":0}";
}

std::string HttpStatusServer::handleDashboard() const
{
    return sx1255::web::dashboardHtml();
}

void HttpStatusServer::run()
{
    while (m_running.load()) {
        const int client = ::accept(m_fd, nullptr, nullptr);
        if (client < 0) {
            if (!m_running.load()) break;
            continue;
        }

        char buf[1024]{};
        const ssize_t n = ::recv(client, buf, sizeof(buf) - 1, 0);
        std::string body;
        std::string status = "200 OK";
        std::string contentType = "application/json";
        if (n > 0) {
            buf[n] = '\0';
            if (std::strstr(buf, "GET /api/status"))
                body = handleStatus();
            else if (std::strstr(buf, "GET /api/plugins"))
                body = handlePlugins();
            else if (std::strstr(buf, "GET /api/iq"))
                body = handleIq();
            else if (std::strstr(buf, "GET /api/spectrum"))
                body = handleSpectrum();
            else if (std::strstr(buf, "GET /api/waterfall"))
                body = handleWaterfall(parseSince(buf));
            else if (std::strstr(buf, "GET /api/health"))
                body = handleHealth();
            else if (std::strstr(buf, "GET /api/system"))
                body = handleSystem();
            else if (std::strstr(buf, "GET / ")) {
                body = handleDashboard();
                contentType = "text/html; charset=utf-8";
            }
            else
                status = "404 Not Found";
        }

        std::string response = "HTTP/1.1 " + status + "\r\n"
                               "Content-Type: " + contentType + "\r\n"
                               "Cache-Control: no-store, max-age=0\r\n"
                               "Connection: close\r\n"
                               "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        ::send(client, response.data(), response.size(), MSG_NOSIGNAL);
        ::close(client);
    }
}

}  // namespace sx1255

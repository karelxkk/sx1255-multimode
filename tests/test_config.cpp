#include "sx1255/config/Config.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

std::string writeTemp(const std::string& contents)
{
    const char* tmp = std::getenv("TMPDIR");
    std::string path = (tmp && *tmp ? tmp : "/tmp");
    path += "/sx1255-test-XXXXXX";

    // mkstemp needs a mutable buffer.
    std::vector<char> buffer(path.begin(), path.end());
    buffer.push_back('\0');
    const int fd = mkstemp(buffer.data());
    if (fd < 0)
        throw std::runtime_error("mkstemp failed");
    path = buffer.data();

    std::ofstream out(path);
    out << contents;
    out.close();
    close(fd);
    return path;
}

}  // namespace

int main()
{
    const std::string yaml =
        "frontend:\n"
        "  rx_freq: 430975000\n"
        "  tx_freq: 438575000\n"
        "  power: 10\n"
        "  squelch: -120\n"
        "\n"
        "modes:\n"
        "  fm:\n"
        "    enabled: true\n"
        "    ctcss: 88.5\n"
        "  dstar:\n"
        "    enabled: false\n"
        "    callsign: OK1XYZ\n"
        "    tx_tail_ms: 70\n"
        "    gateway:\n"
        "      enabled: true\n"
        "      port: 20020\n"
        "  m17:\n"
        "    enabled: true\n"
        "network:\n"
        "  mode: dstar\n";

    const std::string path = writeTemp(yaml);
    const sx1255::Config config = sx1255::loadConfig(path);
    std::remove(path.c_str());

    if (config.frontend.rxFrequency != 430975000U)
        return 1;
    if (config.frontend.txFrequency != 438575000U)
        return 2;
    if (config.frontend.power != 10)
        return 3;
    if (config.frontend.squelch != -120)
        return 4;
    if (!config.fm.enabled)
        return 5;
    if (config.fm.ctcssRx < 88.4 || config.fm.ctcssRx > 88.6)
        return 6;
    if (config.dstar.enabled)
        return 7;
    if (config.dstar.callsign != "OK1XYZ")
        return 11;
    if (config.dstar.txTailMs != 70)
        return 12;
    if (config.dstar.txPreambleBytes != 210)
        return 13;
    if (!config.dstar.gateway.enabled)
        return 14;
    if (config.dstar.gateway.port != 20020)
        return 15;
    if (!config.m17.enabled)
        return 8;
    if (config.network.mode != "dstar")
        return 16;
    if (sx1255::parseNetworkMode(config.network.mode) != sx1255::Mode::DStar)
        return 17;

    // Serialization must contain the values we just parsed.
    const std::string serialized = sx1255::serializeConfig(config);
    if (serialized.find("430975000") == std::string::npos)
        return 9;
    if (serialized.find("438575000") == std::string::npos)
        return 10;

    return 0;
}

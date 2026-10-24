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
        "    bridge:\n"
        "      enabled: true\n"
        "      host: 127.0.0.1\n"
        "      port: 17001\n"
        "      module: A\n"
        "    diagnostics:\n"
        "      candidate_log: true\n"
        "      candidate_log_file: /tmp/m17-candidates.log\n"
        "      frame_audit_log: true\n"
        "      frame_audit_log_file: /tmp/m17-frame-audit.log\n"
        "      audit_directory: /tmp/m17-audit\n"
        "      near_miss_min_distance: 4.5\n"
        "      near_miss_max_distance: 7.0\n"
        "      near_miss_rssi_dbfs: -54\n"
        "      near_miss_interval_ms: 125\n"
        "network:\n"
        "  mode: dstar\n"
        "power_save:\n"
        "  enabled: true\n"
        "  sleep_below_dbfs: -58.5\n"
        "  wake_above_dbfs: -56.5\n"
        "automatic_capture:\n"
        "  trigger:\n"
        "    m17_near_miss: true\n";

    const std::string path = writeTemp(yaml);
    const sx1255::Config config = sx1255::loadConfig(path);
    std::remove(path.c_str());

    if (config.frontend.rxFrequency != 430975000U)
        return 1;
    if (config.frontend.txFrequency != 438575000U)
        return 2;
    if (config.frontend.power != 10)
        return 3;
    if (config.frontend.squelch != -55)
        return 4;
    if (!config.fm.enabled)
        return 5;
    if (config.fm.rssiSquelchDb < -60.0 || config.fm.rssiSquelchDb > -50.0)
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
    if (!config.m17.bridge.enabled)
        return 18;
    if (config.m17.bridge.host != "127.0.0.1" || config.m17.bridge.port != 17001)
        return 19;
    if (!config.m17.diagnostics.candidateLog ||
        config.m17.diagnostics.candidateLogFile != "/tmp/m17-candidates.log")
        return 20;
    if (!config.m17.diagnostics.frameAuditLog ||
        config.m17.diagnostics.frameAuditLogFile != "/tmp/m17-frame-audit.log" ||
        config.m17.diagnostics.auditDirectory != "/tmp/m17-audit") {
        return 23;
    }
    if (config.m17.diagnostics.nearMissMaxDistance != 7.0 ||
        config.m17.diagnostics.nearMissRssiDbfs != -54.0 ||
        config.m17.diagnostics.nearMissIntervalMs != 125)
        return 21;
    if (!config.automaticCapture.triggerM17NearMiss)
        return 22;
    if (config.network.mode != "dstar")
        return 16;
    if (sx1255::parseNetworkMode(config.network.mode) != sx1255::Mode::DStar)
        return 17;
    if (!config.powerSave.enabled ||
        config.powerSave.sleepBelowDbfs != -58.5 ||
        config.powerSave.wakeAboveDbfs != -56.5 ||
        config.powerSave.prebufferMs != 250.0) {
        return 24;
    }

    // Serialization must contain the values we just parsed.
    const std::string serialized = sx1255::serializeConfig(config);
    if (serialized.find("430975000") == std::string::npos)
        return 9;
    if (serialized.find("438575000") == std::string::npos)
        return 10;
    if (serialized.find("sleep_below_dbfs: -58.5") == std::string::npos ||
        serialized.find("wake_above_dbfs: -56.5") == std::string::npos ||
        serialized.find("prebuffer_ms: 250") == std::string::npos) {
        return 25;
    }

    return 0;
}

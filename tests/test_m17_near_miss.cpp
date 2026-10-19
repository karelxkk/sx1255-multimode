#include "sx1255/config/Config.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

int main()
{
    M17Config config;
    config.enabled = true;
    config.bridge.enabled = false;
    config.reflector.enabled = false;
    config.diagnostics.nearMissMinDistance = 0.0;
    config.diagnostics.nearMissMaxDistance = 10.0;
    config.diagnostics.nearMissRssiDbfs = -120.0;
    config.diagnostics.nearMissIntervalMs = 0;
    config.diagnostics.candidateLog = true;
    config.diagnostics.candidateLogFile = "/tmp/sx1255-m17-near-miss.log";
    config.diagnostics.frameAuditLog = true;
    config.diagnostics.frameAuditLogFile = "/tmp/sx1255-m17-frame-audit.log";
    config.diagnostics.auditDirectory = "/tmp/sx1255-m17-audit";
    std::remove(config.diagnostics.candidateLogFile.c_str());
    std::remove(config.diagnostics.frameAuditLogFile.c_str());
    std::filesystem::remove_all(config.diagnostics.auditDirectory);

    const auto plugin = createM17Plugin(config, nullptr);
    std::atomic<int> captures{0};
    plugin->setNearMissHandler([&captures] { ++captures; });

    const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
    std::vector<Symbol> symbols = appendPreamble({}, false);
    std::vector<Symbol> frame = generateLSFSymbols(lsf);
    // Keep the sync word intact, destroy the encoded payload so the LSF is a
    // real sync hit with a bad CRC: exactly the near-miss condition.
    for (std::size_t i = 16; i < frame.size(); i += 3)
        frame[i] = frame[i] > 0.0F ? -3.0F : +3.0F;
    symbols.insert(symbols.end(), frame.begin(), frame.end());
    for (int i = 0; i < 400; ++i)
        symbols.push_back(i % 2 ? +3.0F : -3.0F);

    const std::vector<CD> iqDouble = modulateSymbols(symbols);
    std::vector<ComplexFloat> iq(iqDouble.size());
    for (std::size_t i = 0; i < iqDouble.size(); ++i)
        iq[i] = {static_cast<float>(iqDouble[i].re), static_cast<float>(iqDouble[i].im)};

    constexpr std::size_t kBlock = 512;
    std::uint64_t sequence = 0;
    for (std::size_t off = 0; off < iq.size(); off += kBlock) {
        const std::size_t count = std::min<std::size_t>(kBlock, iq.size() - off);
        plugin->detect(IQBlock{sequence++, iq.data() + off, count, 125000.0});
    }

    if (captures.load() == 0)
        return 1;
    const std::string diagnostics = plugin->diagnosticsJson();
    if (diagnostics.find("\"result\":\"Rejected\"") == std::string::npos)
        return 2;
    if (diagnostics.find("\"afc_correction_hz\"") == std::string::npos ||
        diagnostics.find("\"sync_distance\"") == std::string::npos) {
        return 3;
    }
    std::ifstream log(config.diagnostics.candidateLogFile);
    const std::string line((std::istreambuf_iterator<char>(log)),
                           std::istreambuf_iterator<char>());
    if (line.find("Result=Rejected") == std::string::npos ||
        line.find("AFC=") == std::string::npos ||
        line.find("SyncDistance=") == std::string::npos) {
        return 4;
    }
    std::ifstream frameLog(config.diagnostics.frameAuditLogFile);
    const std::string frameLine((std::istreambuf_iterator<char>(frameLog)),
                                std::istreambuf_iterator<char>());
    if (frameLine.find("SoftbitRMS=") == std::string::npos ||
        frameLine.find("ViterbiMetric=") == std::string::npos ||
        frameLine.find("CRC=FAIL") == std::string::npos) {
        return 5;
    }
    if (!std::filesystem::exists(
            std::string(config.diagnostics.auditDirectory) + "/softbit_stats.json")) {
        return 6;
    }
    bool artifact = false;
    for (const auto& entry :
         std::filesystem::directory_iterator(config.diagnostics.auditDirectory)) {
        if (entry.path().extension() == ".json" &&
            entry.path().filename().string().rfind("frame_", 0) == 0) {
            artifact = true;
        }
    }
    if (!artifact)
        return 7;
    std::remove(config.diagnostics.candidateLogFile.c_str());
    std::remove(config.diagnostics.frameAuditLogFile.c_str());
    std::filesystem::remove_all(config.diagnostics.auditDirectory);
    return 0;
}

#include "sx1255/config/Config.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
    config.diagnostics.candidateLog = false;
    config.diagnostics.frameAuditLog = false;

    const auto plugin = createM17Plugin(config, nullptr);
    const LSF lsf = makeLSF("N1ADJ", "OK1LBC", 1, 2, 0);
    const std::vector<std::uint8_t> payload(16, 0x42);

    std::vector<Symbol> symbols = appendPreamble({}, false);
    const std::vector<Symbol> lsfSymbols = generateLSFSymbols(lsf);
    symbols.insert(symbols.end(), lsfSymbols.begin(), lsfSymbols.end());

    for (std::uint16_t frame = 0; frame < 14; ++frame) {
        if (frame == 7) {
            // Insert an LSF-shaped burst where a stream sync would be expected.
            // Its random payload will fail CRC and must not change the BER.
            symbols.insert(symbols.end(),
                           LSFPreambleSymbols.begin(), LSFPreambleSymbols.end());
            symbols.insert(symbols.end(),
                           LSFSyncSymbols.begin(), LSFSyncSymbols.end());
            for (std::size_t i = 0; i < SymbolsPerPayload; ++i)
                symbols.push_back((i & 1U) ? -1.0F : +1.0F);
        }

        const std::vector<Symbol> stream = generateStreamSymbols(lsf, frame, payload);
        symbols.insert(symbols.end(), stream.begin(), stream.end());
    }

    const std::vector<CD> iqDouble = modulateSymbols(symbols);
    std::vector<ComplexFloat> iq(iqDouble.size());
    for (std::size_t i = 0; i < iqDouble.size(); ++i)
        iq[i] = {static_cast<float>(iqDouble[i].re),
                 static_cast<float>(iqDouble[i].im)};

    constexpr std::size_t kBlock = 512;
    std::uint64_t sequence = 0;
    for (std::size_t off = 0; off < iq.size(); off += kBlock) {
        const std::size_t count = std::min<std::size_t>(kBlock, iq.size() - off);
        plugin->detect(IQBlock{sequence++, iq.data() + off, count, 125000.0});
    }

    const PluginTelemetry telemetry = plugin->telemetry();
    const auto ber = telemetry.metrics.find("last_frame_ber_percent");
    if (ber == telemetry.metrics.end() || ber->second != 0.0)
        return 1;
    const auto mer = telemetry.metrics.find("last_frame_mer_db");
    if (mer == telemetry.metrics.end() || mer->second < 10.0 || mer->second > 20.0)
        return 5;

    const std::string diagnostics = plugin->diagnosticsJson();
    if (diagnostics.find(
            "\"crc\":\"FAIL\",\"lsf\":\"FAIL\",\"frame\":\"FAIL\",\"result\":\"M17\"") !=
        std::string::npos) {
        return 2;
    }
    if (diagnostics.find(
            "\"crc\":\"FAIL\",\"lsf\":\"FAIL\",\"frame\":\"FAIL\",\"result\":\"Rejected\"") ==
        std::string::npos) {
        return 3;
    }
    if (diagnostics.find("\"last_frame_mer_db\":") == std::string::npos ||
        diagnostics.find("\"mer_db\":") == std::string::npos) {
        return 6;
    }

    // The frames following the false LSF must still have been decoded.
    const std::string laterStream =
        "\"sync_type\":\"Stream\"";
    if (diagnostics.find(laterStream) == std::string::npos)
        return 4;

    return 0;
}

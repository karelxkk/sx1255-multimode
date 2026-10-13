#include "sx1255/config/Config.hpp"
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/plugins/PluginFactory.hpp"
#include "sx1255/Types.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string expectedModeFor(const std::string& path)
{
    if (path.find("/fm/") != std::string::npos || path.find("fm") != std::string::npos)
        return "fm";
    if (path.find("/dstar/") != std::string::npos || path.find("dstar") != std::string::npos)
        return "dstar";
    if (path.find("/m17/") != std::string::npos || path.find("m17") != std::string::npos)
        return "m17";
    return "unknown";
}

struct AuditResult {
    std::string file;
    std::string expected;
    bool fm = false;
    bool dstar = false;
    bool m17 = false;
};

}  // namespace

int main(int argc, char** argv)
{
    std::string rootDir = "datasets";
    std::string outPath = "false_positive_report.md";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--dataset" || arg == "-d") && i + 1 < argc)
            rootDir = argv[++i];
        else if ((arg == "--out" || arg == "-o") && i + 1 < argc)
            outPath = argv[++i];
        else {
            std::fprintf(stderr, "usage: %s [--dataset DIR] [--out REPORT.md]\n", argv[0]);
            return 2;
        }
    }

    sx1255::Config config;
    config.fm.enabled = true;
    config.dstar.enabled = true;
    config.m17.enabled = true;

    std::vector<std::string> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(rootDir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        if (it->is_regular_file() && it->path().extension() == ".iq")
            files.push_back(it->path().string());
    }
    std::sort(files.begin(), files.end());

    std::vector<AuditResult> results;

    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        sx1255::debug::IQFileHeader header;
        if (!in.is_open() || !sx1255::debug::readIQHeader(in, header) ||
            !sx1255::debug::validateIQHeader(header)) {
            std::fprintf(stderr, "skip invalid IQ file: %s\n", file.c_str());
            continue;
        }

        std::vector<sx1255::ComplexFloat> iq(header.sampleCount);
        if (sx1255::debug::readIQSamples(in, iq.data(), iq.size()) != iq.size()) {
            std::fprintf(stderr, "truncated IQ file: %s\n", file.c_str());
            continue;
        }

        sx1255::SystemStatistics stats;
        auto fm = sx1255::createFmPlugin(config.fm, &stats);
        auto dstar = sx1255::createDStarPlugin(config.dstar, &stats);
        auto m17 = sx1255::createM17Plugin(config.m17, &stats);

        AuditResult result;
        result.file = file;
        result.expected = expectedModeFor(file);

        constexpr std::size_t kBlock = 2048;
        for (std::size_t off = 0; off < iq.size(); off += kBlock) {
            const std::size_t count = std::min(kBlock, iq.size() - off);
            const sx1255::IQBlock block{0, iq.data() + off, count, 125000.0};
            if (fm->detect(block).detected)
                result.fm = true;
            if (dstar->detect(block).detected)
                result.dstar = true;
            if (m17->detect(block).detected)
                result.m17 = true;
        }

        results.push_back(std::move(result));
    }

    std::size_t falsePositives = 0;
    std::string md = "# False Positive Audit\n\n";
    md += "Audit vzájemných záměn módů. `false_positive` = dataset označený jako jeden mód detekoval jiný mód.\n\n";
    md += "| soubor | očekáván | FM | D-Star | M17 | výsledek |\n";
    md += "|---|---|---|---|---|---|\n";

    for (const auto& r : results) {
        bool fp = false;
        if (r.expected == "fm" && (r.dstar || r.m17))
            fp = true;
        else if (r.expected == "dstar" && (r.fm || r.m17))
            fp = true;
        else if (r.expected == "m17" && (r.fm || r.dstar))
            fp = true;
        else if (r.expected == "unknown" && (r.fm || r.dstar || r.m17))
            fp = true;

        if (fp)
            ++falsePositives;

        md += "| " + r.file + " | " + r.expected + " | " +
              (r.fm ? "ano" : "ne") + " | " +
              (r.dstar ? "ano" : "ne") + " | " +
              (r.m17 ? "ano" : "ne") + " | " +
              (fp ? "FALSE POSITIVE" : "ok") + " |\n";
    }

    md += "\n## Shrnutí\n\n";
    md += "- datasetů: " + std::to_string(results.size()) + "\n";
    md += "- false positives: " + std::to_string(falsePositives) + "\n";
    md += "- cíl: 0\n\n";
    md += (falsePositives == 0 ? "**Výsledek: PASS (0 false positives)**\n"
                               : "**Výsledek: FAIL**\n");

    std::ofstream out(outPath, std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    out << md;

    std::fprintf(stderr, "audit: %zu datasets, %zu false positives -> %s\n",
                 results.size(), falsePositives, outPath.c_str());
    return falsePositives == 0 ? 0 : 1;
}

// AFC audit helper: run the sx1255-multimode 125 kSa/s -> 25 kSa/s front end
// and the C++ AFC, then export the decimated baseband for the original Go
// implementation.
//
//   m17_afc_audit input.iq --baseband baseband.f64 --csv cpp_afc.csv
//
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/m17/M17DSP.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

namespace {

struct Options {
    std::string input;
    std::string baseband;
    std::string csv;
    std::string summary;
    std::uint64_t stride = 25;
    bool raw = false;
};

bool parseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--baseband" && i + 1 < argc) {
            options.baseband = argv[++i];
        } else if (arg == "--csv" && i + 1 < argc) {
            options.csv = argv[++i];
        } else if (arg == "--summary" && i + 1 < argc) {
            options.summary = argv[++i];
        } else if (arg == "--stride" && i + 1 < argc) {
            options.stride = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--raw") {
            options.raw = true;
        } else if (!arg.empty() && arg[0] != '-' && options.input.empty()) {
            options.input = arg;
        } else {
            return false;
        }
    }
    return !options.input.empty() && !options.baseband.empty() &&
           !options.csv.empty() && options.stride > 0;
}

void writeDouble(std::ostream& out, double value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::fprintf(stderr,
            "usage: %s input.iq --baseband FILE --csv FILE "
            "[--summary FILE] [--stride N] [--raw]\n", argv[0]);
        return 2;
    }

    std::ifstream input(options.input, std::ios::binary);
    if (!input.is_open()) {
        std::fprintf(stderr, "cannot open %s\n", options.input.c_str());
        return 1;
    }
    if (options.raw) {
        input.clear();
        input.seekg(0, std::ios::beg);
    } else {
        debug::IQFileHeader header;
        if (!debug::readIQHeader(input, header) || !debug::validateIQHeader(header)) {
            std::fprintf(stderr,
                         "%s is not a valid IQ recording (use --raw for headerless "
                         "interleaved float32 IQ)\n", options.input.c_str());
            return 1;
        }
        if (header.sampleRateHz != static_cast<std::uint32_t>(sampleRateSX1255)) {
            std::fprintf(stderr,
                         "warning: header says %u Hz, processing as %d Hz\n",
                         header.sampleRateHz, sampleRateSX1255);
        }
    }

    std::ofstream baseband(options.baseband, std::ios::binary | std::ios::trunc);
    std::ofstream csv(options.csv, std::ios::trunc);
    if (!baseband.is_open() || !csv.is_open()) {
        std::fprintf(stderr, "cannot open audit output files\n");
        return 1;
    }
    csv << "step,afc_correction_hz\n";

    ComplexDCRemoval dcr(0.9999);
    PolyphaseDecimator decimator(
        designChannelFilter(channelBWSX1255, sampleRateSX1255, channelTapsSX1255),
        rxDecimSX1255);
    FMDemodAFC afc(basebandDCAvgCntSX1255, rxFastNSX1255, rxSlowNSX1255);

    std::vector<ComplexFloat> block(4096);
    double ncoPhase = 0.0;
    const double ncoStep =
        -2.0 * std::numbers::pi * 25000.0 / static_cast<double>(sampleRateSX1255);
    std::uint64_t iqSamples = 0;
    std::uint64_t steps = 0;
    for (;;) {
        const std::size_t got = debug::readIQSamples(input, block.data(), block.size());
        if (got == 0)
            break;
        for (std::size_t n = 0; n < got; ++n) {
            double i = static_cast<double>(block[n].i);
            double q = static_cast<double>(block[n].q);
            if (!options.raw) {
                const double c = std::cos(ncoPhase);
                const double s = std::sin(ncoPhase);
                const double ri = i * c - q * s;
                const double rq = i * s + q * c;
                i = ri;
                q = rq;
                ncoPhase += ncoStep;
            }
            const CD sample{i, q};
            ++iqSamples;
            for (const CD dc : decimator.process(dcr.process(sample))) {
                writeDouble(baseband, dc.re);
                writeDouble(baseband, dc.im);
                afc.process(dc);
                if (steps % options.stride == 0)
                    csv << steps << ',' << afc.offsetHz(25000.0) << '\n';
                ++steps;
            }
        }
    }

    if (!options.summary.empty()) {
        std::ofstream summary(options.summary, std::ios::trunc);
        if (!summary.is_open())
            return 1;
        summary << "{\"input\":\"" << options.input
                << "\",\"iq_samples\":" << iqSamples
                << ",\"decimated_samples\":" << steps
                << ",\"stride\":" << options.stride
                << ",\"cpp_afc_csv\":\"" << options.csv
                << "\",\"baseband_f64\":\"" << options.baseband
                << "\"}\n";
    }

    std::printf("IQ=%llu decimated=%llu stride=%llu\n",
                static_cast<unsigned long long>(iqSamples),
                static_cast<unsigned long long>(steps),
                static_cast<unsigned long long>(options.stride));
    return 0;
}

// Offline per-sync frame quality audit for M17 IQ recordings.
//
//   m17-frame-audit --dataset datasets/m17/dm1701 --output dm1701_audit.json
//   m17-frame-audit capture.iq --output audit.json
//
// The tool uses the same RX DSP, synchronizer and decoder as M17Plugin.
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

namespace {

struct FrameRecord {
    std::string file;
    std::uint64_t sample = 0;
    std::string syncType;
    double rssi = -120.0;
    double afcHz = 0.0;
    float syncDistance = 0.0F;
    double softbitRms = 0.0;
    int viterbiMetric = -1;
    bool viterbiOk = false;
    int golayOk = 0;
    int golayFail = 0;
    int crcStatus = -1;
    int lsfStatus = -1;
    int frameStatus = -1;
    std::string result;
    SoftbitStats softbits;
};

struct Options {
    std::string input;
    std::string dataset;
    std::string output = "dm1701_audit.json";
    std::string trace;
    bool raw = false;
};

bool parseOptions(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dataset" && i + 1 < argc) o.dataset = argv[++i];
        else if (arg == "--output" && i + 1 < argc) o.output = argv[++i];
        else if (arg == "--reference-trace" && i + 1 < argc) o.trace = argv[++i];
        else if (arg == "--raw") o.raw = true;
        else if (!arg.empty() && arg[0] != '-' && o.input.empty()) o.input = arg;
        else return false;
    }
    return !o.input.empty() || !o.dataset.empty();
}

std::string statusName(int status)
{
    if (status == 1) return "OK";
    if (status == 0) return "FAIL";
    return "N/A";
}

std::string syncTypeName(std::uint16_t type)
{
    switch (type) {
        case LSFSync: return "LSF";
        case PacketSync: return "Packet";
        case StreamSync: return "Stream";
        case EOTMarker: return "EOT";
        default: return "Unknown";
    }
}

std::string hexBytes(const std::vector<std::uint8_t>& bytes)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const std::uint8_t value : bytes)
        out << std::setw(2) << static_cast<unsigned int>(value);
    return out.str();
}

double blockRssiDb(const ComplexFloat* samples, std::size_t count)
{
    if (count == 0)
        return -120.0;
    double power = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double re = static_cast<double>(samples[i].i);
        const double im = static_cast<double>(samples[i].q);
        power += re * re + im * im;
    }
    return 10.0 * std::log10(power / static_cast<double>(count) + 1e-12);
}

void writeJsonArray(std::ostream& out, const std::vector<SoftBit>& values)
{
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << values[i];
    }
    out << ']';
}

void writeSymbolArray(std::ostream& out, const std::vector<Symbol>& values)
{
    out << std::fixed << std::setprecision(6) << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ',';
        out << values[i];
    }
    out << ']';
}

bool analyzeFile(const std::string& path, bool raw,
                 std::vector<FrameRecord>& frames, std::ostream* trace)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
        return false;
    if (raw) {
        input.seekg(0, std::ios::beg);
    } else {
        debug::IQFileHeader header;
        if (!debug::readIQHeader(input, header) || !debug::validateIQHeader(header)) {
            std::fprintf(stderr, "%s: invalid IQ header (try --raw)\n", path.c_str());
            return false;
        }
    }

    ComplexDCRemoval dcr(0.9999);
    PolyphaseDecimator decimator(
        designChannelFilter(channelBWSX1255, sampleRateSX1255, channelTapsSX1255),
        rxDecimSX1255);
    FMDemodAFC afc(basebandDCAvgCntSX1255, rxFastNSX1255, rxSlowNSX1255);
    RationalResampler resampler(24, 25);
    SampleToSymbol s2s(std::vector<double>(rrcTaps5.begin(), rrcTaps5.end()),
                       rxScalingCoeffSX1255);
    Decoder::Callbacks callbacks;
    Decoder decoder(std::move(callbacks));

    double currentRssi = -120.0;
    std::uint64_t samples = 0;
    M17Synchronizer* syncPtr = nullptr;
    M17Synchronizer synchronizer(5, [&](std::uint16_t typ,
                                        const std::vector<SoftBit>& softBits) {
        const SyncEvaluation evaluation = syncPtr->lastEvaluation();
        decoder.decodeFrame(typ, softBits);
        const FrameAudit audit = decoder.lastFrameAudit();

        FrameRecord record;
        record.file = path;
        record.sample = samples;
        record.syncType = syncTypeName(typ);
        record.rssi = currentRssi;
        record.afcHz = afc.offsetHz(25000.0);
        record.syncDistance = evaluation.distance;
        record.softbitRms = audit.softbitStats.rms;
        record.viterbiMetric = audit.viterbiMetric;
        record.viterbiOk = audit.viterbiOk;
        record.golayOk = audit.golayOk;
        record.golayFail = audit.golayFail;
        record.crcStatus = audit.crcStatus;
        record.lsfStatus = audit.lsfStatus;
        record.frameStatus = audit.frameStatus;
        record.softbits = audit.softbitStats;
        if (audit.crcStatus == 0 || audit.lsfStatus == 0 || audit.frameStatus == 0)
            record.result = typ == EOTMarker ? "EOT" : "Rejected";
        else if (typ == EOTMarker)
            record.result = "EOT";
        else
            record.result = "M17";
        frames.push_back(record);

        if (trace) {
            *trace << "{\"file\":\"" << path << "\",\"sample\":" << samples
                   << ",\"sync_type\":\"" << record.syncType
                   << "\",\"rssi_dbfs\":" << currentRssi
                   << ",\"afc_correction_hz\":" << record.afcHz
                   << ",\"sync_distance\":" << record.syncDistance
                   << ",\"symbols\":";
            writeSymbolArray(*trace, syncPtr->lastPayloadSymbols());
            *trace << ",\"softbits\":";
            writeJsonArray(*trace, audit.rawSoftbits);
            *trace << ",\"deinterleaved_softbits\":";
            writeJsonArray(*trace, audit.deinterleavedSoftbits);
            *trace << ",\"viterbi_output_hex\":\""
                   << hexBytes(audit.viterbiOutput)
                   << "\",\"lich_hex\":\"" << hexBytes(audit.lich)
                   << "\",\"viterbi_metric\":" << audit.viterbiMetric
                   << ",\"viterbi_ok\":" << (audit.viterbiOk ? "true" : "false")
                   << ",\"golay_ok\":" << audit.golayOk
                   << ",\"golay_fail\":" << audit.golayFail
                   << ",\"crc\":\"" << statusName(audit.crcStatus)
                   << "\",\"lsf\":\"" << statusName(audit.lsfStatus)
                   << "\",\"frame\":\"" << statusName(audit.frameStatus)
                   << "\"}\n";
        }
    });
    syncPtr = &synchronizer;

    std::vector<ComplexFloat> block(4096);
    for (;;) {
        const std::size_t got = debug::readIQSamples(input, block.data(), block.size());
        if (got == 0)
            break;
        currentRssi = blockRssiDb(block.data(), got);
        for (std::size_t n = 0; n < got; ++n) {
            const CD sample{static_cast<double>(block[n].i),
                            static_cast<double>(block[n].q)};
            ++samples;
            for (const CD dc : decimator.process(dcr.process(sample))) {
                const double dem = afc.process(dc);
                for (const double value : resampler.process(dem))
                    synchronizer.push(s2s.process(value));
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::fprintf(stderr,
            "usage: %s INPUT.iq|--dataset DIR [--output FILE] "
            "[--reference-trace FILE] [--raw]\n", argv[0]);
        return 2;
    }

    std::vector<std::string> files;
    if (!options.dataset.empty()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(options.dataset, ec), end;
             it != end; it.increment(ec)) {
            if (ec)
                break;
            if (it->is_regular_file() && it->path().extension() == ".iq") {
                const std::filesystem::path resolved =
                    std::filesystem::canonical(it->path(), ec);
                files.push_back(ec ? it->path().string() : resolved.string());
                ec.clear();
            }
        }
        std::sort(files.begin(), files.end());
    } else {
        files.push_back(options.input);
    }

    std::ofstream trace;
    if (!options.trace.empty()) {
        trace.open(options.trace, std::ios::trunc);
        if (!trace.is_open()) {
            std::fprintf(stderr, "cannot write %s\n", options.trace.c_str());
            return 1;
        }
    }

    std::vector<FrameRecord> frames;
    for (const std::string& file : files) {
        if (!analyzeFile(file, options.raw, frames, trace ? &trace : nullptr))
            return 1;
    }

    std::ofstream out(options.output, std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "cannot write %s\n", options.output.c_str());
        return 1;
    }
    std::size_t ok = 0;
    std::size_t fail = 0;
    for (const FrameRecord& frame : frames)
        (frame.result == "M17" ? ok : fail)++;

    out << std::fixed << std::setprecision(3);
    out << "{\n"
        << "  \"tool\": \"m17-frame-audit\",\n"
        << "  \"input\": \"" << (options.dataset.empty() ? options.input : options.dataset)
        << "\",\n"
        << "  \"files\": " << files.size() << ",\n"
        << "  \"frames\": " << frames.size() << ",\n"
        << "  \"m17_frames\": " << ok << ",\n"
        << "  \"failed_frames\": " << fail << ",\n"
        << "  \"records\": [\n";
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const FrameRecord& f = frames[i];
        out << "    {\"file\":\"" << f.file << "\",\"sample\":" << f.sample
            << ",\"sync_type\":\"" << f.syncType
            << "\",\"rssi_dbfs\":" << f.rssi
            << ",\"afc_correction_hz\":" << f.afcHz
            << ",\"sync_distance\":" << f.syncDistance
            << ",\"softbit_rms\":" << f.softbitRms
            << ",\"softbit_saturated_low\":" << f.softbits.saturated_low
            << ",\"softbit_saturated_high\":" << f.softbits.saturated_high
            << ",\"softbit_near_centre\":" << f.softbits.near_centre
            << ",\"viterbi_metric\":" << f.viterbiMetric
            << ",\"viterbi_ok\":" << (f.viterbiOk ? "true" : "false")
            << ",\"golay_ok\":" << f.golayOk
            << ",\"golay_fail\":" << f.golayFail
            << ",\"crc\":\"" << statusName(f.crcStatus)
            << "\",\"lsf\":\"" << statusName(f.lsfStatus)
            << "\",\"frame\":\"" << statusName(f.frameStatus)
            << "\",\"result\":\"" << f.result << "\"}";
        out << (i + 1 < frames.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    std::printf("%zu files, %zu frames, %zu M17, %zu failed\n",
                files.size(), frames.size(), ok, fail);
    return 0;
}

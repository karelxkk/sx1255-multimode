// Offline M17 diagnostics for one IQ recording.  It runs exactly the same RX
// DSP/synchronizer/decoder chain as M17Plugin and exports sync candidates,
// AFC correction, RSSI and symbol level as JSON.
#include "sx1255/debug/IQFileFormat.hpp"
#include "sx1255/m17/M17DSP.hpp"
#include "sx1255/m17/M17FrameDecoder.hpp"
#include "sx1255/m17/M17Synchronizer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

using namespace sx1255;
using namespace sx1255::m17;

namespace {

struct Candidate {
    std::uint64_t sample = 0;
    double rssi = -120.0;
    double afcHz = 0.0;
    float distance = 0.0F;
    std::string type;
    std::string result;
};

struct Options {
    std::string input;
    std::string output;
    bool raw = false;
    double rssiGate = -55.0;
    double nearMin = 4.5;
    double nearMax = 7.0;
    double intervalMs = 100.0;
    std::size_t maxCandidates = 5000;
};

bool parseOptions(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) o.output = argv[++i];
        else if (arg == "--raw") o.raw = true;
        else if (arg == "--rssi-gate" && i + 1 < argc) o.rssiGate = std::atof(argv[++i]);
        else if (arg == "--near-min" && i + 1 < argc) o.nearMin = std::atof(argv[++i]);
        else if (arg == "--near-max" && i + 1 < argc) o.nearMax = std::atof(argv[++i]);
        else if (arg == "--interval-ms" && i + 1 < argc) o.intervalMs = std::atof(argv[++i]);
        else if (arg == "--max-candidates" && i + 1 < argc)
            o.maxCandidates = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        else if (!arg.empty() && arg[0] != '-' && o.input.empty()) o.input = arg;
        else return false;
    }
    return !o.input.empty() && !o.output.empty();
}

std::string syncTypeName(std::uint16_t type)
{
    switch (type) {
        case LSFSync:    return "LSF";
        case PacketSync: return "Packet";
        case StreamSync: return "Stream";
        case EOTMarker:  return "EOT";
        default:         return "None";
    }
}

double blockRssiDb(const ComplexFloat* samples, std::size_t count)
{
    if (count == 0)
        return -120.0;
    double power = 0.0;
    for (std::size_t n = 0; n < count; ++n) {
        const double i = static_cast<double>(samples[n].i);
        const double q = static_cast<double>(samples[n].q);
        power += i * i + q * q;
    }
    return 10.0 * std::log10(power / static_cast<double>(count) + 1e-12);
}

}  // namespace

int main(int argc, char** argv)
{
    Options o;
    if (!parseOptions(argc, argv, o)) {
        std::fprintf(stderr,
            "usage: %s input.iq --output FILE [--raw] [--rssi-gate DBFS] "
            "[--near-min D] [--near-max D] [--interval-ms MS]\n", argv[0]);
        return 2;
    }

    std::ifstream input(o.input, std::ios::binary);
    if (!input.is_open()) {
        std::fprintf(stderr, "cannot open %s\n", o.input.c_str());
        return 1;
    }
    if (!o.raw) {
        debug::IQFileHeader header;
        if (!debug::readIQHeader(input, header) || !debug::validateIQHeader(header)) {
            std::fprintf(stderr, "%s is not a valid IQ recording (try --raw)\n",
                         o.input.c_str());
            return 1;
        }
    } else {
        input.seekg(0, std::ios::beg);
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
    callbacks.onLSF = [](const LSF&, double) {};
    callbacks.onLSFBadCRC = [](const LSF&, double) {};
    callbacks.onPacket = [](const LSF&, const std::vector<std::uint8_t>&, double) {};
    callbacks.onPacketBadCRC = [](const LSF&, const std::vector<std::uint8_t>&, double) {};
    callbacks.onStream = [](const LSF&, const std::vector<std::uint8_t>&,
                            std::uint16_t, std::uint16_t, double) {};
    callbacks.onStreamLICH = [](const LSF&, double) {};
    callbacks.onStreamEOT = [](const LSF&, std::uint16_t, std::uint16_t, double) {};
    callbacks.onFrameDropped = [](std::uint16_t, const char*) {};
    callbacks.onEOT = [] {};
    callbacks.onPacketReattached = [](const LSF&) {};
    callbacks.onPacketFrame = [](std::uint8_t, bool, int) {};
    Decoder decoder(std::move(callbacks));

    std::vector<Candidate> candidates;
    std::uint64_t samples = 0;
    std::uint64_t acceptedHits = 0;
    std::uint64_t rejectedHits = 0;
    std::uint64_t nearMisses = 0;
    std::uint64_t validLsf = 0;
    std::uint64_t validFrames = 0;
    double currentRssi = -120.0;
    double rssiMax = -120.0;
    double rssiSum = 0.0;
    std::uint64_t rssiBlocks = 0;
    double afcMin = 1e30;
    double afcMax = -1e30;
    double afcSum = 0.0;
    std::uint64_t afcCount = 0;
    float symbolLevel = 0.0F;
    double lastRejectedSecond = -1e30;
    bool lsfDecoded = false;
    bool lsfRejected = false;
    float bestAcceptedDistance = 1e30F;
    float bestRejectedDistance = 1e30F;
    M17Synchronizer* syncPtr = nullptr;

    auto addCandidate = [&](const SyncEvaluation& evaluation, const char* result,
                            bool rejected) {
        if (candidates.size() >= o.maxCandidates)
            return;
        Candidate c;
        c.sample = samples;
        c.rssi = currentRssi;
        c.afcHz = afc.offsetHz(25000.0);
        c.distance = evaluation.distance;
        c.type = syncTypeName(evaluation.type);
        c.result = result;
        candidates.push_back(std::move(c));
        if (!rejected && evaluation.distance < bestAcceptedDistance)
            bestAcceptedDistance = evaluation.distance;
        if (rejected && evaluation.distance < bestRejectedDistance)
            bestRejectedDistance = evaluation.distance;
    };

    M17Synchronizer synchronizer(5, [&](std::uint16_t typ,
                                        const std::vector<SoftBit>& softBits) {
        const SyncEvaluation evaluation = syncPtr->lastEvaluation();
        const std::uint16_t syncedBefore = decoder.syncedType();
        lsfDecoded = false;
        lsfRejected = false;
        decoder.decodeFrame(typ, softBits);
        const bool useful = lsfDecoded || syncedBefore != 0 || decoder.syncedType() != 0;
        if (useful) {
            ++acceptedHits;
            ++validFrames;
            if (typ == LSFSync)
                ++validLsf;
            addCandidate(evaluation, "M17", false);
        } else if (typ != EOTMarker) {
            ++rejectedHits;
            addCandidate(evaluation, "Rejected", true);
        }
        if (lsfRejected) {
            syncPtr->retract();
            lsfRejected = false;
        }
        syncPtr->setExpectedType(decoder.syncedType() == PacketSync ? PacketSync : 0);
    });
    syncPtr = &synchronizer;
    synchronizer.setEvaluationSink(static_cast<float>(o.nearMax),
        [&](const SyncEvaluation& evaluation) {
            if (evaluation.accepted)
                return;
            const double seconds = static_cast<double>(samples) / sampleRateSX1255;
            if (evaluation.distance < o.nearMin || evaluation.distance > o.nearMax ||
                currentRssi < o.rssiGate ||
                seconds - lastRejectedSecond < o.intervalMs / 1000.0) {
                return;
            }
            lastRejectedSecond = seconds;
            ++nearMisses;
            addCandidate(evaluation, "Rejected", true);
        });

    std::vector<ComplexFloat> block(4096);
    double ncoPhase = 0.0;
    const double ncoStep =
        -2.0 * std::numbers::pi * 25000.0 / static_cast<double>(sampleRateSX1255);
    for (;;) {
        const std::size_t got = debug::readIQSamples(input, block.data(), block.size());
        if (got == 0)
            break;
        currentRssi = blockRssiDb(block.data(), got);
        rssiMax = std::max(rssiMax, currentRssi);
        rssiSum += currentRssi;
        ++rssiBlocks;
        for (std::size_t n = 0; n < got; ++n) {
            double i = static_cast<double>(block[n].i);
            double q = static_cast<double>(block[n].q);
            if (!o.raw) {
                const double c = std::cos(ncoPhase);
                const double s = std::sin(ncoPhase);
                const double ri = i * c - q * s;
                const double rq = i * s + q * c;
                i = ri;
                q = rq;
                ncoPhase += ncoStep;
            }
            const CD sample{i, q};
            ++samples;
            for (const CD dc : decimator.process(dcr.process(sample))) {
                const double dem = afc.process(dc);
                const double hz = afc.offsetHz(25000.0);
                afcMin = std::min(afcMin, hz);
                afcMax = std::max(afcMax, hz);
                afcSum += hz;
                ++afcCount;
                for (const double r : resampler.process(dem)) {
                    const float symbol = s2s.process(r);
                    symbolLevel += (std::fabs(symbol) - symbolLevel) / 512.0F;
                    synchronizer.push(symbol);
                }
            }
        }
    }

    std::ofstream out(o.output, std::ios::trunc);
    if (!out.is_open()) {
        std::fprintf(stderr, "cannot write %s\n", o.output.c_str());
        return 1;
    }
    out << std::fixed << std::setprecision(6);
    out << "{\n"
        << "  \"file\": \"" << o.input << "\",\n"
        << "  \"raw\": " << (o.raw ? "true" : "false") << ",\n"
        << "  \"sample_rate_hz\": " << sampleRateSX1255 << ",\n"
        << "  \"iq_samples\": " << samples << ",\n"
        << "  \"duration_seconds\": " << static_cast<double>(samples) / sampleRateSX1255 << ",\n"
        << "  \"rssi_gate_dbfs\": " << o.rssiGate << ",\n"
        << "  \"rssi_max_dbfs\": " << rssiMax << ",\n"
        << "  \"rssi_mean_dbfs\": " << (rssiBlocks ? rssiSum / rssiBlocks : -120.0) << ",\n"
        << "  \"afc_correction_min_hz\": " << (afcCount ? afcMin : 0.0) << ",\n"
        << "  \"afc_correction_max_hz\": " << (afcCount ? afcMax : 0.0) << ",\n"
        << "  \"afc_correction_mean_hz\": " << (afcCount ? afcSum / afcCount : 0.0) << ",\n"
        << "  \"average_symbol_level\": " << symbolLevel << ",\n"
        << "  \"detected\": " << (validLsf != 0 ? "true" : "false") << ",\n"
        << "  \"valid_lsf\": " << validLsf << ",\n"
        << "  \"valid_frames\": " << validFrames << ",\n"
        << "  \"accepted_sync_bursts\": " << acceptedHits << ",\n"
        << "  \"rejected_sync_bursts\": " << rejectedHits << ",\n"
        << "  \"near_miss_windows\": " << nearMisses << ",\n"
        << "  \"best_accepted_sync_distance\": "
        << (bestAcceptedDistance < 1e29F ? bestAcceptedDistance : 0.0F) << ",\n"
        << "  \"best_rejected_sync_distance\": "
        << (bestRejectedDistance < 1e29F ? bestRejectedDistance : 0.0F) << ",\n"
        << "  \"candidates\": [\n";
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates[i];
        out << "    {\"sample\":" << c.sample
            << ",\"rssi_dbfs\":" << c.rssi
            << ",\"afc_correction_hz\":" << c.afcHz
            << ",\"sync_distance\":" << c.distance
            << ",\"sync_type\":\"" << c.type
            << "\",\"result\":\"" << c.result << "\"}";
        out << (i + 1 < candidates.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return 0;
}

#include "sx1255/dataset/DatasetRunner.hpp"

#include "sx1255/config/Config.hpp"
#include "sx1255/core/IQDispatcher.hpp"
#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/debug/IQReplay.hpp"
#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/frontend/RadioFrontend.hpp"
#include "sx1255/plugins/PluginFactory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

namespace sx1255::dataset {

namespace fs = std::filesystem;

namespace {

std::string expectedModeFor(const std::string& path)
{
    const std::string lower = path;
    if (lower.find("/fm/") != std::string::npos || lower.find("fm") != std::string::npos)
        return "fm";
    if (lower.find("/dstar/") != std::string::npos || lower.find("dstar") != std::string::npos)
        return "dstar";
    if (lower.find("/m17/") != std::string::npos || lower.find("m17") != std::string::npos)
        return "m17";
    if (lower.find("/unknown/") != std::string::npos || lower.find("unknown") != std::string::npos)
        return "unknown";
    return "unknown";
}

std::string modeName(SessionManager::State state)
{
    switch (state) {
        case SessionManager::State::ActiveFm:    return "FM";
        case SessionManager::State::ActiveDStar: return "DSTAR";
        case SessionManager::State::ActiveM17:   return "M17";
        default:                                 return "NONE";
    }
}

SessionManager::State runOne(const std::string& path)
{
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
    sessions.setStatistics(&stats);
    IQDispatcher dispatcher(registry, sessions, nullptr);

    auto device = std::make_unique<debug::IQReplay>(path);
    RadioFrontend frontend(std::move(device), dispatcher);
    dispatcher.setTransmitter(&frontend);

    RadioFrontend::Settings settings;
    settings.blockSize = 2048;
    if (!frontend.start(settings))
        return SessionManager::State::Idle;

    for (int i = 0; i < 20000 && frontend.running(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto state = sessions.state();
    sessions.reset();  // release the owner before the frontend is destroyed
    frontend.stop();
    return state;
}

}  // namespace

std::vector<DatasetResult> replayDatasets(const std::string& rootDir)
{
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

    std::vector<DatasetResult> results;
    results.reserve(files.size());
    for (const auto& file : files) {
        DatasetResult result;
        result.file = file;
        result.expected = expectedModeFor(file);
        const auto state = runOne(file);
        result.detected = modeName(state);

        if (result.expected == "unknown")
            result.pass = (state == SessionManager::State::Idle ||
                           state == SessionManager::State::Detecting);
        else if (result.expected == "fm")
            result.pass = (state == SessionManager::State::ActiveFm);
        else if (result.expected == "dstar")
            result.pass = (state == SessionManager::State::ActiveDStar);
        else if (result.expected == "m17")
            result.pass = (state == SessionManager::State::ActiveM17);

        results.push_back(std::move(result));
    }
    return results;
}

bool writeRegressionReport(const std::vector<DatasetResult>& results,
                           const std::string& outPath)
{
    std::ofstream out(outPath, std::ios::trunc);
    if (!out.is_open())
        return false;

    std::size_t passed = 0;
    std::size_t failed = 0;
    for (const auto& r : results)
        (r.pass ? passed : failed)++;

    out << "{\n"
        << "  \"passed\": " << passed << ",\n"
        << "  \"failed\": " << failed << ",\n"
        << "  \"total\": " << results.size() << ",\n"
        << "  \"results\": [\n";

    for (std::size_t i = 0; i < results.size(); ++i) {
        out << "    {\"file\":\"" << results[i].file << "\","
            << "\"expected\":\"" << results[i].expected << "\","
            << "\"detected\":\"" << results[i].detected << "\","
            << "\"pass\":" << (results[i].pass ? "true" : "false") << "}";
        out << (i + 1 < results.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return static_cast<bool>(out);
}

int runDatasetBatch(const std::string& rootDir, const std::string& outPath)
{
    const auto results = replayDatasets(rootDir);
    if (!writeRegressionReport(results, outPath)) {
        std::fprintf(stderr, "cannot write regression report to %s\n", outPath.c_str());
        return 1;
    }

    int failures = 0;
    for (const auto& r : results) {
        if (!r.pass)
            ++failures;
        std::fprintf(stderr, "[%s] %s -> %s (%s)\n",
                     r.pass ? "PASS" : "FAIL", r.file.c_str(),
                     r.detected.c_str(), r.expected.c_str());
    }
    return failures;
}

}  // namespace sx1255::dataset

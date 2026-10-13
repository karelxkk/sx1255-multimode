#include "sx1255/config/Config.hpp"
#include "sx1255/core/IQDispatcher.hpp"
#include "sx1255/core/PluginRegistry.hpp"
#include "sx1255/core/SessionManager.hpp"
#include "sx1255/dataset/DatasetRunner.hpp"
#include "sx1255/debug/AutomaticCapture.hpp"
#include "sx1255/debug/IQRecorder.hpp"
#include "sx1255/debug/IQReplay.hpp"
#include "sx1255/diag/HttpStatusServer.hpp"
#include "sx1255/diag/SystemStatistics.hpp"
#include "sx1255/frontend/NullSX1255.hpp"
#include "sx1255/frontend/RadioFrontend.hpp"
#include "sx1255/frontend/SX1255GPIO.hpp"
#include "sx1255/frontend/SX1255Hardware.hpp"
#include "sx1255/frontend/SX1255SPI.hpp"
#include "sx1255/monitor/HealthMonitor.hpp"
#include "sx1255/plugins/PluginFactory.hpp"
#include "sx1255/spectrum/SpectrumAnalyzer.hpp"
#include "sx1255/spectrum/WaterfallGenerator.hpp"
#include "sx1255/util/Logger.hpp"
#include "sx1255/util/SystemInfo.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <memory>
#include <functional>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_shutdown{false};
sx1255::debug::AutomaticCapture* g_autoCapture = nullptr;

void handleSignal(int)
{
    g_shutdown = true;
}

void usage(const char* program)
{
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "  -config FILE        YAML configuration file (default: sx1255-multimode.yaml)\n"
        "  --log-level LEVEL   debug | info | warn | error\n"
        "  --capture FILE      record live SX1255 IQ to FILE\n"
        "  --replay FILE       replay IQ from FILE (no radio)\n"
        "  --dataset DIR       replay all datasets under DIR, write regression_report.json\n",
        program);
}

}  // namespace

int main(int argc, char** argv)
{
    std::string configPath = "sx1255-multimode.yaml";
    std::string captureFile;
    std::string replayFile;
    std::string datasetDir;
    sx1255::util::LogLevel logLevel = sx1255::util::LogLevel::Info;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        if ((arg == "-config" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
            continue;
        }
        if (arg == "--log-level" && i + 1 < argc) {
            if (!sx1255::util::parseLogLevel(argv[++i], logLevel)) {
                std::fprintf(stderr, "invalid --log-level value\n");
                return 2;
            }
            continue;
        }
        if (arg == "--capture" && i + 1 < argc) {
            captureFile = argv[++i];
            continue;
        }
        if (arg == "--replay" && i + 1 < argc) {
            replayFile = argv[++i];
            continue;
        }
        if (arg == "--dataset" && i + 1 < argc) {
            datasetDir = argv[++i];
            continue;
        }
        std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
        usage(argv[0]);
        return 2;
    }

    try {
        sx1255::util::Logger::instance().configure(logLevel, "sx1255-multimode");

        if (!datasetDir.empty()) {
            const int failures = sx1255::dataset::runDatasetBatch(
                datasetDir, "regression_report.json");
            return failures == 0 ? 0 : 1;
        }

        sx1255::Config config = sx1255::loadConfig(configPath);
        if (!captureFile.empty()) {
            config.recording.enabled = true;
            config.recording.file = captureFile;
        }
        if (!replayFile.empty()) {
            config.frontend.type = "replay";
            config.replay.file = replayFile;
        }

        sx1255::SystemStatistics stats;
        stats.load("stats.json");

        sx1255::PluginRegistry registry;
        if (config.fm.enabled)
            registry.registerPlugin(sx1255::createFmPlugin(config.fm, &stats));
        if (config.dstar.enabled)
            registry.registerPlugin(sx1255::createDStarPlugin(config.dstar, &stats));
        if (config.m17.enabled)
            registry.registerPlugin(sx1255::createM17Plugin(config.m17, &stats));

        sx1255::SessionManager sessions;
        sessions.setStatistics(&stats);
        sx1255::IQDispatcher dispatcher(registry, sessions, nullptr);

        sx1255::spectrum::SpectrumAnalyzer spectrum(
            static_cast<std::size_t>(config.spectrum.fftSize), 125000.0,
            config.spectrum.updateHz);
        sx1255::spectrum::WaterfallGenerator waterfall(
            static_cast<std::size_t>(config.spectrum.waterfallBins),
            static_cast<std::size_t>(config.spectrum.waterfallRows));
        sx1255::debug::AutomaticCapture autoCapture(
            125000, config.automaticCapture.preSeconds,
            config.automaticCapture.postSeconds,
            config.automaticCapture.directory);
        g_autoCapture = &autoCapture;

        std::atomic<std::uint64_t> spectrumFrames{0};
        std::atomic<std::uint64_t> dispatcherBlocks{0};
        spectrum.setFrameHandler([&waterfall, &spectrumFrames](const std::vector<float>& spectrumDb) {
            waterfall.addRow(spectrumDb);
            spectrumFrames.fetch_add(1, std::memory_order_relaxed);
        });

        // Route diagnostic tap + auto-capture from the single dispatch point.
        dispatcher.setIqSink([&spectrum, &autoCapture, &config, &dispatcherBlocks](const sx1255::ComplexFloat* samples, std::size_t count) {
            spectrum.push(samples, count);
            dispatcherBlocks.fetch_add(1, std::memory_order_relaxed);
            if (config.automaticCapture.enabled)
                autoCapture.push(samples, count);
        });

        std::unique_ptr<sx1255::ISX1255> device;
        const bool replayMode = config.frontend.type == "replay" || config.replay.enabled;

        if (replayMode) {
            device = std::make_unique<sx1255::debug::IQReplay>(config.replay.file);
        } else if (config.frontend.type == "sx1255") {
            sx1255::SX1255HardwareConfig hw;
            hw.spiDevice    = config.frontend.spiDevice;
            hw.spiSpeed     = config.frontend.spiSpeed;
            hw.gpioChip     = config.frontend.gpioChip;
            hw.gpioReset    = config.frontend.gpioReset;
            hw.gpioTxEnable = config.frontend.gpioTxEnable;
            hw.gpioRxEnable = config.frontend.gpioRxEnable;
            hw.gpioPtt      = config.frontend.gpioPtt;
            hw.alsaCapture  = config.frontend.alsaCapture;
            hw.alsaPlayback = config.frontend.alsaPlayback;
            hw.lnaGain      = config.frontend.lnaGain;
            hw.pgaGain      = config.frontend.pgaGain;
            hw.dacGain      = config.frontend.dacGain;
            hw.mixerGain    = config.frontend.mixerGain;
            hw.blockSize    = config.frontend.blockSize;

            auto hardware = std::make_unique<sx1255::SX1255Hardware>(
                hw,
                std::make_unique<sx1255::SX1255SpiDev>(config.frontend.spiDevice, config.frontend.spiSpeed),
                std::make_unique<sx1255::SX1255Gpiod>(config.frontend.gpioChip,
                                                      config.frontend.gpioReset,
                                                      config.frontend.gpioTxEnable,
                                                      config.frontend.gpioRxEnable,
                                                      config.frontend.gpioPtt));
            if (config.recording.enabled)
                device = std::make_unique<sx1255::debug::IQRecorder>(
                    std::move(hardware), config.recording.file);
            else
                device = std::move(hardware);
        } else {
            device = std::make_unique<sx1255::NullSX1255>();
        }
        sx1255::RadioFrontend frontend(std::move(device), dispatcher);
        dispatcher.setTransmitter(&frontend);
        for (const auto& plugin : registry.plugins())
            if (plugin)
                plugin->setTransmitter(&frontend);
        for (const auto& plugin : registry.plugins())
            if (plugin)
                plugin->setTxArbiter(&sessions);

        if (config.automaticCapture.enabled)
            autoCapture.start();

        sessions.setFalsePositiveHandler([&autoCapture, &config] {
            if (config.automaticCapture.enabled && config.automaticCapture.triggerFalsePositive)
                autoCapture.trigger(sx1255::debug::CaptureTrigger::FalsePositive);
        });

        sx1255::HttpStatusServer http(8080, stats, sessions, config,
                                      [&frontend] { return frontend.rssi(); });
        http.setIqStatusProvider([&frontend] {
            sx1255::IQStatusSnapshot snapshot;
            if (auto* recorder = dynamic_cast<sx1255::debug::IQRecorder*>(&frontend.device())) {
                snapshot.recording = recorder->isRecording();
                snapshot.capturedSamples = recorder->capturedSamples();
                snapshot.file = recorder->file();
            } else if (auto* replay = dynamic_cast<sx1255::debug::IQReplay*>(&frontend.device())) {
                snapshot.replay = true;
                snapshot.capturedSamples = replay->totalSamples();
                snapshot.file = replay->file();
            }
            return snapshot;
        });
        http.setSpectrumProvider([&spectrum] { return spectrum.toJson(); });
        http.setWaterfallProvider([&waterfall](std::uint64_t sinceSeq) {
            return waterfall.toJson(sinceSeq);
        });
        http.setPluginRegistry(&registry);
        http.setSystemProvider([&frontend] {
            return sx1255::util::systemInfoJson(frontend.running() ? "running" : "stopped");
        });
        http.start();

        sx1255::RadioFrontend::Settings frontendSettings;
        frontendSettings.rxFrequency = config.frontend.rxFrequency;
        frontendSettings.txFrequency = config.frontend.txFrequency;
        frontendSettings.power       = config.frontend.power;
        frontendSettings.squelch     = config.frontend.squelch;
        frontendSettings.blockSize   = config.frontend.blockSize;

        if (!frontend.start(frontendSettings)) {
            std::fprintf(stderr, "failed to start the radio frontend\n");
            return 1;
        }

        if (config.spectrum.enabled)
            spectrum.start();

        sx1255::monitor::HealthMonitor healthMonitor;
        healthMonitor.setProblemHandler([&autoCapture, &config](const std::string& name) {
            sx1255::util::Logger::instance().warn("health problem in '%s', dumping IQ", name.c_str());
            if (config.automaticCapture.enabled)
                autoCapture.trigger(sx1255::debug::CaptureTrigger::Health);
        });

        healthMonitor.registerComponent(
            "frontend",
            [&frontend] { return frontend.running(); },
            [&frontend, &frontendSettings] {
                frontend.stop();
                frontend.start(frontendSettings);
            });

        std::uint64_t lastDispatched = 0;
        healthMonitor.registerComponent(
            "dispatcher",
            [&dispatcherBlocks, &lastDispatched]() mutable {
                const std::uint64_t now = dispatcherBlocks.load(std::memory_order_relaxed);
                const bool progressed = now != lastDispatched;
                lastDispatched = now;
                return progressed;
            },
            [] {});

        healthMonitor.registerComponent(
            "session_manager",
            [&sessions] { return sessions.state() != sx1255::SessionManager::State::Timeout; },
            [] {});

        healthMonitor.registerComponent(
            "http",
            [&http] { return http.running(); },
            [&http] { http.stop(); http.start(); });

        std::uint64_t lastSpectrumFrames = 0;
        healthMonitor.registerComponent(
            "spectrum",
            [&spectrumFrames, &lastSpectrumFrames]() mutable {
                const std::uint64_t now = spectrumFrames.load(std::memory_order_relaxed);
                const bool progressed = now != lastSpectrumFrames;
                lastSpectrumFrames = now;
                return progressed;
            },
            [&spectrum] { spectrum.stop(); spectrum.start(); });

        http.setHealthProvider([&healthMonitor] { return healthMonitor.healthJson(); });
        healthMonitor.start();

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        std::fprintf(stderr, "sx1255-multimode running (%zu plugins)\n",
                     registry.count());

        // Lightweight watchdog for the automatic capture triggers that are not
        // produced by a single synchronous code path (unknown signal, false
        // positive deltas).
        std::atomic<bool> watchdogStop{false};
        std::thread watchdog([&] {
            std::chrono::steady_clock::time_point unknownSince{};

            while (!g_shutdown.load() && !watchdogStop.load()) {
                if (config.automaticCapture.enabled) {
                    const double rssi = frontend.rssi();
                    const bool detecting =
                        sessions.state() == sx1255::SessionManager::State::Detecting;
                    if (detecting && rssi > -90.0) {
                        const auto now = std::chrono::steady_clock::now();
                        if (unknownSince == std::chrono::steady_clock::time_point{})
                            unknownSince = now;
                        else if (now - unknownSince >= std::chrono::seconds(2) &&
                                 config.automaticCapture.triggerUnknownSignal) {
                            autoCapture.trigger(sx1255::debug::CaptureTrigger::UnknownSignal);
                            unknownSince = now;
                        }
                    } else {
                        unknownSince = {};
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        while (!g_shutdown.load() && !(replayMode && !frontend.running()))
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        watchdogStop = true;
        if (watchdog.joinable())
            watchdog.join();

        // Release any active session (and thus PTT) before the frontend
        // object is torn down; plugins hold an ITransmit* into it.
        sessions.reset();
        healthMonitor.stop();
        frontend.stop();
        spectrum.stop();
        if (config.automaticCapture.enabled)
            autoCapture.stop();
        http.stop();
        stats.save("stats.json");
        std::fprintf(stderr, "sx1255-multimode stopped\n");
        return 0;
    } catch (const std::exception& e) {
        if (g_autoCapture) {
            g_autoCapture->trigger(sx1255::debug::CaptureTrigger::Crash);
            g_autoCapture->stop();
        }
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

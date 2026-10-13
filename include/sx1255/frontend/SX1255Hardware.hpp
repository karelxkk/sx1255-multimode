#pragma once

#include "sx1255/frontend/ISX1255.hpp"
#include "sx1255/frontend/SPSCQueue.hpp"
#include "sx1255/frontend/SX1255GPIO.hpp"
#include "sx1255/frontend/SX1255SPI.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct _snd_pcm;  // ALSA's snd_pcm_t

namespace sx1255 {

struct SX1255HardwareConfig {
    std::string spiDevice = "/dev/spidev0.0";
    std::uint32_t spiSpeed = 500000;

    std::string gpioChip = "/dev/gpiochip0";
    int gpioReset = -1;
    int gpioTxEnable = -1;
    int gpioRxEnable = -1;
    int gpioPtt = -1;

    std::string alsaCapture;
    std::string alsaPlayback;

    int lnaGain = 24;
    int pgaGain = 0;
    int dacGain = -3;
    double mixerGain = -12.0;

    std::size_t blockSize = 2048;
    std::size_t rxQueueSize = 1 << 16;
    std::size_t txQueueSize = 1 << 16;
};

struct FrontendStats {
    std::uint64_t rxSamples = 0;
    std::uint64_t txSamples = 0;
    double rssiDbm = -120.0;
    dBm txPowerDbm = 10;
    std::uint64_t rxOverruns = 0;
    std::uint64_t txUnderruns = 0;
    std::uint64_t spiErrors = 0;
    std::uint64_t alsaErrors = 0;
};

// Convert a frequency in Hz to the 24-bit PLL register value
// (val = round(freq * 2^20 / 32 MHz), from m17-gateway).
std::uint32_t sx1255FreqToReg(std::uint32_t hz);

// Real SX1255 frontend.  Implements ISX1255 using spidev, libgpiod and ALSA.
// Contains no mode logic: it only moves IQ samples and drives RF control.
class SX1255Hardware final : public ISX1255 {
public:
    SX1255Hardware(SX1255HardwareConfig config,
                   std::unique_ptr<ISX1255SPI> spi,
                   std::unique_ptr<ISX1255GPIO> gpio);
    ~SX1255Hardware() override;

    SX1255Hardware(const SX1255Hardware&) = delete;
    SX1255Hardware& operator=(const SX1255Hardware&) = delete;

    std::string name() const override { return "sx1255"; }

    bool open() override;
    void close() override;

    bool setRxFrequency(Hertz hz) override;
    bool setTxFrequency(Hertz hz) override;
    bool setPower(dBm power) override;
    bool setPtt(bool on) override;
    double readRssi() override;
    bool setSquelch(dBm level) override;

    bool readIQ(std::vector<ComplexFloat>& out, std::size_t maxSamples) override;
    bool writeIQ(const ComplexFloat* samples, std::size_t count) override;
    bool drainTx() override;

    FrontendStats stats() const;

private:
    bool writeReg(std::uint8_t addr, std::uint8_t value);
    bool readReg(std::uint8_t addr, std::uint8_t& value);
    bool updateReg(std::uint8_t addr, std::uint8_t mask, std::uint8_t value);
    bool setMode(std::uint8_t set, std::uint8_t clear);
    bool enableRx(bool on);
    bool enableTx(bool on);
    bool setRate();
    bool setGains();
    bool initChip();

    bool openAlsa();
    void closeAlsa();
    bool reopenTxPlayback();
    void captureThread();

    SX1255HardwareConfig m_config;
    std::unique_ptr<ISX1255SPI> m_spi;
    std::unique_ptr<ISX1255GPIO> m_gpio;

    struct _snd_pcm* m_capture = nullptr;
    struct _snd_pcm* m_playback = nullptr;
    std::mutex m_alsaMutex;

    SPSCQueue<ComplexFloat> m_rxQueue;

    std::thread m_captureThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ptt{false};
    std::atomic<double> m_rssi{-120.0};
    std::atomic<dBm> m_power{10};
    std::atomic<dBm> m_squelch{-120};

    std::atomic<std::uint64_t> m_rxSamples{0};
    std::atomic<std::uint64_t> m_txSamples{0};
    std::atomic<std::uint64_t> m_rxOverruns{0};
    std::atomic<std::uint64_t> m_txUnderruns{0};
    std::atomic<std::uint64_t> m_spiErrors{0};
    std::atomic<std::uint64_t> m_alsaErrors{0};
};

}  // namespace sx1255

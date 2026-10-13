#include "sx1255/frontend/SX1255Hardware.hpp"

#include "mock_hw.hpp"

#include <cstdint>
#include <memory>

using namespace sx1255;

int main()
{
    const std::uint32_t reg = sx1255FreqToReg(430975020);
    if (reg == 0)
        return 1;

    auto* spiRaw = new test::MockSPI();
    auto* gpioRaw = new test::MockGPIO();
    spiRaw->regs[0x07] = 0x11;

    SX1255HardwareConfig cfg;
    cfg.gpioReset = 25;
    cfg.gpioTxEnable = 27;
    cfg.gpioRxEnable = 22;
    cfg.alsaCapture = "/definitely/not/a/device";
    cfg.alsaPlayback = "/definitely/not/a/device";

    SX1255Hardware hw(cfg,
                      std::unique_ptr<ISX1255SPI>(spiRaw),
                      std::unique_ptr<ISX1255GPIO>(gpioRaw));

    if (hw.open())
        return 2;

    if (spiRaw->regs[0x0D] != 0x37) return 3;
    if (spiRaw->regs[0x0B] != 5)    return 4;
    if (spiRaw->regs[0x13] != 0x2C) return 5;
    if (spiRaw->regs[0x12] != 0x22) return 6;

    if ((spiRaw->regs[0x00] & 0x01) == 0) return 7;
    if ((spiRaw->regs[0x00] & 0x02) == 0) return 8;
    if ((spiRaw->regs[0x00] & 0x0C) != 0) return 9;

    if (gpioRaw->resetCalls.size() != 2) return 10;
    if (!gpioRaw->resetCalls[0] || gpioRaw->resetCalls[1]) return 11;

    hw.setRxFrequency(430975020);
    if (spiRaw->regs[0x01] != static_cast<std::uint8_t>(reg >> 16)) return 12;
    if (spiRaw->regs[0x02] != static_cast<std::uint8_t>(reg >> 8))  return 13;
    if (spiRaw->regs[0x03] != static_cast<std::uint8_t>(reg))       return 14;

    // setPower maps dBm to the TX DAC gain field (0x08 bits [6:4]).
    hw.setPower(10);  // -> code 3, 0 dB attenuation
    if (((spiRaw->regs[0x08] >> 4) & 0x7) != 3) return 15;
    hw.setPower(0);   // -> code 0, -9 dB attenuation
    if (((spiRaw->regs[0x08] >> 4) & 0x7) != 0) return 16;

    hw.close();
    return 0;
}

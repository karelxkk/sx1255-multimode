#include "sx1255/frontend/SX1255Hardware.hpp"

#include "mock_hw.hpp"

#include <cstdint>
#include <memory>

using namespace sx1255;

int main()
{
    auto* spiRaw = new test::MockSPI();
    auto* gpioRaw = new test::MockGPIO();
    spiRaw->regs[0x07] = 0x11;

    SX1255HardwareConfig cfg;
    cfg.gpioReset = 25;
    cfg.gpioTxEnable = 27;
    cfg.gpioRxEnable = 22;
    cfg.gpioPtt = 18;
    cfg.alsaCapture = "/definitely/not/a/device";
    cfg.alsaPlayback = "/definitely/not/a/device";

    SX1255Hardware hw(cfg,
                      std::unique_ptr<ISX1255SPI>(spiRaw),
                      std::unique_ptr<ISX1255GPIO>(gpioRaw));

    if (hw.open())
        return 1;

    if (gpioRaw->rxCalls.empty() || !gpioRaw->rxCalls.back())
        return 2;

    if (!hw.setPtt(true))
        return 3;
    if (gpioRaw->pttCalls.empty() || !gpioRaw->pttCalls.back())
        return 4;
    if (gpioRaw->txCalls.empty() || !gpioRaw->txCalls.back())
        return 5;
    if ((spiRaw->regs[0x00] & 0x0C) != 0x0C)
        return 6;

    if (!hw.setPtt(false))
        return 7;
    if (gpioRaw->pttCalls.empty() || gpioRaw->pttCalls.back())
        return 8;
    if (gpioRaw->txCalls.empty() || gpioRaw->txCalls.back())
        return 9;
    if ((spiRaw->regs[0x00] & 0x0C) != 0)
        return 10;

    hw.close();
    return 0;
}

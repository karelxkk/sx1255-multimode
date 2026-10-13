#pragma once

#include "sx1255/frontend/SX1255GPIO.hpp"
#include "sx1255/frontend/SX1255SPI.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace sx1255::test {

// Records register traffic; no hardware is touched.
class MockSPI final : public ISX1255SPI {
public:
    bool open() override
    {
        opened = true;
        return true;
    }
    void close() override { opened = false; }

    bool readRegister(std::uint8_t addr, std::uint8_t& value) override
    {
        reads.push_back(addr);
        value = regs.count(addr) ? regs[addr] : 0;
        return true;
    }
    bool writeRegister(std::uint8_t addr, std::uint8_t value) override
    {
        writes.emplace_back(addr, value);
        regs[addr] = value;
        return true;
    }
    bool burstRead(std::uint8_t, std::uint8_t*, std::size_t) override { return true; }
    bool burstWrite(std::uint8_t, const std::uint8_t*, std::size_t) override { return true; }

    std::map<std::uint8_t, std::uint8_t> regs;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> writes;
    std::vector<std::uint8_t> reads;
    bool opened = false;
};

// Records GPIO line activity; no hardware is touched.
class MockGPIO final : public ISX1255GPIO {
public:
    bool open() override
    {
        opened = true;
        return true;
    }
    void close() override { opened = false; }

    bool setReset(bool on) override { resetCalls.push_back(on); return true; }
    bool setTxEnable(bool on) override { txCalls.push_back(on); return true; }
    bool setRxEnable(bool on) override { rxCalls.push_back(on); return true; }
    bool setPtt(bool on) override { pttCalls.push_back(on); return true; }

    std::vector<bool> resetCalls;
    std::vector<bool> txCalls;
    std::vector<bool> rxCalls;
    std::vector<bool> pttCalls;
    bool opened = false;
};

}  // namespace sx1255::test

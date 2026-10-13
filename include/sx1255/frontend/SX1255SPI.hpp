#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sx1255 {

// SPI transport abstraction for the SX1255.  Concrete implementation uses
// Linux spidev; tests inject a mock.
class ISX1255SPI {
public:
    virtual ~ISX1255SPI() = default;

    virtual bool open() = 0;
    virtual void close() = 0;

    virtual bool readRegister(std::uint8_t addr, std::uint8_t& value) = 0;
    virtual bool writeRegister(std::uint8_t addr, std::uint8_t value) = 0;
    virtual bool burstRead(std::uint8_t addr, std::uint8_t* buf, std::size_t len) = 0;
    virtual bool burstWrite(std::uint8_t addr, const std::uint8_t* buf, std::size_t len) = 0;
};

// Linux spidev implementation (SPI mode 0, 8 bits per word).
class SX1255SpiDev final : public ISX1255SPI {
public:
    explicit SX1255SpiDev(std::string device, std::uint32_t speed = 500000);
    ~SX1255SpiDev() override;

    bool open() override;
    void close() override;

    bool readRegister(std::uint8_t addr, std::uint8_t& value) override;
    bool writeRegister(std::uint8_t addr, std::uint8_t value) override;
    bool burstRead(std::uint8_t addr, std::uint8_t* buf, std::size_t len) override;
    bool burstWrite(std::uint8_t addr, const std::uint8_t* buf, std::size_t len) override;

private:
    bool transfer(const std::uint8_t* tx, std::uint8_t* rx, std::size_t len);

    std::string m_device;
    std::uint32_t m_speed;
    int m_fd = -1;
};

}  // namespace sx1255

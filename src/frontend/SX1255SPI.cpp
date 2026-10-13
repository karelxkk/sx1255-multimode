#include "sx1255/frontend/SX1255SPI.hpp"

#include <linux/spi/spidev.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace sx1255 {

SX1255SpiDev::SX1255SpiDev(std::string device, std::uint32_t speed)
    : m_device(std::move(device)), m_speed(speed)
{
}

SX1255SpiDev::~SX1255SpiDev()
{
    close();
}

bool SX1255SpiDev::open()
{
    if (m_fd >= 0)
        return true;

    m_fd = ::open(m_device.c_str(), O_RDWR);
    if (m_fd < 0)
        return false;

    std::uint8_t mode = SPI_MODE_0;
    std::uint8_t bits = 8;
    if (::ioctl(m_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ::ioctl(m_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ::ioctl(m_fd, SPI_IOC_WR_MAX_SPEED_HZ, &m_speed) < 0) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    return true;
}

void SX1255SpiDev::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool SX1255SpiDev::transfer(const std::uint8_t* tx, std::uint8_t* rx, std::size_t len)
{
    if (m_fd < 0)
        return false;

    struct spi_ioc_transfer tr {};
    tr.tx_buf = reinterpret_cast<std::uintptr_t>(tx);
    tr.rx_buf = reinterpret_cast<std::uintptr_t>(rx);
    tr.len = static_cast<std::uint32_t>(len);
    tr.speed_hz = m_speed;
    tr.bits_per_word = 8;
    return ::ioctl(m_fd, SPI_IOC_MESSAGE(1), &tr) >= 0;
}

bool SX1255SpiDev::readRegister(std::uint8_t addr, std::uint8_t& value)
{
    const std::uint8_t tx[2] = {static_cast<std::uint8_t>(addr & 0x7F), 0x00};
    std::uint8_t rx[2] = {0, 0};
    if (!transfer(tx, rx, 2))
        return false;
    value = rx[1];
    return true;
}

bool SX1255SpiDev::writeRegister(std::uint8_t addr, std::uint8_t value)
{
    const std::uint8_t tx[2] = {static_cast<std::uint8_t>(addr | 0x80), value};
    std::uint8_t rx[2] = {0, 0};
    return transfer(tx, rx, 2);
}

bool SX1255SpiDev::burstRead(std::uint8_t addr, std::uint8_t* buf, std::size_t len)
{
    std::vector<std::uint8_t> tx(len + 1, 0);
    tx[0] = static_cast<std::uint8_t>(addr & 0x7F);
    std::vector<std::uint8_t> rx(len + 1, 0);
    if (!transfer(tx.data(), rx.data(), len + 1))
        return false;
    std::memcpy(buf, rx.data() + 1, len);
    return true;
}

bool SX1255SpiDev::burstWrite(std::uint8_t addr, const std::uint8_t* buf, std::size_t len)
{
    std::vector<std::uint8_t> tx(len + 1);
    tx[0] = static_cast<std::uint8_t>(addr | 0x80);
    std::memcpy(tx.data() + 1, buf, len);
    std::vector<std::uint8_t> rx(len + 1, 0);
    return transfer(tx.data(), rx.data(), len + 1);
}

}  // namespace sx1255

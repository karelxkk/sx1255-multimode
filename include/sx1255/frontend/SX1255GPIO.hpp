#pragma once

#include <array>
#include <string>

namespace sx1255 {

// Optional GPIO controls for the SX1255 HAT.  reset toggles the chip reset
// line; txEnable / rxEnable / ptt drive optional external PA/LNA/PTT lines.
// A line number < 0 means "not wired" and its method becomes a no-op.
class ISX1255GPIO {
public:
    virtual ~ISX1255GPIO() = default;

    virtual bool open() = 0;
    virtual void close() = 0;

    virtual bool setReset(bool on) = 0;
    virtual bool setTxEnable(bool on) = 0;
    virtual bool setRxEnable(bool on) = 0;
    virtual bool setPtt(bool on) = 0;
};

// libgpiod (v2) implementation.  libgpiod is loaded dynamically so the build
// needs no development headers and the binary still links without it.
class SX1255Gpiod final : public ISX1255GPIO {
public:
    SX1255Gpiod(std::string chip, int resetLine, int txEnableLine,
                int rxEnableLine, int pttLine);
    ~SX1255Gpiod() override;

    bool open() override;
    void close() override;

    bool setReset(bool on) override;
    bool setTxEnable(bool on) override;
    bool setRxEnable(bool on) override;
    bool setPtt(bool on) override;

private:
    enum Index { kReset = 0, kTx = 1, kRx = 2, kPtt = 3, kCount = 4 };

    bool setLine(Index index, bool on);

    std::string m_chip;
    std::array<int, kCount> m_lines;
    std::array<unsigned int, kCount> m_offsets{};  // request order
    unsigned int m_requestCount = 0;
    void* m_chipHandle = nullptr;    // gpiod_chip*
    void* m_request = nullptr;       // gpiod_line_request*
};

}  // namespace sx1255

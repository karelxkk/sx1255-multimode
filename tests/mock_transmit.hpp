#pragma once

#include "sx1255/frontend/ITransmit.hpp"

#include <cstddef>
#include <vector>

namespace sx1255::test {

// Records PTT transitions and TX IQ samples for plugin loopback tests.
class MockTransmit final : public ITransmit {
public:
    bool setTxFrequency(Hertz) override { return true; }

    bool setPtt(bool on) override
    {
        ptt = on;
        pttCalls.push_back(on);
        return true;
    }

    bool writeIQ(const ComplexFloat* samples, std::size_t count) override
    {
        for (std::size_t i = 0; i < count; ++i)
            iq.push_back(samples[i]);
        return true;
    }

    bool isReady() const override { return true; }

    bool ptt = false;
    std::vector<ComplexFloat> iq;
    std::vector<bool> pttCalls;
};

}  // namespace sx1255::test

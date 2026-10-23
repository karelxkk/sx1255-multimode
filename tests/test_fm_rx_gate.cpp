#include "sx1255/fm/FmRxGate.hpp"

#include <cstddef>
#include <vector>

using namespace sx1255;

int main()
{
    fm::FmRxGate gate(8);
    std::vector<ComplexFloat> first(6);
    std::vector<ComplexFloat> second(6);
    for (std::size_t i = 0; i < first.size(); ++i)
        first[i] = {static_cast<float>(i), 0.0F};
    for (std::size_t i = 0; i < second.size(); ++i)
        second[i] = {static_cast<float>(i + 6), 0.0F};

    gate.buffer(first.data(), first.size());
    gate.buffer(second.data(), second.size());
    if (gate.samples() != 8)
        return 1;

    std::vector<float> flushed;
    gate.flush([&](const ComplexFloat* samples, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            flushed.push_back(samples[i].i);
    });
    if (flushed.size() != 8 || gate.samples() != 0 || !gate.empty())
        return 2;
    for (std::size_t i = 0; i < flushed.size(); ++i) {
        if (flushed[i] != static_cast<float>(i + 4))
            return 3;
    }

    gate.buffer(first.data(), first.size());
    gate.clear();
    gate.flush([&](const ComplexFloat*, std::size_t) { return; });
    if (gate.samples() != 0 || !gate.empty())
        return 4;

    gate.buffer(first.data(), first.size());
    gate.buffer(second.data(), second.size());
    flushed.clear();
    gate.flushTail([&](const ComplexFloat* samples, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i)
            flushed.push_back(samples[i].i);
    }, 3);
    if (flushed.size() != 3 || gate.samples() != 0 ||
        flushed[0] != 9.0F || flushed[1] != 10.0F || flushed[2] != 11.0F) {
        return 5;
    }

    return 0;
}

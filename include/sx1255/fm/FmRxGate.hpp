#pragma once

#include "sx1255/Types.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <vector>

namespace sx1255::fm {

// Holds RF IQ while the FM detector is waiting for its RSSI confirmation
// window.  A confirmed analogue carrier flushes the complete pre-roll into
// SVXLink; a higher-priority digital mode discards it before any audio leaks.
class FmRxGate {
public:
    using Sink = std::function<void(const ComplexFloat*, std::size_t)>;

    explicit FmRxGate(std::size_t maxSamples)
        : m_maxSamples(maxSamples)
    {
    }

    void buffer(const ComplexFloat* samples, std::size_t count)
    {
        if (count == 0 || m_maxSamples == 0)
            return;

        std::vector<ComplexFloat> block(samples, samples + count);
        m_samples += block.size();
        m_blocks.push_back(std::move(block));

        while (m_samples > m_maxSamples && !m_blocks.empty()) {
            const std::size_t excess = m_samples - m_maxSamples;
            auto& front = m_blocks.front();
            if (front.size() <= excess) {
                m_samples -= front.size();
                m_blocks.pop_front();
            } else {
                front.erase(front.begin(),
                            front.begin() + static_cast<std::ptrdiff_t>(excess));
                m_samples -= excess;
            }
        }
    }

    void flush(const Sink& sink)
    {
        for (const auto& block : m_blocks)
            sink(block.data(), block.size());
        clear();
    }

    void flushTail(const Sink& sink, std::size_t tailSamples)
    {
        std::size_t skip = m_samples > tailSamples ? m_samples - tailSamples : 0;
        for (const auto& block : m_blocks) {
            if (skip >= block.size()) {
                skip -= block.size();
                continue;
            }
            const std::size_t offset = skip;
            skip = 0;
            sink(block.data() + offset, block.size() - offset);
        }
        clear();
    }

    void clear()
    {
        m_blocks.clear();
        m_samples = 0;
    }

    std::size_t samples() const { return m_samples; }
    bool empty() const { return m_blocks.empty(); }

private:
    std::size_t m_maxSamples;
    std::size_t m_samples = 0;
    std::deque<std::vector<ComplexFloat>> m_blocks;
};

}  // namespace sx1255::fm

#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace sx1255 {

// Lock-free single-producer / single-consumer ring buffer.
// Capacity is rounded up to the next power of two.
template <typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(std::size_t capacity)
    {
        std::size_t cap = 1;
        while (cap < capacity + 1)
            cap <<= 1;
        m_buffer.resize(cap);
        m_mask = cap - 1;
    }

    // Producer side.  Returns false if the queue is full.
    bool push(const T& value)
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & m_mask;
        if (next == m_head.load(std::memory_order_acquire))
            return false;
        m_buffer[tail] = value;
        m_tail.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side.  Returns false if the queue is empty.
    bool pop(T& value)
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire))
            return false;
        value = m_buffer[head];
        m_head.store((head + 1) & m_mask, std::memory_order_release);
        return true;
    }

    std::size_t capacity() const { return m_buffer.size() - 1; }

private:
    std::vector<T> m_buffer;
    std::size_t m_mask = 0;
    std::atomic<std::size_t> m_head{0};
    std::atomic<std::size_t> m_tail{0};
};

}  // namespace sx1255

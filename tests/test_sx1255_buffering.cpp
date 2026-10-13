#include "sx1255/frontend/SPSCQueue.hpp"
#include "sx1255/frontend/SX1255Hardware.hpp"

#include "mock_hw.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace sx1255;

int main()
{
    {
        SPSCQueue<int> q(4);
        const int cap = static_cast<int>(q.capacity());
        for (int i = 0; i < cap; ++i)
            if (!q.push(i))
                return 1;
        if (q.push(999))
            return 2;

        for (int i = 0; i < cap; ++i) {
            int v = -1;
            if (!q.pop(v) || v != i)
                return 3;
        }
        int v = -1;
        if (q.pop(v))
            return 4;
    }

    {
        constexpr int kTotal = 100000;
        SPSCQueue<int> q(1 << 14);
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};
        std::atomic<int> dropped{0};

        std::thread producer([&] {
            for (int i = 0; i < kTotal; ++i) {
                if (q.push(i))
                    produced++;
                else
                    dropped++;
            }
        });
        std::thread consumer([&] {
            while (consumed.load() + dropped.load() < kTotal) {
                int v = 0;
                if (q.pop(v))
                    consumed++;
                else
                    std::this_thread::yield();
            }
        });

        producer.join();
        consumer.join();

        if (produced.load() + dropped.load() != kTotal)
            return 5;
        if (consumed.load() != produced.load())
            return 6;
    }

    {
        auto spi = std::make_unique<test::MockSPI>();
        auto gpio = std::make_unique<test::MockGPIO>();
        SX1255HardwareConfig cfg;
        cfg.txQueueSize = 4;
        SX1255Hardware hw(cfg, std::move(spi), std::move(gpio));

        std::vector<ComplexFloat> samples(10, ComplexFloat{0.5F, -0.5F});
        hw.writeIQ(samples.data(), samples.size());

        const FrontendStats s = hw.stats();
        if (s.txSamples > 10)
            return 7;
        if (s.txSamples + s.txUnderruns != 10)
            return 8;
    }

    return 0;
}

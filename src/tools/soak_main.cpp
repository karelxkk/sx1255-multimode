#include "sx1255/config/Config.hpp"
#include "sx1255/soak/SoakTester.hpp"
#include "sx1255/util/Logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_deallocations{0};

void usage(const char* program)
{
    std::fprintf(stderr,
        "Usage: %s -config FILE [--duration SECONDS] [--out soak_report.json]\n",
        program);
}

}  // namespace

void* operator new(std::size_t size)
{
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (size == 0)
        size = 1;
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (size == 0)
        size = 1;
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept
{
    if (ptr)
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    if (ptr)
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    if (ptr)
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    if (ptr)
        g_deallocations.fetch_add(1, std::memory_order_relaxed);
    std::free(ptr);
}

int main(int argc, char** argv)
{
    std::string configPath = "sx1255-multimode.yaml";
    std::string outPath = "soak_report.json";
    std::uint64_t durationSeconds = 60;
    sx1255::util::LogLevel logLevel = sx1255::util::LogLevel::Info;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "-config" || arg == "--config") && i + 1 < argc)
            configPath = argv[++i];
        else if (arg == "--duration" && i + 1 < argc)
            durationSeconds = std::stoull(argv[++i]);
        else if (arg == "--out" && i + 1 < argc)
            outPath = argv[++i];
        else if (arg == "--log-level" && i + 1 < argc) {
            if (!sx1255::util::parseLogLevel(argv[++i], logLevel)) {
                std::fprintf(stderr, "invalid --log-level\n");
                return 2;
            }
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    sx1255::util::Logger::instance().configure(logLevel, {});

    try {
        const sx1255::Config config = sx1255::loadConfig(configPath);
        sx1255::soak::SoakTester tester(config, std::chrono::seconds(durationSeconds));
        tester.setAllocationCounter([] { return g_allocations.load(); });

        if (!tester.run()) {
            std::fprintf(stderr, "soak run failed\n");
            return 1;
        }

        tester.writeReport(outPath);
        std::fprintf(stderr, "soak report written to %s (allocations=%llu, frees=%llu)\n",
                     outPath.c_str(),
                     static_cast<unsigned long long>(g_allocations.load()),
                     static_cast<unsigned long long>(g_deallocations.load()));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

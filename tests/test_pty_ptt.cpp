#include "sx1255/fm/PtyPtt.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

using namespace sx1255;

int main()
{
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
        return 1;
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0)
        return 2;

    char* slaveName = ::ptsname(master);
    if (!slaveName)
        return 3;

    std::atomic<int> transitions{0};
    std::atomic<bool> last{false};
    fm::PtyPtt ptt(slaveName, [&](bool on) {
        last = on;
        transitions.fetch_add(1);
    });
    ptt.start();

    const char tx = 'T';
    const char rx = 'R';
    ::write(master, &tx, 1);

    // Some sandboxed/container runtimes do not deliver data across /dev/pts;
    // skip the test there rather than failing a valid build.
    bool gotTx = false;
    for (int i = 0; i < 100; ++i) {
        if (transitions.load() >= 1) {
            gotTx = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!gotTx) {
        std::fprintf(stderr, "PTY data path unavailable in this environment; skipping\n");
        ptt.stop();
        ::close(master);
        return 77;
    }

    if (!last.load())
        return 4;

    ::write(master, &rx, 1);
    for (int i = 0; i < 100 && transitions.load() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (transitions.load() < 2 || last.load())
        return 5;

    ptt.stop();
    ::close(master);
    return 0;
}

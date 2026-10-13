#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace sx1255::fm {

// ---------------------------------------------------------------------------
// PtyPtt
//
// Reads SVXLink's PTT signal directly from a PTY.  SVXLink (with
// PTT_TYPE=PTY) writes 'T' when it starts transmitting and 'R' when it
// returns to receive; no external PTT bridge process is required.
// ---------------------------------------------------------------------------
class PtyPtt {
public:
    using Callback = std::function<void(bool txOn)>;

    PtyPtt(std::string path, Callback callback);
    ~PtyPtt();

    PtyPtt(const PtyPtt&) = delete;
    PtyPtt& operator=(const PtyPtt&) = delete;

    void start();
    void stop();

private:
    void run();

    std::string m_path;
    Callback    m_callback;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

}  // namespace sx1255::fm

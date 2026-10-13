#include "sx1255/fm/PtyPtt.hpp"

#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace sx1255::fm {

PtyPtt::PtyPtt(std::string path, Callback callback)
    : m_path(std::move(path)), m_callback(std::move(callback))
{
}

PtyPtt::~PtyPtt()
{
    stop();
}

void PtyPtt::start()
{
    if (m_running.load())
        return;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void PtyPtt::stop()
{
    if (!m_running.load())
        return;
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void PtyPtt::run()
{
    // The PTY (usually /run/svxlink/ptt, a symlink to /dev/pts/N) appears only
    // after SVXLink opens it; retry until stop() is requested.
    while (m_running.load()) {
        const int fd = ::open(m_path.c_str(), O_RDONLY | O_NOCTTY | O_CLOEXEC);
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        while (m_running.load()) {
            pollfd pfd{fd, POLLIN, 0};
            const int pr = ::poll(&pfd, 1, 200);
            if (pr < 0)
                break;      // error: reopen
            if (pr == 0)
                continue;   // timeout: re-check m_running

            unsigned char byte = 0;
            const ssize_t n = ::read(fd, &byte, 1);
            if (n <= 0)
                break;      // EOF/error: reopen
            if (byte == 'T' || byte == 't')
                m_callback(true);
            else if (byte == 'R' || byte == 'r')
                m_callback(false);
        }
        ::close(fd);
    }
}

}  // namespace sx1255::fm

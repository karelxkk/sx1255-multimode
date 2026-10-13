#include "sx1255/fm/SqlPty.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cstdio>
#include <string>

using namespace sx1255;

int main()
{
    const std::string link = "/tmp/sx1255_sql_pty_test";

    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0)
        return 1;
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        ::close(master);
        return 2;
    }
    const char* slavePath = ::ptsname(master);
    if (!slavePath) {
        ::close(master);
        return 3;
    }
    ::unlink(link.c_str());
    if (::symlink(slavePath, link.c_str()) != 0) {
        ::close(master);
        return 4;
    }

    fm::SqlPty sql(link, "");
    if (!sql.open())
        return 5;

    // multimode is the writer; SVXLink (the master) is the reader.
    auto readOne = [&]() -> int {
        pollfd pfd{master, POLLIN, 0};
        if (::poll(&pfd, 1, 1000) <= 0)
            return -1;
        char c = 0;
        return ::read(master, &c, 1) == 1 ? static_cast<unsigned char>(c) : -1;
    };

    if (!sql.set(true))
        return 6;
    if (readOne() != 'O')
        return 7;

    if (!sql.set(false))
        return 8;
    if (readOne() != 'Z')
        return 9;

    sql.close();
    ::close(master);
    ::unlink(link.c_str());
    return 0;
}

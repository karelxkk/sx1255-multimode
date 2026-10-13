#include "sx1255/fm/SqlPty.hpp"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <thread>

namespace sx1255::fm {

namespace {

bool chownTo(const std::string& path, const std::string& owner)
{
    if (owner.empty())
        return true;

    std::string user = owner;
    std::string grpName;
    const auto colon = owner.find(':');
    if (colon != std::string::npos) {
        user = owner.substr(0, colon);
        grpName = owner.substr(colon + 1);
    }

    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
    if (!user.empty()) {
        const passwd* pw = ::getpwnam(user.c_str());
        if (!pw)
            return false;
        uid = pw->pw_uid;
        gid = pw->pw_gid;
    }
    if (!grpName.empty()) {
        const struct group* gr = ::getgrnam(grpName.c_str());
        if (!gr)
            return false;
        gid = gr->gr_gid;
    }
    return ::chown(path.c_str(), uid, gid) == 0;
}

std::string parentDir(const std::string& path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool ensureParent(const std::string& linkPath, const std::string& owner)
{
    const std::string dir = parentDir(linkPath);
    if (dir == ".")
        return true;
    if (::mkdir(dir.c_str(), 0775) != 0 && errno != EEXIST)
        return false;
    // Let SVXLink create its own SQL PTY symlink in the same directory.
    if (!owner.empty()) {
        chownTo(dir, owner);
        ::chmod(dir.c_str(), 0775);
    }
    return true;
}

}  // namespace

SqlPty::SqlPty(std::string linkPath, std::string owner)
    : m_linkPath(std::move(linkPath)), m_owner(std::move(owner))
{
}

SqlPty::~SqlPty()
{
    close();
}

bool SqlPty::open()
{
    return ensureParent(m_linkPath, m_owner);
}

void SqlPty::close()
{
}

bool SqlPty::set(bool on)
{
    const char byte = on ? 'O' : 'Z';

    // Open the symlink fresh on every state change.  SVXLink recreates its PTY
    // (and the symlink target) when the service restarts, so a cached fd would
    // go stale; reopening also handles a short startup race.
    for (int attempt = 0; attempt < 40; ++attempt) {
        const int fd = ::open(m_linkPath.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) {
            const ssize_t n = ::write(fd, &byte, 1);
            ::close(fd);
            if (n == 1)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return false;
}

}  // namespace sx1255::fm

#pragma once

#include <string>

namespace sx1255::fm {

// Exposes multimode's FM detection result as a SVXLink squelch signal over the
// PTY protocol that SVXLink itself creates (SQL_DET=PTY, PTY_PATH=<link>).
// SVXLink owns the PTY master and links its slave to <link>; multimode writes
// 'O' (squelch open) or 'Z' (squelch closed) to that slave.  CTCSS is therefore
// evaluated exactly once, by the FM plugin.
class SqlPty {
public:
    SqlPty(std::string linkPath, std::string owner);
    ~SqlPty();

    SqlPty(const SqlPty&) = delete;
    SqlPty& operator=(const SqlPty&) = delete;

    bool open();
    void close();

    // Assert (on=true => 'O') or deassert (on=false => 'Z') the SQL signal.
    bool set(bool on);

private:
    std::string m_linkPath;
    std::string m_owner;   // "user" or "user:group", empty = keep root
};

}  // namespace sx1255::fm

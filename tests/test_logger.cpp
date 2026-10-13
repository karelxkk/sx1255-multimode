#include "sx1255/util/Logger.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

using namespace sx1255;

int main()
{
    util::LogLevel level = util::LogLevel::Info;
    if (!util::parseLogLevel("debug", level) || level != util::LogLevel::Debug)
        return 1;
    if (!util::parseLogLevel("WARN", level) || level != util::LogLevel::Warn)
        return 2;
    if (!util::parseLogLevel("error", level) || level != util::LogLevel::Error)
        return 3;
    if (util::parseLogLevel("bogus", level))
        return 4;

    const std::string prefix = "/tmp/sx1255_logger_" + std::to_string(::getpid());
    util::Logger::instance().configure(util::LogLevel::Info, prefix, 1024, 2);
    util::Logger::instance().info("info line %d", 42);
    util::Logger::instance().warn("warn line");
    util::Logger::instance().error("error line");

    std::ifstream file(prefix + ".log");
    if (!file.is_open())
        return 5;
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    if (contents.find("info line 42") == std::string::npos)
        return 6;
    if (contents.find("[WARN]") == std::string::npos)
        return 7;
    if (contents.find("[ERROR]") == std::string::npos)
        return 8;

    std::remove((prefix + ".log").c_str());
    return 0;
}

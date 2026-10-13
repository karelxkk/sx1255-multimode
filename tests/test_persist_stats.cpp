#include "sx1255/diag/SystemStatistics.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

using namespace sx1255;

int main()
{
    const std::string path = "/tmp/sx1255_stats_" + std::to_string(::getpid()) + ".json";

    {
        SystemStatistics stats;
        stats.recordDetection(Mode::FM);
        stats.recordDetection(Mode::FM);
        stats.recordDetection(Mode::M17);
        stats.recordSession(Mode::M17);
        stats.recordSession(Mode::M17);
        stats.recordFalsePositive();

        if (!stats.save(path))
            return 1;
    }

    {
        SystemStatistics stats;
        if (!stats.load(path))
            return 2;
        if (stats.fmDetected() != 2)
            return 3;
        if (stats.m17Detected() != 1)
            return 4;
        if (stats.m17Sessions() != 2)
            return 5;
        if (stats.falsePositives() != 1)
            return 6;

        std::ifstream file(path);
        std::string contents((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        if (contents.find("\"total_uptime_seconds\"") == std::string::npos)
            return 7;
    }

    std::remove(path.c_str());
    return 0;
}

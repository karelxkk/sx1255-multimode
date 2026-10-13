#include "sx1255/spectrum/WaterfallGenerator.hpp"

#include <string>
#include <vector>

using namespace sx1255;

int main()
{
    constexpr std::size_t kBins = 8;
    constexpr std::size_t kMaxRows = 3;

    spectrum::WaterfallGenerator waterfall(kBins, kMaxRows);

    for (int r = 0; r < 5; ++r) {
        std::vector<float> row(kBins);
        for (std::size_t b = 0; b < kBins; ++b)
            row[b] = static_cast<float>(r * 10 + static_cast<int>(b));
        waterfall.addRow(row);
    }

    if (waterfall.bins() != kBins)
        return 1;
    if (waterfall.maxRows() != kMaxRows)
        return 2;
    if (waterfall.rowCount() != kMaxRows)
        return 3;

    // Only the newest 3 rows survive, oldest first.
    const auto rows = waterfall.rows();
    if (rows.size() != kMaxRows)
        return 4;
    if (rows[0][0] != 20.0F)  // r=2
        return 5;
    if (rows[1][0] != 30.0F)  // r=3
        return 6;
    if (rows[2][0] != 40.0F)  // r=4
        return 7;

    const std::string json = waterfall.toJson();
    if (json.find("\"max_rows\":3") == std::string::npos)
        return 8;
    if (json.find("\"rows\":3") == std::string::npos)
        return 9;
    if (json.find("\"bins\":8") == std::string::npos)
        return 10;
    if (json.find("[[20,21") == std::string::npos)
        return 11;

    // Incremental serialization: only rows newer than `since` are emitted.
    if (waterfall.latestSeq() != 5)
        return 12;
    const std::string partial = waterfall.toJson(3);
    if (partial.find("\"rows\":2") == std::string::npos)
        return 13;
    if (partial.find("\"seq\":5") == std::string::npos)
        return 14;
    const std::string empty = waterfall.toJson(5);
    if (empty.find("\"rows\":0") == std::string::npos)
        return 15;

    return 0;
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sx1255::dataset {

struct DatasetResult {
    std::string file;
    std::string expected;    // fm | dstar | m17 | unknown
    std::string detected;    // FM | DSTAR | M17 | NONE
    bool        pass = false;
    std::uint64_t samples = 0;
};

// Replay every .iq file under rootDir (recursively), classify each through the
// real plugin pipeline and write regression_report.json to outPath.
// Returns the number of failures (0 means all datasets passed).
int runDatasetBatch(const std::string& rootDir, const std::string& outPath);

std::vector<DatasetResult> replayDatasets(const std::string& rootDir);
bool writeRegressionReport(const std::vector<DatasetResult>& results,
                           const std::string& outPath);

}  // namespace sx1255::dataset

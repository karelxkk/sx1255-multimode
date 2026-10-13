#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace sx1255::spectrum {

// ---------------------------------------------------------------------------
// WaterfallGenerator
//
// Bounded history of spectrum frames (oldest first, newest last).  The web
// dashboard polls it to render the scrolling waterfall.
//
// Rows are stored pre-quantized as int16 dB and decimated to m_bins, so the
// 500-row history stays small and serializes quickly even on a Pi Zero 2 W.
// ---------------------------------------------------------------------------
class WaterfallGenerator {
public:
    WaterfallGenerator(std::size_t bins, std::size_t maxRows = 500);

    // Accepts a spectrum of any bin count; it is decimated/averaged to the
    // configured bin count before being stored.
    void addRow(const std::vector<float>& spectrumDb);

    std::size_t rowCount() const;
    std::size_t bins() const;
    std::size_t maxRows() const;
    std::vector<std::vector<float>> rows() const;   // oldest -> newest

    // Serialize only rows newer than `sinceSeq` (0 = full history).  The JSON
    // carries the latest sequence number so clients can request increments.
    std::string toJson(std::uint64_t sinceSeq = 0) const;
    std::uint64_t latestSeq() const;

private:
    static std::vector<std::int16_t> decimate(const std::vector<float>& spectrumDb,
                                              std::size_t bins);

    std::size_t m_bins;
    std::size_t m_maxRows;
    struct Row {
        std::uint64_t seq;
        std::vector<std::int16_t> data;
    };
    std::deque<Row> m_rows;
    std::uint64_t m_nextSeq = 0;
    mutable std::mutex m_mutex;
};

}  // namespace sx1255::spectrum

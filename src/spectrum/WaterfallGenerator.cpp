#include "sx1255/spectrum/WaterfallGenerator.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace sx1255::spectrum {

namespace {

void appendInt(std::string& out, std::int16_t value)
{
    char buf[8];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, result.ptr);
}

}  // namespace

WaterfallGenerator::WaterfallGenerator(std::size_t bins, std::size_t maxRows)
    : m_bins(std::max<std::size_t>(1, bins))
    , m_maxRows(maxRows)
{
}

void WaterfallGenerator::addRow(const std::vector<float>& spectrumDb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rows.push_back(Row{++m_nextSeq, decimate(spectrumDb, m_bins)});
    while (m_rows.size() > m_maxRows)
        m_rows.pop_front();
}

std::size_t WaterfallGenerator::rowCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rows.size();
}

std::size_t WaterfallGenerator::bins() const
{
    return m_bins;
}

std::size_t WaterfallGenerator::maxRows() const
{
    return m_maxRows;
}

std::vector<std::vector<float>> WaterfallGenerator::rows() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::vector<float>> out;
    out.reserve(m_rows.size());
    for (const auto& row : m_rows) {
        std::vector<float> floats(row.data.size());
        for (std::size_t i = 0; i < row.data.size(); ++i)
            floats[i] = static_cast<float>(row.data[i]);
        out.push_back(std::move(floats));
    }
    return out;
}

std::uint64_t WaterfallGenerator::latestSeq() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_nextSeq;
}

std::string WaterfallGenerator::toJson(std::uint64_t sinceSeq) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::size_t count = 0;
    for (const auto& row : m_rows)
        if (row.seq > sinceSeq)
            ++count;

    char head[128];
    std::snprintf(head, sizeof(head),
        "{\"bins\":%zu,\"max_rows\":%zu,\"rows\":%zu,\"seq\":%llu,\"data\":[",
        m_bins, m_maxRows, count,
        static_cast<unsigned long long>(m_nextSeq));
    std::string out = head;

    bool firstRow = true;
    for (const auto& row : m_rows) {
        if (row.seq <= sinceSeq)
            continue;
        if (!firstRow)
            out += ',';
        firstRow = false;
        out += '[';
        for (std::size_t b = 0; b < row.data.size(); ++b) {
            if (b)
                out += ',';
            appendInt(out, row.data[b]);
        }
        out += ']';
    }
    out += "]}";
    return out;
}

std::vector<std::int16_t> WaterfallGenerator::decimate(const std::vector<float>& spectrumDb,
                                                       std::size_t bins)
{
    if (spectrumDb.empty())
        return std::vector<std::int16_t>(bins, static_cast<std::int16_t>(-120));

    std::vector<std::int16_t> out(bins);
    if (spectrumDb.size() == bins) {
        for (std::size_t i = 0; i < bins; ++i)
            out[i] = static_cast<std::int16_t>(std::clamp(
                static_cast<int>(std::lround(spectrumDb[i])), -32768, 32767));
        return out;
    }

    // Average contiguous source bins into each target bin.
    const std::size_t src = spectrumDb.size();
    for (std::size_t i = 0; i < bins; ++i) {
        const std::size_t begin = i * src / bins;
        const std::size_t end = std::min<std::size_t>(src, (i + 1) * src / bins);
        double sum = 0.0;
        for (std::size_t j = begin; j < end; ++j)
            sum += static_cast<double>(spectrumDb[j]);
        const double avg = sum / static_cast<double>(end - begin);
        out[i] = static_cast<std::int16_t>(std::clamp(
            static_cast<int>(std::lround(avg)), -32768, 32767));
    }
    return out;
}

}  // namespace sx1255::spectrum

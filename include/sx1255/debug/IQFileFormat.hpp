#pragma once

#include "sx1255/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace sx1255::debug {

// ---------------------------------------------------------------------------
// IQFileFormat
//
// Deterministic, streamable, endian-safe binary container for raw SX1255 IQ.
//
//   [64-byte header][float32 I][float32 Q][float32 I][float32 Q] ...
//
// All multi-byte values are stored little-endian on disk regardless of the
// host byte order.  The header carries its own size and a version number, so
// future fields can be appended without breaking old readers.
// ---------------------------------------------------------------------------

inline constexpr std::array<std::uint8_t, 8> kIqMagic = {
    'S', 'X', '1', '2', '5', '5', 'I', 'Q'};

inline constexpr std::uint32_t kIqVersion   = 1;
inline constexpr std::size_t   kIqHeaderSize = 64;

// Byte offset of the sampleCount field inside the header; writers seek here
// to patch the final count once a streaming recording is finished.
inline constexpr std::size_t kIqSampleCountOffset = 36;
inline constexpr std::size_t kIqFrequencyOffset   = 24;

struct IQFileHeader {
    std::array<std::uint8_t, 8> magic = kIqMagic;
    std::uint32_t version      = kIqVersion;
    std::uint32_t headerSize   = kIqHeaderSize;
    std::uint32_t flags        = 0;
    std::uint32_t sampleRateHz = 125000;
    std::uint32_t frequencyHz  = 0;    // 0 = unknown / not applicable
    std::int64_t  timestampSec = 0;    // Unix seconds
    std::uint64_t sampleCount  = 0;    // 0 while streaming; patched on close
    std::array<std::uint8_t, 20> reserved{};
};

// Write/read the fixed-size header.  Returns false on stream failure.
bool writeIQHeader(std::ostream& os, const IQFileHeader& header);
bool readIQHeader(std::istream& is, IQFileHeader& header);

// Validate magic/version/size; throws-free predicate used by IQReplay.
bool validateIQHeader(const IQFileHeader& header);

// Write interleaved complex float32 samples.  Returns false on stream failure.
bool writeIQSamples(std::ostream& os, const ComplexFloat* samples, std::size_t count);

// Read up to maxSamples interleaved complex float32 samples into out.
// Returns the number of complete samples actually read (0 at EOF).
std::size_t readIQSamples(std::istream& is, ComplexFloat* out, std::size_t maxSamples);

// Low-level, endian-safe pack/unpack helpers (exposed for unit tests).
void writeU32Le(std::ostream& os, std::uint32_t value);
void writeU64Le(std::ostream& os, std::uint64_t value);
void writeI64Le(std::ostream& os, std::int64_t value);
void writeF32Le(std::ostream& os, float value);

bool readU32Le(std::istream& is, std::uint32_t& value);
bool readU64Le(std::istream& is, std::uint64_t& value);
bool readI64Le(std::istream& is, std::int64_t& value);
bool readF32Le(std::istream& is, float& value);

}  // namespace sx1255::debug

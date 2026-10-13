#include "sx1255/debug/IQFileFormat.hpp"

#include <cstring>
#include <istream>
#include <ostream>

namespace sx1255::debug {

namespace {

union FloatBits {
    float    f;
    std::uint32_t u;
};

}  // namespace

void writeU32Le(std::ostream& os, std::uint32_t value)
{
    const unsigned char b[4] = {
        static_cast<unsigned char>(value & 0xFFu),
        static_cast<unsigned char>((value >> 8) & 0xFFu),
        static_cast<unsigned char>((value >> 16) & 0xFFu),
        static_cast<unsigned char>((value >> 24) & 0xFFu),
    };
    os.write(reinterpret_cast<const char*>(b), sizeof(b));
}

void writeU64Le(std::ostream& os, std::uint64_t value)
{
    writeU32Le(os, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    writeU32Le(os, static_cast<std::uint32_t>(value >> 32));
}

void writeI64Le(std::ostream& os, std::int64_t value)
{
    writeU64Le(os, static_cast<std::uint64_t>(value));
}

void writeF32Le(std::ostream& os, float value)
{
    FloatBits bits{};
    bits.f = value;
    writeU32Le(os, bits.u);
}

bool readU32Le(std::istream& is, std::uint32_t& value)
{
    unsigned char b[4];
    if (!is.read(reinterpret_cast<char*>(b), sizeof(b)))
        return false;
    value = static_cast<std::uint32_t>(b[0]) |
            (static_cast<std::uint32_t>(b[1]) << 8) |
            (static_cast<std::uint32_t>(b[2]) << 16) |
            (static_cast<std::uint32_t>(b[3]) << 24);
    return true;
}

bool readU64Le(std::istream& is, std::uint64_t& value)
{
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;
    if (!readU32Le(is, lo) || !readU32Le(is, hi))
        return false;
    value = (static_cast<std::uint64_t>(hi) << 32) | lo;
    return true;
}

bool readI64Le(std::istream& is, std::int64_t& value)
{
    std::uint64_t raw = 0;
    if (!readU64Le(is, raw))
        return false;
    value = static_cast<std::int64_t>(raw);
    return true;
}

bool readF32Le(std::istream& is, float& value)
{
    std::uint32_t raw = 0;
    if (!readU32Le(is, raw))
        return false;
    FloatBits bits{};
    bits.u = raw;
    value = bits.f;
    return true;
}

bool writeIQHeader(std::ostream& os, const IQFileHeader& header)
{
    os.write(reinterpret_cast<const char*>(header.magic.data()),
             static_cast<std::streamsize>(header.magic.size()));
    writeU32Le(os, header.version);
    writeU32Le(os, header.headerSize);
    writeU32Le(os, header.flags);
    writeU32Le(os, header.sampleRateHz);
    writeU32Le(os, header.frequencyHz);
    writeI64Le(os, header.timestampSec);
    writeU64Le(os, header.sampleCount);
    os.write(reinterpret_cast<const char*>(header.reserved.data()),
             static_cast<std::streamsize>(header.reserved.size()));
    return static_cast<bool>(os);
}

bool readIQHeader(std::istream& is, IQFileHeader& header)
{
    if (!is.read(reinterpret_cast<char*>(header.magic.data()),
                 static_cast<std::streamsize>(header.magic.size())))
        return false;
    if (!readU32Le(is, header.version) ||
        !readU32Le(is, header.headerSize) ||
        !readU32Le(is, header.flags) ||
        !readU32Le(is, header.sampleRateHz) ||
        !readU32Le(is, header.frequencyHz) ||
        !readI64Le(is, header.timestampSec) ||
        !readU64Le(is, header.sampleCount))
        return false;
    if (!is.read(reinterpret_cast<char*>(header.reserved.data()),
                 static_cast<std::streamsize>(header.reserved.size())))
        return false;
    return static_cast<bool>(is);
}

bool validateIQHeader(const IQFileHeader& header)
{
    return header.magic == kIqMagic &&
           header.version == kIqVersion &&
           header.headerSize >= kIqHeaderSize &&
           header.sampleRateHz != 0;
}

bool writeIQSamples(std::ostream& os, const ComplexFloat* samples, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        writeF32Le(os, samples[i].i);
        writeF32Le(os, samples[i].q);
    }
    return static_cast<bool>(os);
}

std::size_t readIQSamples(std::istream& is, ComplexFloat* out, std::size_t maxSamples)
{
    std::size_t count = 0;
    for (; count < maxSamples; ++count) {
        float i = 0.0F;
        float q = 0.0F;
        if (!readF32Le(is, i) || !readF32Le(is, q))
            break;
        out[count] = ComplexFloat{i, q};
    }
    return count;
}

}  // namespace sx1255::debug

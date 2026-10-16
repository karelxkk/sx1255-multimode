#include "sx1255/m17/M17FrameDecoder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace sx1255::m17 {

// ---------------------------------------------------------------------------
// CRC (crc.go)
// ---------------------------------------------------------------------------

namespace {

std::array<std::uint16_t, 256> buildCrcTable()
{
    std::array<std::uint16_t, 256> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
        std::uint16_t crc = static_cast<std::uint16_t>(i) << 8;
        for (int j = 0; j < 8; ++j)
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>(crc << 1 ^ crcPoly)
                                 : static_cast<std::uint16_t>(crc << 1);
        t[i] = crc;
    }
    return t;
}

const auto g_crcTable = buildCrcTable();

std::uint16_t hammingWeight(std::uint16_t value)
{
    int weight = 0;
    for (int i = 0; i < 16; ++i)
        if ((value >> i) & 1)
            weight++;
    return static_cast<std::uint16_t>(weight);
}

std::uint16_t softToInt(const std::vector<SoftBit>& in)
{
    std::uint16_t result = 0;
    for (std::size_t i = 0; i < in.size(); ++i)
        if (in[i] > 0x7FFF)
            result |= static_cast<std::uint16_t>(1u << i);
    return result;
}

std::uint32_t golaySoftDetectErrors(const std::array<SoftBit, 24>& codeword)
{
    std::uint32_t hardCodeword = 0;
    for (int i = 0; i < 24; ++i)
        if (codeword[static_cast<std::size_t>(i)] > 0x7FFF)
            hardCodeword |= (1u << i);

    const std::uint16_t data = static_cast<std::uint16_t>((hardCodeword >> 12) & 0xFFF);
    const std::uint16_t parity = static_cast<std::uint16_t>(hardCodeword & 0xFFF);
    const std::uint16_t expectedParity = static_cast<std::uint16_t>(golayEncode24(data) & 0xFFF);
    const std::uint16_t syndrome = parity ^ expectedParity;

    if (syndrome == 0)
        return 0;
    if (hammingWeight(syndrome) <= 3)
        return syndrome;

    for (int i = 0; i < 12; ++i) {
        const std::uint16_t testData = data ^ static_cast<std::uint16_t>(1 << i);
        const std::uint16_t testSyndrome = parity ^ static_cast<std::uint16_t>(golayEncode24(testData) & 0xFFF);
        if (hammingWeight(testSyndrome) <= 2)
            return (1u << (12 + i)) | testSyndrome;
    }

    for (int i = 0; i < 12; ++i) {
        for (int j = i + 1; j < 12; ++j) {
            const std::uint16_t testData = data ^ static_cast<std::uint16_t>(1 << i) ^ static_cast<std::uint16_t>(1 << j);
            const std::uint16_t testSyndrome = parity ^ static_cast<std::uint16_t>(golayEncode24(testData) & 0xFFF);
            if (hammingWeight(testSyndrome) <= 1)
                return (1u << (12 + i)) | (1u << (12 + j)) | testSyndrome;
        }
    }

    for (int i = 0; i < 12; ++i) {
        for (int j = i + 1; j < 12; ++j) {
            for (int k = j + 1; k < 12; ++k) {
                const std::uint16_t testData = data ^ static_cast<std::uint16_t>(1 << i)
                                                    ^ static_cast<std::uint16_t>(1 << j)
                                                    ^ static_cast<std::uint16_t>(1 << k);
                const std::uint16_t testSyndrome = parity ^ static_cast<std::uint16_t>(golayEncode24(testData) & 0xFFF);
                if (testSyndrome == 0)
                    return (1u << (12 + i)) | (1u << (12 + j)) | (1u << (12 + k));
            }
        }
    }

    for (int i = 0; i < 12; ++i) {
        const std::uint16_t testData = data ^ static_cast<std::uint16_t>(1 << i);
        const std::uint16_t testSyndrome = parity ^ static_cast<std::uint16_t>(golayEncode24(testData) & 0xFFF);
        if (hammingWeight(testSyndrome) == 1)
            return (1u << (12 + i)) | testSyndrome;
    }

    for (int i = 0; i < 12; ++i) {
        for (int j = i + 1; j < 12; ++j) {
            const std::uint16_t testData = data ^ static_cast<std::uint16_t>(1 << i) ^ static_cast<std::uint16_t>(1 << j);
            const std::uint16_t testSyndrome = parity ^ static_cast<std::uint16_t>(golayEncode24(testData) & 0xFFF);
            if (hammingWeight(testSyndrome) == 1)
                return (1u << (12 + i)) | (1u << (12 + j)) | testSyndrome;
        }
    }

    return 0xFFFFFFFF;
}

std::vector<Bit> unpackBits(const std::vector<std::uint8_t>& in)
{
    std::vector<Bit> bits;
    bits.reserve(in.size() * 8);
    for (const std::uint8_t byte : in)
        for (int j = 7; j >= 0; --j)
            bits.push_back((byte >> j) & 1);
    return bits;
}

std::vector<std::uint8_t> extractLICH(int lichCnt, const LSF& lsf)
{
    const auto bytes = lsf.toBytes();
    std::vector<std::uint8_t> lich(bytes.begin() + static_cast<std::ptrdiff_t>(lichCnt * 5),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(lichCnt * 5 + 5));
    lich.push_back(static_cast<std::uint8_t>(lichCnt) << 5);
    return lich;
}

}  // namespace

std::uint16_t crc16(const std::uint8_t* data, std::size_t len)
{
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < len; ++i)
        crc = static_cast<std::uint16_t>(crc << 8) ^ g_crcTable[(crc >> 8) ^ data[i]];
    return crc;
}

// ---------------------------------------------------------------------------
// Callsigns (encoding.go)
// ---------------------------------------------------------------------------

std::array<std::uint8_t, EncodedCallsignLen> encodeCallsign(const std::string& callsign)
{
    std::string cs = callsign;
    std::transform(cs.begin(), cs.end(), cs.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (cs == "@ALL")
        return {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    std::uint64_t address = 0;
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(cs.size()) - 1; i >= 0; --i) {
        std::uint8_t val = 0;
        const char c = cs[static_cast<std::size_t>(i)];
        if (c == ' ') val = 0;
        else if (c >= 'A' && c <= 'Z') val = static_cast<std::uint8_t>(c - 'A' + 1);
        else if (c >= '0' && c <= '9') val = static_cast<std::uint8_t>(c - '0' + 27);
        else if (c == '-') val = 37;
        else if (c == '/') val = 38;
        else if (c == '.') val = 39;
        else throw std::runtime_error("callsign '" + callsign + "' contains invalid character");
        address = 40 * address + val;
    }

    std::array<std::uint8_t, EncodedCallsignLen> ret{};
    for (int i = 5; i >= 0; --i) {
        ret[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(address & 0xff);
        address /= 0x100;
    }
    return ret;
}

std::string decodeCallsign(const std::array<std::uint8_t, EncodedCallsignLen>& encoded)
{
    static const char* kChars = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

    std::uint64_t address = 0;
    for (int i = 0; i < 6; ++i)
        address = address * 0x100 + encoded[static_cast<std::size_t>(i)];

    if (address == 0xFFFFFFFFFFFFULL)
        return "@ALL";

    std::string cs;
    for (; address != 0; address /= 40)
        cs += kChars[address % 40];
    return cs;
}

// ---------------------------------------------------------------------------
// LSF (lsf.go)
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> LSF::toBytes() const
{
    std::vector<std::uint8_t> b;
    b.reserve(LSFLen);
    b.insert(b.end(), dst.begin(), dst.end());
    b.insert(b.end(), src.begin(), src.end());
    b.insert(b.end(), type.begin(), type.end());
    b.insert(b.end(), meta.begin(), meta.end());
    b.insert(b.end(), crc.begin(), crc.end());
    return b;
}

std::uint16_t LSF::calcCRC()
{
    const auto a = toBytes();
    const std::uint16_t crc = crc16(a.data(), LSFLen - CRCLen);
    this->crc[0] = static_cast<std::uint8_t>(crc >> 8);
    this->crc[1] = static_cast<std::uint8_t>(crc & 0xff);
    return crc;
}

bool LSF::checkCRC() const
{
    const auto a = toBytes();
    return crc16(a.data(), a.size()) == 0;
}

LSF LSF::newFromBytes(const std::uint8_t* buf)
{
    LSF lsf;
    std::memcpy(lsf.dst.data(), buf + 0, 6);
    std::memcpy(lsf.src.data(), buf + 6, 6);
    std::memcpy(lsf.type.data(), buf + 12, 2);
    std::memcpy(lsf.meta.data(), buf + 14, 14);
    std::memcpy(lsf.crc.data(), buf + 28, 2);
    return lsf;
}

std::string LSF::dstCallsign() const { return decodeCallsign(dst); }
std::string LSF::srcCallsign() const { return decodeCallsign(src); }

LSF makeLSF(const std::string& dst, const std::string& src, std::uint8_t type,
            std::uint8_t dataType, std::uint8_t can)
{
    LSF lsf;
    lsf.dst = encodeCallsign(dst);
    lsf.src = encodeCallsign(src);
    if (type == 0)
        dataType = 0;
    lsf.type[0] = can & 0x7;
    lsf.type[1] = (type & 0x1) | ((dataType & 0x3) << 1);
    lsf.calcCRC();
    return lsf;
}

// ---------------------------------------------------------------------------
// Soft bits / interleave / randomize / convolutional (codec.go)
// ---------------------------------------------------------------------------

std::vector<SoftBit> calcSoftbits(const std::vector<Symbol>& pld)
{
    if (pld.size() > SymbolsPerPayload)
        throw std::runtime_error("payload symbol count exceeds 184");

    std::vector<SoftBit> softBit(2 * SymbolsPerPayload);

    for (std::size_t i = 0; i < pld.size(); ++i) {
        const Symbol sym = pld[i];

        if (sym >= SymbolList[3]) {
            softBit[i * 2 + 1] = SoftTrue;
        } else if (sym >= SymbolList[2]) {
            softBit[i * 2 + 1] = static_cast<SoftBit>(
                -static_cast<double>(SoftTrue) / ((SymbolList[3] - SymbolList[2]) * SymbolList[2])
                + sym * static_cast<double>(SoftTrue) / (SymbolList[3] - SymbolList[2]));
        } else if (sym >= SymbolList[1]) {
            softBit[i * 2 + 1] = SoftFalse;
        } else if (sym >= SymbolList[0]) {
            softBit[i * 2 + 1] = static_cast<SoftBit>(
                static_cast<double>(SoftTrue) / ((SymbolList[1] - SymbolList[0]) * SymbolList[1])
                - sym * static_cast<double>(SoftTrue) / (SymbolList[1] - SymbolList[0]));
        } else {
            softBit[i * 2 + 1] = SoftTrue;
        }

        if (sym >= SymbolList[2]) {
            softBit[i * 2] = SoftFalse;
        } else if (sym >= SymbolList[1]) {
            softBit[i * 2] = static_cast<SoftBit>(
                SoftMaybe - sym * static_cast<double>(SoftTrue) / (SymbolList[2] - SymbolList[1]));
        } else {
            softBit[i * 2] = SoftTrue;
        }
    }
    return softBit;
}

std::vector<SoftBit> deinterleaveSoftBits(const std::vector<SoftBit>& softBits)
{
    std::vector<SoftBit> out(SymbolsPerPayload * 2);
    for (std::size_t i = 0; i < SymbolsPerPayload * 2; ++i)
        out[i] = softBits[interleaveSequence[i]];
    return out;
}

void derandomizeSoftBits(std::vector<SoftBit>& softBits)
{
    for (std::size_t i = 0; i < softBits.size(); ++i) {
        if ((randomizeSeq[i / 8] >> (7 - (i % 8))) & 1)
            softBits[i] = static_cast<SoftBit>(SoftTrue - softBits[i]);
    }
}

std::vector<Bit> convolutionalEncode(const std::vector<std::uint8_t>& in,
                                     const PuncturePattern& pp, std::uint8_t finalBit)
{
    if (in.empty())
        throw std::runtime_error("empty input not allowed");

    std::vector<std::uint8_t> unpackedBits(4, 0);  // 4 leading bits
    for (std::size_t i = 0; i < in.size(); ++i) {
        for (int j = 0; j < 8; ++j) {
            if (i < in.size() - 1 || j <= static_cast<int>(finalBit))
                unpackedBits.push_back((in[i] >> (7 - j)) & 1);
        }
    }
    for (int i = 0; i < 4; ++i)
        unpackedBits.push_back(0);

    int p = 0;
    const int ppLen = pp.empty() ? 1 : static_cast<int>(pp.size());
    std::vector<Bit> out;
    out.reserve(2 * unpackedBits.size());

    for (std::size_t i = 0; i + 4 < unpackedBits.size(); ++i) {
        if (pp.empty() || pp[static_cast<std::size_t>(p)]) {
            const std::uint8_t g2 = static_cast<std::uint8_t>(
                (unpackedBits[i + 4] + unpackedBits[i + 1] + unpackedBits[i + 0]) % 2);
            out.push_back(g2 != 0);
        }
        p = (p + 1) % ppLen;

        if (pp.empty() || pp[static_cast<std::size_t>(p)]) {
            const std::uint8_t g2 = static_cast<std::uint8_t>(
                (unpackedBits[i + 4] + unpackedBits[i + 3] + unpackedBits[i + 2] + unpackedBits[i + 0]) % 2);
            out.push_back(g2 != 0);
        }
        p = (p + 1) % ppLen;
    }
    return out;
}

std::vector<Bit> convolutionalEncodeStream(const std::vector<Bit>& lichBits,
                                           std::uint16_t frameNumber,
                                           const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> frame;
    frame.push_back(static_cast<std::uint8_t>(frameNumber >> 8));
    frame.push_back(static_cast<std::uint8_t>(frameNumber & 0xff));
    frame.insert(frame.end(), payload.begin(), payload.end());

    std::vector<Bit> bits = lichBits;
    const auto encoded = convolutionalEncode(frame, std::vector<bool>(StreamPuncturePattern.begin(), StreamPuncturePattern.end()), 7);
    bits.insert(bits.end(), encoded.begin(), encoded.end());
    return bits;
}

PayloadBits interleaveBits(const PayloadBits& in)
{
    PayloadBits out{};
    for (std::size_t i = 0; i < SymbolsPerPayload * 2; ++i)
        out[i] = in[interleaveSequence[i]];
    return out;
}

void randomizeBits(PayloadBits& bits)
{
    for (std::size_t i = 0; i < bits.size(); ++i) {
        if ((randomizeSeq[i / 8] >> (7 - (i % 8))) & 1)
            bits[i] = !bits[i];
    }
}

// ---------------------------------------------------------------------------
// TX symbol generation (codec.go / modem.go)
// ---------------------------------------------------------------------------

std::vector<Symbol> appendPreamble(std::vector<Symbol> out, bool bert)
{
    for (std::size_t i = 0; i < SymbolsPerFrame / 2; ++i) {
        if (bert) {
            out.push_back(-3.0F);
            out.push_back(+3.0F);
        } else {
            out.push_back(+3.0F);
            out.push_back(-3.0F);
        }
    }
    return out;
}

std::vector<Symbol> appendSyncwordSymbols(std::vector<Symbol> out, std::uint16_t syncword)
{
    for (int i = 0; i < static_cast<int>(SymbolsPerSyncword) * 2; i += 2)
        out.push_back(SymbolMap[(syncword >> (14 - i)) & 3]);
    return out;
}

std::vector<Symbol> appendBits(std::vector<Symbol> out, const PayloadBits& data)
{
    for (std::size_t i = 0; i < SymbolsPerPayload; ++i) {
        int d = 0;
        if (data[2 * i + 1]) d += 1;
        if (data[2 * i]) d += 2;
        out.push_back(SymbolMap[static_cast<std::size_t>(d)]);
    }
    return out;
}

std::vector<Symbol> appendEOT(std::vector<Symbol> out)
{
    for (std::size_t i = 0; i < SymbolsPerFrame; ++i)
        out.push_back(EOTSymbols[i % 8]);
    return out;
}

std::vector<Symbol> generateLSFSymbols(const LSF& lsf)
{
    std::vector<Symbol> syms = appendSyncwordSymbols({}, LSFSync);
    const auto b = convolutionalEncode(lsf.toBytes(),
                                       std::vector<bool>(LSFPuncturePattern.begin(), LSFPuncturePattern.end()),
                                       LSFFinalBit);
    PayloadBits encodedBits{};
    for (std::size_t i = 0; i < b.size() && i < encodedBits.size(); ++i)
        encodedBits[i] = b[i];
    PayloadBits rfBits = interleaveBits(encodedBits);
    randomizeBits(rfBits);
    return appendBits(std::move(syms), rfBits);
}

std::vector<Symbol> generateStreamSymbols(const LSF& lsf, std::uint16_t frameNumber,
                                          const std::vector<std::uint8_t>& payload)
{
    std::vector<Symbol> syms = appendSyncwordSymbols({}, StreamSync);
    const int lichCnt = static_cast<int>((frameNumber & 0x7fff) % 6);
    const auto lich = extractLICH(lichCnt, lsf);
    const auto encodedLICH = encodeLICH({lich[0], lich[1], lich[2], lich[3], lich[4], lich[5]});
    const std::vector<Bit> lichBits =
        unpackBits(std::vector<std::uint8_t>(encodedLICH.begin(), encodedLICH.end()));
    const auto b = convolutionalEncodeStream(lichBits, frameNumber, payload);

    PayloadBits encodedBits{};
    for (std::size_t i = 0; i < b.size() && i < encodedBits.size(); ++i)
        encodedBits[i] = b[i];
    PayloadBits rfBits = interleaveBits(encodedBits);
    randomizeBits(rfBits);
    return appendBits(std::move(syms), rfBits);
}

std::vector<Symbol> generatePacketSymbols(const LSF& lsf,
                                          const std::vector<std::uint8_t>& packetPayload)
{
    std::vector<Symbol> syms = appendPreamble({}, false);
    const std::vector<Symbol> lsfSymbols = generateLSFSymbols(lsf);
    syms.insert(syms.end(), lsfSymbols.begin(), lsfSymbols.end());

    constexpr std::size_t kChunkBytes = 25;
    std::size_t chunkCnt = 0;
    const std::vector<bool> pp(PacketPuncturePattern.begin(), PacketPuncturePattern.end());

    for (std::size_t offset = 0; offset < packetPayload.size(); offset += kChunkBytes) {
        syms = appendSyncwordSymbols(std::move(syms), PacketSync);

        std::array<std::uint8_t, kChunkBytes + 1> chunk{};
        const std::size_t bytesLeft = packetPayload.size() - offset;
        const std::size_t n = std::min(bytesLeft, kChunkBytes);
        std::copy_n(packetPayload.begin() + static_cast<std::ptrdiff_t>(offset),
                    n, chunk.begin());

        if (bytesLeft > kChunkBytes) {
            chunk[kChunkBytes] = static_cast<std::uint8_t>(chunkCnt << 2);
        } else {
            chunk[kChunkBytes] = static_cast<std::uint8_t>(
                (1u << 7) | (static_cast<unsigned int>(n) << 2));
        }

        const std::vector<std::uint8_t> chunkBytes(chunk.begin(), chunk.end());
        const std::vector<Bit> b =
            convolutionalEncode(chunkBytes, pp, PacketModeFinalBit);
        PayloadBits encodedBits{};
        for (std::size_t i = 0; i < b.size() && i < encodedBits.size(); ++i)
            encodedBits[i] = b[i];
        PayloadBits rfBits = interleaveBits(encodedBits);
        randomizeBits(rfBits);
        syms = appendBits(std::move(syms), rfBits);
        ++chunkCnt;
    }

    return appendEOT(std::move(syms));
}

// ---------------------------------------------------------------------------
// Golay(24,12) (golay.go)
// ---------------------------------------------------------------------------

std::uint32_t golayEncode24(std::uint16_t data)
{
    std::uint16_t checksum = 0;
    for (int i = 0; i < 12; ++i)
        if (data & (1u << i))
            checksum ^= golayEncodeMatrix[static_cast<std::size_t>(i)];
    return (static_cast<std::uint32_t>(data) << 12) | checksum;
}

std::uint16_t golaySoftDecode24(const std::array<SoftBit, 24>& codeword)
{
    std::array<SoftBit, 24> cw{};
    for (int i = 0; i < 24; ++i)
        cw[static_cast<std::size_t>(i)] = codeword[static_cast<std::size_t>(23 - i)];

    const std::uint32_t errors = golaySoftDetectErrors(cw);
    if (errors == 0xFFFFFFFF)
        return 0xFFFF;

    std::vector<SoftBit> low(cw.begin(), cw.begin() + 16);
    std::vector<SoftBit> high(cw.begin() + 16, cw.begin() + 24);
    const std::uint32_t receivedWord = softToInt(low) | (static_cast<std::uint32_t>(softToInt(high)) << 16);
    const std::uint32_t correctedWord = receivedWord ^ errors;
    return static_cast<std::uint16_t>((correctedWord >> 12) & 0x0FFF);
}

std::array<std::uint8_t, 6> decodeLICH(const std::vector<SoftBit>& inp, std::size_t offset)
{
    std::array<std::uint8_t, 6> outp{};

    for (int block = 0; block < 4; ++block) {
        std::array<SoftBit, 24> blockData{};
        std::copy_n(inp.begin() + static_cast<std::ptrdiff_t>(offset + static_cast<std::size_t>(block * 24)),
                    24, blockData.begin());

        std::uint16_t tmp = golaySoftDecode24(blockData);
        if (tmp == 0xFFFF) {
            std::uint16_t hardBits = 0;
            for (int i = 0; i < 24; ++i)
                if (blockData[static_cast<std::size_t>(23 - i)] > 0x7FFF)
                    hardBits |= static_cast<std::uint16_t>(1u << i);
            tmp = static_cast<std::uint16_t>((hardBits >> 12) & 0x0FFF);
        }

        switch (block) {
            case 0:
                outp[0] = static_cast<std::uint8_t>((tmp >> 4) & 0xFF);
                outp[1] |= static_cast<std::uint8_t>((tmp & 0xF) << 4);
                break;
            case 1:
                outp[1] |= static_cast<std::uint8_t>((tmp >> 8) & 0xF);
                outp[2] = static_cast<std::uint8_t>(tmp & 0xFF);
                break;
            case 2:
                outp[3] = static_cast<std::uint8_t>((tmp >> 4) & 0xFF);
                outp[4] |= static_cast<std::uint8_t>((tmp & 0xF) << 4);
                break;
            case 3:
                outp[4] |= static_cast<std::uint8_t>((tmp >> 8) & 0xF);
                outp[5] = static_cast<std::uint8_t>(tmp & 0xFF);
                break;
        }
    }
    return outp;
}

std::array<std::uint8_t, 12> encodeLICH(const std::array<std::uint8_t, 6>& inp)
{
    std::array<std::uint8_t, 12> outp{};

    std::uint32_t val = golayEncode24(static_cast<std::uint16_t>((inp[0] << 4) | (inp[1] >> 4)));
    outp[0] = static_cast<std::uint8_t>((val >> 16) & 0xFF);
    outp[1] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
    outp[2] = static_cast<std::uint8_t>(val & 0xFF);

    val = golayEncode24(static_cast<std::uint16_t>(((inp[1] & 0x0F) << 8) | inp[2]));
    outp[3] = static_cast<std::uint8_t>((val >> 16) & 0xFF);
    outp[4] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
    outp[5] = static_cast<std::uint8_t>(val & 0xFF);

    val = golayEncode24(static_cast<std::uint16_t>((inp[3] << 4) | (inp[4] >> 4)));
    outp[6] = static_cast<std::uint8_t>((val >> 16) & 0xFF);
    outp[7] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
    outp[8] = static_cast<std::uint8_t>(val & 0xFF);

    val = golayEncode24(static_cast<std::uint16_t>(((inp[4] & 0x0F) << 8) | inp[5]));
    outp[9] = static_cast<std::uint8_t>((val >> 16) & 0xFF);
    outp[10] = static_cast<std::uint8_t>((val >> 8) & 0xFF);
    outp[11] = static_cast<std::uint8_t>(val & 0xFF);

    return outp;
}

// ---------------------------------------------------------------------------
// Viterbi decoder (codec.go)
// ---------------------------------------------------------------------------

void ViterbiDecoder::init(int l)
{
    m_history.assign(static_cast<std::size_t>(l / 2 + l % 2), 0);
    m_prevMetrics.fill(0);
    m_currMetrics.fill(0);
    m_prevMetricsData.fill(0);
    m_currMetricsData.fill(0);
}

namespace {

std::uint32_t absDiff(SoftBit v1, SoftBit v2)
{
    return v1 > v2 ? v1 - v2 : v2 - v1;
}

}  // namespace

void ViterbiDecoder::decodeBit(SoftBit sb0, SoftBit sb1, int pos)
{
    for (int i = 0; i < ConvolutionStates / 2; ++i) {
        const std::uint32_t metric = absDiff(costTable0[static_cast<std::size_t>(i)], sb0)
                                     + absDiff(costTable1[static_cast<std::size_t>(i)], sb1);

        const std::uint32_t m0 = m_prevMetrics[static_cast<std::size_t>(i)] + metric;
        const std::uint32_t m1 = m_prevMetrics[static_cast<std::size_t>(i + ConvolutionStates / 2)]
                                 + (0x1FFFE - metric);
        const std::uint32_t m2 = m_prevMetrics[static_cast<std::size_t>(i)] + (0x1FFFE - metric);
        const std::uint32_t m3 = m_prevMetrics[static_cast<std::size_t>(i + ConvolutionStates / 2)] + metric;

        const int i0 = 2 * i;
        const int i1 = i0 + 1;

        if (m0 >= m1) {
            m_history[static_cast<std::size_t>(pos)] |= static_cast<std::uint16_t>(1u << i0);
            m_currMetrics[static_cast<std::size_t>(i0)] = m1;
        } else {
            m_history[static_cast<std::size_t>(pos)] &= static_cast<std::uint16_t>(~(1u << i0));
            m_currMetrics[static_cast<std::size_t>(i0)] = m0;
        }

        if (m2 >= m3) {
            m_history[static_cast<std::size_t>(pos)] |= static_cast<std::uint16_t>(1u << i1);
            m_currMetrics[static_cast<std::size_t>(i1)] = m3;
        } else {
            m_history[static_cast<std::size_t>(pos)] &= static_cast<std::uint16_t>(~(1u << i1));
            m_currMetrics[static_cast<std::size_t>(i1)] = m2;
        }
    }

    std::array<std::uint32_t, ConvolutionStates> tmp{};
    for (int i = 0; i < ConvolutionStates; ++i)
        tmp[static_cast<std::size_t>(i)] = m_currMetrics[static_cast<std::size_t>(i)];
    for (int i = 0; i < ConvolutionStates; ++i) {
        m_currMetrics[static_cast<std::size_t>(i)] = m_prevMetrics[static_cast<std::size_t>(i)];
        m_prevMetrics[static_cast<std::size_t>(i)] = tmp[static_cast<std::size_t>(i)];
    }
}

std::vector<std::uint8_t> ViterbiDecoder::chainback(int pos, int l, std::uint32_t& e)
{
    std::uint8_t state = 0;
    int bitPos = l + 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>((l - 1) / 8 + 1), 0);

    while (pos > 0) {
        bitPos--;
        pos--;
        const std::uint16_t bit = m_history[static_cast<std::size_t>(pos)]
                                   & static_cast<std::uint16_t>(1u << (state >> 4));
        state >>= 1;
        if (bit != 0 && bitPos / 8 < static_cast<int>(out.size())) {
            state |= 0x80;
            out[static_cast<std::size_t>(bitPos / 8)] |= static_cast<std::uint8_t>(1u << (7 - (bitPos % 8)));
        }
    }

    e = *std::min_element(m_prevMetrics.begin(), m_prevMetrics.end());
    return out;
}

void ViterbiDecoder::decode(const std::vector<SoftBit>& softBits,
                            std::vector<std::uint8_t>& out, std::uint32_t& e)
{
    init(static_cast<int>(softBits.size()));
    int pos = 0;
    for (std::size_t i = 0; i < softBits.size(); i += 2)
        decodeBit(softBits[i], softBits[i + 1], pos++);
    out = chainback(pos, static_cast<int>(softBits.size() / 2), e);
}

std::pair<std::vector<std::uint8_t>, int> ViterbiDecoder::decodePunctured(
    const std::vector<SoftBit>& puncturedSoftBits, const PuncturePattern& pp)
{
    std::vector<SoftBit> softBits(2 * puncturedSoftBits.size());
    int p = 0, u = 0, erased = 0;

    for (std::size_t i = 0; i < puncturedSoftBits.size();) {
        if (pp[static_cast<std::size_t>(p)]) {
            softBits[static_cast<std::size_t>(u)] = puncturedSoftBits[i];
            i++;
        } else {
            softBits[static_cast<std::size_t>(u)] = SoftMaybe;
            erased++;
        }
        u++;
        p = (p + 1) % static_cast<int>(pp.size());
    }
    if (u % 2 == 1) {
        softBits[static_cast<std::size_t>(u)] = SoftMaybe;
        erased++;
        u++;
    }
    softBits.resize(static_cast<std::size_t>(u));

    std::vector<std::uint8_t> out;
    std::uint32_t raw = 0;
    decode(softBits, out, raw);
    const double cost = std::round((static_cast<double>(raw) - static_cast<double>(erased) * SoftMaybe) / SoftTrue);
    return {out, std::max(static_cast<int>(cost), 0)};
}

// ---------------------------------------------------------------------------
// Frame decoder (decoder.go)
// ---------------------------------------------------------------------------

Decoder::Decoder(Callbacks callbacks) : m_cb(std::move(callbacks)) {}

std::pair<LSF, int> Decoder::decodeLSF(const std::vector<SoftBit>& softBits)
{
    std::vector<SoftBit> bits = softBits;
    derandomizeSoftBits(bits);
    const std::vector<SoftBit> dSoftBit = deinterleaveSoftBits(bits);

    ViterbiDecoder vd;
    auto [lsfBytes, cost] = vd.decodePunctured(dSoftBit,
                                               std::vector<bool>(LSFPuncturePattern.begin(), LSFPuncturePattern.end()));
    (void)cost;

    std::vector<std::uint8_t> lsf(lsfBytes.begin() + 1, lsfBytes.begin() + 1 + LSFLen);
    const int e = channelBitErrors(lsf,
                                   std::vector<bool>(LSFPuncturePattern.begin(), LSFPuncturePattern.end()),
                                   LSFFinalBit, dSoftBit);
    return {LSF::newFromBytes(lsf.data()), e};
}

Decoder::StreamFrame Decoder::decodeStreamFrame(const std::vector<SoftBit>& softBits)
{
    std::vector<SoftBit> bits = softBits;
    derandomizeSoftBits(bits);
    const std::vector<SoftBit> dSoftBit = deinterleaveSoftBits(bits);

    const std::array<std::uint8_t, 6> lich = decodeLICH(dSoftBit, 0);
    const std::uint8_t lichCnt = lich[5] >> 5;

    ViterbiDecoder vd;
    const std::vector<SoftBit> frameBits(dSoftBit.begin() + 96, dSoftBit.end());
    auto [frameData, cost] = vd.decodePunctured(frameBits,
                                                std::vector<bool>(StreamPuncturePattern.begin(), StreamPuncturePattern.end()));
    (void)cost;

    const int e = channelBitErrors(
        std::vector<std::uint8_t>(frameData.begin() + 1, frameData.begin() + 1 + 2 + 16),
        std::vector<bool>(StreamPuncturePattern.begin(), StreamPuncturePattern.end()),
        7, frameBits);

    const std::uint16_t fn = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(frameData[1]) << 8) | frameData[2]);
    std::vector<std::uint8_t> payload(frameData.begin() + 3, frameData.end());

    return {payload, std::vector<std::uint8_t>(lich.begin(), lich.end()), fn, lichCnt, e};
}

std::pair<std::vector<std::uint8_t>, int> Decoder::decodePacketFrame(const std::vector<SoftBit>& softBits)
{
    std::vector<SoftBit> bits = softBits;
    derandomizeSoftBits(bits);
    const std::vector<SoftBit> dSoftBit = deinterleaveSoftBits(bits);

    ViterbiDecoder vd;
    auto [pkt, cost] = vd.decodePunctured(dSoftBit,
                                          std::vector<bool>(PacketPuncturePattern.begin(), PacketPuncturePattern.end()));
    (void)cost;

    const int e = channelBitErrors(std::vector<std::uint8_t>(pkt.begin() + 1, pkt.begin() + 1 + 26),
                                   std::vector<bool>(PacketPuncturePattern.begin(), PacketPuncturePattern.end()),
                                   PacketModeFinalBit, dSoftBit);
    return {std::vector<std::uint8_t>(pkt.begin() + 1, pkt.end()), e};
}

int Decoder::channelBitErrors(const std::vector<std::uint8_t>& data, const PuncturePattern& pp,
                              std::uint8_t finalBit, const std::vector<SoftBit>& received)
{
    const auto sent = convolutionalEncode(data, pp, finalBit);
    int errs = 0;
    const std::size_t n = std::min(sent.size(), received.size());
    for (std::size_t i = 0; i < n; ++i)
        if (sent[i] != (received[i] > SoftMaybe))
            errs++;
    return errs;
}

void Decoder::lichLSF(const LSF& lsf, int e)
{
    bool newStream = m_syncedType != StreamSync || !m_gotLSF;
    if (!newStream && m_lsf && !(m_lsf->dst == lsf.dst && m_lsf->src == lsf.src && m_lsf->type == lsf.type)) {
        const double ber = static_cast<double>(m_errors - e) / std::max(m_bits - 272, 1) * 100.0;
        if (m_cb.onStreamEOT)
            m_cb.onStreamEOT(*m_lsf, m_streamID,
                             static_cast<std::uint16_t>(((m_lastStreamFN + 1) & 0x7fff) | 0x8000), ber);
        m_errors = e;
        m_bits = 272;
        newStream = true;
    }
    if (newStream) {
        m_syncedType = StreamSync;
        m_streamID = static_cast<std::uint16_t>(m_streamCounter++);
    }
    m_lsf = lsf;
    m_gotLSF = true;
    m_timeoutCnt = 0;
    if (m_cb.onStreamLICH)
        m_cb.onStreamLICH(*m_lsf, static_cast<double>(m_errors) / m_bits * 100.0);
}

void Decoder::decodeFrame(std::uint16_t typ, const std::vector<SoftBit>& softBits)
{
    // Any burst counts as activity for the LSF retention below.
    if (m_lastPacketLsfAge < kLsfRetentionFrames + 1)
        ++m_lastPacketLsfAge;

    if (typ == LSFSync) {
        // A valid LSF always starts a new transmission.  It must never be
        // ignored because the decoder still believes it is inside an earlier
        // stream or packet: a carrier that dropped without an EOT used to
        // swallow every following transmission, packets included.
        auto [lsf, e] = decodeLSF(softBits);
        if (!lsf.checkCRC()) {
            m_counters.lsfBadCRC++;
            if (m_cb.onLSFBadCRC)
                m_cb.onLSFBadCRC(lsf, static_cast<double>(e) / 3.68);
            return;
        }

        if (m_syncedType == StreamSync) {
            finishStream();
            m_counters.resync++;
        } else if (m_syncedType == PacketSync) {
            if (m_lastPacketFN != 0xff)
                dropFrame(typ, "packet abandoned: a new transmission started");
            m_counters.resync++;
        }

        m_lsf = lsf;
        m_gotLSF = true;
        m_timeoutCnt = 0;
        m_lastStreamFN = 0xffff;
        m_lastPacketFN = 0xff;
        m_errors = e;
        m_bits = 368;
        m_counters.lsf++;

        if ((lsf.type[1] & 1) == 1) {
            m_syncedType = StreamSync;
            m_lichParts = 0;
            m_streamFN = 0;
            m_streamID = static_cast<std::uint16_t>(m_streamCounter++);
            m_lastPacketLsf.reset();
        } else {
            m_syncedType = PacketSync;
            m_packetData.assign(33 * 25, 0);
            m_lastPacketLsf = lsf;
            m_lastPacketLsfAge = 0;
        }
        if (m_cb.onLSF)
            m_cb.onLSF(*m_lsf, static_cast<double>(e) / 3.68);
        return;
    }

    if (typ == PacketSync) {
        if (m_syncedType != PacketSync) {
            if (m_lastPacketLsf && m_lastPacketLsfAge <= kLsfRetentionFrames) {
                // The header of this transmission was decoded a moment ago but
                // the state was reset in between (a spurious end marker, or the
                // next attempt whose header was missed).  The frames still
                // belong to that header; the payload CRC decides afterwards.
                m_lsf = *m_lastPacketLsf;
                m_gotLSF = true;
                m_syncedType = PacketSync;
                m_packetData.assign(33 * 25, 0);
                m_lastPacketFN = 0xff;
                m_errors = 0;
                m_bits = 368;
                m_lastPacketLsfAge = 0;
                ++m_counters.reattached;
                if (m_cb.onPacketReattached)
                    m_cb.onPacketReattached(*m_lsf);
            } else {
                // No header at all: the frame cannot be routed anywhere.
                dropFrame(typ, "packet frame without a valid LSF");
                return;
            }
        }
        auto [pktFrame, e] = decodePacketFrame(softBits);
        m_errors += e;
        m_bits += 368;

        const bool lastFrame = (pktFrame[25] >> 7) != 0;
        std::uint8_t frameNumOrByteCnt = static_cast<std::uint8_t>((pktFrame[25] >> 2) & 0x1F);
        if (lastFrame && frameNumOrByteCnt > 25)
            frameNumOrByteCnt = 25;

        if (!lastFrame) {
            // A non-last frame carries its frame number, so a frame that
            // arrives out of order (or after a damaged one) still lands at
            // the right offset instead of being thrown away.  The old code
            // required frameNum == lastPacketFN + 1, which discarded every
            // frame of a packet with more than one frame.
            const std::size_t start = static_cast<std::size_t>(frameNumOrByteCnt) * 25;
            if (start + 25 > m_packetData.size()) {
                dropFrame(typ, "packet frame number out of range");
                return;
            }
            std::copy_n(pktFrame.begin(), 25,
                        m_packetData.begin() + static_cast<std::ptrdiff_t>(start));
            if (m_lastPacketFN == 0xff || frameNumOrByteCnt > m_lastPacketFN)
                m_lastPacketFN = frameNumOrByteCnt;
            m_counters.packetFrames++;
            return;
        }

        // Last frame: the field is the byte count of this final chunk, which
        // follows the highest frame number seen so far.
        const std::uint8_t nextFrame = (m_lastPacketFN == 0xff)
            ? 0
            : static_cast<std::uint8_t>(m_lastPacketFN + 1);
        const std::size_t start = static_cast<std::size_t>(nextFrame) * 25;
        if (start >= m_packetData.size()) {
            dropFrame(typ, "packet too long for the reassembly buffer");
            reset();
            return;
        }
        const std::size_t end = std::min(start + frameNumOrByteCnt, m_packetData.size());
        std::copy_n(pktFrame.begin(), end - start,
                    m_packetData.begin() + static_cast<std::ptrdiff_t>(start));
        m_packetData.resize(end);
        m_counters.packetFrames++;
        if (crc16(m_packetData.data(), m_packetData.size()) == 0) {
            m_counters.packets++;
            if (m_cb.onPacket)
                m_cb.onPacket(*m_lsf, m_packetData, ber());
        } else {
            m_counters.packetsBadCRC++;
            if (m_cb.onPacketBadCRC)
                m_cb.onPacketBadCRC(*m_lsf, m_packetData, ber());
        }
        reset();
        return;
    }

    if (typ == StreamSync) {
        if (m_syncedType == PacketSync) {
            // A stream frame in the middle of a packet transmission cannot be
            // used (no stream LSF is behind it) and must not disturb the
            // packet that is being reassembled.
            dropFrame(typ, "stream frame while a packet is open");
            return;
        }
        const StreamFrame f = decodeStreamFrame(softBits);
        const std::uint16_t fn7 = f.fn & 0x7fff;
        bool dropRepeat = fn7 == m_lastStreamFN;

        if (!dropRepeat) {
            m_errors += f.e;
            m_bits += 272;
            if (m_lichParts != 0x3F && f.lichCnt < 6) {
                std::copy(f.lich.begin(), f.lich.begin() + 5,
                          m_lsfBytes.begin() + static_cast<std::ptrdiff_t>(f.lichCnt * 5));
                m_lichParts |= (1 << f.lichCnt);
                if (m_lichParts == 0x3F) {
                    m_lichParts = 0;
                    const LSF lsfB = LSF::newFromBytes(m_lsfBytes.data());
                    if (lsfB.checkCRC())
                        lichLSF(lsfB, f.e);
                }
            }

            const bool lastFrame = (f.fn & 0x8000) == 0x8000 && fn7 == ((m_lastStreamFN + 1) & 0x7fff);
            if (m_gotLSF) {
                m_streamFN = f.fn;
                if (m_cb.onStream)
                    m_cb.onStream(*m_lsf, f.frameData, m_streamID, m_streamFN,
                                  static_cast<double>(m_errors) / m_bits * 100.0);
                m_timeoutCnt = 0;
                if (lastFrame && m_cb.onStreamEOT)
                    m_cb.onStreamEOT(*m_lsf, m_streamID, m_streamFN,
                                     static_cast<double>(m_errors) / m_bits * 100.0);
            } else {
                dropFrame(typ, "stream frame without a valid LSF");
            }
            if (lastFrame)
                reset();
            else
                m_lastStreamFN = f.fn;
        }
        return;
    }

    if (typ == EOTMarker) {
        if (m_syncedType == 0) {
            // Stray marker in noise: nothing is running, so there is nothing
            // to end and nothing to report.
            return;
        }
        if (m_syncedType == PacketSync && m_lastPacketFN != 0xff)
            dropFrame(typ, "packet transmission ended without its last frame");
        finishStream();
        if (m_cb.onEOT)
            m_cb.onEOT();
        reset();
        return;
    }

    if (m_syncedType != 0) {
        m_timeoutCnt++;
        if (m_timeoutCnt > 960 * 2) {
            if (m_syncedType == PacketSync && m_lastPacketFN != 0xff)
                dropFrame(typ, "packet timed out before its last frame");
            finishStream();
            reset();
        }
    }
}

double Decoder::ber() const
{
    return m_bits > 0 ? static_cast<double>(m_errors) / m_bits * 100.0 : 0.0;
}

void Decoder::finishStream()
{
    if (m_syncedType != StreamSync || !m_gotLSF || !m_lsf)
        return;
    m_streamFN = static_cast<std::uint16_t>((m_lastStreamFN + 1) | 0x8000);
    if (m_cb.onStreamEOT)
        m_cb.onStreamEOT(*m_lsf, m_streamID, m_streamFN, ber());
}

void Decoder::dropFrame(std::uint16_t typ, const char* reason)
{
    m_counters.framesDropped++;
    if (m_cb.onFrameDropped)
        m_cb.onFrameDropped(typ, reason);
}

void Decoder::expireRetention()
{
    m_lastPacketLsf.reset();
    m_lastPacketLsfAge = kLsfRetentionFrames + 1;
}

void Decoder::reset()
{
    m_syncedType = 0;
    m_lsf.reset();
    m_gotLSF = false;
    m_timeoutCnt = 0;
    m_lastPacketFN = 0xff;
    m_lastStreamFN = 0xffff;
    m_lichParts = 0;
    m_errors = 0;
    m_bits = 0;
}

}  // namespace sx1255::m17

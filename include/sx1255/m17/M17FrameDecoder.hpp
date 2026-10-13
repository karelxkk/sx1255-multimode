#pragma once

#include "sx1255/m17/M17Constants.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sx1255::m17 {

using Bit = bool;
using PayloadBits = std::array<Bit, BitsPerPayload>;
using PuncturePattern = std::vector<bool>;

// --- CRC (crc.go) ---
std::uint16_t crc16(const std::uint8_t* data, std::size_t len);

// --- Callsigns (encoding.go) ---
std::array<std::uint8_t, EncodedCallsignLen> encodeCallsign(const std::string& callsign);
std::string decodeCallsign(const std::array<std::uint8_t, EncodedCallsignLen>& encoded);

// --- Link Setup Frame (lsf.go) ---
struct LSF {
    std::array<std::uint8_t, EncodedCallsignLen> dst{};
    std::array<std::uint8_t, EncodedCallsignLen> src{};
    std::array<std::uint8_t, 2> type{};
    std::array<std::uint8_t, 14> meta{};
    std::array<std::uint8_t, CRCLen> crc{};

    std::vector<std::uint8_t> toBytes() const;
    std::uint16_t calcCRC();
    bool checkCRC() const;

    static LSF newFromBytes(const std::uint8_t* buf);
    std::string dstCallsign() const;
    std::string srcCallsign() const;
    std::uint8_t lsfType() const { return type[1] & 0x1; }
    std::uint8_t dataType() const { return (type[1] >> 1) & 0x3; }
};

// Port of m17.NewLSF.
LSF makeLSF(const std::string& dst, const std::string& src, std::uint8_t type, std::uint8_t dataType, std::uint8_t can);

// --- Symbol <-> soft bits (decoder.go / codec.go) ---
std::vector<SoftBit> calcSoftbits(const std::vector<Symbol>& pld);
std::vector<SoftBit> deinterleaveSoftBits(const std::vector<SoftBit>& softBits);
void derandomizeSoftBits(std::vector<SoftBit>& softBits);

// --- Convolutional code (codec.go) ---
std::vector<Bit> convolutionalEncode(const std::vector<std::uint8_t>& in,
                                     const PuncturePattern& pp, std::uint8_t finalBit);
std::vector<Bit> convolutionalEncodeStream(const std::vector<Bit>& lichBits,
                                           std::uint16_t frameNumber,
                                           const std::vector<std::uint8_t>& payload);

// --- Interleave / randomize (codec.go) ---
PayloadBits interleaveBits(const PayloadBits& in);
void randomizeBits(PayloadBits& bits);

// --- TX symbol generation (codec.go / modem.go) ---
std::vector<Symbol> appendPreamble(std::vector<Symbol> out, bool bert);
std::vector<Symbol> appendSyncwordSymbols(std::vector<Symbol> out, std::uint16_t syncword);
std::vector<Symbol> appendBits(std::vector<Symbol> out, const PayloadBits& data);
std::vector<Symbol> appendEOT(std::vector<Symbol> out);
std::vector<Symbol> generateLSFSymbols(const LSF& lsf);
std::vector<Symbol> generateStreamSymbols(const LSF& lsf, std::uint16_t frameNumber,
                                          const std::vector<std::uint8_t>& payload);

// --- Golay(24,12) (golay.go) ---
std::uint32_t golayEncode24(std::uint16_t data);
std::uint16_t golaySoftDecode24(const std::array<SoftBit, 24>& codeword);
std::array<std::uint8_t, 6> decodeLICH(const std::vector<SoftBit>& inp, std::size_t offset);
std::array<std::uint8_t, 12> encodeLICH(const std::array<std::uint8_t, 6>& inp);

// --- Viterbi decoder (codec.go) ---
class ViterbiDecoder {
public:
    // Returns (decoded bytes, path cost).
    std::pair<std::vector<std::uint8_t>, int>
    decodePunctured(const std::vector<SoftBit>& puncturedSoftBits, const PuncturePattern& pp);

private:
    void init(int l);
    void decode(const std::vector<SoftBit>& softBits, std::vector<std::uint8_t>& out, std::uint32_t& e);
    void decodeBit(SoftBit sb0, SoftBit sb1, int pos);
    std::vector<std::uint8_t> chainback(int pos, int l, std::uint32_t& e);

    std::vector<std::uint16_t> m_history;
    std::array<std::uint32_t, ConvolutionStates> m_prevMetrics{};
    std::array<std::uint32_t, ConvolutionStates> m_currMetrics{};
    std::array<std::uint32_t, ConvolutionStates> m_prevMetricsData{};
    std::array<std::uint32_t, ConvolutionStates> m_currMetricsData{};
};

// --- Frame decoder (decoder.go) ---
class Decoder {
public:
    struct Callbacks {
        std::function<void(const LSF&, double ber)> onLSF;
        std::function<void(const LSF&, const std::vector<std::uint8_t>& payload,
                           std::uint16_t sid, std::uint16_t fn, double ber)> onStream;
        std::function<void(const LSF&, double ber)> onStreamLICH;
        std::function<void(const LSF&, std::uint16_t sid, std::uint16_t fn, double ber)> onStreamEOT;
        std::function<void(const LSF&, const std::vector<std::uint8_t>& payload, double ber)> onPacket;
    };

    explicit Decoder(Callbacks callbacks);

    void decodeFrame(std::uint16_t typ, const std::vector<SoftBit>& softBits);
    void reset();

    bool gotLSF() const { return m_gotLSF; }
    const LSF* currentLSF() const { return m_lsf ? &m_lsf.value() : nullptr; }

private:
    std::pair<LSF, int> decodeLSF(const std::vector<SoftBit>& softBits);
    struct StreamFrame { std::vector<std::uint8_t> frameData; std::vector<std::uint8_t> lich; std::uint16_t fn; std::uint8_t lichCnt; int e; };
    StreamFrame decodeStreamFrame(const std::vector<SoftBit>& softBits);
    std::pair<std::vector<std::uint8_t>, int> decodePacketFrame(const std::vector<SoftBit>& softBits);
    int channelBitErrors(const std::vector<std::uint8_t>& data, const PuncturePattern& pp,
                         std::uint8_t finalBit, const std::vector<SoftBit>& received);
    void lichLSF(const LSF& lsf, int e);

    Callbacks m_cb;
    std::uint16_t m_syncedType = 0;
    std::optional<LSF> m_lsf;
    std::vector<std::uint8_t> m_frameData;
    std::vector<std::uint8_t> m_packetData;
    int m_timeoutCnt = 0;
    bool m_gotLSF = false;
    std::uint8_t m_lastPacketFN = 0xff;
    std::uint16_t m_lastStreamFN = 0xffff;
    int m_lichParts = 0;
    std::uint16_t m_streamID = 0;
    std::uint16_t m_streamFN = 0;
    std::array<std::uint8_t, 30> m_lsfBytes{};
    int m_errors = 0;
    int m_bits = 0;
    std::uint32_t m_streamCounter = 1;
};

}  // namespace sx1255::m17

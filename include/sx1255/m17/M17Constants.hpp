#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sx1255::m17 {

// 1:1 port of github.com/jancona/m17 constants (codec.go, decoder.go,
// golay.go, transform.go, modem/sx1255_alsa.go).  Keep the exact numeric
// values: several are empirically tuned for the SX1255 receive path.

using Symbol  = float;
using SoftBit = std::uint16_t;

inline constexpr std::size_t SymbolsPerSyncword = 8;
inline constexpr std::size_t SymbolsPerPayload  = 184;
inline constexpr std::size_t SymbolsPerFrame    = 192;
inline constexpr std::size_t BitsPerSymbol      = 2;
inline constexpr std::size_t BitsPerPayload     = SymbolsPerPayload * BitsPerSymbol;  // 368
inline constexpr std::size_t BytesPerFrame      = SymbolsPerFrame * BitsPerSymbol / 8; // 48

inline constexpr int PacketModeFinalBit = 5;
inline constexpr int LSFFinalBit        = 7;

inline constexpr int ConvolutionK      = 5;
inline constexpr int ConvolutionStates = 1 << (ConvolutionK - 1);  // 16

inline constexpr SoftBit SoftTrue  = 0xFFFF;
inline constexpr SoftBit SoftMaybe = SoftTrue / 2;
inline constexpr SoftBit SoftFalse = 0;

inline constexpr std::size_t CRCLen            = 2;
inline constexpr std::size_t LSFLen            = 30;
inline constexpr std::size_t EncodedCallsignLen = 6;

inline constexpr std::array<Symbol, 4> SymbolMap  = {+1, +3, -1, -3};
inline constexpr std::array<Symbol, 4> SymbolList = {-3, -1, +1, +3};

inline constexpr std::array<Symbol, 8> EOTSymbols = {+3, +3, +3, +3, +3, +3, -3, +3};

inline constexpr std::array<SoftBit, 8> costTable0 =
    {SoftFalse, SoftFalse, SoftFalse, SoftFalse, SoftTrue, SoftTrue, SoftTrue, SoftTrue};
inline constexpr std::array<SoftBit, 8> costTable1 =
    {SoftFalse, SoftTrue, SoftTrue, SoftFalse, SoftFalse, SoftTrue, SoftTrue, SoftFalse};

// Sync words
inline constexpr std::uint16_t LSFSync    = 0x55F7;
inline constexpr std::uint16_t StreamSync = 0xFF5D;
inline constexpr std::uint16_t PacketSync = 0x75FF;
inline constexpr std::uint16_t BERTSync   = 0xDF55;
inline constexpr std::uint16_t EOTMarker  = 0x555D;

inline constexpr std::array<Symbol, 8> LSFPreambleSymbols = {+3, -3, +3, -3, +3, -3, +3, -3};
inline constexpr std::array<Symbol, 8> LSFSyncSymbols      = {+3, +3, +3, +3, -3, -3, +3, -3};  // 0x55F7
inline constexpr std::array<Symbol, 8> StreamSyncSymbols   = {-3, -3, -3, -3, +3, +3, -3, +3};  // 0xFF5D
inline constexpr std::array<Symbol, 8> PacketSyncSymbols   = {+3, -3, +3, +3, -3, -3, -3, -3};  // 0x75FF
inline constexpr std::array<Symbol, 8> BERTSyncSymbols     = {-3, +3, -3, -3, +3, +3, +3, +3};  // 0xDF55
inline constexpr std::array<Symbol, 8> EOTMarkerSymbols    = {+3, +3, +3, +3, +3, +3, -3, +3};  // 0x555D

// ExtLSFSyncSymbols = LSFPreambleSymbols ++ LSFSyncSymbols
inline constexpr std::array<Symbol, 16> ExtLSFSyncSymbols = {
    +3, -3, +3, -3, +3, -3, +3, -3,
    +3, +3, +3, +3, -3, -3, +3, -3,
};

// Puncture patterns (Bit == bool)
// LSFPuncturePattern = 7 x 11011101 + 11011
inline constexpr std::array<bool, 61> LSFPuncturePattern = [] {
    std::array<bool, 61> p{};
    const bool byte[8] = {true, true, false, true, true, true, false, true};  // 11011101
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 8; ++j)
            p[i * 8 + j] = byte[j];
    const bool tail[5] = {true, true, false, true, true};  // 11011
    for (int j = 0; j < 5; ++j)
        p[56 + j] = tail[j];
    return p;
}();

inline constexpr std::array<bool, 12> StreamPuncturePattern =
    {true, true, true, true, true, true, true, true, true, true, true, false};

inline constexpr std::array<bool, 8> PacketPuncturePattern =
    {true, true, true, true, true, true, true, false};

// Scrambler / randomizer sequence
inline constexpr std::array<std::uint8_t, 46> randomizeSeq = {
    0xD6, 0xB5, 0xE2, 0x30, 0x82, 0xFF, 0x84, 0x62, 0xBA, 0x4E,
    0x96, 0x90, 0xD8, 0x98, 0xDD, 0x5D, 0x0C, 0xC8, 0x52, 0x43,
    0x91, 0x1D, 0xF8, 0x6E, 0x68, 0x2F, 0x35, 0xDA, 0x14, 0xEA,
    0xCD, 0x76, 0x19, 0x8D, 0xD5, 0x80, 0xD1, 0x33, 0x87, 0x13,
    0x57, 0x18, 0x2D, 0x29, 0x78, 0xC3,
};

// Block interleaver sequence (368 entries).
inline constexpr std::array<std::uint16_t, 368> interleaveSequence = {
    0, 137, 90, 227, 180, 317, 270, 39, 360, 129, 82, 219, 172, 309, 262, 31,
    352, 121, 74, 211, 164, 301, 254, 23, 344, 113, 66, 203, 156, 293, 246, 15,
    336, 105, 58, 195, 148, 285, 238, 7, 328, 97, 50, 187, 140, 277, 230, 367,
    320, 89, 42, 179, 132, 269, 222, 359, 312, 81, 34, 171, 124, 261, 214, 351,
    304, 73, 26, 163, 116, 253, 206, 343, 296, 65, 18, 155, 108, 245, 198, 335,
    288, 57, 10, 147, 100, 237, 190, 327, 280, 49, 2, 139, 92, 229, 182, 319,
    272, 41, 362, 131, 84, 221, 174, 311, 264, 33, 354, 123, 76, 213, 166, 303,
    256, 25, 346, 115, 68, 205, 158, 295, 248, 17, 338, 107, 60, 197, 150, 287,
    240, 9, 330, 99, 52, 189, 142, 279, 232, 1, 322, 91, 44, 181, 134, 271,
    224, 361, 314, 83, 36, 173, 126, 263, 216, 353, 306, 75, 28, 165, 118, 255,
    208, 345, 298, 67, 20, 157, 110, 247, 200, 337, 290, 59, 12, 149, 102, 239,
    192, 329, 282, 51, 4, 141, 94, 231, 184, 321, 274, 43, 364, 133, 86, 223,
    176, 313, 266, 35, 356, 125, 78, 215, 168, 305, 258, 27, 348, 117, 70, 207,
    160, 297, 250, 19, 340, 109, 62, 199, 152, 289, 242, 11, 332, 101, 54, 191,
    144, 281, 234, 3, 324, 93, 46, 183, 136, 273, 226, 363, 316, 85, 38, 175,
    128, 265, 218, 355, 308, 77, 30, 167, 120, 257, 210, 347, 300, 69, 22, 159,
    112, 249, 202, 339, 292, 61, 14, 151, 104, 241, 194, 331, 284, 53, 6, 143,
    96, 233, 186, 323, 276, 45, 366, 135, 88, 225, 178, 315, 268, 37, 358, 127,
    80, 217, 170, 307, 260, 29, 350, 119, 72, 209, 162, 299, 252, 21, 342, 111,
    64, 201, 154, 291, 244, 13, 334, 103, 56, 193, 146, 283, 236, 5, 326, 95,
    48, 185, 138, 275, 228, 365, 318, 87, 40, 177, 130, 267, 220, 357, 310, 79,
    32, 169, 122, 259, 212, 349, 302, 71, 24, 161, 114, 251, 204, 341, 294, 63,
    16, 153, 106, 243, 196, 333, 286, 55, 8, 145, 98, 235, 188, 325, 278, 47,
};

// RRC taps: alpha=0.5, span=8, sps=5, gain=sqrt(sps)
inline constexpr std::array<double, 41> rrcTaps5 = {
    -0.004519384154389, -0.002744505321971, 0.002187793653660, 0.006734308458208,
    0.006823188093192, 0.001355815246317, -0.005994389201970, -0.008697733303330,
    -0.002410076268276, 0.010204314627992, 0.018981413448435, 0.011949415510291,
    -0.015182045838927, -0.051615756197679, -0.072094910038768, -0.047453533621088,
    0.039168634270669, 0.179164496628150, 0.336694345124862, 0.461088271869920,
    0.508340710642860, 0.461088271869920, 0.336694345124862, 0.179164496628150,
    0.039168634270669, -0.047453533621088, -0.072094910038768, -0.051615756197679,
    -0.015182045838927, 0.011949415510291, 0.018981413448435, 0.010204314627992,
    -0.002410076268276, -0.008697733303330, -0.005994389201970, 0.001355815246317,
    0.006823188093192, 0.006734308458208, 0.002187793653660, -0.002744505321971,
    -0.004519384154389,
};

inline constexpr double transmitGain = std::sqrt(5.0);

// SX1255 receive path tuning constants (must match m17-gateway exactly).
inline constexpr int    sampleRateSX1255          = 125000;
inline constexpr int    rxDecimSX1255             = 5;
inline constexpr double rxScalingCoeffSX1255      = 2.48;
inline constexpr int    basebandDCAvgCntSX1255    = 4000;
inline constexpr double rxFastNSX1255             = 25.0;
inline constexpr double rxSlowNSX1255             = 2500.0;
inline constexpr double carrierRiseSX1255         = 10.0;
inline constexpr double carrierFallSX1255         = 2.0;
inline constexpr double channelBWSX1255           = 12500.0;
inline constexpr int    channelTapsSX1255        = 200;

// Golay(24,12) encoding matrix (golay.go).
inline constexpr std::array<std::uint16_t, 12> golayEncodeMatrix = {
    0x8eb, 0x93e, 0xa97, 0xdc6, 0x367, 0x6cd,
    0xd99, 0x3da, 0x7b4, 0xf68, 0x63b, 0xc75,
};

// M17 CRC-16 polynomial.
inline constexpr std::uint16_t crcPoly = 0x5935;

}  // namespace sx1255::m17

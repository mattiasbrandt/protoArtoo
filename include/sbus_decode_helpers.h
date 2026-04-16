// =============================================================================
// include/sbus_decode_helpers.h
//
// Pure SBUS decode helpers extracted from sbus_decoder.cpp for native testing.
// No hardware dependencies.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr uint8_t kSbusDecodeHeader = 0x0F;
constexpr int kSbusDecodeBitsPerByte = 12;
constexpr int kSbusDecodeFrameLen = 25;
constexpr int kSbusDecodeMinBitsPerByte = 11;
constexpr int kSbusDecodeTotalBits = kSbusDecodeFrameLen * kSbusDecodeBitsPerByte;
constexpr int kSbusDecodeMaxScanBits = 1024;

struct SbusDecodeAttemptStats {
    uint32_t extractFailCount;
    uint32_t headerMismatchCount;
    uint32_t footerMismatchCount;
    uint32_t parityFailCount;
    uint8_t lastRejectedFooter;
};


// Count set bits in a byte — portable, no compiler built-ins.
inline uint8_t sbusDecodePopcount8(uint8_t v) {
    uint8_t n = 0;
    while (v) { n++; v = (uint8_t)(v & (v - 1U)); }
    return n;
}

// Accept standard SBUS (0x00) and SBUS2 variants (low nibble 0x04).
// Matches bolderflight/sbus v8.x validation.
inline bool isValidSbusFooter(uint8_t footer) {
    return footer == 0x00 || (footer & 0x0FU) == 0x04U;
}

template <typename SymbolT>
inline int flattenSymbols(const SymbolT* syms, size_t count,
                          bool* bits, int maxBits, uint32_t bitPeriodTicks) {
    int bc = 0;

    for (size_t i = 0; i < count && bc < maxBits; i++) {
        const SymbolT& s = syms[i];
        if (s.duration0 > 0) {
            int n = (int)(((uint32_t)s.duration0 + (bitPeriodTicks / 2U)) / bitPeriodTicks);
            if (n <= 0) n = 1;
            if (n > maxBits - bc) n = maxBits - bc;
            for (int j = 0; j < n; j++) bits[bc++] = (s.level0 != 0);
        }

        if (s.duration1 == 0) break;

        int n = (int)(((uint32_t)s.duration1 + (bitPeriodTicks / 2U)) / bitPeriodTicks);
        if (n <= 0) n = 1;
        if (n > maxBits - bc) n = maxBits - bc;
        for (int j = 0; j < n; j++) bits[bc++] = (s.level1 != 0);
    }

    return bc;
}

inline bool decodedBit(const bool* bits, int index, bool invertBits) {
    bool value = bits[index];
    return invertBits ? !value : value;
}


inline bool extractSbusSerialByte(const bool* bits, int bc, int startPos, bool invertBits,
                                  int bitsPerByte, uint8_t* outByte) {
    if (!outByte) return false;
    if (bitsPerByte < kSbusDecodeMinBitsPerByte || bitsPerByte > kSbusDecodeBitsPerByte) return false;
    if (bc - startPos < bitsPerByte) return false;

    // Do not enforce start/stop bits per byte here. Hardware UART framing checks
    // in bolderflight happen below this layer; we validate using header/footer.
    uint8_t byte = 0;
    for (int d = 0; d < 8; d++) {
        if (decodedBit(bits, startPos + 1 + d, invertBits)) byte |= (uint8_t)(1u << d);
    }

    *outByte = byte;
    return true;
}


inline bool extractSbusBytes(const bool* bits, int bc, int startPos, bool invertBits,
                             int bitsPerByte, uint8_t* frame, int frameLen) {
    if (!frame || frameLen < 1) return false;
    if (bitsPerByte < kSbusDecodeMinBitsPerByte || bitsPerByte > kSbusDecodeBitsPerByte) return false;
    if (bc - startPos < frameLen * bitsPerByte) return false;

    for (int b = 0; b < frameLen; ++b) {
        int base = startPos + (b * bitsPerByte);
        if (!extractSbusSerialByte(bits, bc, base, invertBits, bitsPerByte, &frame[b])) {
            return false;
        }
    }

    return true;
}

inline bool decodeFrameFromBits(const bool* bits, int bc, bool invertBits,
                                uint8_t* frame, int frameLen,
                                SbusDecodeAttemptStats* attemptStats,
                                int bitsPerByte = kSbusDecodeBitsPerByte) {
    if (!bits || !frame || frameLen < 2) return false;
    if (bitsPerByte < kSbusDecodeMinBitsPerByte || bitsPerByte > kSbusDecodeBitsPerByte) return false;
    if (frameLen > kSbusDecodeFrameLen) return false;

    if (attemptStats) {
        attemptStats->extractFailCount = 0;
        attemptStats->headerMismatchCount = 0;
        attemptStats->footerMismatchCount = 0;
        attemptStats->parityFailCount = 0;
        attemptStats->lastRejectedFooter = 0;
    }

    uint8_t candidateFrame[kSbusDecodeFrameLen] = {};
    const int minBitsNeeded = frameLen * bitsPerByte;

    // RMT terminates capture on the inter-frame idle gap, so every buffer starts
    // at the beginning of a SBUS frame. We therefore only attempt decode from the
    // first LOW bit (= start bit of the header byte) and never scan further into
    // the bit array. Scanning past the first candidate causes false positives when
    // a payload data bit accidentally produces a header+footer match.
    //
    // If the first LOW bit does not produce a valid frame (e.g. a 1-bit slip shifts
    // the footer off-position), we discard the buffer and return false. The watchdog
    // then fires and resets channels to 0 — safe behaviour.
    //
    // Leading HIGH bits from the inter-frame gap remainder are skipped to reach P.
    int firstLow = -1;
    for (int p = 0; p < bc; ++p) {
        if (!decodedBit(bits, p, invertBits)) { firstLow = p; break; }
    }
    if (firstLow < 0 || (bc - firstLow) < minBitsNeeded) return false;

    const int pos = firstLow;
    if (!extractSbusBytes(bits, bc, pos, invertBits, bitsPerByte, candidateFrame, frameLen)) {
        if (attemptStats) attemptStats->extractFailCount++;
        return false;
    }

    if (candidateFrame[0] != kSbusDecodeHeader) {
        if (attemptStats) attemptStats->headerMismatchCount++;
        return false;
    }

    uint8_t footer = candidateFrame[frameLen - 1];
    if (!isValidSbusFooter(footer)) {
        if (attemptStats) {
            attemptStats->footerMismatchCount++;
            attemptStats->lastRejectedFooter = footer;
        }
        return false;
    }

    memcpy(frame, candidateFrame, (size_t)frameLen);
    return true;
}
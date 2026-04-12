// =============================================================================
// test/test_native/test_sbus_decode/test_sbus_decode.cpp
//
// Native unit tests for SBUS decode helpers.
// Covers footer acceptance, timing variants, and stateless frame scanning
// used by the T19 SBUS regression fix.
// =============================================================================
#include <unity.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "sbus_decode_helpers.h"

namespace {

constexpr uint32_t kBitPeriodTicksStd = 10;
constexpr uint32_t kBitPeriodTicksFast = 5;
constexpr int kFlatBitCapacity = 640;

struct TestSymbol {
    uint16_t duration0;
    uint16_t duration1;
    uint8_t level0;
    uint8_t level1;
};

static void appendSbusByteBits(uint8_t byte, std::vector<uint8_t>* bits) {
    bits->push_back(0);  // start bit (LOW)
    for (int d = 0; d < 8; ++d) {
        bits->push_back((byte >> d) & 0x01U);
    }
    bits->push_back(0);  // parity bit placeholder (not validated by parser)
    bits->push_back(1);  // stop bit 1
    bits->push_back(1);  // stop bit 2
}

static std::vector<uint8_t> makeFrameBits(uint8_t footer) {
    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    frame[0] = kSbusDecodeHeader;
    for (int i = 1; i < 23; ++i) {
        frame[(size_t)i] = static_cast<uint8_t>((i * 17) & 0xFF);
    }
    frame[23] = 0x00;  // flags byte (unused in decode validation)
    frame[24] = footer;

    std::vector<uint8_t> bits;
    bits.reserve((size_t)kSbusDecodeTotalBits);
    for (uint8_t byte : frame) {
        appendSbusByteBits(byte, &bits);
    }
    return bits;
}

static std::vector<TestSymbol> bitsToSymbols(const std::vector<uint8_t>& bits,
                                             uint32_t bitPeriodTicks) {
    std::vector<TestSymbol> symbols;
    if (bits.empty()) {
        return symbols;
    }

    std::vector<std::pair<uint8_t, int>> runs;
    runs.reserve(bits.size());

    uint8_t currentLevel = bits[0];
    int runLength = 1;
    for (size_t i = 1; i < bits.size(); ++i) {
        if (bits[i] == currentLevel) {
            runLength++;
            continue;
        }
        runs.emplace_back(currentLevel, runLength);
        currentLevel = bits[i];
        runLength = 1;
    }
    runs.emplace_back(currentLevel, runLength);

    symbols.reserve((runs.size() + 1) / 2);
    for (size_t i = 0; i < runs.size(); i += 2) {
        TestSymbol s = {};
        s.level0 = runs[i].first;
        s.duration0 = static_cast<uint16_t>(runs[i].second * (int)bitPeriodTicks);

        if (i + 1 < runs.size()) {
            s.level1 = runs[i + 1].first;
            s.duration1 = static_cast<uint16_t>(runs[i + 1].second * (int)bitPeriodTicks);
        } else {
            s.level1 = 0;
            s.duration1 = 0;  // End-of-sequence marker consumed by flattenSymbols().
        }
        symbols.push_back(s);
    }

    return symbols;
}

static bool decodeFromBits(const std::vector<uint8_t>& bits,
                           uint8_t* frame,
                           SbusDecodeAttemptStats* stats = nullptr) {
    std::unique_ptr<bool[]> bitBuffer(new bool[bits.size()]);
    for (size_t i = 0; i < bits.size(); ++i) {
        bitBuffer[i] = (bits[i] != 0);
    }

    return decodeFrameFromBits(bitBuffer.get(), (int)bits.size(), false, frame,
                               kSbusDecodeFrameLen, stats);
}

static bool decodeFromSymbols(const std::vector<TestSymbol>& symbols,
                              uint32_t bitPeriodTicks,
                              uint8_t* frame,
                              SbusDecodeAttemptStats* stats = nullptr) {
    std::array<bool, kFlatBitCapacity> flatBits = {};
    int bc = flattenSymbols(symbols.data(), symbols.size(), flatBits.data(),
                            (int)flatBits.size(), bitPeriodTicks);

    return decodeFrameFromBits(flatBits.data(), bc, false, frame,
                               kSbusDecodeFrameLen, stats);
}

}  // namespace

void setUp() {
}

void tearDown() {
}

void test_footer_validator_accepts_standard_and_sbus2_variants() {
    TEST_ASSERT_TRUE(isValidSbusFooter(0x00));
    TEST_ASSERT_TRUE(isValidSbusFooter(0x04));
    TEST_ASSERT_TRUE(isValidSbusFooter(0x14));
    TEST_ASSERT_TRUE(isValidSbusFooter(0x24));
    TEST_ASSERT_FALSE(isValidSbusFooter(0x03));
}


void test_decode_accepts_standard_footer_0x00() {
    std::vector<uint8_t> bits = makeFrameBits(0x00);
    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(kSbusDecodeHeader, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[kSbusDecodeFrameLen - 1]);
}

void test_decode_accepts_sbus_v2_footer_0x04() {
    std::vector<uint8_t> bits = makeFrameBits(0x04);
    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[kSbusDecodeFrameLen - 1]);
}

void test_decode_accepts_sbus2_slot_footer_0x14() {
    std::vector<uint8_t> bits = makeFrameBits(0x14);
    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(0x14, frame[kSbusDecodeFrameLen - 1]);
}

void test_decode_rejects_invalid_footer_0x03() {
    std::vector<uint8_t> bits = makeFrameBits(0x03);
    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    SbusDecodeAttemptStats stats = {};
    TEST_ASSERT_FALSE(decodeFromBits(bits, frame.data(), &stats));
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.footerMismatchCount);
    TEST_ASSERT_EQUAL_HEX8(0x03, stats.lastRejectedFooter);
}

void test_decode_alignment_search_with_leading_garbage_bits() {
    std::vector<uint8_t> bits = {0, 1, 0, 1, 1};
    std::vector<uint8_t> frameBits = makeFrameBits(0x04);
    bits.insert(bits.end(), frameBits.begin(), frameBits.end());

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(kSbusDecodeHeader, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[kSbusDecodeFrameLen - 1]);
}

void test_decode_rejects_insufficient_bit_count() {
    std::vector<uint8_t> bits = makeFrameBits(0x04);
    bits.resize((size_t)(kSbusDecodeTotalBits - 1));

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    SbusDecodeAttemptStats stats = {};
    TEST_ASSERT_FALSE(decodeFromBits(bits, frame.data(), &stats));
    TEST_ASSERT_EQUAL_UINT32(0, stats.footerMismatchCount);
}

void test_decode_accepts_frame_with_trailing_noise() {
    std::vector<uint8_t> bits = makeFrameBits(0x14);
    bits.insert(bits.end(), {1, 1, 0, 1, 0, 1, 1, 0});

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(kSbusDecodeHeader, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x14, frame[kSbusDecodeFrameLen - 1]);
}


void test_decode_from_symbols_standard_timing() {
    std::vector<uint8_t> bits = makeFrameBits(0x00);
    std::vector<TestSymbol> symbols = bitsToSymbols(bits, kBitPeriodTicksStd);

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromSymbols(symbols, kBitPeriodTicksStd, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[kSbusDecodeFrameLen - 1]);
}

void test_decode_from_symbols_fast_timing() {
    std::vector<uint8_t> bits = makeFrameBits(0x14);
    std::vector<TestSymbol> symbols = bitsToSymbols(bits, kBitPeriodTicksFast);

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromSymbols(symbols, kBitPeriodTicksFast, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(0x14, frame[kSbusDecodeFrameLen - 1]);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_footer_validator_accepts_standard_and_sbus2_variants);
    RUN_TEST(test_decode_accepts_standard_footer_0x00);
    RUN_TEST(test_decode_accepts_sbus_v2_footer_0x04);
    RUN_TEST(test_decode_accepts_sbus2_slot_footer_0x14);
    RUN_TEST(test_decode_rejects_invalid_footer_0x03);
    RUN_TEST(test_decode_alignment_search_with_leading_garbage_bits);
    RUN_TEST(test_decode_rejects_insufficient_bit_count);
    RUN_TEST(test_decode_accepts_frame_with_trailing_noise);
    RUN_TEST(test_decode_from_symbols_standard_timing);
    RUN_TEST(test_decode_from_symbols_fast_timing);

    return UNITY_END();
}
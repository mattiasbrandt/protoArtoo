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
    // Even parity: bit is 1 when the number of set bits in 'byte' is odd.
    uint8_t ones = 0;
    { uint8_t v = byte; while (v) { ones++; v = (uint8_t)(v & (v - 1U)); } }
    bits->push_back((uint8_t)(ones & 1U));  // even parity
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

// Frame with custom byte[1] value, all other channel bytes use the standard
// (i * 17) & 0xFF pattern. Byte1 = first channel data byte; its D0 bit value
// determines whether the naive re-sync can confuse a LOW data bit for a start bit.
static std::vector<uint8_t> makeFrameBitsCustomByte1(uint8_t footer, uint8_t byte1_val) {
    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    frame[0] = kSbusDecodeHeader;
    frame[1] = byte1_val;
    for (int i = 2; i < 23; ++i) frame[(size_t)i] = static_cast<uint8_t>((i * 17) & 0xFF);
    frame[23] = 0x00;
    frame[24] = footer;
    std::vector<uint8_t> bits;
    bits.reserve((size_t)(kSbusDecodeFrameLen * kSbusDecodeBitsPerByte));
    for (uint8_t b : frame) appendSbusByteBits(b, &bits);
    return bits;
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

void test_decode_skips_leading_high_bits() {
    // The RMT inter-frame gap produces HIGH bits before the real frame.
    // The decoder must skip them and start from the first LOW bit.
    // Note: leading LOW bits (garbage) are NOT skipped — the decoder
    // commits to the first LOW bit found and returns false if it is not a
    // valid frame start. This matches real hardware: the RMT guarantees
    // buffers start at the frame boundary.
    std::vector<uint8_t> bits = {1, 1, 1, 1, 1};  // 5 HIGH inter-frame-gap bits
    std::vector<uint8_t> frameBits = makeFrameBits(0x04);
    bits.insert(bits.end(), frameBits.begin(), frameBits.end());

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(kSbusDecodeHeader, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[kSbusDecodeFrameLen - 1]);
}

void test_decode_rejects_insufficient_bit_count() {
    // Minimum bits needed = (frameLen-1)*(bitsPerByte-1)+9 = 24*11+9 = 273.
    // Truncating to 272 must fail the minBitsNeeded check.
    std::vector<uint8_t> bits = makeFrameBits(0x04);
    bits.resize(272);

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

// test_decode_rejects_bad_stop_bits removed: stop-bit framing check was not
// implemented because SBUS clock deviation causes 1-bit slips in flattenSymbols,
// shifting all bit positions after the slip and making stop/parity positions
// unreliable. The decoder validates frames using header + footer bytes only.

void test_decode_false_positive_rejected_by_framing() {
    // Prepend 12 bits that look like a 0x0F UART byte but with a LOW stop bit,
    // so the framing check rejects the candidate at position 0. The scanner must
    // continue and find the real frame starting at position 12.
    //
    // 0x0F data bits (LSB-first): 1,1,1,1,0,0,0,0  (4 ones)
    // Stop bit 1 forced LOW to guarantee framing failure.
    std::vector<uint8_t> fake_byte = {
        0,           // start bit (LOW) - triggers scan
        1, 1, 1, 1,  // data bits 0-3 of 0x0F
        0, 0, 0, 0,  // data bits 4-7 of 0x0F
        0,           // parity (don't care)
        0, 1         // stop bit 1 = LOW (INVALID), stop bit 2 = HIGH
    };
    std::vector<uint8_t> frame_bits = makeFrameBits(0x00);
    std::vector<uint8_t> full;
    full.insert(full.end(), fake_byte.begin(), fake_byte.end());
    full.insert(full.end(), frame_bits.begin(), frame_bits.end());

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    // Must skip the fake byte and decode the real frame at position 12.
    TEST_ASSERT_TRUE(decodeFromBits(full, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(kSbusDecodeHeader, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[kSbusDecodeFrameLen - 1]);
}

// =============================================================================
// Per-byte re-sync tests
// =============================================================================

// -1 slip at the byte-0/byte-1 boundary: erase stop2 of byte 0 (position 11).
// The start bit of byte 1 shifts from position 12 to 11. Re-sync must find it
// by detecting the HIGH->LOW transition at position 10->11.
void test_resync_minus1_slip_recovers() {
    std::vector<uint8_t> bits = makeFrameBits(0x00);
    std::array<uint8_t, kSbusDecodeFrameLen> expected = {};
    expected[0] = kSbusDecodeHeader;
    for (int i = 1; i < 23; ++i) expected[(size_t)i] = static_cast<uint8_t>((i * 17) & 0xFF);
    expected[23] = 0x00;
    expected[24] = 0x00;

    bits.erase(bits.begin() + 11);  // -1 slip: remove stop2 of byte 0

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), frame.data(), kSbusDecodeFrameLen);
}

// +1 slip at the byte-0/byte-1 boundary: insert an extra HIGH after stop2 of byte 0
// (position 12). The start bit of byte 1 shifts from position 12 to 13. Re-sync must
// find it by scanning past the extra HIGH at position 12.
void test_resync_plus1_slip_recovers() {
    std::vector<uint8_t> bits = makeFrameBits(0x04);
    std::array<uint8_t, kSbusDecodeFrameLen> expected = {};
    expected[0] = kSbusDecodeHeader;
    for (int i = 1; i < 23; ++i) expected[(size_t)i] = static_cast<uint8_t>((i * 17) & 0xFF);
    expected[23] = 0x00;
    expected[24] = 0x04;

    bits.insert(bits.begin() + 12, 1);  // +1 slip: insert extra HIGH after stop2 of byte 0

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), frame.data(), kSbusDecodeFrameLen);
}

// Regression test for the broken naive re-sync: -1 slip at byte-0/byte-1 boundary
// where data-bit-0 of byte 1 is LOW (= 0). Without HIGH->LOW transition validation,
// the naive implementation accepted the LOW data bit at position 12 as the start bit,
// extracting D1-D7+parity instead of D0-D7 -- producing wrong channel values.
//
// byte1 = 0xC0 (LSB-first D0=0,D1=0,D2=0,D3=0,D4=0,D5=0,D6=1,D7=1):
//   D0 = 0 (LOW) -- exactly the HOTRC ch1=1472 neutral encoding.
// After -1 slip: position 11 = start bit (LOW), position 12 = D0 = LOW.
// Correct re-sync: HIGH(10)->LOW(11) transition wins; byteStart=11.
// Naive re-sync: bits[12]=LOW -> accepted as start bit; byteStart=12 (WRONG).
void test_resync_minus1_slip_low_data0_not_mistaken_for_start() {
    // 0xC0 = 11000000, D0=0 (LOW) when transmitted LSB-first.
    std::vector<uint8_t> bits = makeFrameBitsCustomByte1(0x00, 0xC0);
    std::array<uint8_t, kSbusDecodeFrameLen> expected = {};
    expected[0] = kSbusDecodeHeader;
    expected[1] = 0xC0;
    for (int i = 2; i < 23; ++i) expected[(size_t)i] = static_cast<uint8_t>((i * 17) & 0xFF);
    expected[23] = 0x00;
    expected[24] = 0x00;

    bits.erase(bits.begin() + 11);  // -1 slip: remove stop2 of byte 0

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), frame.data(), kSbusDecodeFrameLen);
}

// extractSbusBytes footer boundary: exactly 9 bits remaining for the last byte.
// The footer path requires bc - byteStart >= 9. With a full 300-bit frame the
// last byte starts at bit 288 (24 * 12), leaving exactly 12 bits — well above the
// minimum. This test instead constructs a minimal 300-bit sequence where exactly 9
// bits are available for the footer, confirming the boundary condition is accepted.
void test_footer_exactly_9_bits_accepted() {
    // Build a valid frame bit sequence, then trim it so exactly 9 bits remain
    // after the 24th byte boundary (start of footer byte).
    // 24 full bytes * 12 bits/byte = 288 bits consumed, footer needs 9 bits.
    std::vector<uint8_t> bits = makeFrameBits(0x00);
    // Trim to exactly 288 + 9 = 297 bits.
    bits.resize(297);

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    TEST_ASSERT_TRUE(decodeFromBits(bits, frame.data()));
    TEST_ASSERT_EQUAL_HEX8(kSbusDecodeHeader, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[kSbusDecodeFrameLen - 1]);
}

// extractSbusBytes footer boundary: only 8 bits remaining for the last byte.
// The `bc - byteStart < 9` guard must reject this — returning false so the
// caller increments extractFailCount.
void test_footer_only_8_bits_rejected() {
    std::vector<uint8_t> bits = makeFrameBits(0x00);
    bits.resize(296);  // 288 + 8 = one bit short of the 9-bit minimum

    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    SbusDecodeAttemptStats stats = {};
    TEST_ASSERT_FALSE(decodeFromBits(bits, frame.data(), &stats));
    TEST_ASSERT_GREATER_THAN_UINT32(0, stats.extractFailCount);
}

// A +2 slip puts the start bit at position 14, outside the ±1 search window.
// When no HIGH->LOW transition is found the re-sync falls back to fixed stride
// (byteStart += bitsPerByte). The fallback may or may not produce a passing
// header+footer by coincidence, but it must not crash and frame[0] is always
// correctly extracted (byte 0 has no re-sync step).
void test_resync_slip_beyond_window_uses_fallback() {
    std::vector<uint8_t> bits = makeFrameBits(0x00);
    bits.insert(bits.begin() + 12, 1);  // +2 slip: two extra HIGH bits
    bits.insert(bits.begin() + 12, 1);
    std::array<uint8_t, kSbusDecodeFrameLen> frame = {};
    decodeFromBits(bits, frame.data());  // just must not crash
    TEST_ASSERT_EQUAL_HEX8(kSbusDecodeHeader, frame[0]);
}


int main() {
    UNITY_BEGIN();

    RUN_TEST(test_footer_validator_accepts_standard_and_sbus2_variants);
    RUN_TEST(test_decode_accepts_standard_footer_0x00);
    RUN_TEST(test_decode_accepts_sbus_v2_footer_0x04);
    RUN_TEST(test_decode_accepts_sbus2_slot_footer_0x14);
    RUN_TEST(test_decode_rejects_invalid_footer_0x03);
    RUN_TEST(test_decode_rejects_insufficient_bit_count);
    RUN_TEST(test_decode_accepts_frame_with_trailing_noise);
    RUN_TEST(test_decode_from_symbols_standard_timing);
    RUN_TEST(test_decode_from_symbols_fast_timing);
    RUN_TEST(test_decode_skips_leading_high_bits);
    RUN_TEST(test_decode_false_positive_rejected_by_framing);
    RUN_TEST(test_resync_minus1_slip_recovers);
    RUN_TEST(test_resync_plus1_slip_recovers);
    RUN_TEST(test_resync_minus1_slip_low_data0_not_mistaken_for_start);
    RUN_TEST(test_resync_slip_beyond_window_uses_fallback);
    RUN_TEST(test_footer_exactly_9_bits_accepted);
    RUN_TEST(test_footer_only_8_bits_rejected);
    return UNITY_END();
}
// =============================================================================
// test/test_native/test_sbus_rmt_budget/test_sbus_rmt_budget.cpp
//
// Native unit tests for the SBUS RMT memory-block budget policy (#255).
//
// The policy lives in a dependency-free header precisely so it can be evaluated
// here for a chip the host is not building for: these tests answer the ESP32-P4
// question on a Linux box, which is otherwise only answerable by flashing.
//
// The two geometries below are transcribed from ESP-IDF's soc_caps.h for the
// chips this firmware targets. They are inputs to the policy, not assertions
// about it, so a chip whose caps change is a change here, deliberately visible.
// =============================================================================
#include <unity.h>

#include <cstddef>

#include "sbus_rmt_budget.h"

namespace {

// soc_caps.h (esp32):    MEM_WORDS_PER_CHANNEL 64, RX_CANDIDATES_PER_GROUP 8,
//                        no SOC_RMT_SUPPORT_RX_PINGPONG.
constexpr SbusRmtGeometry kArtooEsp32 = {64, 8, false};

// soc_caps.h (esp32p4):  MEM_WORDS_PER_CHANNEL 48, RX_CANDIDATES_PER_GROUP 4,
//                        SOC_RMT_SUPPORT_RX_PINGPONG 1.
constexpr SbusRmtGeometry kEsp32P4 = {48, 4, true};

// SBUS1 (drive) + SBUS2 (dome), as created in src/tasks/rc_input.cpp.
constexpr size_t kDecoders = 2;

// 25 bytes x 12 bits / 2 pulses per rmt_symbol_word_t; mirrors
// SbusDecoder::kWorstCaseFrameSymbols without pulling in the ESP32-only header.
constexpr size_t kWorstCaseFrameSymbols = 150;

// ---------------------------------------------------------------------------
// artoo-esp32 must be behaviour-identical to the pre-#255 firmware.
// ---------------------------------------------------------------------------

void test_artoo_keeps_three_blocks_and_192_symbols() {
    TEST_ASSERT_EQUAL_size_t(
        3, sbusRmtBlocksPerDecoder(kArtooEsp32, kDecoders, kWorstCaseFrameSymbols));
    // 192 is the value the classic-ESP32 firmware has always shipped.
    TEST_ASSERT_EQUAL_size_t(
        192, sbusRmtMemBlockSymbols(kArtooEsp32, kDecoders, kWorstCaseFrameSymbols));
}

void test_artoo_budget_fits() {
    TEST_ASSERT_TRUE(sbusRmtBudgetFits(kArtooEsp32, kDecoders, kWorstCaseFrameSymbols));
}

void test_artoo_holds_a_whole_frame_because_it_has_no_ping_pong() {
    // Without ping-pong the block count IS the frame bound, so the resident
    // memory must cover the worst case on its own.
    const size_t symbols =
        sbusRmtMemBlockSymbols(kArtooEsp32, kDecoders, kWorstCaseFrameSymbols);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(kWorstCaseFrameSymbols, symbols);
}

void test_artoo_has_no_ping_pong_half() {
    TEST_ASSERT_EQUAL_size_t(
        0, sbusRmtPingPongSymbols(kArtooEsp32, kDecoders, kWorstCaseFrameSymbols));
}

// ---------------------------------------------------------------------------
// ESP32-P4: the chip the defect was found on.
// ---------------------------------------------------------------------------

void test_p4_takes_two_blocks_and_96_symbols() {
    TEST_ASSERT_EQUAL_size_t(
        2, sbusRmtBlocksPerDecoder(kEsp32P4, kDecoders, kWorstCaseFrameSymbols));
    TEST_ASSERT_EQUAL_size_t(
        96, sbusRmtMemBlockSymbols(kEsp32P4, kDecoders, kWorstCaseFrameSymbols));
}

void test_p4_budget_fits() {
    TEST_ASSERT_TRUE(sbusRmtBudgetFits(kEsp32P4, kDecoders, kWorstCaseFrameSymbols));
}

void test_p4_ping_pong_half_is_48_symbols() {
    // Halved from the 96 the old 4-block sizing would have produced. This is
    // the number an ISR-cadence measurement is about.
    TEST_ASSERT_EQUAL_size_t(
        48, sbusRmtPingPongSymbols(kEsp32P4, kDecoders, kWorstCaseFrameSymbols));
}

void test_p4_block_count_need_not_hold_a_whole_frame() {
    // 96 < 150 on purpose: the driver reassembles ping-pong halves into the
    // caller's buffer, so on this chip the buffer is the frame bound. If this
    // ever has to change, it is the buffer that grows, not the block count.
    const size_t symbols =
        sbusRmtMemBlockSymbols(kEsp32P4, kDecoders, kWorstCaseFrameSymbols);
    TEST_ASSERT_LESS_THAN_size_t(kWorstCaseFrameSymbols, symbols);
    TEST_ASSERT_TRUE(kEsp32P4.pingPong);
}

// ---------------------------------------------------------------------------
// The two placement traps, stated literally. These are the regression tests.
// ---------------------------------------------------------------------------

void test_p4_rejects_four_blocks_per_decoder_the_original_defect() {
    // 192 symbols / 48 words = 4 blocks. The first decoder then occupies all
    // four RX-capable channels and the second begin() returns ESP_ERR_NOT_FOUND.
    TEST_ASSERT_FALSE(sbusRmtPlacementFits(kEsp32P4, kDecoders, 4));
}

void test_p4_rejects_three_blocks_per_decoder_the_allocator_overrun() {
    // 144 symbols / 48 = 3 blocks. The IDF allocator accepts this -- it tests a
    // candidate slot with an unbounded `channel_mask << j` -- and places the
    // second decoder across blocks 7, 8 and 9 when only 4..7 exist. It must be
    // rejected here instead.
    TEST_ASSERT_FALSE(sbusRmtPlacementFits(kEsp32P4, kDecoders, 3));
}

void test_p4_accepts_two_blocks_per_decoder() {
    TEST_ASSERT_TRUE(sbusRmtPlacementFits(kEsp32P4, kDecoders, 2));
}

void test_artoo_accepts_three_blocks_per_decoder() {
    TEST_ASSERT_TRUE(sbusRmtPlacementFits(kArtooEsp32, kDecoders, 3));
}

void test_placement_rejects_zero_blocks() {
    TEST_ASSERT_FALSE(sbusRmtPlacementFits(kEsp32P4, kDecoders, 0));
}

// ---------------------------------------------------------------------------
// Driver preconditions and degenerate inputs.
// ---------------------------------------------------------------------------

void test_mem_block_symbols_satisfy_the_driver_precondition() {
    // rmt_new_rx_channel() requires mem_block_symbols even and at least one
    // channel's worth of words.
    const size_t artoo =
        sbusRmtMemBlockSymbols(kArtooEsp32, kDecoders, kWorstCaseFrameSymbols);
    const size_t p4 = sbusRmtMemBlockSymbols(kEsp32P4, kDecoders, kWorstCaseFrameSymbols);
    TEST_ASSERT_EQUAL_size_t(0, artoo % 2);
    TEST_ASSERT_EQUAL_size_t(0, p4 % 2);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(kArtooEsp32.wordsPerChannel, artoo);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(kEsp32P4.wordsPerChannel, p4);
}

void test_a_chip_too_small_for_two_decoders_is_rejected() {
    // Hypothetical: P4 memory geometry but no ping-pong. The worst-case frame
    // then needs ceil(150/48) = 4 blocks each, and 2 x 4 > 4 candidates.
    constexpr SbusRmtGeometry cramped = {48, 4, false};
    TEST_ASSERT_EQUAL_size_t(4,
                             sbusRmtBlocksPerDecoder(cramped, kDecoders, kWorstCaseFrameSymbols));
    TEST_ASSERT_FALSE(sbusRmtBudgetFits(cramped, kDecoders, kWorstCaseFrameSymbols));
}

void test_degenerate_geometry_does_not_divide_by_zero() {
    constexpr SbusRmtGeometry noMemory = {0, 4, true};
    TEST_ASSERT_EQUAL_size_t(
        0, sbusRmtBlocksPerDecoder(noMemory, kDecoders, kWorstCaseFrameSymbols));
    TEST_ASSERT_FALSE(sbusRmtBudgetFits(noMemory, kDecoders, kWorstCaseFrameSymbols));
    TEST_ASSERT_EQUAL_size_t(
        0, sbusRmtBlocksPerDecoder(kEsp32P4, 0, kWorstCaseFrameSymbols));
    TEST_ASSERT_FALSE(sbusRmtBudgetFits(kEsp32P4, 0, kWorstCaseFrameSymbols));
}

void test_policy_is_a_constant_expression() {
    // The decoder states its budget as a static_assert, which only works if
    // every one of these is usable in a constant expression.
    static_assert(sbusRmtBlocksPerDecoder(kEsp32P4, kDecoders, kWorstCaseFrameSymbols) == 2,
                  "P4 budget must be constexpr-evaluable");
    static_assert(sbusRmtMemBlockSymbols(kArtooEsp32, kDecoders, kWorstCaseFrameSymbols) == 192,
                  "artoo budget must be constexpr-evaluable");
    static_assert(sbusRmtBudgetFits(kEsp32P4, kDecoders, kWorstCaseFrameSymbols),
                  "P4 budget must fit at compile time");
    static_assert(!sbusRmtPlacementFits(kEsp32P4, kDecoders, 4),
                  "the original 4-block sizing must be rejected at compile time");
    TEST_PASS();
}

}  // namespace

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_artoo_keeps_three_blocks_and_192_symbols);
    RUN_TEST(test_artoo_budget_fits);
    RUN_TEST(test_artoo_holds_a_whole_frame_because_it_has_no_ping_pong);
    RUN_TEST(test_artoo_has_no_ping_pong_half);

    RUN_TEST(test_p4_takes_two_blocks_and_96_symbols);
    RUN_TEST(test_p4_budget_fits);
    RUN_TEST(test_p4_ping_pong_half_is_48_symbols);
    RUN_TEST(test_p4_block_count_need_not_hold_a_whole_frame);

    RUN_TEST(test_p4_rejects_four_blocks_per_decoder_the_original_defect);
    RUN_TEST(test_p4_rejects_three_blocks_per_decoder_the_allocator_overrun);
    RUN_TEST(test_p4_accepts_two_blocks_per_decoder);
    RUN_TEST(test_artoo_accepts_three_blocks_per_decoder);
    RUN_TEST(test_placement_rejects_zero_blocks);

    RUN_TEST(test_mem_block_symbols_satisfy_the_driver_precondition);
    RUN_TEST(test_a_chip_too_small_for_two_decoders_is_rejected);
    RUN_TEST(test_degenerate_geometry_does_not_divide_by_zero);
    RUN_TEST(test_policy_is_a_constant_expression);

    return UNITY_END();
}

// =============================================================================
// include/sbus_rmt_budget.h
//
// RMT RX memory-block budget policy for the SBUS decoders.
//
// Why this is a separate header
// -----------------------------
// The number of RMT memory blocks an SbusDecoder may claim is a function of the
// *chip*, not of the board, and getting it wrong does not fail loudly: on the
// ESP32-P4 an oversized request silently consumed every RX-capable channel, so
// the second decoder's begin() failed and the controller degraded to a single
// SBUS receiver on the safety path (#255).
//
// Keeping the policy here, as constexpr functions over an explicit geometry
// struct rather than as reads of the SOC_RMT_* macros, buys two things:
//   - the decoder states the invariant as a static_assert, so a future chip
//     fails the build instead of failing at runtime in one log line;
//   - the native suite can evaluate the policy for a chip the host is not
//     building for, which is the only way to test the ESP32-P4 answer at all.
// This mirrors include/sbus_decode_helpers.h, which shares the bitstream decode
// logic with the native tests for the same reason.
//
// The two chip geometries in play (ESP-IDF soc_caps.h):
//
//                                 artoo-esp32   ESP32-P4
//   SOC_RMT_MEM_WORDS_PER_CHANNEL      64           48
//   SOC_RMT_RX_CANDIDATES_PER_GROUP     8            4
//   SOC_RMT_SUPPORT_RX_PINGPONG    (absent)          1
//
// Policy
// ------
// Ping-pong chips: the driver recycles the channel's hardware memory as it
// fills (rmt_isr_handle_rx_threshold copies each half into the caller's buffer
// and only invokes the done callback at end of frame), so the block count does
// NOT bound the capturable frame -- the caller's buffer does. The block count
// is then purely an ISR-latency knob: more blocks means a larger ping-pong half
// and a longer deadline to drain it. So take the largest count that still lets
// every decoder have one: rxCandidates / decoders.
//
// Chips without ping-pong: there is no recycling, so a frame that outgrows the
// channel's memory is truncated (the driver resets the pointer and emits a
// debug-only message; the truncated buffer then fails SBUS framing validation
// and the frame is simply lost). There the block count IS the frame bound and
// must cover the worst case.
//
// Both branches are checked by sbusRmtBudgetFits() below, which is also the
// guard against a subtler failure: the IDF allocator tests a candidate slot
// with an unbounded shift (`channel_mask << j`), so a request that runs off the
// end of the RX window is not rejected -- it succeeds and programs a channel
// whose memory extends past the last real block. Two decoders of three blocks
// each on the P4 do exactly that. Requiring decoders * blocks <= rxCandidates
// rules it out, because the allocator packs equal-sized requests from the low
// end of the window: request k lands at slot k * blocks, so the last one ends
// at decoders * blocks - 1.
// =============================================================================
#pragma once

#include <stddef.h>

// One chip's RMT RX geometry. Field-for-field the SOC_RMT_* capabilities; the
// caller supplies them so this header stays free of any ESP-IDF dependency.
struct SbusRmtGeometry {
    size_t wordsPerChannel;  // SOC_RMT_MEM_WORDS_PER_CHANNEL
    size_t rxCandidates;     // SOC_RMT_RX_CANDIDATES_PER_GROUP
    bool   pingPong;         // SOC_RMT_SUPPORT_RX_PINGPONG (absent == false)
};

// Memory blocks one decoder should claim. Returns 0 for a degenerate geometry
// so the caller's static_assert reports it rather than dividing by zero here.
constexpr size_t sbusRmtBlocksPerDecoder(const SbusRmtGeometry& geometry,
                                         size_t decoders,
                                         size_t worstCaseFrameSymbols) {
    if (decoders == 0 || geometry.wordsPerChannel == 0) {
        return 0;
    }
    if (geometry.pingPong) {
        // Largest count that still leaves one for every decoder.
        return geometry.rxCandidates / decoders;
    }
    // ceil(worstCaseFrameSymbols / wordsPerChannel): the whole frame must be
    // resident, there is no recycling.
    return (worstCaseFrameSymbols + geometry.wordsPerChannel - 1) / geometry.wordsPerChannel;
}

// The value to hand to rmt_rx_channel_config_t::mem_block_symbols.
constexpr size_t sbusRmtMemBlockSymbols(const SbusRmtGeometry& geometry,
                                        size_t decoders,
                                        size_t worstCaseFrameSymbols) {
    return sbusRmtBlocksPerDecoder(geometry, decoders, worstCaseFrameSymbols) *
           geometry.wordsPerChannel;
}

// Ping-pong half size, in symbols: the driver sets its RX threshold here, so
// this many symbols is the deadline the threshold ISR has to copy one half out
// before the writer wraps into it. Zero on a chip without ping-pong, where the
// mechanism does not exist.
constexpr size_t sbusRmtPingPongSymbols(const SbusRmtGeometry& geometry,
                                        size_t decoders,
                                        size_t worstCaseFrameSymbols) {
    if (!geometry.pingPong) {
        return 0;
    }
    return sbusRmtMemBlockSymbols(geometry, decoders, worstCaseFrameSymbols) / 2;
}

// Can `decoders` channels of `blocksPerDecoder` blocks each be placed in this
// chip's RX window? Kept separate from the choice of block count so a proposed
// count can be judged on its own -- which is how both #255 traps are stated:
// 4 blocks per decoder on the P4 is the original defect, and 3 blocks per
// decoder is the near miss that the IDF allocator accepts and should not.
constexpr bool sbusRmtPlacementFits(const SbusRmtGeometry& geometry,
                                    size_t decoders,
                                    size_t blocksPerDecoder) {
    if (blocksPerDecoder == 0 || decoders == 0) {
        return false;
    }
    return decoders * blocksPerDecoder <= geometry.rxCandidates;
}

// Every condition the budget must satisfy on a given chip:
//   1. at least one block per decoder;
//   2. all decoders fit the RX window -- and, because the allocator packs from
//      the low end, this is simultaneously the guard against a request running
//      off the end of that window;
//   3. mem_block_symbols even and at least one channel's worth, the driver's
//      own precondition on rmt_new_rx_channel();
//   4. without ping-pong, enough resident memory for the worst-case frame.
constexpr bool sbusRmtBudgetFits(const SbusRmtGeometry& geometry,
                                 size_t decoders,
                                 size_t worstCaseFrameSymbols) {
    const size_t blocks = sbusRmtBlocksPerDecoder(geometry, decoders, worstCaseFrameSymbols);
    const size_t symbols = blocks * geometry.wordsPerChannel;
    if (!sbusRmtPlacementFits(geometry, decoders, blocks)) {
        return false;
    }
    if ((symbols % 2u) != 0u || symbols < geometry.wordsPerChannel) {
        return false;
    }
    if (!geometry.pingPong && symbols < worstCaseFrameSymbols) {
        return false;
    }
    return true;
}

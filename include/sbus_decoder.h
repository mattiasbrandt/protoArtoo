// =============================================================================
// include/sbus_decoder.h
//
// RMT-based SBUS decoder for ESP32.
// Receives SBUS frames (100 kbaud, 8E2, inverted) on any GPIO using the
// ESP32 RMT peripheral. No hardware UART is consumed  --  UART1 and UART2
// remain exclusively owned by DriveTask (hoverboard) and DomeLinkTask
// (dome serial) respectively. Resolves both Conflict A (UART1) and
// Conflict B (UART2) from the UART contention audit.
//
// Hardware: HOTRC SBUS-A receivers on GPIO 15 (drive) and GPIO 13 (dome).
// Protocol: 25-byte frame; supports standard 100 kbaud SBUS and fast 200 kbaud
// timing variants seen on some receiver/transmitter combinations.
//
// Frame format: [0x0F header][22 data bytes][flags byte][0x00 footer]
//   16 channels x 11 bits packed LSB-first into bytes 1-22.
//   Flags byte (index 23): bit0=CH17, bit1=CH18, bit2=lost_frame, bit3=failsafe.
//
// Physical signal handling:
//   - RMT channel uses invert_in=1 for standard inverted SBUS wiring.
//   - Decoder includes task-context polarity fallback for non-standard output paths.
//
// RMT configuration:
//   resolution_hz = 1 MHz  --  1 us/tick.
//   signal_range_max_ns = 300 us  --  inter-frame gap marker (kFrameGapNs).
//   mem_block_symbols  --  derived per chip, not a literal. The policy lives in
//     include/sbus_rmt_budget.h; src/drivers/sbus_decoder.cpp instantiates it
//     from the SOC_RMT_* capabilities and static_asserts the result.
//
// RMT channel budget. The two chips differ in every dimension that matters, and
// the classic-ESP32 numbers do not carry over -- assuming they did is what left
// the P4's second decoder unable to initialise (#255):
//
//                                     artoo-esp32        ESP32-P4
//   memory words per channel               64               48
//   RX-capable channels                     8                4
//   RX ping-pong support                   no              yes
//   blocks per decoder                      3                2
//   mem_block_symbols                     192               96
//   two decoders occupy            6 of 8 blocks    4 of 4 channels
//   ping_pong_symbols                     n/a               48
//
// On the P4 the block count does not bound the frame: the driver reassembles
// ping-pong halves into the buffer passed to rmt_receive(), so kSymBufSize is
// the real bound there and the block count is an ISR-latency knob. On the
// classic ESP32 there is no ping-pong, the block count IS the bound, and 3
// blocks (192 symbols) must stay.
//
// SbusDecoder API:
//   .begin(rxPin)   --  initialize RMT channel; returns false if no channel free
//   .read()         --  returns true when a new valid 25-byte frame is decoded
//   .data()         --  returns SbusData{ch[16], failsafe, lost_frame, ch17, ch18}
//   .end()          --  release RMT channel and queue
//   .isInitialized()  --  true if begin() succeeded and end() has not been called
//   ch[]            --  0-indexed, range SBUS_MIN(172)..SBUS_MAX(1811), center ~992
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "sbus_decode_helpers.h"
#include "sbus_flags.h"
#include "sbus_unpack.h"

#ifdef ARDUINO_ARCH_ESP32
#  include <driver/rmt_rx.h>
#  include <freertos/FreeRTOS.h>
#  include <freertos/queue.h>
#endif

// -----------------------------------------------------------------------------
// SbusData  --  decoded SBUS frame
// -----------------------------------------------------------------------------
struct SbusData {
    uint16_t ch[16];   // 11-bit channel values (0-2047; typical SBUS range 172-1811)
    bool ch17;
    bool ch18;
    bool lost_frame;
    bool failsafe;
};

struct SbusDecoderDebugStats {
    uint32_t rxDoneCount;
    uint32_t queuedCount;
    uint32_t shortDropCount;
    uint32_t parseOkCount;
    uint32_t parseFailCount;
    uint32_t bitCountLowCount;      // flattenSymbols produced < kTotalBits
    uint32_t extractFailCount;      // extractSbusBytes returned false
    uint32_t headerMismatchCount;   // frame[0] != 0x0F
    uint32_t footerMismatchCount;   // footer not in accepted set
    uint8_t  lastRejectedFooter;    // last footer byte that failed validation
    uint32_t rearmFailCount;
    uint32_t parityFailCount;     // parity mismatch on a header+footer-passing candidate
    uint32_t lastSymbolCount;
    uint32_t maxSymbolCount;
};

// -----------------------------------------------------------------------------
// SbusDecoder  --  one instance per physical SBUS receiver
// -----------------------------------------------------------------------------
class SbusDecoder {
public:
    // Bits per SBUS byte frame: start(1) + data(8) + parity(1) + stop(2)
    static constexpr int kBitsPerByte = 12;

    // SBUS frame length in bytes (header + 22 data + flags + footer)
    static constexpr int kFrameLen = 25;

    // Worst-case frame length in RMT symbols. All-alternating bit levels make
    // every bit its own pulse, and one rmt_symbol_word_t carries two
    // (level, duration) pairs: 25 x 12 = 300 bits -> 150 symbols.
    static constexpr size_t kWorstCaseFrameSymbols =
        (size_t)kFrameLen * (size_t)kBitsPerByte / 2;

    SbusDecoder();

    // Initialize RMT channel on rxPin. Returns false if no RMT channel is
    // available or channel configuration fails. Safe to call again after end().
    bool begin(int rxPin);

    // Returns true if a new valid frame has been decoded since the last call.
    // Drains all pending ISR-queued buffers; returns true on the first valid
    // one found. Must be called from the task that owns this decoder.
    bool read();

    // Release the RMT channel and free the ISR queue.
    // Safe to call even if begin() was never called or already failed.
    void end();

    SbusData data()    const { return _data; }
    bool failsafe()    const { return _data.failsafe; }
    bool lostFrame()   const { return _data.lost_frame; }
    bool isInitialized() const {
#ifdef ARDUINO_ARCH_ESP32
        return _channel != nullptr;
#else
        return false;
#endif
    }

    SbusDecoderDebugStats debugStats() const;

private:
#ifdef ARDUINO_ARCH_ESP32
    // Symbol buffer capacity per decoder instance, in rmt_symbol_word_t.
    //
    // This is the buffer handed to rmt_receive(), and on a ping-pong chip it --
    // not mem_block_symbols -- is what bounds the longest frame that can be
    // assembled. Deliberately NOT tied to the RMT block budget: the two were
    // numerically equal on the classic ESP32, and that coincidence is exactly
    // what hid the P4 sizing defect (#255). 192 against a 150-symbol worst case
    // is ~28% margin; the cost is 2 x 192 x 4 B = 1536 B of .bss per decoder.
    static constexpr size_t kSymBufSize = 192;

    static_assert(kSymBufSize >= kWorstCaseFrameSymbols,
                  "SBUS symbol buffer must hold a worst-case frame");

    // Double-buffered storage: ISR writes one buffer while the task reads the other.
    struct RxBuf {
        rmt_symbol_word_t symbols[kSymBufSize];
        size_t            count;
    };

    rmt_channel_handle_t  _channel;
    QueueHandle_t         _queue;      // element: uint8_t buffer index (0 or 1)
    RxBuf                 _rxBufs[2];  // ping-pong buffers
    uint8_t               _activeBuf;  // index currently being filled by RMT
    rmt_receive_config_t  _rxCfg;
    // Set by _onRecvDone when rmt_receive() fails (ISR cannot log).
    // read() checks this flag, attempts task-context recovery, and logs.
    volatile bool         _isrRearmFailed;
    volatile uint32_t     _rxDoneCount;
    volatile uint32_t     _queuedCount;
    volatile uint32_t     _shortDropCount;
    volatile uint32_t     _parseOkCount;
    volatile uint32_t     _parseFailCount;
    volatile uint32_t     _bitCountLowCount;
    volatile uint32_t     _extractFailCount;
    volatile uint32_t     _headerMismatchCount;
    volatile uint32_t     _footerMismatchCount;
    volatile uint8_t      _lastRejectedFooter;
    volatile uint32_t     _rearmFailCount;
    volatile uint32_t     _parityFailCount;
    volatile uint32_t     _lastSymbolCount;
    volatile uint32_t     _maxSymbolCount;

    // ISR callback  --  IRAM_ATTR required (called from RMT interrupt context).
    // Swaps buffers, re-arms receive immediately, notifies task via queue.
    static bool IRAM_ATTR _onRecvDone(rmt_channel_handle_t chan,
                                      const rmt_rx_done_event_data_t* edata,
                                      void* ctx);

    // Convert one symbol buffer to a validated SBUS frame and update _data.
    // Returns true on success. Runs in task context, not ISR.
    bool _parseSymbols(const RxBuf& buf);
#endif  // ARDUINO_ARCH_ESP32

    SbusData _data;
};

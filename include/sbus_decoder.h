// =============================================================================
// include/sbus_decoder.h
//
// RMT-based SBUS decoder for ESP32.
// Receives SBUS frames (100 kbaud, 8E2, inverted) on any GPIO using the
// ESP32 RMT peripheral. No hardware UART is consumed — UART1 and UART2
// remain exclusively owned by DriveTask (hoverboard) and DomeLinkTask
// (dome serial) respectively. Resolves both Conflict A (UART1) and
// Conflict B (UART2) from the UART contention audit.
//
// Hardware: HOTRC SBUS-A receivers on GPIO 15 (drive) and GPIO 13 (dome).
// Protocol: 25-byte frame at 50–100 Hz. Channels 172–1811, center ~992.
//
// Frame format: [0x0F header][22 data bytes][flags byte][0x00 footer]
//   16 channels × 11 bits packed LSB-first into bytes 1–22.
//   Flags byte (index 23): bit0=CH17, bit1=CH18, bit2=lost_frame, bit3=failsafe.
//
// Physical signal (SBUS wire):
//   Baud: 100000 (100 kbaud) | Bits: 8 | Parity: Even | Stop: 2 | Inverted
//   Idle LOW on wire; start bit HIGH; data '1' = LOW, data '0' = HIGH.
//   12 bits per byte frame: start(1) + data(8) + parity(1) + stop(2).
//
// RMT configuration:
//   invert_in=1 — GPIO matrix re-inverts signal to standard UART polarity
//     so the decoder sees: idle=HIGH, start=LOW, data '1'=HIGH, data '0'=LOW.
//   resolution_hz = 1 MHz — 1 µs per tick; 10 ticks per SBUS bit.
//   signal_range_max_ns = 3 ms — frame gap delimiter.
//     Max in-frame same-level run ≈ 100 µs; min inter-frame gap at 100 Hz ≈ 7 ms.
//   mem_block_symbols = 192 — 3 RMT memory blocks; worst-case frame ≈ 150 symbols.
//
// RMT channel budget (classic ESP32 has 8 channels / 8 memory blocks):
//   SBUS1 decoder: 3 blocks. SBUS2 decoder: 3 blocks. 2 blocks remain free.
//
// SbusDecoder API:
//   .begin(rxPin)  — initialize RMT channel; returns false if no channel free
//   .read()        — returns true when a new valid 25-byte frame is decoded
//   .data()        — returns SbusData{ch[16], failsafe, lost_frame, ch17, ch18}
//   .end()         — release RMT channel and queue
//   .isInitialized() — true if begin() succeeded and end() has not been called
//   ch[]           — 0-indexed, range SBUS_MIN(172)..SBUS_MAX(1811), center ~992
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "sbus_flags.h"
#include "sbus_unpack.h"

#ifdef ARDUINO_ARCH_ESP32
#  include <driver/rmt_rx.h>
#  include <freertos/FreeRTOS.h>
#  include <freertos/queue.h>
#endif

// -----------------------------------------------------------------------------
// SbusData — decoded SBUS frame
// -----------------------------------------------------------------------------
struct SbusData {
    uint16_t ch[16];   // 11-bit channel values (0–2047; typical SBUS range 172–1811)
    bool ch17;
    bool ch18;
    bool lost_frame;
    bool failsafe;
};

// -----------------------------------------------------------------------------
// SbusDecoder — one instance per physical SBUS receiver
// -----------------------------------------------------------------------------
class SbusDecoder {
public:
    // Bits per SBUS byte frame: start(1) + data(8) + parity(1) + stop(2)
    static constexpr int kBitsPerByte = 12;

    // SBUS frame length in bytes (header + 22 data + flags + footer)
    static constexpr int kFrameLen = 25;

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

private:
#ifdef ARDUINO_ARCH_ESP32
    // Symbol buffer capacity per decoder instance.
    // Worst-case SBUS frame (all-alternating bits): 300 bits → ~150 rmt_symbol_word_t.
    // 192 = 3 RMT memory blocks provides a safe margin.
    static constexpr size_t kSymBufSize = 192;

    // Double-buffered storage: ISR writes one buffer while the task reads the other.
    struct RxBuf {
        rmt_symbol_word_t symbols[kSymBufSize];
        size_t            count;
    };

    rmt_channel_handle_t  _channel;
    QueueHandle_t         _queue;      // element: uint8_t buffer index (0 or 1)
    RxBuf                 _rxBufs[2];  // ping-pong buffers
    uint8_t               _activeBuf; // index currently being filled by RMT
    rmt_receive_config_t  _rxCfg;
    // Set by _onRecvDone when rmt_receive() fails (ISR cannot log).
    // read() checks this flag, attempts task-context recovery, and logs.
    volatile bool         _isrRearmFailed;

    // ISR callback — IRAM_ATTR required (called from RMT interrupt context).
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

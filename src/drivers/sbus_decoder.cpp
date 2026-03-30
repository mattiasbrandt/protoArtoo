// =============================================================================
// src/drivers/sbus_decoder.cpp
//
// RMT-based SBUS decoder for ESP32.
// Replaces the former UART-based implementation. See sbus_decoder.h for full
// protocol, RMT configuration rationale, and channel availability notes.
//
// Frame reconstruction pipeline:
//   1. RMT ISR captures signal edge timings into a double-buffered symbol array.
//   2. ISR re-arms receive on the other buffer immediately so no frame is missed.
//      Buffers with too few symbols (idle timeouts) are dropped at the ISR level.
//   3. read() in task context: flattens RMT symbols → flat bit array → 25 bytes.
//   4. Validates SBUS header (0x0F) and footer (0x00).
//   5. Calls sbusUnpackChannels() + parseSbusFlags() — shared, independently
//      tested helpers in sbus_unpack.h and sbus_flags.h.
//
// Signal polarity after invert_in=1:
//   SBUS wire uses inverted UART: idle LOW, start bit HIGH, data '1' = LOW.
//   With invert_in=1 the GPIO matrix re-inverts before RMT, so the decoder
//   sees standard polarity: idle HIGH, start bit LOW, data '1' = HIGH.
//   No manual bit inversion is needed during frame reconstruction.
//
// Bit timing with 1 µs/tick (1 MHz RMT clock):
//   SBUS bit period = 10 µs = 10 ticks.
//   Rounding: n_bits = (duration_ticks + 5) / 10 — correct for ±1 tick error.
//
// Frame gap detection:
//   signal_range_max_ns = 3 ms. Max in-frame same-level run ≈ 100 µs (7 data
//   '1' bits + even parity + 2 stop bits = 10 × 10 µs). Inter-frame idle is
//   at least 7 ms at 100 Hz. The 3 ms threshold correctly separates them.
// =============================================================================

#include "sbus_decoder.h"

#include <esp_log.h>
#include <string.h>

static const char* TAG = "SbusDecoder";

// ---------------------------------------------------------------------------
// RMT channel configuration
// ---------------------------------------------------------------------------

// 1 µs per tick — 10 ticks per SBUS bit at 100 kbaud.
static constexpr uint32_t kResolutionHz = 1'000'000;

// 3 RMT memory blocks per channel; worst-case SBUS frame ≈ 150 symbols.
// Classic ESP32 has 8 blocks total; 2 decoders use 6, leaving 2 free.
static constexpr size_t kMemBlockSymbols = 192;

// Ignore pulses shorter than 3 µs (glitch filter).
static constexpr uint32_t kGlitchNs = 3'000;

// Terminate receive after 3 ms of same-level signal (inter-frame gap marker).
// Max in-frame same-level run ≈ 100 µs; min inter-frame gap ≈ 7 ms at 100 Hz.
static constexpr uint32_t kFrameGapNs = 3'000'000;

// Minimum symbols to forward to the task queue.
// Idle timeouts produce 0–2 symbols; a real SBUS frame produces ≥ 20.
static constexpr size_t kMinSymsForFrame = 10;

// ---------------------------------------------------------------------------
// Bit extraction constants
// ---------------------------------------------------------------------------

// SBUS bit period in RMT ticks (1 µs/tick → 10 ticks/bit at 100 kbaud).
static constexpr uint32_t kBitPeriodTicks = 10;

// Half-period used for round-to-nearest-bit arithmetic.
static constexpr uint32_t kBitHalfTicks = 5;

// SBUS frame constants
static constexpr uint8_t kSbusHeader = 0x0F;
static constexpr uint8_t kSbusFooter = 0x00;

// Total bits in one complete SBUS frame (25 bytes × 12 bits/byte).
// 12 bits/byte = start(1) + data(8) + parity(1) + stop(2).
static constexpr int kTotalBits = SbusDecoder::kFrameLen * SbusDecoder::kBitsPerByte;  // 300

// ---------------------------------------------------------------------------
// SbusDecoder
// ---------------------------------------------------------------------------

SbusDecoder::SbusDecoder()
    : _channel(nullptr), _queue(nullptr), _activeBuf(0), _rxCfg{}, _data{} {}

bool SbusDecoder::begin(int rxPin) {
    if (_channel) end();

    // Queue carries uint8_t buffer indices (0 or 1); depth 2 prevents ISR
    // blocking if the task is one frame behind.
    _queue = xQueueCreate(2, sizeof(uint8_t));
    if (!_queue) {
        ESP_LOGE(TAG, "queue alloc failed — GPIO%d", rxPin);
        return false;
    }

    rmt_rx_channel_config_t cfg = {};
    cfg.gpio_num          = (gpio_num_t)rxPin;
    cfg.clk_src           = RMT_CLK_SRC_DEFAULT;  // APB 80 MHz; driver prescales to resolution_hz
    cfg.resolution_hz     = kResolutionHz;
    cfg.mem_block_symbols = kMemBlockSymbols;
    cfg.flags.invert_in   = 1;  // SBUS inverted logic → standard polarity after GPIO matrix

    esp_err_t err = rmt_new_rx_channel(&cfg, &_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s GPIO%d", esp_err_to_name(err), rxPin);
        vQueueDelete(_queue); _queue = nullptr;
        return false;
    }

    rmt_rx_event_callbacks_t cbs = {};
    cbs.on_recv_done = _onRecvDone;
    err = rmt_rx_register_event_callbacks(_channel, &cbs, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register callbacks failed: %s GPIO%d", esp_err_to_name(err), rxPin);
        rmt_del_channel(_channel); _channel = nullptr;
        vQueueDelete(_queue);      _queue   = nullptr;
        return false;
    }

    err = rmt_enable(_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable failed: %s GPIO%d", esp_err_to_name(err), rxPin);
        rmt_del_channel(_channel); _channel = nullptr;
        vQueueDelete(_queue);      _queue   = nullptr;
        return false;
    }

    _rxCfg.signal_range_min_ns = kGlitchNs;
    _rxCfg.signal_range_max_ns = kFrameGapNs;
    _activeBuf = 0;

    err = rmt_receive(_channel, _rxBufs[0].symbols,
                      sizeof(_rxBufs[0].symbols), &_rxCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_receive failed: %s GPIO%d", esp_err_to_name(err), rxPin);
        rmt_disable(_channel);
        rmt_del_channel(_channel); _channel = nullptr;
        vQueueDelete(_queue);      _queue   = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "started — GPIO%d  RMT 1us/tick  gap_threshold=3ms", rxPin);
    return true;
}

void SbusDecoder::end() {
    if (_channel) {
        rmt_disable(_channel);
        rmt_del_channel(_channel);
        _channel = nullptr;
    }
    if (_queue) {
        vQueueDelete(_queue);
        _queue = nullptr;
    }
    _activeBuf = 0;
}

bool SbusDecoder::read() {
    if (!_channel || !_queue) return false;

    // Drain all pending buffers; return true on the first valid parsed frame.
    uint8_t idx;
    while (xQueueReceive(_queue, &idx, 0) == pdTRUE) {
        if (_parseSymbols(_rxBufs[idx])) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// _onRecvDone — ISR callback (IRAM_ATTR)
//
// Called by the RMT driver when the receive window closes — either the signal
// held one level for > kFrameGapNs (normal frame end) or the symbol buffer
// filled up (should not happen for normal SBUS traffic with kMemBlockSymbols=192).
//
// Immediately re-arms receive on the other buffer so no frame is missed while
// the task is parsing the completed buffer.
//
// Only forwards buffers large enough to plausibly contain a real frame. Idle
// timeouts produce 0–2 symbols and are discarded here without waking the task.
// -----------------------------------------------------------------------------
bool IRAM_ATTR SbusDecoder::_onRecvDone(rmt_channel_handle_t chan,
                                         const rmt_rx_done_event_data_t* edata,
                                         void* ctx) {
    SbusDecoder* self = static_cast<SbusDecoder*>(ctx);
    BaseType_t   woken = pdFALSE;

    // Record symbol count for the buffer that just completed.
    self->_rxBufs[self->_activeBuf].count = edata->num_symbols;
    uint8_t doneIdx = self->_activeBuf;

    // Swap to the other buffer and re-arm receive before anything else.
    // rmt_receive() is ISR-safe in IDF 5.x.
    self->_activeBuf ^= 1;
    rmt_receive(chan,
                self->_rxBufs[self->_activeBuf].symbols,
                sizeof(self->_rxBufs[self->_activeBuf].symbols),
                &self->_rxCfg);

    // Wake the task only for buffers likely to contain a real frame.
    if (edata->num_symbols >= kMinSymsForFrame) {
        xQueueSendFromISR(self->_queue, &doneIdx, &woken);
    }

    return woken == pdTRUE;
}

// -----------------------------------------------------------------------------
// _parseSymbols — convert one RMT symbol buffer into a validated SBUS frame
//
// Runs in task context (never in ISR).
//
// Step 1: Flatten RMT symbols into a boolean bit array.
//   Each rmt_symbol_word_t represents two consecutive signal levels:
//   {level0, duration0, level1, duration1}. duration1=0 is the IDF end marker.
//   We convert duration ticks → bit count by rounding to the nearest 10 µs.
//
// Step 2: Skip the initial idle (HIGH=1) bits before the first start bit.
//   The frame begins at the first LOW=0 bit (start bit of byte 0).
//
// Step 3: Extract 25 bytes (12 bits each: start + 8 data + parity + 2 stop).
//   Bit layout per byte at position b, offset from first start bit:
//     bits[b*12 + 0]    = start bit  (must be 0)
//     bits[b*12 + 1..8] = data bits D0..D7 (LSB first, standard polarity)
//     bits[b*12 + 9]    = parity bit (not validated; rely on header/footer)
//     bits[b*12 + 10,11] = stop bits (should be 1)
//
// Step 4: Validate header (0x0F) and footer (0x00).
//
// Step 5: Decode channels via sbusUnpackChannels() and parseSbusFlags().
// -----------------------------------------------------------------------------
bool SbusDecoder::_parseSymbols(const RxBuf& buf) {
    if (buf.count < kMinSymsForFrame) return false;

    // Flat bit array — static to avoid stack pressure.
    // Size: kTotalBits (300) + 64 headroom for initial idle bits.
    // Static local is safe because _parseSymbols is only called from RcInputTask.
    static bool bits[364];
    int bc = 0;

    for (size_t i = 0; i < buf.count && bc < (int)(sizeof(bits)); i++) {
        const rmt_symbol_word_t& s = buf.symbols[i];

        if (s.duration0 > 0) {
            int n = (int)((s.duration0 + kBitHalfTicks) / kBitPeriodTicks);
            int lim = (int)(sizeof(bits)) - bc;
            if (n > lim) n = lim;
            for (int j = 0; j < n; j++) bits[bc++] = (s.level0 != 0);
        }

        // duration1 == 0 is the IDF end-of-sequence marker.
        if (s.duration1 == 0) break;

        {
            int n = (int)((s.duration1 + kBitHalfTicks) / kBitPeriodTicks);
            int lim = (int)(sizeof(bits)) - bc;
            if (n > lim) n = lim;
            for (int j = 0; j < n; j++) bits[bc++] = (s.level1 != 0);
        }
    }

    // Find the first start bit: first LOW (0) after the initial idle (HIGH).
    // After invert_in=1: idle = HIGH = 1; SBUS start bit = LOW = 0.
    int pos = 0;
    while (pos < bc && bits[pos]) pos++;

    // Need at least kTotalBits (300 bits = 25 bytes × 12 bits) from here.
    if (bc - pos < kTotalBits) return false;

    // Extract 25 bytes.
    uint8_t frame[kFrameLen];
    for (int b = 0; b < kFrameLen; b++) {
        int base = pos + b * kBitsPerByte;
        if (base + kBitsPerByte > bc) return false;
        if (bits[base]) return false;  // start bit must be LOW (0)

        // Reconstruct data byte from D0..D7 (LSB first, standard polarity).
        uint8_t byte = 0;
        for (int d = 0; d < 8; d++) {
            if (bits[base + 1 + d]) byte |= (uint8_t)(1u << d);
        }
        frame[b] = byte;
        // bits[base+9]  = parity (not validated — header/footer check is sufficient)
        // bits[base+10] = stop bit 1 (should be 1)
        // bits[base+11] = stop bit 2 (should be 1)
    }

    // Validate SBUS frame boundaries.
    if (frame[0]  != kSbusHeader) return false;
    if (frame[24] != kSbusFooter) return false;

    // Decode channels and flags using shared, independently-tested helpers.
    int16_t ch[16];
    sbusUnpackChannels(&frame[1], ch);
    for (int i = 0; i < 16; i++) _data.ch[i] = (uint16_t)ch[i];

    SbusFlags flags = parseSbusFlags(frame[23]);
    _data.ch17       = flags.ch17;
    _data.ch18       = flags.ch18;
    _data.lost_frame = flags.lost_frame;
    _data.failsafe   = flags.failsafe;

    return true;
}

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
//   3. read() in task context: flattens RMT symbols -> flat bit array -> 25 bytes.
//   4. Validates SBUS header (0x0F) and accepted footer variants (0x00/0x?4).
//   5. Calls sbusUnpackChannels() + parseSbusFlags()  --  shared, independently
//      tested helpers in sbus_unpack.h and sbus_flags.h.
//
// Signal handling and compatibility notes:
//   - RMT input is configured with invert_in=1 (expected for inverted SBUS wire).
//   - Decode path includes a polarity fallback in task context for receivers that
//     present non-standard or pre-inverted output.
//   - Parser tries both standard (100 kbaud) and fast (200 kbaud) SBUS bit timing.
//
// Frame gap detection:
//   signal_range_max_ns = 300 us keeps frame segmentation stable while tolerating
//   higher refresh SBUS variants with shorter inter-frame gaps.
// =============================================================================

#include "sbus_decoder.h"
#include "sbus_decode_helpers.h"
#include "sbus_rmt_budget.h"

#include <esp_log.h>
#include <soc/soc_caps.h>
#include <string.h>

static const char* TAG = "SbusDecoder";

// ---------------------------------------------------------------------------
// RMT channel configuration
// ---------------------------------------------------------------------------

// 1 us per tick  --  10 ticks per SBUS bit at 100 kbaud.
static constexpr uint32_t kResolutionHz = 1'000'000;

// --- RMT memory-block budget, derived per chip (#255) ------------------------
//
// Two decoders have to coexist: SBUS1 (drive) and SBUS2 (dome), both created in
// src/tasks/rc_input.cpp. A literal block count here is a defect waiting for the
// next chip. 192 symbols is 3 blocks on the classic ESP32 (64 words per block,
// 8 RX-capable channels) but 4 on the ESP32-P4 (48 words, 4 channels) -- so the
// first decoder claimed every RX-capable channel and the second one's begin()
// returned ESP_ERR_NOT_FOUND, degrading the controller to a single receiver.
//
// The policy and the reasoning behind it are in include/sbus_rmt_budget.h; what
// follows is only its instantiation, so the two can be read -- and tested --
// apart. Derived values:
//
//   artoo-esp32   3 blocks x 64 = 192 symbols   (unchanged: no ping-pong, so
//                                                the whole frame must be resident)
//   ESP32-P4      2 blocks x 48 =  96 symbols
//
// Consequence worth meeting here rather than in a ticket: the driver puts its RX
// threshold at half the channel's memory, so on the P4 ping_pong_symbols halves
// from 96 to 48. The threshold ISR then fires about 3 times per worst-case frame
// instead of 1, and the deadline to copy one half out before the writer wraps
// into it drops from ~1.9 ms to ~0.96 ms (worst case is 150 symbols over the
// ~3 ms on-wire frame, i.e. 20 us per symbol). Both are comfortable; both are
// what an ISR-cadence measurement should be looking at.
static constexpr size_t kConcurrentDecoders = 2;

// SOC_RMT_SUPPORT_RX_PINGPONG is defined only on chips that have the feature,
// so testing it with #if on a possibly-undefined macro (undefined -> 0) is
// deliberate, and is how ESP-IDF's own rmt_rx.c tests it. Do not convert this to
// #ifdef: it is the value that matters, not merely the definedness.
static constexpr SbusRmtGeometry kRmtGeometry = {
    SOC_RMT_MEM_WORDS_PER_CHANNEL,
    SOC_RMT_RX_CANDIDATES_PER_GROUP,
#if SOC_RMT_SUPPORT_RX_PINGPONG
    true,
#else
    false,
#endif
};

static constexpr size_t kMemBlockSymbols = sbusRmtMemBlockSymbols(
    kRmtGeometry, kConcurrentDecoders, SbusDecoder::kWorstCaseFrameSymbols);

// A chip whose RMT geometry cannot host both decoders must fail the build here,
// not degrade to one receiver at runtime behind a single log line. This also
// rules out a subtler failure the IDF allocator does not catch: it tests a
// candidate slot with an unbounded `channel_mask << j`, so a request that runs
// off the end of the RX window succeeds and programs a channel whose memory
// extends past the last real block. Two 3-block decoders on the P4 do exactly
// that; requiring decoders * blocks <= SOC_RMT_RX_CANDIDATES_PER_GROUP rejects it.
static_assert(sbusRmtBudgetFits(kRmtGeometry, kConcurrentDecoders,
                                SbusDecoder::kWorstCaseFrameSymbols),
              "RMT RX budget does not fit this chip: two SBUS decoders cannot both "
              "be placed within SOC_RMT_RX_CANDIDATES_PER_GROUP, or a chip without "
              "ping-pong cannot hold a worst-case frame");

// Ignore pulses shorter than 3 us (glitch filter).
static constexpr uint32_t kGlitchNs = 3'000;

// Terminate receive after 0.3 ms of same-level signal (inter-frame gap marker).
// In-frame same-level runs are << 0.3 ms, so this aggressively splits at frame
// boundaries and avoids multi-frame buffer fill/fragmentation.
static constexpr uint32_t kFrameGapNs = 300'000;

// Minimum symbols to forward to the task queue.
// Idle timeouts produce 0-2 symbols; a real SBUS frame produces >= 20.
static constexpr size_t kMinSymsForFrame = 10;

// ---------------------------------------------------------------------------
// Bit extraction constants
// ---------------------------------------------------------------------------

// SBUS bit period in RMT ticks (1 us/tick):
// - Standard SBUS 100 kbaud: 10 ticks/bit
// - Fast SBUS 200 kbaud: 5 ticks/bit
static constexpr uint32_t kBitPeriodTicksStd = 10;
static constexpr uint32_t kBitPeriodTicksFast = 5;

// SBUS frame constants
static constexpr uint8_t kSbusHeader = 0x0F;
[[maybe_unused]] static constexpr uint8_t kSbusFooter = 0x00;

// Total bits in one complete SBUS frame (25 bytes x 12 bits/byte).
// 12 bits/byte = start(1) + data(8) + parity(1) + stop(2).
static constexpr int kTotalBits    = SbusDecoder::kFrameLen * SbusDecoder::kBitsPerByte;  // 300

// Bit array capacity: one frame plus headroom for leading idle and occasional
// extra symbols. Sized to tolerate both standard and fast SBUS timing variants.
static constexpr int kBitArraySize = 640;  // bits

// ---------------------------------------------------------------------------
// SbusDecoder
// ---------------------------------------------------------------------------

SbusDecoder::SbusDecoder()
    : _channel(nullptr), _queue(nullptr), _rxBufs{}, _activeBuf(0), _rxCfg{},
      _isrRearmFailed(false), _rxDoneCount(0), _queuedCount(0),
      _shortDropCount(0), _parseOkCount(0), _parseFailCount(0),
      _bitCountLowCount(0), _extractFailCount(0), _headerMismatchCount(0),
      _footerMismatchCount(0), _lastRejectedFooter(0), _rearmFailCount(0),
      _parityFailCount(0), _lastSymbolCount(0), _maxSymbolCount(0), _data{} {}

bool SbusDecoder::begin(int rxPin) {
    if (_channel) end();

    // Queue carries uint8_t buffer indices (0 or 1); depth 2 prevents ISR
    // blocking if the task is one frame behind.
    _queue = xQueueCreate(2, sizeof(uint8_t));
    if (!_queue) {
        ESP_LOGE(TAG, "queue alloc failed - GPIO%d", rxPin);
        return false;
    }

    rmt_rx_channel_config_t cfg = {};
    cfg.gpio_num          = (gpio_num_t)rxPin;
    cfg.clk_src           = RMT_CLK_SRC_DEFAULT;  // APB 80 MHz; driver prescales to resolution_hz
    cfg.resolution_hz     = kResolutionHz;
    cfg.mem_block_symbols = kMemBlockSymbols;
    cfg.flags.invert_in   = 1;  // SBUS inverted logic -> standard polarity after GPIO matrix

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
        rmt_disable(_channel);  // safe to call even on partial enable failure
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

    ESP_LOGI(TAG, "started - GPIO%d  RMT 1us/tick  gap_threshold=300us", rxPin);
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

SbusDecoderDebugStats SbusDecoder::debugStats() const {
    SbusDecoderDebugStats stats = {};
    stats.rxDoneCount = _rxDoneCount;
    stats.queuedCount = _queuedCount;
    stats.shortDropCount = _shortDropCount;
    stats.parseOkCount = _parseOkCount;
    stats.parseFailCount = _parseFailCount;
    stats.bitCountLowCount = _bitCountLowCount;
    stats.extractFailCount = _extractFailCount;
    stats.headerMismatchCount = _headerMismatchCount;
    stats.footerMismatchCount = _footerMismatchCount;
    stats.lastRejectedFooter = _lastRejectedFooter;
    stats.rearmFailCount = _rearmFailCount;
    stats.parityFailCount = _parityFailCount;
    stats.lastSymbolCount = _lastSymbolCount;
    stats.maxSymbolCount = _maxSymbolCount;
    return stats;
}


bool SbusDecoder::read() {
    if (!_channel || !_queue) return false;

    // If the ISR failed to re-arm rmt_receive() (cannot log from ISR), attempt
    // recovery from task context before draining the queue.
    if (_isrRearmFailed) {
        _isrRearmFailed = false;
        ESP_LOGW(TAG, "ISR re-arm failed - attempting recovery from task context");
        esp_err_t err = rmt_receive(_channel,
                                    _rxBufs[_activeBuf].symbols,
                                    sizeof(_rxBufs[_activeBuf].symbols),
                                    &_rxCfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "task-context re-arm also failed: %s", esp_err_to_name(err));
        }
    }

    // Drain all pending buffers; return true on the first valid parsed frame.
    uint8_t idx;
    while (xQueueReceive(_queue, &idx, 0) == pdTRUE) {
        if (_parseSymbols(_rxBufs[idx])) {
            _parseOkCount = _parseOkCount + 1;
            return true;
        }
        _parseFailCount = _parseFailCount + 1;
    }
    return false;
}

// -----------------------------------------------------------------------------
// _onRecvDone  --  ISR callback (IRAM_ATTR)
//
// Called by the RMT driver when the receive window closes  --  either the signal
// held one level for > kFrameGapNs (normal frame end) or the buffer passed to
// rmt_receive() filled up -- that bound is kSymBufSize (192 symbols), not the
// RMT block count, and normal SBUS traffic stays well under it.
//
// Immediately re-arms receive on the other buffer so no frame is missed while
// the task is parsing the completed buffer.
//
// Only forwards buffers large enough to plausibly contain a real frame. Idle
// timeouts produce 0-2 symbols and are discarded here without waking the task.
// -----------------------------------------------------------------------------
bool IRAM_ATTR SbusDecoder::_onRecvDone(rmt_channel_handle_t chan,
                                         const rmt_rx_done_event_data_t* edata,
                                         void* ctx) {
    SbusDecoder* self = static_cast<SbusDecoder*>(ctx);
    BaseType_t   woken = pdFALSE;

    // Record symbol count for the buffer that just completed.
    // Double-buffer timing note: with queue depth 2 and SBUS at 100 Hz (10 ms),
    // the ISR could cycle back to writing buf[N] while the task holds buf[N] in the
    // queue. In practice this requires the task to miss >20 ms of execution  --  not
    // possible at the 5 ms poll rate. If it did occur, _parseSymbols() would reject
    // the corrupt frame via header/footer validation, so there is no safety impact.
    self->_rxDoneCount = self->_rxDoneCount + 1;
    self->_lastSymbolCount = (uint32_t)edata->num_symbols;
    if ((uint32_t)edata->num_symbols > self->_maxSymbolCount) {
        self->_maxSymbolCount = (uint32_t)edata->num_symbols;
    }

    self->_rxBufs[self->_activeBuf].count = edata->num_symbols;
    uint8_t doneIdx = self->_activeBuf;

    // Swap to the other buffer and re-arm receive before anything else.
    // rmt_receive() is ISR-safe in IDF 5.x.
    self->_activeBuf ^= 1;
    esp_err_t rearmErr = rmt_receive(chan,
                                     self->_rxBufs[self->_activeBuf].symbols,
                                     sizeof(self->_rxBufs[self->_activeBuf].symbols),
                                     &self->_rxCfg);
    if (rearmErr != ESP_OK) {
        // Cannot call ESP_LOG from ISR. Set flag; read() will log and recover.
        self->_isrRearmFailed = true;
        self->_rearmFailCount = self->_rearmFailCount + 1;
    }
    // Wake the task only for buffers likely to contain a real frame.
    if (edata->num_symbols >= kMinSymsForFrame) {
        if (xQueueSendFromISR(self->_queue, &doneIdx, &woken) == pdTRUE) {
            self->_queuedCount = self->_queuedCount + 1;
        }
    } else {
        self->_shortDropCount = self->_shortDropCount + 1;
    }

    return woken == pdTRUE;
}

// -----------------------------------------------------------------------------
// SBUS bitstream decode helpers are shared with native tests in
// include/sbus_decode_helpers.h.
// -----------------------------------------------------------------------------

static void storeDecodedFrame(const uint8_t* frame, SbusData* out) {
    int16_t ch[16];
    sbusUnpackChannels(&frame[1], ch);
    for (int i = 0; i < 16; i++) out->ch[i] = (uint16_t)ch[i];

    SbusFlags flags = parseSbusFlags(frame[23]);
    out->ch17       = flags.ch17;
    out->ch18       = flags.ch18;
    out->lost_frame = flags.lost_frame;
    out->failsafe   = flags.failsafe;
}

// -----------------------------------------------------------------------------
// _parseSymbols
// Orchestrates flatten -> locate -> extract -> validate -> decode.
// Tries standard and fast SBUS timing, then polarity fallback.
// Runs in task context (never in ISR).
// -----------------------------------------------------------------------------
bool SbusDecoder::_parseSymbols(const RxBuf& buf) {
    if (buf.count < kMinSymsForFrame) return false;

    // Static buffer avoids stack pressure. Safe for sequential single-task use.
    static bool bits[kBitArraySize];

    // Estimate bit period from total frame ticks / 300 bits.
    // See sbusEstimateBitPeriod() in sbus_decode_helpers.h for full rationale
    // (last-symbol gap exclusion, clamping, and HOTRC 115 kbaud behaviour).
    uint32_t adaptivePeriod = sbusEstimateBitPeriod(buf.symbols, buf.count);

    // Only try the adaptive period with correct (non-inverted) polarity.
    // kBitPeriodTicksFast (200 kbaud) and invertBits=true are wrong for the
    // HOTRC DS-650 at 100 kbaud with RMT invert_in=1. Trying them wastes three
    // of the four attempts per frame, triples the header-mismatch counter noise,
    // and opens a path for false positives via coincidental header+footer match.
    const uint32_t bitPeriods[]  = {adaptivePeriod};
    const bool     invertOptions[] = {false};

    for (uint32_t bitPeriod : bitPeriods) {
        int bc = flattenSymbols(buf.symbols, buf.count, bits, kBitArraySize, bitPeriod);
        if (bc < kBitsPerByte) {
            _bitCountLowCount = _bitCountLowCount + 1;
            continue;
        }

        for (bool invert : invertOptions) {
            uint8_t frame[kFrameLen];
            SbusDecodeAttemptStats attemptStats = {};
            bool decoded = decodeFrameFromBits(bits, bc, invert, frame, kFrameLen,
                                              &attemptStats);

            _extractFailCount = _extractFailCount + attemptStats.extractFailCount;
            _headerMismatchCount = _headerMismatchCount + attemptStats.headerMismatchCount;
            _footerMismatchCount = _footerMismatchCount + attemptStats.footerMismatchCount;
            if (attemptStats.footerMismatchCount > 0) {
                _lastRejectedFooter = attemptStats.lastRejectedFooter;
            }
            _parityFailCount = _parityFailCount + attemptStats.parityFailCount;

            if (!decoded) {
                continue;
            }

            storeDecodedFrame(frame, &_data);
            return true;
        }
    }

    return false;
}
// =============================================================================
// include/log_buffer.h
//
// Pure ring-buffer helpers for the in-memory log store.
// No Arduino, no FreeRTOS, no hardware dependencies  --  testable in native env.
//
// LogBuffer holds a fixed-size circular array of fixed-width lines.
// Callers are responsible for any locking needed around these functions.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"  // PA_CHIP_TARGET_* (chip-target selection)

// -----------------------------------------------------------------------------
// Compile-time ring-buffer dimensions. This header is the only declaration of
// them: main.cpp's bootstrap ring and boot-time sizing, the SSE batch buffers in
// web_server.cpp and the native test storage all read them from here.
// -----------------------------------------------------------------------------
// LOG_LINE_MAX: max chars per stored line (including null terminator space).
// 128 chars covers all normal log lines; longer lines are truncated at source.
//
// Deliberately NOT chip-target specific. Emission is bounded separately at
// PA_LOG_SERIAL_LINE_MAX (256, logging.h), so a retained line is clipped at
// half of what was printed -- but the rationale above is "covers all normal log
// lines", not a heap argument, so widening it is not the inherited-scarcity fix
// this file makes. It would also widen the SSE batch statics that are sized off
// it (s_sseLogLines / s_sseLogBatch, web_server.cpp), spending permanent DRAM on
// a dimension that is not what limits retained history.
static constexpr size_t LOG_LINE_MAX = 128;

// Ring depth follows the operator's saved log level. The ring is sized ONCE at
// boot from NVS (see paLogRingApplyBootDepth in main.cpp): changing the level
// on the Setup page changes emission immediately and ring depth at the next
// reboot, so log level remains the single knob and history depth follows the
// chosen verbosity.
//
// The ladder is chip-target specific. Each rung costs 2 x depth x LOG_LINE_MAX
// bytes of heap -- the ring itself plus the /api/logs body buffer allocated
// beside it (paLogRingApplyBootDepth) -- so depth is bought with the same heap
// everything else on the board competes for. Note the direction of risk here is
// the opposite of a task stack's: an under-deep ring loses history, which costs
// a diagnosis, while an over-deep one takes heap from the web stack and the
// sequence store, which costs a function. Under-sizing is the safe direction.
//
// ESP32 (artoo-esp32): unchanged. The first rung's own comment gave the reason,
// and it is a scarcity reason -- "minimal memory use" on a board with 42692 B of
// measured free heap (see the task-stack block in config.h).
//
//   ERROR (1): 16 lines  --  faults only; minimal memory use
//   WARN  (2): 20 lines  --  faults plus safety warnings; default production
//   INFO  (3): 24 lines  --  normal operator use
//   DEBUG (4): 48 lines  --  verbose; more history needed for diagnosis
//
// ESP32-P4: sized so that each rung RETAINS THE WHOLE BOOT SEQUENCE at its own
// verbosity, because on a board still bringing up its network backend the
// retained history is what remote troubleshooting has -- /api/logs is how the
// OTA diagnosis got its answer once the serial boot window was gone.
//
// SIZING RULE: the measured boot-path log-site count at that level, rounded up
// to the next multiple of 16 lines. The count comes from the call closure of
// setup() plus the web bring-up entered from the WiFi event callback
// (webServerInit / startHttpServerOnce / webNetworkBootstrap), with the task
// entry points excluded because their loops are runtime, not boot:
//
//   level         boot-path log sites      rung        heap (2 x depth x 128)
//   ERROR (1)              20               32               8192 B
//   WARN  (2)              55               64              16384 B   <- default
//   INFO  (3)              96               96              24576 B
//   DEBUG (4)             104              112              28672 B
//
// That count is neither a strict upper nor a strict lower bound: not every
// branch runs on a given boot, and a site inside a boot loop (the /seq scan
// warning per invalid file) can emit more than one line. The rounding is the
// only margin, and at INFO it happens to be zero because 96 is already a
// multiple of 16 -- the rung above it is what absorbs a chattier boot.
//
// The deepest rung costs 28672 B, which is 27.4% of the ~102 KB of internal
// free heap the ESP32-P4 has after the static growth in this ticket (#245
// measured ~114 KB; see heap_health.h and tasks/safety.cpp). artoo-esp32's
// deepest rung costs 12288 B of its 42692 B, i.e. 28.8%, so the P4 buys 2.3x
// the history for a slightly smaller share of its heap. A failed ring
// allocation is not fatal on either board: paLogRingApplyBootDepth keeps the
// bootstrap ring and logs the failure.
//
// UNKNOWN, and worth closing: no /api/profiler heap reading has been taken from
// a P4 running this image, so the ~114 KB figure is the only measurement the
// depths rest on. A device reading would let these rungs be re-derived from the
// real steady-state heap rather than from a single recorded sample.
//
// `#if defined` rather than `#if`: PA_CHIP_TARGET_* are presence macros defined
// only for the selected chip, not 0/1 Board Capability Gates -- see config.h's
// "Chip target mapping".
#if defined(PA_CHIP_TARGET_ESP32P4)
static constexpr size_t LOG_RING_LINES_ERROR = 32;
static constexpr size_t LOG_RING_LINES_WARN  = 64;
static constexpr size_t LOG_RING_LINES_INFO  = 96;
static constexpr size_t LOG_RING_LINES_DEBUG = 112;
#elif defined(PA_CHIP_TARGET_ESP32)
static constexpr size_t LOG_RING_LINES_ERROR = 16;
static constexpr size_t LOG_RING_LINES_WARN  = 20;
static constexpr size_t LOG_RING_LINES_INFO  = 24;
static constexpr size_t LOG_RING_LINES_DEBUG = 48;
#else
  #error "the log ring depth ladder has no value for this chip target"
#endif

// LOG_RING_BOOTSTRAP_LINES backs the static ring that captures the few boot
// lines emitted before NVS config loads; those lines are carried into the
// boot-sized ring. LOG_RING_MAX_LINES bounds the sized ring and the native
// test storage, so it is the deepest rung by definition rather than a separate
// number that could drift from it.
static constexpr size_t LOG_RING_BOOTSTRAP_LINES = 8;
static constexpr size_t LOG_RING_MAX_LINES = LOG_RING_LINES_DEBUG;

static_assert(LOG_RING_LINES_ERROR <= LOG_RING_LINES_WARN &&
                  LOG_RING_LINES_WARN <= LOG_RING_LINES_INFO &&
                  LOG_RING_LINES_INFO <= LOG_RING_LINES_DEBUG,
              "the log ring ladder must not lose depth as verbosity rises");
static_assert(LOG_RING_BOOTSTRAP_LINES <= LOG_RING_LINES_ERROR,
              "the boot-sized ring must be able to carry the bootstrap lines over");

// Ring depth for a runtime log level (1=Error .. 4=Debug); out-of-range low
// values get the minimum depth, high values the maximum.
size_t logRingLinesForLevel(uint8_t level);

// -----------------------------------------------------------------------------
// LogBuffer  --  plain-old-data ring view over caller-owned storage.
// Zero-initialise, then logBufferInit() with storage before first use.
// -----------------------------------------------------------------------------
struct LogBuffer {
    char (*lines)[LOG_LINE_MAX];  // caller-owned storage, `capacity` slots
    size_t capacity;        // number of line slots in `lines`
    size_t count;           // number of valid entries (0..capacity)
    size_t head;            // index where the NEXT write will go (wraps)
    uint32_t totalWritten;  // monotonically increasing count of all appends ever
};

// -----------------------------------------------------------------------------
// logBufferInit()
// Point the ring at caller-owned storage and reset it to empty.
// params: buf       --  ring buffer to initialise (must not be null)
//         storage   --  array of `capacity` line slots (must not be null)
//         capacity  --  number of slots in storage (must be > 0)
// thread-safe: NO  --  caller must hold any required lock
// -----------------------------------------------------------------------------
void logBufferInit(LogBuffer* buf, char (*storage)[LOG_LINE_MAX], size_t capacity);

// -----------------------------------------------------------------------------
// logBufferAppend()
// Append a null-terminated line to the ring buffer.
// If the buffer is full the oldest entry is overwritten.
// The line is truncated to LOG_LINE_MAX-1 characters.
// params: buf   --  ring buffer to write into (must not be null)
//         line  --  null-terminated string to append (must not be null)
// thread-safe: NO  --  caller must hold any required lock
// -----------------------------------------------------------------------------
void logBufferAppend(LogBuffer* buf, const char* line);

// -----------------------------------------------------------------------------
// logBufferCopy()
// Copy all buffered lines (oldest first) into a caller-supplied flat buffer,
// each line separated by '\n'.  The output is always null-terminated.
// If the output buffer is too small the copy stops and the result is truncated.
// params: buf         --  ring buffer to read from (must not be null)
//         out         --  destination character buffer (must not be null)
//         outSize     --  size of out in bytes (must be > 0)
// returns: number of bytes written (excluding the null terminator)
// thread-safe: NO  --  caller must hold any required lock
// -----------------------------------------------------------------------------
size_t logBufferCopy(const LogBuffer* buf, char* out, size_t outSize);

// -----------------------------------------------------------------------------
// logBufferDrainNext()
// Read the next line at or after *cursor into `out` and advance *cursor by one.
//
// The cursor is an index into the ring's `totalWritten` sequence, not a slot,
// so it survives wrap: a reader that falls behind is told how many lines the
// writer overwrote before it caught up rather than silently skipping them.
// That is the whole difference from copyNewLogLinesSince() (src/main.cpp),
// which drops evicted lines without counting them.
//
// The Console task's serial drain is the caller (ADR 0037): it owns the wire,
// keeps one cursor, and writes what this hands it. Kept here, pure, so the
// cursor arithmetic - the part with the wrap and the eviction accounting in it
// - is provable on the host without a board or a task.
//
// params: buf      --  ring buffer to read from (must not be null)
//         cursor   --  in/out: next totalWritten index to read (must not be null)
//         out      --  destination for the line (must not be null)
//         outSize  --  size of out in bytes (must be > 0); the line is
//                      truncated to outSize - 1 and always null-terminated
//         evicted  --  out: how many lines the writer overwrote before the one
//                      returned, 0 when the reader had kept up. May be null.
//                      Set on every call, including the ones returning false.
// returns: true when a line was copied; false when the cursor has caught up
//          with the writer (nothing new to read)
// thread-safe: NO  --  caller must hold any required lock
// -----------------------------------------------------------------------------
bool logBufferDrainNext(const LogBuffer* buf, uint32_t* cursor, char* out, size_t outSize,
                        uint32_t* evicted);

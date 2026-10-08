// =============================================================================
// include/soft_uart_storm_guard.h
//
// The soft-UART RX storm guard's decision (#417 F15), kept pure so it runs
// under native test. The ISR in src/drivers/audio_soft_uart_rx.cpp calls it
// once per falling edge, before it busy-waits a byte; the guard only counts
// and decides. Disabling the interrupt, and re-arming it later from AudioTask,
// is the caller's.
//
// Why the limit is a protocol number, not a baud-rate number: after the #417
// fix each ISR entry busy-waits about 9.5 bit times (~990 us at 9600 baud), so
// even a floating pin cannot re-enter much more than ~100 times per 100 ms --
// the same rate a continuous 9600-baud stream would produce. Counting against
// "what 9600 baud can deliver" would therefore never trip. What separates
// noise from the MP3 Trigger is how much it ever says at once: its longest
// reply is the S0 version line "=MP3 Trigger v2.NN\r\n", 20 bytes. The limit
// is more than twice that, and trips a storm after ~50 ms of ISR time.
// =============================================================================
#pragma once

#include <stdint.h>

// Counting window, in microseconds. The caller converts it to CPU cycles.
static constexpr uint32_t SOFT_UART_STORM_WINDOW_US = 100000u;

// Entries allowed in one window. The next one disables the RX interrupt.
static constexpr uint16_t SOFT_UART_STORM_MAX_ENTRIES = 48u;

struct SoftUartStormWindow {
    uint32_t windowStart;  // cycle count at the window's first entry
    uint16_t entries;      // entries so far in this window; 0 = no window open
};

// One ISR entry at cycle count `now`. Returns true when this entry is over the
// limit and the caller must disable the interrupt.
//
// `now - windowStart` is unsigned so it survives the 32-bit cycle counter
// wrapping inside a window. A gap longer than a whole counter period (~17.9 s
// at 240 MHz) can alias into the old window and add one entry to it instead of
// opening a new one; that needs dozens of such aliases in a row to matter, and
// a real storm closes its window in well under a period.
inline bool softUartStormGuardEnter(SoftUartStormWindow& w, uint32_t now,
                                    uint32_t windowTicks) {
    if (w.entries == 0u || (uint32_t)(now - w.windowStart) >= windowTicks) {
        w.windowStart = now;
        w.entries = 1u;
    } else if (w.entries < UINT16_MAX) {
        w.entries = (uint16_t)(w.entries + 1u);
    }
    return w.entries > SOFT_UART_STORM_MAX_ENTRIES;
}

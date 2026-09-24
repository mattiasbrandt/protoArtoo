// =============================================================================
// src/drivers/audio_soft_uart_rx.h
//
// Soft-UART RX on PIN_AUDIO_RX for boards where audio shares the dome UART
// controller (PA_CAP_DEDICATED_AUDIO_UART == 0). Sampling happens in a GPIO
// falling-edge ISR so a finish byte can land while the dome still owns UART2
// (#396). Dedicated-UART boards do not include this path.
//
// Not a public header -- only audio driver .cpp files, and AudioTask for the
// storm guard's re-arm, should include this.
// The ISR itself lives in audio_soft_uart_rx.cpp: an IRAM_ATTR inline in a
// header pulled Arduino digitalRead/delayMicroseconds literals after the
// IRAM code (l32r "literal placed after use" on artoo-esp32).
// =============================================================================
#pragma once

#include "audio_soft_uart_tx.h"

#if !PA_CAP_DEDICATED_AUDIO_UART

static constexpr uint8_t SOFT_UART_RX_BUF = 64;

inline volatile uint8_t s_softUartRxBuf[SOFT_UART_RX_BUF] = {};
inline volatile uint8_t s_softUartRxHead = 0;
inline volatile uint8_t s_softUartRxTail = 0;

inline void softUartRxPush(uint8_t b) {
    uint8_t next = (uint8_t)((s_softUartRxHead + 1u) % SOFT_UART_RX_BUF);
    if (next == s_softUartRxTail) {
        return;
    }
    s_softUartRxBuf[s_softUartRxHead] = b;
    s_softUartRxHead = next;
}

#if defined(PA_NATIVE_TEST_STUBS)
inline void softUartRxIsr() {}
inline void softUartRxGuardReset() {}
#else
void softUartRxIsr();
// Opens a fresh storm-guard window and clears the tripped flag (#417 F15).
// Call before the RX interrupt is attached, or while it is off.
void softUartRxGuardReset();
#endif

// Storm guard, AudioTask side (#417 F15). A floating or noisy PIN_AUDIO_RX
// (GPIO 35 has no internal pull; see docs/pin_map.md) makes the ISR re-enter
// back to back, each entry a ~1 ms busy-wait on Core 0. Past
// SOFT_UART_STORM_MAX_ENTRIES in one window the ISR switches its own interrupt
// off and raises this flag; AudioTask logs it and, after a backoff, re-arms.
// Only audio_task.cpp calls these, and it is not built natively, so they have
// no native stub.
//
// True once the ISR has disabled itself; *spanUs is how long the window took
// to fill (SOFT_UART_STORM_MAX_ENTRIES + 1 entries).
bool softUartRxStormTripped(uint32_t* spanUs);
// Clears the tripped flag and any edge latched while off, then re-enables the
// RX interrupt. False when gpio_intr_enable() refused.
bool softUartRxRearm();

inline void softUartRxBegin() {
    pinMode(PIN_AUDIO_RX, INPUT);
    s_softUartRxHead = 0;
    s_softUartRxTail = 0;
    softUartRxGuardReset();
    attachInterrupt(digitalPinToInterrupt(PIN_AUDIO_RX), softUartRxIsr, FALLING);
}

inline int softUartRxAvailable() {
    uint8_t head = s_softUartRxHead;
    uint8_t tail = s_softUartRxTail;
    return (int)((head + SOFT_UART_RX_BUF - tail) % SOFT_UART_RX_BUF);
}

inline int softUartRxRead() {
    uint8_t tail = s_softUartRxTail;
    if (tail == s_softUartRxHead) {
        return -1;
    }
    int b = s_softUartRxBuf[tail];
    s_softUartRxTail = (uint8_t)((tail + 1u) % SOFT_UART_RX_BUF);
    return b;
}

#endif  // !PA_CAP_DEDICATED_AUDIO_UART

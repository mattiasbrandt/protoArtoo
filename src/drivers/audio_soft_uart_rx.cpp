// =============================================================================
// src/drivers/audio_soft_uart_rx.cpp
//
// GPIO falling-edge ISR for MP3 Trigger RX on artoo-esp32 (#396).
// Lives in a translation unit so IRAM code does not pull Arduino flash
// literals (delayMicroseconds / digitalRead) after the ISR.
// =============================================================================

#include "audio_soft_uart_rx.h"

#if !PA_CAP_DEDICATED_AUDIO_UART && !defined(PA_NATIVE_TEST_STUBS)

#include "driver/gpio.h"
#include "rom/ets_sys.h"

void IRAM_ATTR softUartRxIsr() {
    // Start bit already seen. Mid-first-data-bit, then 8 bits LSB-first, stop.
    ets_delay_us(SOFT_UART_BIT_US + (SOFT_UART_BIT_US / 2u));
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        if (gpio_get_level(static_cast<gpio_num_t>(PIN_AUDIO_RX)) != 0) {
            b = static_cast<uint8_t>(b | static_cast<uint8_t>(1u << i));
        }
        ets_delay_us(SOFT_UART_BIT_US);
    }
    ets_delay_us(SOFT_UART_BIT_US);
    softUartRxPush(b);
}

#endif

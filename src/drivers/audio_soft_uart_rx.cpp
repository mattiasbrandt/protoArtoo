// =============================================================================
// src/drivers/audio_soft_uart_rx.cpp
//
// GPIO falling-edge ISR for MP3 Trigger RX on artoo-esp32 (#396).
// Lives in a translation unit so IRAM code does not pull Arduino flash
// literals (delayMicroseconds / digitalRead) after the ISR.
//
// Two things bound what this ISR costs Core 0 (#417 F15):
//   - It clears PIN_AUDIO_RX's latched edge before returning. IDF's GPIO ISR
//     service clears an edge-triggered pin's status on ENTRY
//     (esp_driver_gpio/src/gpio.c gpio_isr_loop, isr_clr_on_entry_mask), so
//     every falling edge inside the byte being sampled -- '=' has two --
//     re-latched and re-ran the ISR mid-byte as a phantom start bit.
//   - A storm guard (include/soft_uart_storm_guard.h) switches the interrupt
//     off when a floating or noisy pin re-enters past what the MP3 Trigger
//     ever sends; AudioTask re-arms it after a backoff.
//
// The register calls are hal/gpio_ll.h for the ESP32 (IDF 5.5.2, from
// framework-arduinoespressif32-libs): always-inline writes, so nothing here
// calls into flash for them. Pins 32-39 are in the high status register
// (status1_w1tc), which is why the clear picks its half by pin number, as
// gpio_hal_clear_intr_status_bit does.
// =============================================================================

#include "audio_soft_uart_rx.h"

#if !PA_CAP_DEDICATED_AUDIO_UART && !defined(PA_NATIVE_TEST_STUBS)

#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "rom/ets_sys.h"
#include "soc/gpio_struct.h"
#include "soft_uart_storm_guard.h"

// Written by the ISR on Core 0. softUartRxGuardReset() writes them only while
// the interrupt is not attached or is switched off, so the two never race.
static SoftUartStormWindow s_stormWindow = {};
static uint32_t s_stormWindowTicks = 0;
static uint32_t s_cpuTicksPerUs = 0;
static volatile bool s_stormTripped = false;
static volatile uint32_t s_stormSpanUs = 0;

static inline void IRAM_ATTR clearRxEdgeLatch() {
    if constexpr (PIN_AUDIO_RX < 32) {
        gpio_ll_clear_intr_status(&GPIO, 1u << PIN_AUDIO_RX);
    } else {
        gpio_ll_clear_intr_status_high(&GPIO, 1u << (PIN_AUDIO_RX - 32));
    }
}

void IRAM_ATTR softUartRxIsr() {
    // Count first, before the busy-wait: a tripped entry costs no byte time.
    const uint32_t now = esp_cpu_get_cycle_count();
    if (softUartStormGuardEnter(s_stormWindow, now, s_stormWindowTicks)) {
        gpio_ll_intr_disable(&GPIO, PIN_AUDIO_RX);
        clearRxEdgeLatch();
        s_stormSpanUs = (now - s_stormWindow.windowStart) / s_cpuTicksPerUs;
        s_stormTripped = true;
        return;
    }

    // Start bit already seen. Mid-first-data-bit, then 8 bits LSB-first; the
    // delay after the last data bit lands mid-stop-bit.
    ets_delay_us(SOFT_UART_BIT_US + (SOFT_UART_BIT_US / 2u));
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        if (gpio_get_level(static_cast<gpio_num_t>(PIN_AUDIO_RX)) != 0) {
            b = static_cast<uint8_t>(b | static_cast<uint8_t>(1u << i));
        }
        ets_delay_us(SOFT_UART_BIT_US);
    }
    const bool stopBitHigh = gpio_get_level(static_cast<gpio_num_t>(PIN_AUDIO_RX)) != 0;

    // Mid-stop-bit the line is high until the next start bit, so clearing here
    // drops only edges from inside this byte; the next real start edge still
    // latches. No wait for the rest of the stop bit.
    clearRxEdgeLatch();

    // A low stop bit is a framing error -- noise, or a start taken mid-byte.
    if (stopBitHigh) {
        softUartRxPush(b);
    }
}

void softUartRxGuardReset() {
    s_cpuTicksPerUs = esp_rom_get_cpu_ticks_per_us();
    s_stormWindowTicks = SOFT_UART_STORM_WINDOW_US * s_cpuTicksPerUs;
    s_stormWindow = {};
    s_stormSpanUs = 0;
    s_stormTripped = false;
}

bool softUartRxStormTripped(uint32_t* spanUs) {
    if (!s_stormTripped) {
        return false;
    }
    if (spanUs != nullptr) {
        *spanUs = s_stormSpanUs;
    }
    return true;
}

bool softUartRxRearm() {
    // The interrupt is off, so the ISR cannot run while the guard resets. Drop
    // any edge latched while it was off, or it would fire the moment it is on.
    softUartRxGuardReset();
    clearRxEdgeLatch();
    return gpio_intr_enable(static_cast<gpio_num_t>(PIN_AUDIO_RX)) == ESP_OK;
}

#endif

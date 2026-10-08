// =============================================================================
// test/test_native/test_soft_uart_storm_guard/test_soft_uart_storm_guard.cpp
//
// The soft-UART RX storm guard's decision (#417 F15): entries in a window in,
// disable yes/no out. The ISR that calls it cannot run natively; this covers
// the decision only.
// =============================================================================

#include <unity.h>

#include <stdint.h>

#include "../../../include/soft_uart_storm_guard.h"

// 240 MHz: the cycle counter's rate on the ESP32 this guard runs on.
static constexpr uint32_t kTicksPerUs = 240u;
static constexpr uint32_t kWindowTicks = SOFT_UART_STORM_WINDOW_US * kTicksPerUs;
// One byte's ISR time at 9600 baud, 10 bit times, in cycles.
static constexpr uint32_t kByteTicks = 1042u * kTicksPerUs;

void setUp() {}
void tearDown() {}

// The MP3 Trigger's longest reply, the 20-byte S0 version line, arrives
// back to back and never trips the guard.
static void test_longest_reply_back_to_back_never_trips() {
    SoftUartStormWindow w{};
    uint32_t now = 1000u;
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_FALSE(softUartStormGuardEnter(w, now, kWindowTicks));
        now += kByteTicks;
    }
}

// A storm trips on the first entry past the limit inside one window, and not
// on the entry before it.
static void test_storm_trips_on_first_entry_past_limit() {
    SoftUartStormWindow w{};
    uint32_t now = 5000u;
    for (uint32_t i = 0; i < SOFT_UART_STORM_MAX_ENTRIES; i++) {
        TEST_ASSERT_FALSE(softUartStormGuardEnter(w, now, kWindowTicks));
        now += kByteTicks;
    }
    TEST_ASSERT_TRUE(softUartStormGuardEnter(w, now, kWindowTicks));
}

// Traffic spread over windows opens a new window each time and never trips,
// however long it runs.
static void test_new_window_resets_the_count() {
    SoftUartStormWindow w{};
    uint32_t now = 0u;
    for (int window = 0; window < 10; window++) {
        for (uint32_t i = 0; i < SOFT_UART_STORM_MAX_ENTRIES; i++) {
            TEST_ASSERT_FALSE(softUartStormGuardEnter(w, now + i, kWindowTicks));
        }
        now += kWindowTicks;
    }
}

// The 32-bit cycle counter wrapping inside a window does not open a new one.
static void test_counter_wrap_inside_window_keeps_counting() {
    SoftUartStormWindow w{};
    uint32_t now = UINT32_MAX - (kByteTicks * 10u);
    for (uint32_t i = 0; i < SOFT_UART_STORM_MAX_ENTRIES; i++) {
        TEST_ASSERT_FALSE(softUartStormGuardEnter(w, now, kWindowTicks));
        now += kByteTicks;  // wraps past zero partway through
    }
    TEST_ASSERT_TRUE(softUartStormGuardEnter(w, now, kWindowTicks));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_longest_reply_back_to_back_never_trips);
    RUN_TEST(test_storm_trips_on_first_entry_past_limit);
    RUN_TEST(test_new_window_resets_the_count);
    RUN_TEST(test_counter_wrap_inside_window_keeps_counting);
    return UNITY_END();
}

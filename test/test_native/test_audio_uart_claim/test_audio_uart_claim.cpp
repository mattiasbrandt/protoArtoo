// =============================================================================
// test/test_native/test_audio_uart_claim/test_audio_uart_claim.cpp
//
// Tests for audioUartClaim() / audioUartRelease() (include/dome_link.h, #254).
//
// These two helpers are the seam that decides whether an audio status query has
// to borrow the dome link's UART controller. On a board without
// PA_CAP_DEDICATED_AUDIO_UART it does, and the claim can be refused -- which is
// what AudioTask reports as AUDIO_RX_BLOCKED_BY_DOME_UART. On a board with the
// capability audio owns UART_PORT_AUDIO outright and the claim never fails.
//
// The native env compiles as PA_BOARD_ARTOO_ESP32 (platformio.ini env:native),
// so this file covers the SHARED posture -- which is the one with behaviour to
// get wrong, and the one the shipping artoo image runs. The dedicated posture
// is a compile-time constant on the other side of the same #if; it is covered
// by test/test_tools/test_board_uart_allocation.py (both boards' capability and
// allocation values, with the coherence static_asserts shown firing) and by the
// firebeetle2 build linking the branch.
//
// Test seam: the arbiter stub in native_test_stubs.cpp stands in for
// robotState.domeUartOwner without requiring FreeRTOS. It is driven here
// through the same public API production uses rather than through its backing
// variable -- releasing both owners is an unconditional way back to
// DOME_UART_NONE, and it keeps the test honest about the contract instead of
// reaching past it.
// =============================================================================

#include <unity.h>

#include "../../../include/config.h"
#include "../../../include/dome_link.h"

namespace {
void resetUartOwner() {
    domeUartRelease(DOME_UART_DOME);
    domeUartRelease(DOME_UART_AUDIO);
}
}  // namespace

void setUp()    { resetUartOwner(); }
void tearDown() { resetUartOwner(); }

// Guard the premise. If this board ever declared the capability, every
// assertion below would be testing the wrong branch and would still pass, so
// fail loudly here instead of silently going vacuous.
void test_native_env_exercises_the_shared_controller_posture() {
    TEST_ASSERT_EQUAL_INT(0, PA_CAP_DEDICATED_AUDIO_UART);
    TEST_ASSERT_EQUAL_UINT8(UART_PORT_DOME, UART_PORT_AUDIO);
}

// Free controller: the claim succeeds and audio is recorded as the owner, so
// the dome link cannot re-open the port underneath an in-flight query.
void test_claim_succeeds_and_takes_ownership_when_controller_is_free() {
    TEST_ASSERT_TRUE(audioUartClaim());
    TEST_ASSERT_TRUE(domeUartOwnedBy(DOME_UART_AUDIO));
}

// The behaviour that must NOT be lost on artoo-esp32: while the dome link holds
// the shared controller the claim is refused, which is what makes AudioTask
// report AUDIO_RX_BLOCKED_BY_DOME_UART instead of "module not responding".
void test_claim_is_refused_while_the_dome_link_holds_the_controller() {
    TEST_ASSERT_TRUE(domeUartAcquire(DOME_UART_DOME));

    TEST_ASSERT_FALSE(audioUartClaim());
    TEST_ASSERT_TRUE(domeUartOwnedBy(DOME_UART_DOME));
}

// A refused claim must not have taken the port away from the dome link as a
// side effect, and the paired release must not steal it either -- AudioTask
// only releases when the claim succeeded, but the arbiter has to be safe
// against the ordering regardless.
void test_refused_claim_leaves_the_dome_link_owning_the_controller() {
    TEST_ASSERT_TRUE(domeUartAcquire(DOME_UART_DOME));

    TEST_ASSERT_FALSE(audioUartClaim());
    audioUartRelease();

    TEST_ASSERT_TRUE(domeUartOwnedBy(DOME_UART_DOME));
}

// Claim/release is paired: after release the controller is free for the dome
// link to reacquire. A leaked claim would strand the dome link off UART.
void test_release_returns_the_controller_so_the_dome_link_can_reacquire() {
    TEST_ASSERT_TRUE(audioUartClaim());
    audioUartRelease();

    TEST_ASSERT_TRUE(domeUartOwnedBy(DOME_UART_NONE));
    TEST_ASSERT_TRUE(domeUartAcquire(DOME_UART_DOME));
    TEST_ASSERT_TRUE(domeUartOwnedBy(DOME_UART_DOME));
}

// Repeated poll cycles must not drift: AudioTask claims and releases on every
// ~2 s auto-query, so an unbalanced pair would show up as the dome link losing
// the port after some number of polls rather than immediately.
void test_repeated_claim_release_cycles_leave_no_residue() {
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(audioUartClaim());
        audioUartRelease();
    }
    TEST_ASSERT_TRUE(domeUartOwnedBy(DOME_UART_NONE));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_native_env_exercises_the_shared_controller_posture);
    RUN_TEST(test_claim_succeeds_and_takes_ownership_when_controller_is_free);
    RUN_TEST(test_claim_is_refused_while_the_dome_link_holds_the_controller);
    RUN_TEST(test_refused_claim_leaves_the_dome_link_owning_the_controller);
    RUN_TEST(test_release_returns_the_controller_so_the_dome_link_can_reacquire);
    RUN_TEST(test_repeated_claim_release_cycles_leave_no_residue);
    return UNITY_END();
}

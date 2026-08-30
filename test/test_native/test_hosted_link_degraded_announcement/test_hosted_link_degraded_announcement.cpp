// =============================================================================
// test_native/test_hosted_link_degraded_announcement.cpp
//
// The dome logic-text command fired once when the Hosted Link Supervisor
// settles into the terminal degraded state
// (include/hosted_link_degraded_announcement.h) must validate against the
// real Protocol Check grammar (src/protocol_check.cpp), not by inspection.
// protocolCheckBranch() is the same validator every Learned Sequence's DT:
// step passes through (ADR 0006), so this proves the constant is a command
// AstroPixelsPlus would actually accept -- a later grammar change that would
// reject it now fails here instead of only being discovered on hardware
// (#189).
// =============================================================================

#include <unity.h>
#include <string.h>

#include "hosted_link_degraded_announcement.h"
#include "protocol_check.h"
#include "sequence_engine.h"

static SeqStep createDomeStep(const char* cmd, uint32_t tMs = 0) {
    SeqStep s = {};
    s.type = STEP_DOME_CMD;
    s.tMs = tMs;
    s.effectClass = FX_NONE;
    strncpy(s.payload, cmd, sizeof(s.payload) - 1);
    s.payload[sizeof(s.payload) - 1] = '\0';
    return s;
}

static SeqStep createEndStep(uint32_t tMs = 100) {
    SeqStep s = {};
    s.type = STEP_END;
    s.tMs = tMs;
    s.effectClass = FX_NONE;
    return s;
}

void setUp(void) {
}

void tearDown(void) {
}

void test_degraded_dome_text_is_prefixed_DT() {
    // Guards against a future edit accidentally pointing this constant at a
    // different command family (DL:/DH:) whose grammar this test does not
    // exercise.
    TEST_ASSERT_EQUAL_INT(0, strncmp(kHostedLinkDegradedDomeText, "DT:", 3));
}

void test_degraded_dome_text_passes_the_real_protocol_check() {
    SeqStep steps[] = {
        createDomeStep(kHostedLinkDegradedDomeText),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE_MESSAGE(r.ok, r.message);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_degraded_dome_text_fits_the_63_char_command_ceiling() {
    // docs/dome-visual-authoring-contract.md: "every generated command must
    // be <= 63 chars" (the Marcduino/AstroPixelsPlus transport limit).
    TEST_ASSERT_TRUE(strlen(kHostedLinkDegradedDomeText) <= 63);
}

void test_degraded_dome_text_fits_the_dome_tx_queue_buffer() {
    // include/dome_link.h: DomeTxCmd::buf is 64 bytes and DomeLinkTask appends
    // the trailing '\r' itself, so the command (without '\r') plus NUL must
    // fit in 64 bytes.
    TEST_ASSERT_TRUE(strlen(kHostedLinkDegradedDomeText) + 1 <= 64);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_degraded_dome_text_is_prefixed_DT);
    RUN_TEST(test_degraded_dome_text_passes_the_real_protocol_check);
    RUN_TEST(test_degraded_dome_text_fits_the_63_char_command_ceiling);
    RUN_TEST(test_degraded_dome_text_fits_the_dome_tx_queue_buffer);
    return UNITY_END();
}

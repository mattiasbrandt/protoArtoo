// =============================================================================
// test/test_native/test_task_stack_floors/test_task_stack_floors.cpp
//
// Every project-created task's stack is re-derived from its Measured Chain by
// the sizing rule, or pinned with a recorded reason where the rule is declined
// (#271, ADR 0038).
//
// include/config.h's static_asserts already stop a stack falling below its own
// chain -- put one there and the compile fails before a single test runs, which
// is the intended order. What those asserts cannot see is a value that still
// clears its chain but no longer follows the derivation it claims: 8192 for the
// Console, say, which covers 7024 and is not what the rule gives. That is what
// this file catches, task by task.
//
// The three states, and why there are three rather than two:
//
//   rule      - the stack is EXACTLY chain + 25% rounded up to the next 512 B.
//   above     - the stack is deliberately larger than the rule, because an
//               earlier ticket raised it on evidence the rule does not carry.
//               Lowering such an arm "to match the rule" would undo that.
//   declined  - the stack is below the rule and still covers the chain. This is
//               #248's tight-heap argument: raising all six declined arms on
//               this board costs 6144 B against ~42.7 KB of measured free heap,
//               for margin the Xtensa walk cannot confirm. ADR 0038 requires
//               the decline to be recorded; include/config.h carries the reason
//               beside each constant.
//
// Native tests always build PA_BOARD_ARTOO_ESP32 (platformio.ini env:native),
// so this file can only reach the artoo-esp32 arm -- which is the arm with all
// the interesting cases. The ESP32-P4 arm, where every task is on the rule, is
// proven by the cross-board compiler probe in
// test/test_tools/test_task_stack_recipes.py, and neither arm's CHAIN is
// checked against the image here: that needs a linked ELF and is
// tools/check_task_stack_chains.py, run as a slice-gate row.
// =============================================================================
#include <unity.h>

#include "config.h"

void setUp() {
}

void tearDown() {
}

// The #248 sizing rule, written once: worst-case chain + 25%, rounded up to the
// next 512 bytes. Integer arithmetic throughout -- the constants are integers
// and a float round-trip is how an off-by-one arrives.
static uint32_t stackByTheRule(uint32_t chainBytes) {
    const uint32_t need = (chainBytes * 5U + 3U) / 4U;
    return ((need + 511U) / 512U) * 512U;
}

// Every task on this chip arm, with the derivation each constant claims.
enum RuleState { RULE_APPLIED, RULE_ABOVE, RULE_DECLINED };

struct TaskStackArm {
    const char* task;
    uint32_t chain;
    uint32_t stack;
    RuleState state;
};

// Values are restated here rather than only referenced, so that an edit which
// moves a constant AND its chain together -- the shape a "simplification" takes
// -- still turns this red. tools/task_stack_recipes.json holds the same figures
// with the recipe that produced them, and
// test/test_tools/test_task_stack_recipes.py asserts the two agree.
static const TaskStackArm kArms[] = {
    {"DriveTask", 4080U, 5632U, RULE_ABOVE},
    {"RCInputTask", 5248U, 7168U, RULE_ABOVE},
    {"ServoTask", 3200U, 4096U, RULE_APPLIED},
    {"DomeTask", 2992U, 3072U, RULE_DECLINED},
    {"AudioTask", 5280U, 6144U, RULE_DECLINED},
    {"AuxLedTask", 3504U, 4096U, RULE_DECLINED},
    {"DomeLinkTask", 5872U, 6144U, RULE_DECLINED},
    {"SafetyMonitor", 3088U, 4096U, RULE_APPLIED},
    {"SeqDisp", 4336U, 5632U, RULE_APPLIED},
    {"Console", 7360U, 9216U, RULE_APPLIED},
    {"WebEvents", 5904U, 6144U, RULE_DECLINED},
    {"ArduinoOTA", 3696U, 4096U, RULE_DECLINED},
};
static const size_t kArmCount = sizeof(kArms) / sizeof(kArms[0]);

// The constants themselves, in the same order, read from the header. Separated
// from the table above on purpose: the table is this test's independent claim,
// this is what config.h actually declares, and the tests compare the two.
static const TaskStackArm kDeclared[] = {
    {"DriveTask", DRIVE_TASK_MEASURED_CHAIN_BYTES, DRIVE_TASK_STACK_BYTES, RULE_ABOVE},
    {"RCInputTask", RC_INPUT_TASK_MEASURED_CHAIN_BYTES, RC_INPUT_TASK_STACK_BYTES, RULE_ABOVE},
    {"ServoTask", SERVO_TASK_MEASURED_CHAIN_BYTES, SERVO_TASK_STACK_BYTES, RULE_APPLIED},
    {"DomeTask", DOME_TASK_MEASURED_CHAIN_BYTES, DOME_TASK_STACK_BYTES, RULE_DECLINED},
    {"AudioTask", AUDIO_TASK_MEASURED_CHAIN_BYTES, AUDIO_TASK_STACK_BYTES, RULE_DECLINED},
    {"AuxLedTask", AUX_LED_TASK_MEASURED_CHAIN_BYTES, AUX_LED_TASK_STACK_BYTES, RULE_DECLINED},
    {"DomeLinkTask", DOME_LINK_TASK_MEASURED_CHAIN_BYTES, DOME_LINK_TASK_STACK_BYTES,
     RULE_DECLINED},
    {"SafetyMonitor", SAFETY_MONITOR_MEASURED_CHAIN_BYTES, SAFETY_MONITOR_STACK_BYTES,
     RULE_APPLIED},
    {"SeqDisp", SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES, SEQ_DISPATCHER_TASK_STACK_BYTES,
     RULE_APPLIED},
    {"Console", CONSOLE_TASK_MEASURED_CHAIN_BYTES, CONSOLE_TASK_STACK_BYTES, RULE_APPLIED},
    {"WebEvents", WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES, WEB_EVENTS_TASK_STACK_BYTES,
     RULE_DECLINED},
    {"ArduinoOTA", OTA_TASK_MEASURED_CHAIN_BYTES, OTA_TASK_STACK_BYTES, RULE_DECLINED},
};

// The rule itself, on the worked example config.h cites: #245 arrived at 4096
// for a 3152 B chain by judgement, and the rule reproduces it from the
// measurement alone. If this is wrong, every assertion below is wrong the same
// way, so it is checked first.
void test_the_sizing_rule_reproduces_the_size_245_reached_by_judgement() {
    TEST_ASSERT_EQUAL_UINT32(4096U, stackByTheRule(3152U));
    // The rounding is up, at both ends of a step: one byte past a step boundary
    // must not stay on it.
    TEST_ASSERT_EQUAL_UINT32(512U, stackByTheRule(1U));
    TEST_ASSERT_EQUAL_UINT32(512U, stackByTheRule(409U));   // 409 * 1.25 = 511.25
    TEST_ASSERT_EQUAL_UINT32(1024U, stackByTheRule(410U));  // 410 * 1.25 = 512.5
}

// Every arm covers its own chain. This duplicates config.h's static_asserts on
// purpose: those fire at compile time and are therefore invisible in the test
// report, and a suite that never states the floor cannot show it was checked.
void test_every_task_stack_covers_its_measured_chain() {
    for (size_t i = 0; i < kArmCount; ++i) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(
            kDeclared[i].chain, kDeclared[i].stack, kDeclared[i].task);
    }
}

// The header's constants are the ones this file claims they are. Catches the
// edit that moves a constant without moving the expectation with it.
void test_declared_constants_match_the_expected_arms() {
    for (size_t i = 0; i < kArmCount; ++i) {
        TEST_ASSERT_EQUAL_STRING(kArms[i].task, kDeclared[i].task);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(kArms[i].chain, kDeclared[i].chain, kArms[i].task);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(kArms[i].stack, kDeclared[i].stack, kArms[i].task);
    }
}

// The derivation, per arm. This is the assertion that stops a constant drifting
// from the rule it claims to follow.
void test_every_arm_follows_the_derivation_it_claims() {
    for (size_t i = 0; i < kArmCount; ++i) {
        const uint32_t byRule = stackByTheRule(kDeclared[i].chain);
        switch (kArms[i].state) {
            case RULE_APPLIED:
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(byRule, kDeclared[i].stack, kArms[i].task);
                break;
            case RULE_ABOVE:
                TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(byRule, kDeclared[i].stack,
                                                        kArms[i].task);
                break;
            case RULE_DECLINED:
                // Below the rule and above the chain. The upper bound is what
                // makes "declined" a claim rather than a label: an arm that
                // reaches the rule must be relabelled, not left saying it
                // declined.
                TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(byRule, kDeclared[i].stack, kArms[i].task);
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(kDeclared[i].chain,
                                                            kDeclared[i].stack, kArms[i].task);
                break;
        }
    }
}

// Stacks move in 512-byte steps, on every arm. A value between steps means
// somebody typed a number instead of applying the rule.
void test_every_task_stack_is_a_whole_512_byte_step() {
    for (size_t i = 0; i < kArmCount; ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, kDeclared[i].stack % 512U, kDeclared[i].task);
    }
}

// SafetyMonitor is the one arm this ticket had to move, and the reason is worth
// pinning separately: 3072 stood here from before #245 as a deliberate hold
// against that ticket's ESP32-P4 raise, and #271's walk found it 16 B BELOW the
// chain rather than above it -- the artoo profiler image needs 3088 B, and
// config.h is compiled into that image as well as the product one.
//
// A floor that fails is not the margin question #248 declined, so this arm pays
// the rule. Putting it back to 3072 fails config.h's own static_assert before
// this test runs.
void test_safety_monitor_was_raised_because_its_floor_failed() {
    TEST_ASSERT_EQUAL_UINT32(3088U, SAFETY_MONITOR_MEASURED_CHAIN_BYTES);
    TEST_ASSERT_EQUAL_UINT32(4096U, SAFETY_MONITOR_STACK_BYTES);
    TEST_ASSERT_GREATER_THAN_UINT32(3072U, SAFETY_MONITOR_MEASURED_CHAIN_BYTES);
    TEST_ASSERT_EQUAL_UINT32(stackByTheRule(SAFETY_MONITOR_MEASURED_CHAIN_BYTES),
                             SAFETY_MONITOR_STACK_BYTES);
}

// The three tasks that had no static measurement at all before #271: ServoTask
// (a Core 1 real-time task sized from a high-water mark), the ArduinoOTA task
// and HostedRecovery. Two of the three are reachable from this binary;
// HostedRecovery exists only where PA_CAP_HOSTED_WIFI is 1 and is proven in
// test/test_tools/test_task_stack_recipes.py.
void test_the_previously_unmeasured_tasks_now_carry_chains() {
    TEST_ASSERT_EQUAL_UINT32(3200U, SERVO_TASK_MEASURED_CHAIN_BYTES);
    TEST_ASSERT_EQUAL_UINT32(4096U, SERVO_TASK_STACK_BYTES);
    TEST_ASSERT_EQUAL_UINT32(3696U, OTA_TASK_MEASURED_CHAIN_BYTES);
    TEST_ASSERT_EQUAL_UINT32(4096U, OTA_TASK_STACK_BYTES);
    // artoo-esp32 declares no HostedRecovery pair at all - the task is in no
    // image this board builds, so a chain for it here would be invented. That
    // absence is asserted where a host test can see both arms.
    TEST_ASSERT_EQUAL_INT(0, PA_CAP_HOSTED_WIFI);
}

// The thinnest floor in the block, stated so it is a recorded exposure rather
// than a number nobody looked at. DomeTask is a 50 Hz Core 1 task with 80 B
// between its stack and its measured chain, on a walk that is a lower bound and
// that excludes interrupt frames entirely -- less headroom than one interrupt
// entry costs. It is the pre-existing shipping value and #248's tight-heap
// argument is why it stands; if it is ever raised, this test is where the
// decision is recorded.
void test_the_thinnest_declined_floor_is_dome_task_on_this_board() {
    const uint32_t headroom = DOME_TASK_STACK_BYTES - DOME_TASK_MEASURED_CHAIN_BYTES;
    TEST_ASSERT_EQUAL_UINT32(80U, headroom);
    for (size_t i = 0; i < kArmCount; ++i) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(
            headroom, kDeclared[i].stack - kDeclared[i].chain, kDeclared[i].task);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_sizing_rule_reproduces_the_size_245_reached_by_judgement);
    RUN_TEST(test_every_task_stack_covers_its_measured_chain);
    RUN_TEST(test_declared_constants_match_the_expected_arms);
    RUN_TEST(test_every_arm_follows_the_derivation_it_claims);
    RUN_TEST(test_every_task_stack_is_a_whole_512_byte_step);
    RUN_TEST(test_safety_monitor_was_raised_because_its_floor_failed);
    RUN_TEST(test_the_previously_unmeasured_tasks_now_carry_chains);
    RUN_TEST(test_the_thinnest_declined_floor_is_dome_task_on_this_board);
    return UNITY_END();
}

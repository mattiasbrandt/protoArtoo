// =============================================================================
// test/test_native/test_task_stack_floors/test_task_stack_floors.cpp
//
// Properties every task stack on this chip arm must have, read from the
// figures the build actually compiles (ADR 0040, amended 2026-09-27).
//
// The figures have one home, tools/task_stack_recipes.json, and reach the
// firmware through include/task_stack_figures.h, which is generated from it.
// So nothing here restates a figure: a copy is what went stale every time a
// chain was re-derived. The tests walk TASK_STACK_FIGURES, the generated table
// of every task on the selected chip, and assert what must hold for each.
//
// include/config.h's static_asserts already stop a stack falling below its
// own chain -- break one and the compile fails before a single test runs,
// which is the intended order. The rule state of an arm (on the rule, above
// it, declining it) and the reason every departure must carry are asserted
// against the recipe itself in test/test_tools/test_task_stack_recipes.py,
// which also compiles both chips' arms; native tests always build
// PA_BOARD_ARTOO_ESP32 (platformio.ini env:native). Whether a chain still
// matches the image is tools/check_task_stack_chains.py, the slice-gate row.
// =============================================================================
#include <unity.h>

#include "config.h"

void setUp() {
}

void tearDown() {
}

static const size_t kFigureCount = sizeof(TASK_STACK_FIGURES) / sizeof(TASK_STACK_FIGURES[0]);

// The rule is usable where a constant is needed, which is what lets a stack
// be declared from it.
static_assert(taskStackByTheRule(3152U) == 4096U, "the sizing rule is not a constant expression");

// The rule itself, on the worked example config.h cites: #245 arrived at 4096
// for a 3152 B chain by judgement, and the rule reproduces it from the
// measurement alone. This is the one C++ copy of the rule, in the generated
// header; if it is wrong, every stack the rule sized is wrong the same way.
void test_the_sizing_rule_reproduces_the_size_245_reached_by_judgement() {
    TEST_ASSERT_EQUAL_UINT32(4096U, taskStackByTheRule(3152U));
    // The rounding is up, at both ends of a step: one byte past a step boundary
    // must not stay on it.
    TEST_ASSERT_EQUAL_UINT32(512U, taskStackByTheRule(1U));
    TEST_ASSERT_EQUAL_UINT32(512U, taskStackByTheRule(409U));   // 409 * 1.25 = 511.25
    TEST_ASSERT_EQUAL_UINT32(1024U, taskStackByTheRule(410U));  // 410 * 1.25 = 512.5
    // A chain whose 25% lands exactly on a step stays on it.
    TEST_ASSERT_EQUAL_UINT32(5120U, taskStackByTheRule(4096U));  // 4096 * 1.25 = 5120
}

// Every arm covers its own chain. This duplicates config.h's static_asserts on
// purpose: those fire at compile time and are therefore invisible in the test
// report, and a suite that never states the floor cannot show it was checked.
void test_every_task_stack_covers_its_recorded_chain() {
    for (size_t i = 0; i < kFigureCount; ++i) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(TASK_STACK_FIGURES[i].chainBytes,
                                                    TASK_STACK_FIGURES[i].stackBytes,
                                                    TASK_STACK_FIGURES[i].task);
    }
}

// Stacks move in 512-byte steps, on every arm. A value between steps means
// somebody typed a number instead of applying the rule.
void test_every_task_stack_is_a_whole_512_byte_step() {
    for (size_t i = 0; i < kFigureCount; ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, TASK_STACK_FIGURES[i].stackBytes % 512U,
                                         TASK_STACK_FIGURES[i].task);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_sizing_rule_reproduces_the_size_245_reached_by_judgement);
    RUN_TEST(test_every_task_stack_covers_its_recorded_chain);
    RUN_TEST(test_every_task_stack_is_a_whole_512_byte_step);
    return UNITY_END();
}

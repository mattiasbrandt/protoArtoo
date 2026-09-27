// =============================================================================
// include/task_stack_figures.h
//
// Generated from tools/task_stack_recipes.json by tools/check_task_stack_chains.py
// DO NOT EDIT MANUALLY
//
// Every task's Recorded Chain and stack, per chip target. The recipe file is the
// one home of these figures (ADR 0040, amended 2026-09-27): it carries the
// recipe each chain was walked by, why each chain is as deep as it is, and the
// reason wherever a stack is not what the rule gives. To re-derive them, walk
// the chip's product image and record the result:
//
//   python3 tools/check_task_stack_chains.py --rewrite --chip esp32
//
// which rewrites the recipe and this file together; --help says the rest.
// test/test_tools/test_task_stack_figures_drift.py fails when this file is not
// what the recipe generates.
//
// Included by include/config.h once the chip target is mapped. config.h carries
// the sizing rule's rationale and the static_assert that every stack covers
// its chain.
// =============================================================================
#pragma once

#include <stdint.h>

// ADR 0040's sizing rule: the chain plus 25%, rounded up to the next 512 bytes.
// Integer arithmetic throughout - a float round-trip is how an off-by-one
// arrives. The one C++ copy; tools/check_task_stack_chains.py rule_stack() is
// the one Python copy, and the recipe test proves the two agree.
constexpr uint32_t taskStackByTheRule(uint32_t chainBytes) {
    return (((chainBytes * 5U + 3U) / 4U + 511U) / 512U) * 512U;
}

// One row per task on the selected chip, so a test can walk every arm without
// a hand-kept list of them.
struct TaskStackFigure {
    const char* task;
    uint32_t chainBytes;
    uint32_t stackBytes;
};

// `#if defined` rather than `#if`: PA_CHIP_TARGET_* are presence macros defined
// only for the selected chip (include/config.h "Chip target mapping"), not 0/1
// Board Capability Gates, so `#if` on the undefined one would silently take the
// wrong branch. Keying on the chip target rather than on PA_BOARD means a second
// board variant on either chip inherits the right sizes without a new case.
// HostedRecovery is declared only where PA_CAP_HOSTED_WIFI is 1, so a board
// that turns the capability on elsewhere fails config.h's static_assert rather
// than inheriting a figure measured on someone else's silicon.
#if defined(PA_CHIP_TARGET_ESP32P4)
constexpr uint32_t DRIVE_TASK_MEASURED_CHAIN_BYTES = 5088;
constexpr uint32_t DRIVE_TASK_STACK_BYTES = 6656;  // the rule: 5088 -> 6360 -> 6656
constexpr uint32_t RC_INPUT_TASK_MEASURED_CHAIN_BYTES = 5568;
constexpr uint32_t RC_INPUT_TASK_STACK_BYTES = 7168;  // the rule: 5568 -> 6960 -> 7168
constexpr uint32_t SERVO_TASK_MEASURED_CHAIN_BYTES = 3824;
constexpr uint32_t SERVO_TASK_STACK_BYTES = 5120;  // the rule: 3824 -> 4780 -> 5120
constexpr uint32_t DOME_TASK_MEASURED_CHAIN_BYTES = 4096;
constexpr uint32_t DOME_TASK_STACK_BYTES = 5120;  // the rule: 4096 -> 5120 -> 5120
constexpr uint32_t AUDIO_TASK_MEASURED_CHAIN_BYTES = 7104;
constexpr uint32_t AUDIO_TASK_STACK_BYTES = 9216;  // the rule: 7104 -> 8880 -> 9216
constexpr uint32_t AUX_LED_TASK_MEASURED_CHAIN_BYTES = 5456;
constexpr uint32_t AUX_LED_TASK_STACK_BYTES = 7168;  // the rule: 5456 -> 6820 -> 7168
constexpr uint32_t DOME_LINK_TASK_MEASURED_CHAIN_BYTES = 9712;
constexpr uint32_t DOME_LINK_TASK_STACK_BYTES = 12288;  // the rule: 9712 -> 12140 -> 12288
constexpr uint32_t SAFETY_MONITOR_MEASURED_CHAIN_BYTES = 3824;
constexpr uint32_t SAFETY_MONITOR_STACK_BYTES = 5120;  // the rule: 3824 -> 4780 -> 5120
constexpr uint32_t SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES = 5776;
constexpr uint32_t SEQ_DISPATCHER_TASK_STACK_BYTES = 7680;  // the rule: 5776 -> 7220 -> 7680
constexpr uint32_t CONSOLE_TASK_MEASURED_CHAIN_BYTES = 11536;
constexpr uint32_t CONSOLE_TASK_STACK_BYTES = 14848;  // the rule: 11536 -> 14420 -> 14848
constexpr uint32_t WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES = 7264;
constexpr uint32_t WEB_EVENTS_TASK_STACK_BYTES = 9216;  // the rule: 7264 -> 9080 -> 9216
constexpr uint32_t OTA_TASK_MEASURED_CHAIN_BYTES = 6416;
constexpr uint32_t OTA_TASK_STACK_BYTES = 8192;  // the rule: 6416 -> 8020 -> 8192
constexpr uint32_t HOSTED_RECOVERY_TASK_MEASURED_CHAIN_BYTES = 4560;
constexpr uint32_t HOSTED_RECOVERY_TASK_STACK_BYTES = 6144;  // the rule: 4560 -> 5700 -> 6144

constexpr TaskStackFigure TASK_STACK_FIGURES[] = {
    {"DriveTask", DRIVE_TASK_MEASURED_CHAIN_BYTES, DRIVE_TASK_STACK_BYTES},
    {"RCInputTask", RC_INPUT_TASK_MEASURED_CHAIN_BYTES, RC_INPUT_TASK_STACK_BYTES},
    {"ServoTask", SERVO_TASK_MEASURED_CHAIN_BYTES, SERVO_TASK_STACK_BYTES},
    {"DomeTask", DOME_TASK_MEASURED_CHAIN_BYTES, DOME_TASK_STACK_BYTES},
    {"AudioTask", AUDIO_TASK_MEASURED_CHAIN_BYTES, AUDIO_TASK_STACK_BYTES},
    {"AuxLedTask", AUX_LED_TASK_MEASURED_CHAIN_BYTES, AUX_LED_TASK_STACK_BYTES},
    {"DomeLinkTask", DOME_LINK_TASK_MEASURED_CHAIN_BYTES, DOME_LINK_TASK_STACK_BYTES},
    {"SafetyMonitor", SAFETY_MONITOR_MEASURED_CHAIN_BYTES, SAFETY_MONITOR_STACK_BYTES},
    {"SeqDisp", SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES, SEQ_DISPATCHER_TASK_STACK_BYTES},
    {"Console", CONSOLE_TASK_MEASURED_CHAIN_BYTES, CONSOLE_TASK_STACK_BYTES},
    {"WebEvents", WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES, WEB_EVENTS_TASK_STACK_BYTES},
    {"ArduinoOTA", OTA_TASK_MEASURED_CHAIN_BYTES, OTA_TASK_STACK_BYTES},
    {"HostedRecovery", HOSTED_RECOVERY_TASK_MEASURED_CHAIN_BYTES, HOSTED_RECOVERY_TASK_STACK_BYTES},
};
#elif defined(PA_CHIP_TARGET_ESP32)
constexpr uint32_t DRIVE_TASK_MEASURED_CHAIN_BYTES = 4080;
constexpr uint32_t DRIVE_TASK_STACK_BYTES = 5632;  // above the rule (5120), the recipe says why
constexpr uint32_t RC_INPUT_TASK_MEASURED_CHAIN_BYTES = 5152;
constexpr uint32_t RC_INPUT_TASK_STACK_BYTES = 6656;  // the rule: 5152 -> 6440 -> 6656
constexpr uint32_t SERVO_TASK_MEASURED_CHAIN_BYTES = 3184;
constexpr uint32_t SERVO_TASK_STACK_BYTES = 4096;  // the rule: 3184 -> 3980 -> 4096
constexpr uint32_t DOME_TASK_MEASURED_CHAIN_BYTES = 3200;
constexpr uint32_t DOME_TASK_STACK_BYTES = 4096;  // the rule: 3200 -> 4000 -> 4096
constexpr uint32_t AUDIO_TASK_MEASURED_CHAIN_BYTES = 4240;
constexpr uint32_t AUDIO_TASK_STACK_BYTES = 6144;  // above the rule (5632), the recipe says why
constexpr uint32_t AUX_LED_TASK_MEASURED_CHAIN_BYTES = 2752;
constexpr uint32_t AUX_LED_TASK_STACK_BYTES = 4096;  // above the rule (3584), the recipe says why
constexpr uint32_t DOME_LINK_TASK_MEASURED_CHAIN_BYTES = 6112;
constexpr uint32_t DOME_LINK_TASK_STACK_BYTES = 6144;  // rule declined (7680), the recipe says why
constexpr uint32_t SAFETY_MONITOR_MEASURED_CHAIN_BYTES = 3280;
constexpr uint32_t SAFETY_MONITOR_STACK_BYTES = 4608;  // the rule: 3280 -> 4100 -> 4608
constexpr uint32_t SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES = 3888;
constexpr uint32_t SEQ_DISPATCHER_TASK_STACK_BYTES = 5120;  // the rule: 3888 -> 4860 -> 5120
constexpr uint32_t CONSOLE_TASK_MEASURED_CHAIN_BYTES = 8896;
constexpr uint32_t CONSOLE_TASK_STACK_BYTES = 11264;  // the rule: 8896 -> 11120 -> 11264
constexpr uint32_t WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES = 4992;
constexpr uint32_t WEB_EVENTS_TASK_STACK_BYTES = 6144;  // rule declined (6656), the recipe says why
constexpr uint32_t OTA_TASK_MEASURED_CHAIN_BYTES = 3440;
constexpr uint32_t OTA_TASK_STACK_BYTES = 4096;  // rule declined (4608), the recipe says why

constexpr TaskStackFigure TASK_STACK_FIGURES[] = {
    {"DriveTask", DRIVE_TASK_MEASURED_CHAIN_BYTES, DRIVE_TASK_STACK_BYTES},
    {"RCInputTask", RC_INPUT_TASK_MEASURED_CHAIN_BYTES, RC_INPUT_TASK_STACK_BYTES},
    {"ServoTask", SERVO_TASK_MEASURED_CHAIN_BYTES, SERVO_TASK_STACK_BYTES},
    {"DomeTask", DOME_TASK_MEASURED_CHAIN_BYTES, DOME_TASK_STACK_BYTES},
    {"AudioTask", AUDIO_TASK_MEASURED_CHAIN_BYTES, AUDIO_TASK_STACK_BYTES},
    {"AuxLedTask", AUX_LED_TASK_MEASURED_CHAIN_BYTES, AUX_LED_TASK_STACK_BYTES},
    {"DomeLinkTask", DOME_LINK_TASK_MEASURED_CHAIN_BYTES, DOME_LINK_TASK_STACK_BYTES},
    {"SafetyMonitor", SAFETY_MONITOR_MEASURED_CHAIN_BYTES, SAFETY_MONITOR_STACK_BYTES},
    {"SeqDisp", SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES, SEQ_DISPATCHER_TASK_STACK_BYTES},
    {"Console", CONSOLE_TASK_MEASURED_CHAIN_BYTES, CONSOLE_TASK_STACK_BYTES},
    {"WebEvents", WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES, WEB_EVENTS_TASK_STACK_BYTES},
    {"ArduinoOTA", OTA_TASK_MEASURED_CHAIN_BYTES, OTA_TASK_STACK_BYTES},
};
#else
  #error "task stack sizes have no value for this chip target"
#endif

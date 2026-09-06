#pragma once
// =============================================================================
// include/reset_reason.h
//
// Single source of truth for mapping an ESP-IDF reset reason to a stable,
// human-readable name. Shared by boot logging (main.cpp), the /api/status
// telemetry snapshot, and (#225) the Console's system.status.health query, so
// a reboot cause survives even after the boot log ring buffer rotates out
// (the dome-side blind spot that motivated this).
//
// HEADER-ONLY (#225): moved out of src/reset_reason.cpp so
// captureHealthSnapshot() (src/web/api_status_serializers.cpp, in
// [env:native]'s build_src_filter) can call it too - platformio.ini is
// fenced on this ticket, so a src/reset_reason.cpp with no filter entry has
// no path into the native test binary. `inline`, not `static`, because this
// header is genuinely shared by more than one translation unit (main.cpp,
// web_server.cpp, api_status_serializers.cpp) - the same rule
// include/console_direct_action_system.h's own header comment gives for its
// kComponentToggleFields/consoleFindComponentToggleField precedent.
//
// Takes a plain int, not esp_reset_reason_t: <esp_system.h> does not exist
// on the native toolchain (no stub under test/stubs/include), and every
// existing native-only workaround for that (src/web/web_network_bootstrap.cpp,
// include/failsafe_boot_twdt.h) re-declares its own copy of the enum behind
// an ARDUINO build-flag guard, which the slice gate's diff check
// (tools/slice_verify.py, AGENTS.md check 9) refuses as a NEW such guard
// under include/. An unscoped
// C enum converts to int implicitly at the call site (every caller here
// passes esp_reset_reason()'s return value directly, exactly as
// src/main.cpp already does at its other call site,
// `(int)esp_reset_reason()`), so the switch below cases on the enum's own
// literal values instead of its symbolic names - tools/soak.py's
// own RESET_REASON_NAMES dict already treats these same values as a stable,
// hardcodable contract for the identical reason. Values are
// esp_reset_reason_t's declaration order in the vendored ESP-IDF
// esp_system.h (framework-arduinoespressif32-libs), which IDF's ABI
// stability guarantee holds fixed.
// =============================================================================

// Returns a static string literal naming `reason` (an esp_reset_reason_t
// value, passed as its underlying int). esp_reset_reason() is stable for the
// whole runtime, so callers may invoke it at any time after boot.
inline const char* resetReasonName(int reason) {
    switch (reason) {
        case 0:  // ESP_RST_UNKNOWN
            return "UNKNOWN";
        case 1:  // ESP_RST_POWERON
            return "POWERON";
        case 2:  // ESP_RST_EXT
            return "EXTERNAL";
        case 3:  // ESP_RST_SW
            return "SOFTWARE";
        case 4:  // ESP_RST_PANIC
            return "PANIC";
        case 5:  // ESP_RST_INT_WDT
            return "INT_WDT";
        case 6:  // ESP_RST_TASK_WDT
            return "TASK_WDT";
        case 7:  // ESP_RST_WDT
            return "WDT";
        case 8:  // ESP_RST_DEEPSLEEP
            return "DEEPSLEEP";
        case 9:  // ESP_RST_BROWNOUT
            return "BROWNOUT";
        case 10:  // ESP_RST_SDIO
            return "SDIO";
        default:
            return "OTHER";
    }
}

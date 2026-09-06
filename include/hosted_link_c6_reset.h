// =============================================================================
// include/hosted_link_c6_reset.h
//
// Operator-initiated reboot of the fitted WiFi co-processor: one deliberate
// pulse on the module's active-LOW enable line, so the bounded ESP-Hosted
// transport-failure recovery ladder (include/hosted_link_supervisor.h, #189)
// can be provoked and watched on the image that ships it (#243).
//
// Only defined on boards where PA_CAP_HOSTED_WIFI is set
// (src/web/web_network_manager_hosted.cpp, the file that already owns every
// other piece of Hosted device I/O). Both call sites -- the Console's
// system.action.reboot-wifi-module executor and nothing else -- are guarded by
// the same capability gate, so this header carries no #if of its own: it is
// unreachable, not undefined, on a board without the capability. Same shape as
// include/hosted_link_status.h.
//
// This is NOT the WiFi Module Update path (#241, host-to-module image over
// SDIO) and NOT the pad/UART first-write of an unbootable module (#198).
// Driving the enable line reflashes nothing; the module boots the image it
// already holds.
// =============================================================================
#pragma once

#include <stdint.h>

#include <esp_err.h>

// How long the enable line is held low. Read off the main-board schematic
// under #184: P4 pin 98 = GPIO54 = net C6_EN/C6_RST lands on the
// ESP32-C6-MINI-1 module's EN pin, with R16 10k pull-up to ESP_3V3 and C43
// 100nF to ground -- tau ~= 1 ms, so a 100 ms assert is 100x tau and the
// release edge settles in about 5 ms. Anything slower at the C6_RST test pad
// is a circuit fault rather than RC
// (docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md, "The C6 reset net").
// 100 ms is also the figure bringup/p4_hosted_bench.cpp:112 proved on
// hardware; this is a port of that pulse, not a re-derivation of it.
constexpr uint32_t kHostedLinkResetAssertMs = 100;

// What the two edges actually did. Returned rather than only logged because
// "held low but never released" leaves the module in reset for the rest of
// this boot -- a different operator situation from a clean pulse, and one the
// Console must not report as success. Which pin was driven and for how long
// are in the log line at the call site, not here: nothing reads them back.
struct HostedLinkResetOutcome {
    esp_err_t assertResult = ESP_OK;   // gpio_set_level(pin, 0)
    esp_err_t releaseResult = ESP_OK;  // gpio_set_level(pin, 1)

    // Both edges were driven. API acceptance is not electrical proof: only a
    // scope on the C6_RST pad, or the module's own boot log, shows the module
    // really rebooted (the same boundary bringup/p4_hosted_bench.cpp records
    // as resetEvidenceBoundary).
    bool driven() const { return assertResult == ESP_OK && releaseResult == ESP_OK; }
};

// Pulses the WiFi module's enable line low and releases it. BLOCKS for
// kHostedLinkResetAssertMs on the calling task -- call it only from a task
// that may sleep for a tenth of a second (the Console task, Core 0, priority
// 2), never from Core 1 or an event-loop callback.
//
// Defined in src/web/web_network_manager_hosted.cpp.
HostedLinkResetOutcome hostedLinkResetCoprocessor();

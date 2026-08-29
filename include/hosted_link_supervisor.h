// =============================================================================
// include/hosted_link_supervisor.h
//
// Hosted Link Supervisor Step Core  --  pure phase-model decisions for the
// bounded ESP-Hosted (ESP32-P4 + ESP32-C6 over SDIO) transport-failure
// recovery ladder (#189 slice 2).
//
// Device I/O -- hostedDeinitWiFi()/hostedInitWiFi(), the raw esp_wifi_*
// rejoin, task creation, ESP_HOSTED_EVENT registration, and logging -- all
// stay in src/web/web_network_manager_hosted.cpp. This header/its .cpp own
// only the phase model the #184 bench proved on hardware
// (bringup/p4_hosted_bench.cpp:179-192):
//
//     idle -> armed -> attempting -> {idle, degraded}
//
// and the bound on how many attempts a single ladder run may make before it
// gives up. No FreeRTOS, Arduino, RobotState, hardware I/O, or logging lives
// here -- matches include/dome_link_arbiter.h, include/drive_arbiter.h,
// include/rc_input_step.h. Deliberately free of esp_err_t and every other
// ESP-IDF type too: the recovery task's raw esp_wifi_* rejoin results are
// diagnostics, not phase-model decisions, and keeping them out of this
// header is what lets it compile and run in the native/host test build,
// which has no ESP-IDF headers at all.
//
// Terminal-degraded is by design (ADR 0032): the host must never restart
// itself to clear a dead C6 link, and the ladder itself must not retry
// forever, so DEGRADED refuses to re-arm for the rest of this boot.
// =============================================================================
#pragma once

#include <stdint.h>

// Recovery ladder bounds, device-proven on the #184 bench
// (bringup/p4_hosted_bench.cpp:119-120): each attempt's own SDIO card-init
// timeout (sdio_drv.c CARD_INIT_TIMEOUT_MS = 1500ms, with internal retries)
// needs to fully settle before the next attempt starts, so the interval sits
// well above that; five attempts over roughly 25-35s rode out a transient
// co-processor glitch on hardware without looking wedged (recovered on
// attempt 1/5 in the device-proven run). Changing either is a decision to
// record on #189, not a silent edit.
constexpr unsigned int kHostedLinkRecoveryMaxAttempts = 5;
constexpr uint32_t kHostedLinkRecoveryAttemptIntervalMs = 5000;

enum class HostedLinkPhase : uint8_t {
    Idle,        // no failure outstanding
    Armed,       // a failure was observed; the recovery task has been
                 // notified but has not started its first attempt yet
    Attempting,  // a deinit/re-init cycle is in flight -- the device shell
                 // must not touch WiFi/Hosted independently while this holds
    Degraded,    // the ladder exhausted kHostedLinkRecoveryMaxAttempts;
                 // terminal for this boot. ADR 0032 forbids restarting the
                 // host to clear it, and the ladder itself must not retry
                 // forever -- Idle is the only phase a transport failure can
                 // arm a fresh run from; Degraded never re-arms.
};

const char* hostedLinkPhaseName(HostedLinkPhase phase);

// Cross-call state, owned by the device shell (one instance per boot).
// Default-construction is the boot state (Idle, all counters zero).
struct HostedLinkSupervisorState {
    HostedLinkPhase phase = HostedLinkPhase::Idle;
    unsigned int transportFailureEventCount = 0;  // lifetime ESP_HOSTED_EVENT_TRANSPORT_FAILURE count
    unsigned int transportUpEventCount = 0;        // lifetime ESP_HOSTED_EVENT_TRANSPORT_UP count (observational only)
    unsigned int attemptCount = 0;                 // attempts made in the current/most-recent ladder run
    unsigned int totalAttemptCount = 0;            // lifetime attempts across all ladder runs
    unsigned int recoveredCount = 0;                // ladder runs that reached Idle again
    uint32_t lastFailureAtMs = 0;
    uint32_t lastAttemptAtMs = 0;
    uint32_t degradedAtMs = 0;
};

// -----------------------------------------------------------------------
// ESP_HOSTED_EVENT_TRANSPORT_FAILURE
// -----------------------------------------------------------------------

struct HostedLinkFailureActions {
    // true only when this event should notify the recovery task to start a
    // fresh ladder run (i.e. the ladder was Idle). A failure that arrives
    // while Armed/Attempting folds into the run already in flight; a
    // failure that arrives while Degraded stays terminal by design --
    // neither notifies.
    bool shouldNotifyRecoveryTask = false;
};

// Called by the device shell's ESP_HOSTED_EVENT_TRANSPORT_FAILURE handler.
// Arms a fresh ladder only on the Idle->Armed edge; every other phase folds
// the event into the run already in flight (or stays terminal).
HostedLinkFailureActions hostedLinkSupervisorOnTransportFailure(
    HostedLinkSupervisorState& state, uint32_t nowMs);

// Called by the device shell's ESP_HOSTED_EVENT_TRANSPORT_UP handler.
// Purely observational (mirrors the #184 bench): this event is posted by the
// SDIO driver's own transport_active_cb() independent of anything this
// supervisor believes, and it never drives a phase transition on its own --
// recovery only concludes through hostedLinkSupervisorRecordAttempt()'s
// transportUp evidence.
void hostedLinkSupervisorOnTransportUp(HostedLinkSupervisorState& state);

// -----------------------------------------------------------------------
// Recovery task lifecycle
// -----------------------------------------------------------------------

// Called once by the recovery task after it wakes from the notification
// that armed it, before its attempt loop starts.
void hostedLinkSupervisorBeginAttemptRun(HostedLinkSupervisorState& state);

struct HostedLinkAttemptOutcome {
    bool recovered = false;    // transport reported up this attempt -- ladder returns to Idle
    bool exhausted = false;    // attempt bound reached without recovering -- ladder is now Degraded
    bool shouldRetry = false;  // neither of the above -- caller runs another attempt
};

// Called by the recovery task after each hostedDeinitWiFi()/hostedInitWiFi()
// cycle, with transportUp being the device-truthful outcome
// (hostedIsInitialized() -- never WiFi.status(), see the file header banner
// in web_network_manager_hosted.cpp for why a dead transport still reads
// WL_CONNECTED forever).
HostedLinkAttemptOutcome hostedLinkSupervisorRecordAttempt(
    HostedLinkSupervisorState& state, uint32_t nowMs, bool transportUp);

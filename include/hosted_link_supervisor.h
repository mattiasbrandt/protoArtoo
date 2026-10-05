// =============================================================================
// include/hosted_link_supervisor.h
//
// Hosted Link Supervisor Step Core  --  pure phase-model decisions for the
// bounded ESP-Hosted (ESP32-P4 + ESP32-C6 over SDIO) transport-failure
// recovery ladder (#189).
//
// Device I/O -- hostedDeinitWiFi()/hostedInitWiFi(), the raw esp_wifi_*
// rejoin, task creation, ESP_HOSTED_EVENT registration, and logging -- all
// stay in src/web/web_network_manager_hosted.cpp. This header/its .cpp own
// only the phase model the #184 bench proved on hardware
// (bench/p4_hosted_bench.cpp:179-192):
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
//
// Two things arm a ladder run, and both go through the one Idle->Armed
// transition in the .cpp: ESP_HOSTED_EVENT_TRANSPORT_FAILURE, and (#471) a
// liveness miss -- a C6 that stops answering RPCs while the SDIO transport
// still reads "up" posts no transport event at all, so the supervisor also
// watches the C6's own heartbeat (or, where that cannot be enabled, a cheap
// RPC probe) and arms when it goes quiet.
// =============================================================================
#pragma once

#include <stdint.h>

// Recovery ladder bounds, device-proven on the #184 bench
// (bench/p4_hosted_bench.cpp:119-120): each attempt's own SDIO card-init
// timeout (sdio_drv.c CARD_INIT_TIMEOUT_MS = 1500ms, with internal retries)
// needs to fully settle before the next attempt starts, so the interval sits
// well above that; five attempts over roughly 25-35s rode out a transient
// co-processor glitch on hardware without looking wedged (recovered on
// attempt 1/5 in the device-proven run). Changing either is a decision to
// record on #189, not a silent edit.
constexpr unsigned int kHostedLinkRecoveryMaxAttempts = 5;
constexpr uint32_t kHostedLinkRecoveryAttemptIntervalMs = 5000;

// C6 liveness watch (#471, operator's decision 2026-10-05): the C6 sends a
// heartbeat every 5 s, and the ladder arms after 3 missed (~15 s). The window
// trades SDIO traffic and detection time against false arming during a long
// esp_wifi_* call on the C6. The same interval and miss count drive the RPC
// probe fallback (one probe per interval, armed after 3 consecutive
// failures), so the operator's timing holds whichever path the board takes.
constexpr uint32_t kHostedLinkLivenessIntervalMs = 5000;
constexpr unsigned int kHostedLinkLivenessMissLimit = 3;

enum class HostedLinkPhase : uint8_t {
    Idle,        // no failure outstanding
    Armed,       // a transport failure or a liveness miss was observed and a
                 // ladder run is owed, but its first attempt has not started
                 // yet (the recovery task was notified, or, for a liveness
                 // miss it found itself, is about to run it)
    Attempting,  // a deinit/re-init cycle is in flight -- the device shell
                 // must not touch WiFi/Hosted independently while this holds
    Degraded,    // the ladder exhausted kHostedLinkRecoveryMaxAttempts;
                 // terminal for this boot. ADR 0032 forbids restarting the
                 // host to clear it, and the ladder itself must not retry
                 // forever -- Idle is the only phase a transport failure or a
                 // liveness miss can arm a fresh run from; Degraded never
                 // re-arms.
};

const char* hostedLinkPhaseName(HostedLinkPhase phase);

// Which evidence the liveness watch is reading (#471).
enum class HostedLinkLivenessSource : uint8_t {
    None,       // not watching: before the transport first comes up, and
                // from the moment a ladder run arms until an attempt is
                // recovered (a rebooted C6 has forgotten its heartbeat, so the
                // device shell re-enables it and restarts the watch)
    Heartbeat,  // ESP_HOSTED_EVENT_CP_HEARTBEAT, enabled on the C6
    Probe,      // the heartbeat could not be enabled; one cheap RPC per
                // kHostedLinkLivenessIntervalMs instead
};

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

    // Liveness watch (#471). Kept apart from the transport-failure counters
    // above: a liveness miss is not an ESP_HOSTED_EVENT_TRANSPORT_FAILURE,
    // and /api/status reports that count by name.
    HostedLinkLivenessSource livenessSource = HostedLinkLivenessSource::None;
    uint32_t lastLivenessAtMs = 0;              // last evidence for the active source; the watch's start when none has arrived yet
    unsigned int heartbeatCount = 0;            // lifetime ESP_HOSTED_EVENT_CP_HEARTBEAT count, in every phase
    uint32_t lastHeartbeatNumber = 0;           // the C6's own beat number from the latest event
    unsigned int consecutiveProbeFailures = 0;  // Probe source only
    unsigned int livenessMissCount = 0;         // lifetime liveness misses (each one tried to arm a run)
};

// -----------------------------------------------------------------------
// ESP_HOSTED_EVENT_TRANSPORT_FAILURE
// -----------------------------------------------------------------------

struct HostedLinkFailureActions {
    // true only when this trigger armed a fresh ladder run (i.e. the ladder
    // was Idle). From the transport-failure event handler that means notify
    // the recovery task; from a liveness miss, which the recovery task finds
    // itself, it means run the ladder there and then. A trigger that arrives
    // while Armed/Attempting folds into the run already in flight; one that
    // arrives while Degraded stays terminal by design -- neither starts a run.
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
// C6 liveness watch (#471)
// -----------------------------------------------------------------------

// Starts (or restarts) the watch on `source`, with nowMs as the start of the
// first window: called once the heartbeat has been enabled (or found
// unavailable) at boot, and again after every recovered attempt. Starting the
// clock here, not at the last evidence before the outage, is what keeps the
// first window after a recovery from re-arming at once. A no-op outside
// Idle: a run armed in the meantime restarts the watch itself.
void hostedLinkSupervisorStartLivenessWatch(HostedLinkSupervisorState& state,
                                            HostedLinkLivenessSource source, uint32_t nowMs);

// Called by the device shell's ESP_HOSTED_EVENT_CP_HEARTBEAT handler.
// Counted in every phase (the recovery task reads heartbeatCount to see a
// beat newer than a re-init); refreshes the watch only when the watch is
// reading the heartbeat.
void hostedLinkSupervisorOnHeartbeat(HostedLinkSupervisorState& state, uint32_t nowMs,
                                     uint32_t beatNumber);

// Called by the device shell after each probe while the watch reads Probe.
// `answered` is whether the C6 replied at all (the device shell decides which
// replies count), never a host-side flag.
void hostedLinkSupervisorRecordProbe(HostedLinkSupervisorState& state, uint32_t nowMs,
                                     bool answered);

// true when the watch has gone quiet long enough to arm a ladder run:
// Heartbeat -- no beat for kHostedLinkLivenessMissLimit intervals;
// Probe -- kHostedLinkLivenessMissLimit consecutive unanswered probes.
// Only ever true from Idle with a watch running: Armed/Attempting already
// have a run in flight, Degraded is terminal, and arming clears the source.
bool hostedLinkSupervisorLivenessDue(const HostedLinkSupervisorState& state, uint32_t nowMs);

// Called by the device shell when hostedLinkSupervisorLivenessDue() holds.
// Arms through the same Idle->Armed transition a transport failure takes, so
// Idle stays the only way in and Degraded stays terminal; counts the miss
// in livenessMissCount, not transportFailureEventCount.
HostedLinkFailureActions hostedLinkSupervisorOnLivenessMissed(HostedLinkSupervisorState& state);

// -----------------------------------------------------------------------
// Recovery task lifecycle
// -----------------------------------------------------------------------

// Called once by the recovery task at the start of each ladder run -- after
// the notification a transport failure sent, or after the liveness miss it
// found itself -- before its attempt loop starts.
void hostedLinkSupervisorBeginAttemptRun(HostedLinkSupervisorState& state);

struct HostedLinkAttemptOutcome {
    bool recovered = false;    // transport reported up this attempt -- ladder returns to Idle
    bool exhausted = false;    // attempt bound reached without recovering -- ladder is now Degraded
    bool shouldRetry = false;  // neither of the above -- caller runs another attempt
};

// Called by the recovery task after each hostedDeinitWiFi()/hostedInitWiFi()
// cycle, with transportUp being the device-truthful outcome: the host is
// initialised AND the C6 answered after the re-init (a heartbeat newer than
// the re-init, or an answered RPC -- #471). Never WiFi.status(), which reads
// WL_CONNECTED forever on a dead transport, and never hostedIsInitialized()
// alone, which is a host-side flag that read true while every RPC timed out
// (see hostedRunRecoveryLadder() in web_network_manager_hosted.cpp).
HostedLinkAttemptOutcome hostedLinkSupervisorRecordAttempt(
    HostedLinkSupervisorState& state, uint32_t nowMs, bool transportUp);

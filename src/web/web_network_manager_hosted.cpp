// =============================================================================
// src/web/web_network_manager_hosted.cpp
//
// ESP-Hosted (ESP32-P4 + ESP32-C6 over SDIO) backend for the network manager
// seam (#188/#189). Implements WiFi event handling, registration, and boot
// posture application on top of the same Arduino WiFi API the native backend
// uses, plus (#189) a bounded transport-failure recovery ladder for
// the C6 co-processor.
//
// Boot posture (#189): WiFi.mode()/WiFi.begin()/WiFi.softAP()
// transparently bring the SDIO transport up via hostedInitWiFi() inside the
// Arduino core's wifiLowLevelInit() (WiFiGeneric.cpp, gated on
// CONFIG_ESP_HOSTED_ENABLED) and post the same ARDUINO_EVENT_WIFI_* events as
// the native radio, so the boot-time posture code is identical to the native
// backend's -- shared via web_network_manager_common.h (#189
// de-duplication).
//
// Recovery (#189): Arduino's own WiFi.begin() cannot restart a
// freshly-reset C6 because its _esp_wifi_started/connected() bookkeeping is
// stale-true after the reboot and is not cleared by hostedDeinitWiFi()
// (device-proven, #184 bench session). The rejoin below bypasses WiFi.begin()
// entirely and goes through raw esp_wifi_* calls instead, in the vendor's own
// order, ported from the device-proven bringup/p4_hosted_bench.cpp (recovered
// on attempt 1/5 on hardware, client-unreachable to reachable in ~10s, host
// still running, bootCount unchanged) -- see hostedRejoinAfterRecovery()
// below for the full rationale, carried from the bench almost verbatim.
// =============================================================================

#include "../../include/web_network_manager.h"

#include <Arduino.h>
#include "../../include/config.h"

// Hosted radio backend. Whole-file guard, no #else: when this board's backend
// is not selected, this translation unit contributes nothing, and the
// composition that does apply lives in its own file (ADR 0021 shape). Keeping
// a "not selected" definition here is what let a stale signature survive as a
// silent overload and break the firebeetle2 link once already (see
// web_network_manager_native.cpp).
#if PA_CAP_HOSTED_WIFI

#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32-hal-hosted.h"
// esp_hosted.h: ESP_HOSTED_EVENT base + event ID enum (transport-failure
// recovery ladder). esp_wifi.h: raw esp_wifi_init/set_mode/set_config/start/
// connect, needed because Arduino's own WiFi.begin() cannot restart the WiFi
// driver on a freshly-rebooted co-processor -- see hostedRejoinAfterRecovery().
#include "esp_hosted.h"
#include "esp_wifi.h"

#include "../../include/audio_task.h"
#include "../../include/config_cache.h"
#include "../../include/dome_link.h"
#include "../../include/hosted_link_degraded_announcement.h"
#include "../../include/hosted_link_status.h"
#include "../../include/hosted_link_supervisor.h"
#include "../../include/logging.h"
#include "../../include/web_network_manager_common.h"
#include "../../include/wifi_boot_decision.h"

static const char* TAG = "WebServer";

// Event-cached STA connection status for networkManagerStationConnected().
// Updated by handleWiFiEventBackend(); read by Core 1 dome-link loop.
// Using volatile bool avoids heap allocation and vendor calls on Core 1.
static volatile bool g_staConnected = false;

// ============================================================================
// Hosted Transport-Failure Recovery Ladder (#189)
//
// ESP-Hosted's SDIO driver posts ESP_HOSTED_EVENT_TRANSPORT_FAILURE
// unconditionally when MAX_SDIO_WRITE_RETRY writes fail, then restarts the
// host under #if H_TRANSPORT_RESTART_ON_FAILURE. [env:firebeetle2]'s
// custom_sdkconfig (platformio.ini) leaves that symbol undefined (#189 slice
// 1), so the event fires and the host survives -- but nothing reconnects on
// its own. This is that missing subscriber, ported from the device-proven
// bringup/p4_hosted_bench.cpp rather than re-derived (#189's own instruction:
// "port its shape, do not re-derive it").
//
// The decision logic (phase model, attempt bound) lives in
// include/hosted_link_supervisor.h as a pure, host-testable step core; this
// file owns only the device I/O the core is deliberately free of: task
// creation, event registration, the blocking hostedDeinitWiFi()/
// hostedInitWiFi() cycle, the raw esp_wifi_* rejoin, and logging.
// ============================================================================

static HostedLinkSupervisorState g_hostedLinkState;
static portMUX_TYPE g_hostedLinkMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t g_hostedRecoveryTaskHandle = nullptr;

// Post-recovery WiFi rejoin diagnostics, one field per esp_wifi_* call
// (2026-08-29 #184 device review: a collapsed bool made a real hardware
// failure undiagnosable because Arduino's log_e() is compiled out at this
// log level). Kept separate from HostedLinkSupervisorState because these are
// raw ESP-IDF call outcomes, not phase-model decisions -- the pure core
// (host-testable, no ESP-IDF headers) never sees an esp_err_t.
static struct {
    esp_err_t wifiInitResult = ESP_OK;
    esp_err_t wifiSetModeResult = ESP_OK;
    esp_err_t wifiSetConfigResult = ESP_OK;
    esp_err_t wifiStartResult = ESP_OK;
    esp_err_t wifiConnectResult = ESP_OK;
} g_hostedRejoinResult;
static portMUX_TYPE g_hostedRejoinMux = portMUX_INITIALIZER_UNLOCKED;

// Post-recovery WiFi rejoin. Bypasses WiFi.begin()/WiFi.STA.connect() -- the
// C6 physically rebooted, so its WiFi driver was never (re)started this
// session, but Arduino's own driver-started bookkeeping
// (WiFiGeneric.cpp espWiFiStart()/_esp_wifi_started, STA.cpp
// ESP_NETIF_STARTED_BIT) is stale-true from before the failure: neither flag
// is cleared by hostedDeinitWiFi()/hostedInitWiFi(), only by the
// WiFi.mode(WIFI_MODE_NULL) teardown path, which itself talks to the dead
// transport and fails during the outage. WiFi.STA.connect() was tried and
// rejected too (2026-08-29 device review: staConnect=FAILED, undiagnosable
// because its four return-false branches all log via a compiled-out
// log_e()) -- the bypass has to be complete: mode -> config -> start ->
// connect through raw ESP-IDF calls only, in the vendor's own
// station_example.c example_wifi_init_sta() order, exactly as
// bringup/p4_hosted_bench.cpp:654-670 proved on hardware.
//
// STA rejoin only: this mirrors the bench, which has no posture concept and
// is STA-only. A C6 reset while the controller's boot posture was actually
// PROVISIONING/STANDALONE_AP_MODE/NETWORK_RECOVERY is guarded below:
// hostedDeinitWiFi()/hostedInitWiFi() still recover the SDIO transport itself
// in that case, but this function's esp_wifi_set_mode(WIFI_MODE_STA) would
// force the operator out of a posture they were deliberately in -- an
// unprovisioned controller's Provisioning AP would vanish, and a Standalone
// AP host would be pulled into STA against nothing it asked for. The guard is
// a no-op, not an AP-side rejoin: implementing recovery for those postures is
// unproven on hardware and intentionally out of scope here (flagged on #189,
// not guessed at).
static void hostedRejoinAfterRecovery() {
    const WifiBootPosture bootPosture = configCacheReadActiveWifiBootPosture();
    if (bootPosture != WifiBootPosture::CLIENT_MODE) {
        PA_LOG_INFO(TAG,
                    "Hosted link transport recovered, but boot posture is not CLIENT_MODE "
                    "(posture=%d); leaving it alone rather than forcing WIFI_MODE_STA -- the "
                    "SDIO transport is already back up via hostedDeinitWiFi()/hostedInitWiFi()",
                    (int)bootPosture);
        return;
    }

    WifiConfig activeWifi = {};
    configCacheReadActiveWifi(&activeWifi);
    const char* ssid;
    const char* password;
    wifiNetworkManagerResolveStaCredentialsCommon(activeWifi, &ssid, &password);

    wifi_init_config_t wifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
    const esp_err_t wifiInitResult = esp_wifi_init(&wifiInitCfg);
    const esp_err_t wifiModeResult = esp_wifi_set_mode(WIFI_MODE_STA);

    // Mirrors STAClass::connect()'s own field population
    // (libraries/WiFi/src/STA.cpp) so the config this bypass sends is the
    // same shape Arduino would have sent, just not gated on Arduino's stale
    // driver-started state.
    wifi_config_t staConfig = {};
    snprintf(reinterpret_cast<char*>(staConfig.sta.ssid), sizeof(staConfig.sta.ssid), "%s", ssid);
    snprintf(reinterpret_cast<char*>(staConfig.sta.password), sizeof(staConfig.sta.password), "%s",
             password);
    staConfig.sta.threshold.rssi = -127;
    staConfig.sta.pmf_cfg.capable = true;
    const esp_err_t wifiConfigResult = esp_wifi_set_config(WIFI_IF_STA, &staConfig);

    const esp_err_t wifiStartResult = esp_wifi_start();
    const esp_err_t wifiConnectResult = esp_wifi_connect();

    portENTER_CRITICAL(&g_hostedRejoinMux);
    g_hostedRejoinResult.wifiInitResult = wifiInitResult;
    g_hostedRejoinResult.wifiSetModeResult = wifiModeResult;
    g_hostedRejoinResult.wifiSetConfigResult = wifiConfigResult;
    g_hostedRejoinResult.wifiStartResult = wifiStartResult;
    g_hostedRejoinResult.wifiConnectResult = wifiConnectResult;
    portEXIT_CRITICAL(&g_hostedRejoinMux);

    PA_LOG_INFO(TAG,
                "Hosted link recovery rejoin: wifiInit=%d(%s) wifiSetMode=%d(%s) "
                "wifiSetConfig=%d(%s) wifiStart=%d(%s) wifiConnect=%d(%s); immediate status is "
                "not eventual association proof",
                (int)wifiInitResult, esp_err_to_name(wifiInitResult), (int)wifiModeResult,
                esp_err_to_name(wifiModeResult), (int)wifiConfigResult,
                esp_err_to_name(wifiConfigResult), (int)wifiStartResult,
                esp_err_to_name(wifiStartResult), (int)wifiConnectResult,
                esp_err_to_name(wifiConnectResult));
}

// Runs on its own task, never loop() or the esp_event default-loop task:
// hostedDeinitWiFi()/hostedInitWiFi() and the esp_wifi_* rejoin above can
// block for seconds (CARD_INIT_TIMEOUT_MS retries, RPC teardown/setup), and
// the event handler that notifies this task must stay non-blocking.
static void hostedRecoveryTaskFn(void* arg) {
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        portENTER_CRITICAL(&g_hostedLinkMux);
        hostedLinkSupervisorBeginAttemptRun(g_hostedLinkState);
        portEXIT_CRITICAL(&g_hostedLinkMux);

        bool recovered = false;
        unsigned int attemptsThisRun = 0;

        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(kHostedLinkRecoveryAttemptIntervalMs));

            PA_LOG_INFO(TAG, "Hosted link recovery attempt %u/%u: hostedDeinitWiFi + hostedInitWiFi",
                        attemptsThisRun + 1, kHostedLinkRecoveryMaxAttempts);

            const bool deinitOk = hostedDeinitWiFi();
            const bool initOk = hostedInitWiFi();
            // Device-truthful outcome, never WiFi.status() -- a dead
            // transport reads WL_CONNECTED forever (#184 bench finding).
            const bool transportUp = hostedIsInitialized();
            const uint32_t nowMs = millis();

            PA_LOG_INFO(TAG,
                        "Hosted link recovery attempt result: deinit=%s init=%s "
                        "hostedIsInitialized=%s",
                        deinitOk ? "ok" : "FAIL", initOk ? "ok" : "FAIL",
                        transportUp ? "true" : "false");

            HostedLinkAttemptOutcome outcome;
            portENTER_CRITICAL(&g_hostedLinkMux);
            outcome = hostedLinkSupervisorRecordAttempt(g_hostedLinkState, nowMs, transportUp);
            attemptsThisRun = g_hostedLinkState.attemptCount;
            portEXIT_CRITICAL(&g_hostedLinkMux);

            if (outcome.recovered) {
                recovered = true;
                break;
            }
            if (outcome.exhausted) {
                break;
            }
            // outcome.shouldRetry: loop for another attempt.
        }

        if (recovered) {
            hostedRejoinAfterRecovery();
        } else {
            PA_LOG_WARN(TAG,
                        "Hosted link recovery ladder exhausted after %u attempts; settling in a "
                        "degraded state for the rest of this boot. No further automatic recovery "
                        "will be attempted (ADR 0032: no host restart to clear it).",
                        attemptsThisRun);

            // Announce the terminal degraded state without the web UI -- the
            // C6 *is* the transport, so /api/status alone is insufficient
            // (#189). This runs exactly once per boot: this whole `else`
            // arm is reached only on the attempt loop's `outcome.exhausted`
            // break, which hostedLinkSupervisorRecordAttempt() (the pure step
            // core) can only return once per boot -- once Degraded is set,
            // hostedLinkSupervisorOnTransportFailure() refuses to re-arm a
            // fresh run from it, and this task then parks forever on the next
            // ulTaskNotifyTake() (see the comment below). No second latch is
            // added here.
            //
            // Both calls are non-blocking (queue timeout 0) and safe from this
            // task: audioQueuePlaySlot() returns early, without enqueueing,
            // when sound is disabled at boot (audioOutputInactive() in
            // src/tasks/audio_task.cpp reads the same staged-at-reboot audio
            // toggle every other queue helper gates on), and domeQueueTx()
            // just drops the command if the dome TX queue is full.
            audioQueuePlaySlot(AUDIO_SLOT_SYS_NET_DOWN, SRC_INTERNAL);
            if (!domeQueueTx(kHostedLinkDegradedDomeText)) {
                PA_LOG_WARN(TAG,
                            "Hosted link degraded announcement: dome TX queue full, "
                            "\"%s\" dropped",
                            kHostedLinkDegradedDomeText);
            }
        }

        // Falls through to the top of the loop and parks on the next
        // ulTaskNotifyTake(); hostedLinkSupervisorOnTransportFailure()
        // refuses to re-arm from Degraded, so a degraded ladder now blocks
        // here forever -- the "must not retry forever" bound (ADR 0032).
    }
}

static void hostedTransportFailureHandler(void* arg, esp_event_base_t base, int32_t id,
                                           void* eventData) {
    (void)arg;
    (void)base;
    (void)id;
    (void)eventData;

    HostedLinkFailureActions actions;
    HostedLinkPhase phaseNow;
    unsigned int failureCount;

    portENTER_CRITICAL(&g_hostedLinkMux);
    actions = hostedLinkSupervisorOnTransportFailure(g_hostedLinkState, millis());
    phaseNow = g_hostedLinkState.phase;
    failureCount = g_hostedLinkState.transportFailureEventCount;
    portEXIT_CRITICAL(&g_hostedLinkMux);

    PA_LOG_WARN(TAG, "ESP_HOSTED_EVENT transport-failure #%u phase=%s%s", failureCount,
                hostedLinkPhaseName(phaseNow),
                actions.shouldNotifyRecoveryTask
                    ? "; recovery task notified"
                    : "; folded into the run already in flight, or degraded and terminal");

    if (actions.shouldNotifyRecoveryTask && g_hostedRecoveryTaskHandle != nullptr) {
        xTaskNotifyGive(g_hostedRecoveryTaskHandle);
    }
}

// Secondary, purely observational counter: ESP_HOSTED_EVENT_TRANSPORT_UP is
// posted by the SDIO driver itself (transport_active_cb()) whenever the
// transport reaches TRANSPORT_RX_ACTIVE, independent of anything this file
// believes. It corroborates the ladder's own attempt outcome without being
// derived from WiFi.status().
static void hostedTransportUpHandler(void* arg, esp_event_base_t base, int32_t id,
                                      void* eventData) {
    (void)arg;
    (void)base;
    (void)id;
    (void)eventData;

    portENTER_CRITICAL(&g_hostedLinkMux);
    hostedLinkSupervisorOnTransportUp(g_hostedLinkState);
    portEXIT_CRITICAL(&g_hostedLinkMux);
}

// Subscribes to the Hosted transport-failure recovery ladder. Must run
// before WiFi.begin(): networkManagerInitialize() (which calls this) runs
// before webNetworkBootstrap()'s call to networkManagerApplyBootPosture(),
// which is what eventually calls WiFi.begin().
static void hostedRegisterLinkSupervision() {
    // Create the recovery task BEFORE registering the event handlers below.
    // Order is load-bearing (#184 device review, mirrored from
    // bringup/p4_hosted_bench.cpp:1085-1098): hostedTransportFailureHandler()
    // only notifies g_hostedRecoveryTaskHandle when it is non-null, and
    // hostedLinkSupervisorOnTransportFailure() only arms a fresh ladder once
    // per Idle->Armed edge -- an event arriving in the window between
    // "handler registered" and "task created" would set phase=Armed, skip
    // the notify (null handle), and then be permanently unrecoverable: no
    // later event can re-arm from Armed, only from Idle.
    //
    // Size: HOSTED_RECOVERY_TASK_STACK_BYTES in include/config.h carries the measured
    // chain and the sizing rule. This task had no static measurement at all until #271
    // walked it, and it is declared on the ESP32-P4 arm only because
    // PA_CAP_HOSTED_WIFI is set on no other chip.
    const BaseType_t taskResult = xTaskCreatePinnedToCore(hostedRecoveryTaskFn, "HostedRecovery",
                                                           HOSTED_RECOVERY_TASK_STACK_BYTES,
                                                           nullptr, 2,
                                                           &g_hostedRecoveryTaskHandle, 0);
    if (taskResult != pdPASS) {
        PA_LOG_ERROR(TAG,
                     "Failed to create HostedRecovery task; C6 transport-failure events will not "
                     "be handled");
        g_hostedRecoveryTaskHandle = nullptr;
    }

    const esp_err_t loopResult = esp_event_loop_create_default();
    if (loopResult != ESP_OK && loopResult != ESP_ERR_INVALID_STATE) {
        PA_LOG_ERROR(TAG, "esp_event_loop_create_default failed: %d (%s)", (int)loopResult,
                     esp_err_to_name(loopResult));
    }

    static esp_event_handler_instance_t transportFailureInstance;
    static esp_event_handler_instance_t transportUpInstance;

    esp_err_t err = esp_event_handler_instance_register(ESP_HOSTED_EVENT,
                                                          ESP_HOSTED_EVENT_TRANSPORT_FAILURE,
                                                          &hostedTransportFailureHandler, nullptr,
                                                          &transportFailureInstance);
    if (err != ESP_OK) {
        PA_LOG_ERROR(TAG, "Failed to register ESP_HOSTED_EVENT_TRANSPORT_FAILURE handler: %d (%s)",
                     (int)err, esp_err_to_name(err));
    }

    err = esp_event_handler_instance_register(ESP_HOSTED_EVENT, ESP_HOSTED_EVENT_TRANSPORT_UP,
                                               &hostedTransportUpHandler, nullptr,
                                               &transportUpInstance);
    if (err != ESP_OK) {
        PA_LOG_ERROR(TAG, "Failed to register ESP_HOSTED_EVENT_TRANSPORT_UP handler: %d (%s)",
                     (int)err, esp_err_to_name(err));
    }
}

// Read-only snapshot for /api/status (#189).
HostedLinkStatusSnapshot hostedLinkQueryStatus() {
    HostedLinkStatusSnapshot snap;
    portENTER_CRITICAL(&g_hostedLinkMux);
    snap.phase = g_hostedLinkState.phase;
    snap.transportFailureEventCount = g_hostedLinkState.transportFailureEventCount;
    snap.transportUpEventCount = g_hostedLinkState.transportUpEventCount;
    snap.attemptCount = g_hostedLinkState.attemptCount;
    snap.totalAttemptCount = g_hostedLinkState.totalAttemptCount;
    snap.recoveredCount = g_hostedLinkState.recoveredCount;
    snap.lastFailureAtMs = g_hostedLinkState.lastFailureAtMs;
    snap.lastAttemptAtMs = g_hostedLinkState.lastAttemptAtMs;
    snap.degradedAtMs = g_hostedLinkState.degradedAtMs;
    portEXIT_CRITICAL(&g_hostedLinkMux);
    return snap;
}

// ============================================================================
// Seam entry points
// ============================================================================

// Event handler for the Arduino WiFi driver. Shared switch logic lives in
// web_network_manager_common.cpp; this wrapper only supplies this backend's
// own event cache. Fires identically under ESP-Hosted: the Arduino core
// registers its WIFI_EVENT/IP_EVENT handlers unconditionally of
// CONFIG_ESP_HOSTED_ENABLED (WiFiGeneric.cpp initWiFiEvents()), so
// STA_GOT_IP/STA_DISCONNECTED/AP_START are the same events whether the radio
// is native or relayed over SDIO to the C6.
static void handleWiFiEventBackend(WiFiEvent_t event) {
    wifiNetworkManagerHandleEventCommon(event, TAG, &g_staConnected);
}

// Initialize network manager: register the WiFi event handler and the
// Hosted transport-failure recovery ladder.
void networkManagerInitialize() {
    WiFi.onEvent(handleWiFiEventBackend);
    hostedRegisterLinkSupervision();
}

// Apply WiFi boot posture. Called from webNetworkBootstrap(). Same posture
// code as the native backend (see the file header comment): WiFi.mode()/
// WiFi.begin()/WiFi.softAP() bring the Hosted transport up implicitly on
// first use, and this is the boot path, not the co-processor-recovery
// rejoin path above.
void networkManagerApplyBootPosture(WifiBootPosture posture, const WifiConfig& settings) {
    wifiNetworkManagerApplyBootPostureCommon(posture, settings, TAG);
}

// Query WiFi connectivity status. Reads hardware state and derives
// connectivity fields.
//
// Vendor-boundary note: WiFi.status() itself is safe to read here -- it is
// only unsafe as the sole liveness signal for the SDIO transport (#184's
// hardware finding: a dead transport still reads WL_CONNECTED forever). This
// function answers "what does the radio believe", the same question the
// native backend answers; the transport-truth signal
// (ESP_HOSTED_EVENT_TRANSPORT_FAILURE/_UP) is a separate, additional status
// surface (hostedLinkQueryStatus()) rather than folded into this query.
WifiConnectivityStatus networkManagerQueryConnectivity() {
    return wifiNetworkManagerQueryConnectivityCommon();
}

// Query STA connection status via event cache (Core 1 safe).
bool networkManagerStationConnected() {
    return g_staConnected;
}

#endif  // PA_CAP_HOSTED_WIFI

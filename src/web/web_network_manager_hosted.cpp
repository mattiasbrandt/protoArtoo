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
// order, ported from the device-proven bench/p4_hosted_bench.cpp (recovered
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
// The re-init fit check (#471): heap_caps_* to read the heap the vendor's
// pools come from, sdkconfig.h for the Kconfig sizes they are built from, and
// the vendor version the mirrored sizes were read against.
#include "esp_heap_caps.h"
#include "esp_hosted_host_fw_ver.h"
#include "sdkconfig.h"
// driver/gpio.h: gpio_set_level() for the operator-initiated enable-line
// pulse (hostedLinkResetCoprocessor(), #243). The pin is owned and already
// configured as an output by ESP-Hosted itself, so this file drives a level
// and never a direction -- see that function for why.
#include "driver/gpio.h"

#include "../../include/audio_task.h"
#include "../../include/config_cache.h"
#include "../../include/dome_link.h"
#include "../../include/hosted_link_c6_reset.h"
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
// custom_sdkconfig (platformio.ini) leaves that symbol undefined (#189), so
// the event fires and the host survives -- but nothing reconnects on its
// own. This is that missing subscriber, ported from the device-proven
// bench/p4_hosted_bench.cpp rather than re-derived (#189's own instruction:
// "port its shape, do not re-derive it").
//
// The decision logic (phase model, attempt bound) lives in
// include/hosted_link_supervisor.h as a pure, host-testable step core; this
// file owns only the device I/O the core is deliberately free of: task
// creation, event registration, the blocking hostedDeinitWiFi()/
// hostedInitWiFi() cycle, the raw esp_wifi_* rejoin, and logging.
//
// Liveness (#471): the transport event is not the only way a C6 dies. On
// board 2 it stopped answering RPCs with the SDIO transport still "up", no
// TRANSPORT_FAILURE was posted, and the ladder never armed. So the C6's own
// heartbeat (ESP_HOSTED_EVENT_CP_HEARTBEAT, off by default in ESP-Hosted) is
// enabled once the transport is up, the recovery task checks it every
// kHostedLinkLivenessIntervalMs, and a quiet window arms the ladder through
// the same Idle->Armed transition. Where the heartbeat cannot be enabled, one
// esp_wifi_get_mode() probe per interval stands in for it. The same evidence
// decides whether an attempt recovered: hostedIsInitialized() read true while
// every RPC timed out, so it is no longer enough on its own.
//
// Re-init fit (#471): the vendor's re-init asserts every allocation it makes,
// so a squeezed heap turned a ladder attempt into a controller restart on
// board 2. Each attempt therefore checks that the heap can hold the re-init
// before it calls hostedInitWiFi(), and counts the attempt failed when it
// cannot -- see hostedMeasureReinitFit().
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
// bench/p4_hosted_bench.cpp:654-670 proved on hardware.
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

// The heartbeat interval handed to the C6, in the whole seconds
// esp_hosted_configure_heartbeat() takes.
static constexpr int kHostedLinkHeartbeatIntervalSec =
    static_cast<int>(kHostedLinkLivenessIntervalMs / 1000);

// How long an attempt waits, after the heartbeat configure RPC returns, for
// the C6 to prove it is serving (#471). The slave creates its heartbeat timer
// when it handles that RPC, with a period of one full interval
// (slave_control.c start_heartbeat(): xTimerCreate(duration*TIMEOUT_IN_SEC,
// pdTRUE, ...)), so the first beat lands kHostedLinkLivenessIntervalMs after
// it; the 2 s on top is for the SDIO hop and the esp_event loop delivering
// it. The window therefore starts when the configure call returns, not
// before it. The configure RPC (ahead of the window) and the
// esp_wifi_get_mode() probe (inside it) each run under the vendor's own 5 s
// response timeout (rpc_slave_if.h DEFAULT_RPC_RSP_TIMEOUT), so an attempt
// against a dead C6 spends at most about 12 s here.
static constexpr uint32_t kHostedLinkLivenessConfirmMs = kHostedLinkLivenessIntervalMs + 2000;
static constexpr uint32_t kHostedLinkLivenessPollMs = 100;

// Set once hostedRegisterLinkSupervision() has the CP_HEARTBEAT handler in
// place. Written before the HostedRecovery task is created, never after, so
// the task creation publishes it to the only task that reads it.
static bool g_hostedHeartbeatHandlerRegistered = false;

// Enables the C6 heartbeat, or says why it cannot be read. Without the
// handler a beat would go unseen, and watching for it would arm the ladder
// on a healthy C6 and then fail every attempt -- so that case reports an
// error here and the caller falls back to the probe.
static esp_err_t hostedEnableHeartbeat() {
    if (!g_hostedHeartbeatHandlerRegistered) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_hosted_configure_heartbeat(true, kHostedLinkHeartbeatIntervalSec);
}

// What an attempt saw after its re-init. Raw esp_err_t results, so kept here
// rather than in the pure core, like g_hostedRejoinResult above.
// Any reply from the C6 counts as answered (operator's decision on #471,
// 2026-10-05). ESP_OK is the obvious one. ESP_ERR_WIFI_NOT_INIT can only come
// from the slave: its GetWifiMode handler wraps esp_wifi_get_mode() in
// RPC_RET_FAIL_IF (slave_wifi_std.c) and sends the error back in its
// response, so it proves the round trip just as well -- and it is what a C6
// the re-init has just reset answers, because its WiFi driver comes back only
// with hostedRejoinAfterRecovery(). Host-side failures never reached the
// slave and stay unanswered: check_transport_up()'s bare ESP_FAIL, a NULL
// response, and every RPC_ERR_* (0x2f00 onwards, esp_hosted_rpc.h, so none
// collides with 0x3001).
static bool hostedWifiGetModeAnswered(esp_err_t result) {
    return result == ESP_OK || result == ESP_ERR_WIFI_NOT_INIT;
}

struct HostedLivenessEvidence {
    bool tried = false;  // false when the host was not initialised, so nothing was asked
    esp_err_t heartbeatConfigResult = ESP_OK;
    esp_err_t wifiGetModeResult = ESP_OK;
    bool heartbeatSeen = false;
    uint32_t heartbeatNumber = 0;

    bool answered() const {
        return tried && (heartbeatSeen || hostedWifiGetModeAnswered(wifiGetModeResult));
    }
};

// The liveness half of an attempt's verdict: a heartbeat newer than the
// re-init, or any reply to esp_wifi_get_mode() (hostedWifiGetModeAnswered()),
// whichever comes first (operator's decision on #471), within
// kHostedLinkLivenessConfirmMs of the heartbeat configure returning.
//
// The heartbeat is re-enabled first, before the verdict: hostedInitWiFi()
// resets the C6 on its way up (esp_hosted_connect_to_slave() under
// CONFIG_ESP_HOSTED_SLAVE_RESET_ON_EVERY_HOST_BOOTUP), so the C6 has
// forgotten it, and the watch that follows a recovered attempt reads it.
static HostedLivenessEvidence hostedAwaitLivenessAfterReinit() {
    HostedLivenessEvidence evidence;
    evidence.tried = true;

    unsigned int beatsBefore;
    portENTER_CRITICAL(&g_hostedLinkMux);
    beatsBefore = g_hostedLinkState.heartbeatCount;
    portEXIT_CRITICAL(&g_hostedLinkMux);

    evidence.heartbeatConfigResult = hostedEnableHeartbeat();
    // The slave starts its heartbeat timer when it handles the configure, so
    // the window opens now, not before the RPC (kHostedLinkLivenessConfirmMs).
    const uint32_t windowStartMs = millis();

    wifi_mode_t mode = WIFI_MODE_NULL;
    evidence.wifiGetModeResult = esp_wifi_get_mode(&mode);
    if (hostedWifiGetModeAnswered(evidence.wifiGetModeResult)) {
        return evidence;
    }

    for (;;) {
        portENTER_CRITICAL(&g_hostedLinkMux);
        evidence.heartbeatSeen = g_hostedLinkState.heartbeatCount != beatsBefore;
        evidence.heartbeatNumber = g_hostedLinkState.lastHeartbeatNumber;
        portEXIT_CRITICAL(&g_hostedLinkMux);

        if (evidence.heartbeatSeen ||
            static_cast<uint32_t>(millis() - windowStartMs) >= kHostedLinkLivenessConfirmMs) {
            return evidence;
        }
        vTaskDelay(pdMS_TO_TICKS(kHostedLinkLivenessPollMs));
    }
}

// How many later liveness ticks retry the heartbeat configure after it
// failed, before the watch stays on the probe until the next recovery. One
// transient timeout at boot must not leave the watch on the probe for good;
// a slave that genuinely refuses the request costs this many extra RPCs.
static constexpr unsigned int kHostedLinkHeartbeatEnableRetries = 3;

// Retries left for the current Probe watch. Recovery-task only.
static unsigned int g_hostedHeartbeatRetriesLeft = 0;

// Starts the liveness watch on whichever evidence the C6 can give, from the
// result of the hostedEnableHeartbeat() call just made.
static void hostedStartLivenessWatch(esp_err_t heartbeatConfigResult, const char* when) {
    const bool heartbeat = heartbeatConfigResult == ESP_OK;
    g_hostedHeartbeatRetriesLeft = heartbeat ? 0 : kHostedLinkHeartbeatEnableRetries;

    portENTER_CRITICAL(&g_hostedLinkMux);
    hostedLinkSupervisorStartLivenessWatch(
        g_hostedLinkState,
        heartbeat ? HostedLinkLivenessSource::Heartbeat : HostedLinkLivenessSource::Probe,
        millis());
    portEXIT_CRITICAL(&g_hostedLinkMux);

    if (heartbeat) {
        PA_LOG_INFO(TAG,
                    "Hosted link liveness watch (%s): C6 heartbeat every %d s; the recovery "
                    "ladder arms after %u missed",
                    when, kHostedLinkHeartbeatIntervalSec, kHostedLinkLivenessMissLimit);
    } else {
        PA_LOG_WARN(TAG,
                    "Hosted link liveness watch (%s): C6 heartbeat unavailable: "
                    "%d (%s); falling back to one esp_wifi_get_mode() probe every %u ms, arming "
                    "after %u consecutive unanswered, and retrying the heartbeat on the next %u",
                    when, (int)heartbeatConfigResult, esp_err_to_name(heartbeatConfigResult),
                    (unsigned)kHostedLinkLivenessIntervalMs, kHostedLinkLivenessMissLimit,
                    kHostedLinkHeartbeatEnableRetries);
    }
}

// Boot half of the watch: enables the heartbeat once the transport is up.
// Returns false while it must keep waiting.
//
// Up means ESP_HOSTED_EVENT_TRANSPORT_UP has been seen, not just
// hostedIsInitialized(): hostedInit() sets that flag before esp_hosted_init()
// runs, and a configure call made in that window gets check_transport_up()'s
// bare ESP_FAIL -- the same answer a slave without heartbeat support gives,
// so a healthy board would fall back to the probe. The event is posted by
// transport_active_cb() at the moment the vendor's own transport-up flag is
// set. A configure that times out once the transport is up is not that
// misreading: it is a C6 that is not answering, and the probe fallback then
// arms the ladder after kHostedLinkLivenessMissLimit failures, which is right.
static bool hostedStartBootLivenessWatch() {
    bool transportSeenUp;
    HostedLinkPhase phase;
    portENTER_CRITICAL(&g_hostedLinkMux);
    transportSeenUp = g_hostedLinkState.transportUpEventCount > 0;
    phase = g_hostedLinkState.phase;
    portEXIT_CRITICAL(&g_hostedLinkMux);

    if (!transportSeenUp || !hostedIsInitialized()) {
        return false;
    }
    // A run armed before the watch could start: that run starts the watch
    // when it recovers, and a Degraded ladder is never watched again.
    if (phase != HostedLinkPhase::Idle) {
        return true;
    }

    hostedStartLivenessWatch(hostedEnableHeartbeat(), "boot");
    return true;
}

// One liveness check, on the recovery task's interval wake. On the Probe
// source it first retries the heartbeat while retries are left, then runs
// the probe; then it asks the step core whether the watch has gone quiet.
// Returns true when that miss armed a fresh ladder run.
static bool hostedLivenessTick() {
    HostedLinkLivenessSource source;
    portENTER_CRITICAL(&g_hostedLinkMux);
    source = g_hostedLinkState.livenessSource;
    portEXIT_CRITICAL(&g_hostedLinkMux);

    if (source == HostedLinkLivenessSource::Probe && g_hostedHeartbeatRetriesLeft > 0) {
        g_hostedHeartbeatRetriesLeft--;
        const esp_err_t retryResult = hostedEnableHeartbeat();
        if (retryResult == ESP_OK) {
            // Switches the watch to the heartbeat with a fresh window.
            hostedStartLivenessWatch(retryResult, "heartbeat retry");
            return false;
        }
        if (g_hostedHeartbeatRetriesLeft == 0) {
            PA_LOG_WARN(TAG,
                        "Hosted link liveness watch: C6 heartbeat still unavailable after %u "
                        "retries: %d (%s); staying on the esp_wifi_get_mode() probe until the "
                        "next recovery",
                        kHostedLinkHeartbeatEnableRetries, (int)retryResult,
                        esp_err_to_name(retryResult));
        }
    }

    if (source == HostedLinkLivenessSource::Probe) {
        wifi_mode_t mode = WIFI_MODE_NULL;
        const esp_err_t probeResult = esp_wifi_get_mode(&mode);
        const bool answered = hostedWifiGetModeAnswered(probeResult);
        unsigned int failures;
        portENTER_CRITICAL(&g_hostedLinkMux);
        hostedLinkSupervisorRecordProbe(g_hostedLinkState, millis(), answered);
        failures = g_hostedLinkState.consecutiveProbeFailures;
        portEXIT_CRITICAL(&g_hostedLinkMux);
        if (!answered) {
            PA_LOG_WARN(TAG,
                        "Hosted link liveness probe: esp_wifi_get_mode unanswered: %d (%s), %u/%u",
                        (int)probeResult, esp_err_to_name(probeResult), failures,
                        kHostedLinkLivenessMissLimit);
        }
    }

    bool due;
    HostedLinkFailureActions actions;
    uint32_t silentMs = 0;
    unsigned int failures = 0;
    unsigned int missCount = 0;
    portENTER_CRITICAL(&g_hostedLinkMux);
    const uint32_t nowMs = millis();
    due = hostedLinkSupervisorLivenessDue(g_hostedLinkState, nowMs);
    if (due) {
        source = g_hostedLinkState.livenessSource;
        silentMs = nowMs - g_hostedLinkState.lastLivenessAtMs;
        failures = g_hostedLinkState.consecutiveProbeFailures;
        actions = hostedLinkSupervisorOnLivenessMissed(g_hostedLinkState);
        missCount = g_hostedLinkState.livenessMissCount;
    }
    portEXIT_CRITICAL(&g_hostedLinkMux);

    if (!due) {
        return false;
    }

    const char* outcome = actions.shouldNotifyRecoveryTask
                              ? "recovery ladder armed"
                              : "folded into the run already in flight";
    if (source == HostedLinkLivenessSource::Heartbeat) {
        PA_LOG_WARN(TAG, "Hosted link liveness miss #%u: no C6 heartbeat for %u ms (%u x %u ms); %s",
                    missCount, (unsigned)silentMs, kHostedLinkLivenessMissLimit,
                    (unsigned)kHostedLinkLivenessIntervalMs, outcome);
    } else {
        PA_LOG_WARN(TAG,
                    "Hosted link liveness miss #%u: %u consecutive esp_wifi_get_mode() probes "
                    "unanswered; %s",
                    missCount, failures, outcome);
    }
    return actions.shouldNotifyRecoveryTask;
}

// ---------------------------------------------------------------------------
// Re-init fit check (#471)
//
// hostedInitWiFi() -> esp_hosted_init() rebuilds everything the ladder's
// deinit tore down, and asserts each allocation instead of failing: on board
// 2 the SDIO pool did not fit and the controller restarted
// (`assert failed: sdio_mempool_create sdio_drv.c:258 (buf_mp_g)`), which
// ADR 0032 forbids. So no runtime hostedInitWiFi() runs unless the heap can
// hold the re-init; an attempt that cannot is counted failed and the ladder's
// bound decides. Boot is not checked: the heap is fresh there.
//
// What the re-init allocates, in order (ESP-Hosted 2.12.13):
//   1. The channel mempool: add_esp_wifi_remote_channels() ->
//      transport_drv_add_channel() -> transport_drv_common_mempool_create()
//      (transport_drv.c:283-302, assert(mempool_common)).
//   2. bus_init_internal() (sdio_drv.c:1466-1556): two counting semaphores
//      and six priority queues, each asserted, then the SDIO mempool
//      (sdio_mempool_create(), sdio_drv.c:242-260, assert(buf_mp_g)).
//   3. After it: the SDMMC card and bus mutex, a semaphore, the four SDIO
//      tasks, then rpc_init() under ESP_ERROR_CHECK (mutexes, semaphores,
//      the serial interface, two queues and the two RPC tasks).
// Both pools are one contiguous block each, allocated through
// transport_util_malloc() with HOSTED_MEM_CAP_DMA -> hosted_malloc_align(),
// which asks internal DMA-capable RAM (port_esp_hosted_host_os.c:128-143)
// because CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM is off.
//
// Only the largest free block is knowable, not the second largest, so the
// check is: the largest block holds steps 1 and 2 together, and the free
// total also covers step 3. That is conservative -- two separate holes that
// would each have fitted a pool are refused -- and it errs on the side of a
// failed attempt rather than a vendor assert.
// ---------------------------------------------------------------------------

// The sizes below are mirrored from private vendor headers this file cannot
// include (the component exports only host/ and host/api/include). A vendor
// bump stops the build here until they are re-read.
static_assert(ESP_HOSTED_VERSION_VAL(ESP_HOSTED_VERSION_MAJOR_1, ESP_HOSTED_VERSION_MINOR_1,
                                     ESP_HOSTED_VERSION_PATCH_1) ==
                  ESP_HOSTED_VERSION_VAL(2, 12, 13),
              "ESP-Hosted changed: re-read the pool sizes in sdio_drv.c, transport_drv.c and "
              "mempool_ll.h, then the re-init fit check below");
// The pools exist only with the mempool on, and the check reads internal
// DMA RAM because that is where hosted_malloc_align() puts them without the
// PSRAM preference. Either setting changing needs the check re-derived.
#if !CONFIG_ESP_HOSTED_USE_MEMPOOL || CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM
#error "The hosted re-init fit check assumes ESP-Hosted mempools in internal DMA RAM; re-derive it"
#endif

// The heap hosted_malloc_align() falls back to, and with the PSRAM preference
// off the only one it asks.
static constexpr uint32_t kHostedReinitHeapCaps =
    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;

// Bytes hosted_mempool_create() asks for (mempool.c:76-80):
// MEMPOOL_ALIGNED(OS_MEMPOOL_BYTES(blocks, blockSize), 64). OS_MEMPOOL_BYTES
// rounds each block up to 4-byte words (mempool_ll.h:198-202, no
// OS_MEMPOOL_GUARD defined), and MEMPOOL_ALIGNED adds a full 64 bytes even to
// a size that is already aligned (mempool.c:17) -- that is the vendor's
// arithmetic, mirrored as it is, not a slip here.
static constexpr size_t hostedMempoolWordBytes(size_t blocks, size_t blockSize) {
    return sizeof(uint32_t) * (((blockSize + 3) / 4) * blocks);
}
static constexpr size_t hostedMempoolBytes(size_t blocks, size_t blockSize) {
    return hostedMempoolWordBytes(blocks, blockSize) + 64 -
           (hostedMempoolWordBytes(blocks, blockSize) & 63);
}

// Step 1: (TX queue + MEMPOOL_PADDING 5, transport_drv.c:44) blocks of
// ESP_TRANSPORT_MAX_BUF_SIZE 1600 (esp_hosted_transport.h:50); 40,064 B at
// the TX queue depth of 20.
static constexpr size_t kHostedChannelPoolBytes =
    hostedMempoolBytes(CONFIG_ESP_HOSTED_SDIO_TX_Q_SIZE + 5, 1600);
// Step 2's pool: (RX queue + MIN_MEMPOOL_REQ 11, sdio_drv.c:120-124) blocks
// of MAX_SDIO_BUFFER_SIZE 1536 (esp_hosted_transport.h:44); 47,680 B at the
// RX queue depth of 20.
static constexpr size_t kHostedSdioPoolBytes =
    hostedMempoolBytes(CONFIG_ESP_HOSTED_SDIO_RX_Q_SIZE + 11, 1536);

// Step 2's small allocations, made before the SDIO pool and so counted in
// the same block: six priority queues of the queue depth x 24 B
// interface_buffer_handle_t (3 x 40 x 24 = 2,880 B at 20/20) plus their
// control blocks, two semaphores, the two channel structs and pool headers,
// and up to 64 B of alignment slack for each pool. 4 KB covers them.
static constexpr size_t kHostedReinitBeforeSdioPoolBytes = 4096;

// Step 3: the four SDIO tasks (sdio_rx_buf at CONFIG_ESP_HOSTED_DFLT_TASK_STACK,
// the other three at the port's DFLT_TASK_STACK_SIZE 5 KB,
// port_esp_hosted_host_os.h:65), the two RPC tasks (RPC_TASK_STACK_SIZE 5 KB,
// port_esp_hosted_host_os.h:63), 512 B per task for its TCB and handle, and
// 2 KB for the card, mutexes, semaphores, serial handles and RPC queues.
static constexpr size_t kHostedReinitAfterSdioPoolBytes =
    CONFIG_ESP_HOSTED_DFLT_TASK_STACK + 3 * 5120 + 2 * 5120 + 6 * 512 + 2048;

static constexpr size_t kHostedReinitLargestBlockNeeded =
    kHostedChannelPoolBytes + kHostedReinitBeforeSdioPoolBytes + kHostedSdioPoolBytes;
static constexpr size_t kHostedReinitFreeNeeded =
    kHostedReinitLargestBlockNeeded + kHostedReinitAfterSdioPoolBytes;

struct HostedReinitFit {
    size_t largestBlock = 0;
    size_t freeBytes = 0;

    bool fits() const {
        return largestBlock >= kHostedReinitLargestBlockNeeded &&
               freeBytes >= kHostedReinitFreeNeeded;
    }
};

// Read after the deinit, never before it: the deinit is what frees the old
// pools, tasks and queues, so only then does the heap show what the re-init
// will find.
static HostedReinitFit hostedMeasureReinitFit() {
    HostedReinitFit fit;
    fit.largestBlock = heap_caps_get_largest_free_block(kHostedReinitHeapCaps);
    fit.freeBytes = heap_caps_get_free_size(kHostedReinitHeapCaps);
    return fit;
}

// One ladder run, from Armed to Idle or Degraded.
static void hostedRunRecoveryLadder() {
    portENTER_CRITICAL(&g_hostedLinkMux);
    hostedLinkSupervisorBeginAttemptRun(g_hostedLinkState);
    portEXIT_CRITICAL(&g_hostedLinkMux);

    bool recovered = false;
    unsigned int attemptsThisRun = 0;
    HostedLivenessEvidence liveness;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kHostedLinkRecoveryAttemptIntervalMs));

        PA_LOG_INFO(TAG, "Hosted link recovery attempt %u/%u: hostedDeinitWiFi + hostedInitWiFi",
                    attemptsThisRun + 1, kHostedLinkRecoveryMaxAttempts);

        // Nothing to tear down after an attempt whose init was refused: its
        // deinit already freed the stack. Arduino's hostedDeinit() would
        // return false without touching the vendor here (esp32-hal-hosted.c,
        // `if (!hosted_initialized)`), but the skip is explicit so the log
        // does not read deinit=FAIL, and so a second esp_hosted_deinit() can
        // never run -- it is unguarded, and teardown_transport() leaves its
        // bus_handle pointing at the old card.
        const bool deinitTried = hostedIsInitialized();
        const bool deinitOk = deinitTried && hostedDeinitWiFi();

        const HostedReinitFit fit = hostedMeasureReinitFit();
        const bool initTried = fit.fits();
        if (!initTried) {
            PA_LOG_WARN(TAG,
                        "Hosted link recovery attempt %u/%u: re-init refused, it would not fit "
                        "the internal DMA heap: largest free block %u B, needs %u B (channel pool "
                        "%u + %u + SDIO pool %u); free %u B, needs %u B. Attempt counted failed",
                        attemptsThisRun + 1, kHostedLinkRecoveryMaxAttempts,
                        (unsigned)fit.largestBlock, (unsigned)kHostedReinitLargestBlockNeeded,
                        (unsigned)kHostedChannelPoolBytes,
                        (unsigned)kHostedReinitBeforeSdioPoolBytes, (unsigned)kHostedSdioPoolBytes,
                        (unsigned)fit.freeBytes, (unsigned)kHostedReinitFreeNeeded);
        }
        const bool initOk = initTried && hostedInitWiFi();
        // Device-truthful outcome, never WiFi.status() -- a dead transport
        // reads WL_CONNECTED forever (#184 bench finding). The same holds one
        // layer down: hostedIsInitialized() is a host-side flag, and it read
        // true while every RPC timed out (#471). Only an answered RPC or a
        // heartbeat proves the C6 is serving, so both must hold.
        const bool initialised = hostedIsInitialized();
        liveness = initialised ? hostedAwaitLivenessAfterReinit() : HostedLivenessEvidence{};
        const bool transportUp = initialised && liveness.answered();
        const uint32_t nowMs = millis();

        // wifiGetMode reads ESP_ERR_WIFI_NOT_INIT on a C6 the re-init reset
        // (its WiFi driver comes back with the rejoin, after this verdict);
        // that is a reply, so it counts (hostedWifiGetModeAnswered()).
        const char* verdict =
            transportUp ? "C6 answering, recovered" : "C6 not answering, attempt failed";
        const char* deinitText = !deinitTried ? "skipped" : (deinitOk ? "ok" : "FAIL");
        const char* initText = !initTried ? "refused" : (initOk ? "ok" : "FAIL");
        if (liveness.tried) {
            PA_LOG_INFO(TAG,
                        "Hosted link recovery attempt result: deinit=%s init=%s "
                        "hostedIsInitialized=true heartbeatConfig=%d(%s) wifiGetMode=%d(%s) "
                        "heartbeatSeen=%s heartbeatNumber=%lu -> %s",
                        deinitText, initText, (int)liveness.heartbeatConfigResult,
                        esp_err_to_name(liveness.heartbeatConfigResult),
                        (int)liveness.wifiGetModeResult,
                        esp_err_to_name(liveness.wifiGetModeResult),
                        liveness.heartbeatSeen ? "yes" : "no",
                        (unsigned long)liveness.heartbeatNumber, verdict);
        } else {
            PA_LOG_INFO(TAG,
                        "Hosted link recovery attempt result: deinit=%s init=%s "
                        "hostedIsInitialized=false, liveness not asked -> %s",
                        deinitText, initText, verdict);
        }

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
        // The verdict's configure call already re-enabled the heartbeat on
        // the reset C6; this restarts the watch on it (or on the probe, when
        // the C6 refused), with a fresh first window.
        hostedStartLivenessWatch(liveness.heartbeatConfigResult, "after recovery");
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
        // neither a transport failure nor a liveness miss can re-arm a fresh
        // run from it (see hostedRecoveryTaskFn() below). No second latch is
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
}

// Runs on its own task, never loop() or the esp_event default-loop task:
// hostedDeinitWiFi()/hostedInitWiFi(), the esp_wifi_* rejoin above, and the
// liveness RPCs can block for seconds (CARD_INIT_TIMEOUT_MS retries, RPC
// teardown/setup, the 5 s RPC response timeout), and the event handlers that
// feed this task must stay non-blocking.
static void hostedRecoveryTaskFn(void* arg) {
    (void)arg;

    bool bootWatchPending = true;
    uint32_t nextTickAtMs = millis() + kHostedLinkLivenessIntervalMs;

    for (;;) {
        // Wakes on a transport-failure notification, or on the liveness
        // interval to run the watch (#471). The wait runs to a deadline
        // rather than a fixed delay: a probe against a silent C6 blocks for
        // the vendor's 5 s RPC timeout, and a fixed 5 s on top would stretch
        // the operator's 3 x 5 s window to about 30 s.
        const int32_t remainingMs = static_cast<int32_t>(nextTickAtMs - millis());
        const uint32_t notified =
            ulTaskNotifyTake(pdTRUE, remainingMs > 0 ? pdMS_TO_TICKS(remainingMs) : 0);

        if (notified == 0) {
            nextTickAtMs += kHostedLinkLivenessIntervalMs;
            if (bootWatchPending) {
                bootWatchPending = !hostedStartBootLivenessWatch();
                continue;
            }
            if (!hostedLivenessTick()) {
                continue;
            }
            // The miss armed a fresh run from Idle: run it here rather than
            // notifying this same task.
        }

        hostedRunRecoveryLadder();
        // A run that recovered started the watch itself; a Degraded one is
        // never watched again. Either way the boot step is done.
        bootWatchPending = false;
        nextTickAtMs = millis() + kHostedLinkLivenessIntervalMs;

        // Back to the top. Degraded is terminal: the step core refuses to
        // re-arm from it (hostedLinkSupervisorOnTransportFailure() and
        // hostedLinkSupervisorOnLivenessMissed() fold into nothing, and
        // hostedLinkSupervisorLivenessDue() is false outside Idle), so a
        // degraded ladder wakes on its interval and does nothing, for the rest
        // of this boot -- the "must not retry forever" bound (ADR 0032).
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
// derived from WiFi.status(), and its first arrival is what lets the boot
// liveness watch start (hostedStartBootLivenessWatch()).
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

// ESP_HOSTED_EVENT_CP_HEARTBEAT (#471): the C6's beat, an RPC event it sends
// every kHostedLinkHeartbeatIntervalSec once enabled. Runs on the esp_event
// loop, so it records the time and the beat number and nothing else: no RPC,
// no allocation, no blocking, no log line every five seconds.
static void hostedHeartbeatHandler(void* arg, esp_event_base_t base, int32_t id,
                                   void* eventData) {
    (void)arg;
    (void)base;
    (void)id;

    const uint32_t beatNumber =
        eventData != nullptr
            ? static_cast<const esp_hosted_event_heartbeat_t*>(eventData)->heartbeat
            : 0;

    portENTER_CRITICAL(&g_hostedLinkMux);
    hostedLinkSupervisorOnHeartbeat(g_hostedLinkState, millis(), beatNumber);
    portEXIT_CRITICAL(&g_hostedLinkMux);
}

// Subscribes to the Hosted transport-failure recovery ladder. Must run
// before WiFi.begin(): networkManagerInitialize() (which calls this) runs
// before webNetworkBootstrap()'s call to networkManagerApplyBootPosture(),
// which is what eventually calls WiFi.begin().
static void hostedRegisterLinkSupervision() {
    const esp_err_t loopResult = esp_event_loop_create_default();
    if (loopResult != ESP_OK && loopResult != ESP_ERR_INVALID_STATE) {
        PA_LOG_ERROR(TAG, "esp_event_loop_create_default failed: %d (%s)", (int)loopResult,
                     esp_err_to_name(loopResult));
    }

    static esp_event_handler_instance_t transportFailureInstance;
    static esp_event_handler_instance_t transportUpInstance;
    static esp_event_handler_instance_t heartbeatInstance;

    // The heartbeat handler goes in BEFORE the recovery task is created, the
    // opposite of the two below, and for the same kind of reason: the task
    // reads g_hostedHeartbeatHandlerRegistered, and creating the task after
    // the write is what publishes it to the task. A beat arriving before the
    // task exists only records a time, so this order loses nothing. The C6
    // sends none until the task enables the heartbeat once the transport is up.
    esp_err_t err = esp_event_handler_instance_register(ESP_HOSTED_EVENT,
                                                          ESP_HOSTED_EVENT_CP_HEARTBEAT,
                                                          &hostedHeartbeatHandler, nullptr,
                                                          &heartbeatInstance);
    if (err == ESP_OK) {
        g_hostedHeartbeatHandlerRegistered = true;
    } else {
        PA_LOG_ERROR(TAG,
                     "Failed to register ESP_HOSTED_EVENT_CP_HEARTBEAT handler: %d (%s); the "
                     "liveness watch will use the esp_wifi_get_mode() probe instead",
                     (int)err, esp_err_to_name(err));
    }

    // Create the recovery task BEFORE registering the transport handlers below.
    // Order is load-bearing (#184 device review, mirrored from
    // bench/p4_hosted_bench.cpp:1085-1098): hostedTransportFailureHandler()
    // only notifies g_hostedRecoveryTaskHandle when it is non-null, and
    // hostedLinkSupervisorOnTransportFailure() only arms a fresh ladder once
    // per Idle->Armed edge -- an event arriving in the window between
    // "handler registered" and "task created" would set phase=Armed, skip
    // the notify (null handle), and then be permanently unrecoverable: no
    // later event can re-arm from Armed, only from Idle.
    //
    // Size: HOSTED_RECOVERY_TASK_STACK_BYTES (tools/task_stack_recipes.json) carries the measured
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

    err = esp_event_handler_instance_register(ESP_HOSTED_EVENT,
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
// Operator-initiated WiFi module reboot (#243)
//
// The ladder above is shipped and reachable but had never been observed
// firing: reaching terminal Degraded needs five consecutive co-processor
// failures over ~25-35s, the fitted C6 is healthy, and no shipping image could
// provoke a transport failure at all. This is the missing provocation --
// the same 100 ms enable-line pulse bench/p4_hosted_bench.cpp:742-780 proved
// on hardware, ported into the image that ships the ladder rather than
// re-derived.
//
// Three deliberate choices, each of which has a wrong-looking alternative:
//
// 1. RAW LEVEL WRITES, NOT hostedDeinitWiFi()/hostedInitWiFi(). The vendor's
//    own reinit path resets the slave over this same GPIO on its way
//    (esp_hosted_connect_to_slave() with
//    CONFIG_ESP_HOSTED_SLAVE_RESET_ON_EVERY_HOST_BOOTUP) and would be the
//    polite way to bounce the module -- but it is also exactly what the
//    recovery ladder runs, so calling it here would BYPASS the fault instead
//    of injecting one. The point of this operation is that ESP-Hosted is
//    still running and still believes it owns the transport when the module
//    disappears underneath it: its SDIO writes then fail, it posts
//    ESP_HOSTED_EVENT_TRANSPORT_FAILURE, and the ladder arms. That is the
//    behaviour under test.
//
// 2. NO DIRECTION CONFIG. ESP-Hosted claims this pin at init
//    (hostedAssignPinBuses()/esp_hosted_sdio_set_config(),
//    cores/esp32/esp32-hal-hosted.c) and configures it as an output; a
//    gpio_config()/gpio_set_direction() here would be a second owner racing
//    the vendor's reset FSM. The bench sketch drives the level and nothing
//    else, and that is what ran on hardware.
//
// 3. ACTIVE-LOW. The DFR1172 wiki calls the pin EN and the bundled esp_hosted
//    builds with CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=1, which contradict
//    each other on paper. The schematic settles it: the net lands on the
//    ESP32-C6-MINI-1's EN pin, where LOW holds the module in reset and the
//    R16 pull-up is the release. Inverting the polarity was tested on this
//    board (2026-08-22) and changed nothing, so do not "fix" this to match
//    the Kconfig name -- docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md,
//    "GPIO54 polarity" and "The C6 reset net".
//
// Timing after the release edge is the ladder's business, and it is already
// generous: hostedRunRecoveryLadder() waits kHostedLinkRecoveryAttemptIntervalMs
// (5000 ms) before its first attempt, well above the ~1.1-1.6 s ESP-Hosted's
// own logs need to reach card-init success. So the first ladder attempt can
// never mistake "module still booting" for "module failed".
//
// What follows the pulse, and what "recovered" means for it (#471): the
// rebooted C6 has forgotten its heartbeat, so the liveness watch arms the
// ladder within kHostedLinkLivenessMissLimit intervals even when no
// TRANSPORT_FAILURE is posted. Each attempt then counts as recovered only on
// the same liveness evidence as any other run -- a heartbeat newer than the
// re-init or any reply from the C6 to esp_wifi_get_mode() -- never on
// hostedIsInitialized() alone, which is what reported this action's ladder as recovered on board 2
// while every RPC still timed out. Its re-init passes the same fit check as any
// other run's (hostedMeasureReinitFit()): this action never calls
// hostedInitWiFi() itself. The Console record is unchanged: it says
// both edges were driven, which it never claimed was liveness; the ladder's
// attempt-result line is the outcome.
// ============================================================================
HostedLinkResetOutcome hostedLinkResetCoprocessor() {
    HostedLinkResetOutcome outcome;

    // The live pin, from the Hosted HAL, rather than the compile-time
    // BOARD_SDIO_ESP_HOSTED_RESET the variant defines: hostedGetPins() returns
    // what ESP-Hosted actually configured, so this can never drive a pin the
    // transport is not on -- including if a caller ever overrides the set
    // through WiFi.setPins() before WiFi.begin(). protoArtoo does not, so the
    // two agree at 54 today.
    int8_t clk = -1, cmd = -1, d0 = -1, d1 = -1, d2 = -1, d3 = -1, rst = -1;
    hostedGetPins(&clk, &cmd, &d0, &d1, &d2, &d3, &rst);
    const gpio_num_t resetGpio = (gpio_num_t)rst;

    PA_LOG_WARN(TAG,
                "Operator-initiated WiFi module reboot: holding GPIO%d (C6_EN) low for %ums. The "
                "Hosted transport will fail and the recovery ladder should arm; watch this console "
                "for the attempts.",
                (int)rst, (unsigned)kHostedLinkResetAssertMs);

    outcome.assertResult = gpio_set_level(resetGpio, 0);
    vTaskDelay(pdMS_TO_TICKS(kHostedLinkResetAssertMs));
    // Released unconditionally, including when the assert write failed: HIGH
    // is the line's resting state (R16 pulls it there anyway), and skipping
    // the release after a partial failure is the one outcome that leaves the
    // module held in reset for the rest of this boot.
    outcome.releaseResult = gpio_set_level(resetGpio, 1);

    PA_LOG_WARN(TAG,
                "WiFi module reboot pulse done: assert=%d(%s) release=%d(%s) on GPIO%d. Both writes "
                "returning OK is API acceptance, not electrical proof -- the C6_RST pad or the "
                "module's own boot log is what shows it rebooted.",
                (int)outcome.assertResult, esp_err_to_name(outcome.assertResult),
                (int)outcome.releaseResult, esp_err_to_name(outcome.releaseResult), (int)rst);

    return outcome;
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

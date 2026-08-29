/**
 * P4 ESP-Hosted WiFi reliability bench sketch — #184
 * Lives in bringup/ (fenced outside src/) as a throwaway test image.
 *
 * Tests SDIO WiFi transport stability using PsychicHttp server.
 * Provides endpoints for:
 *  - /api/health: Simple liveness check
 *  - /api/status: Link and health metrics with full transport visibility
 *  - /api/c6/reset: Schedule an abrupt C6 co-processor reset
 *  - /api/events: SSE stream of monotonic counter at fixed cadence
 *
 * Critical constraints (from prepared research #184):
 *  - WiFi credential persistence is DISABLED under ESP-Hosted
 *    Must call WiFi.begin(ssid, pass) explicitly, no NVS fallback
 *  - No liveness API for SDIO link, supervise it ourselves via transport
 *  - Never WiFi.scan() unguarded (EHM-257 NULL memcpy crash)
 *  - Avoid BLE entirely (EHM-238 P4+C6 coexistence broken)
 *
 * SSE fidelity boundary — read before interpreting a stalled stream:
 * This sketch streams /api/events through PsychicEventSource, the vendor class
 * production deliberately does NOT use. protoArtoo's own stream evicts a client
 * that misses its send deadline (ADR 0030, include/web_event_stream.h);
 * PsychicEventSourceClient::sendEvent() instead retries httpd_socket_send() in
 * an unbounded loop against a five-second socket timeout, so ONE slow reader
 * stalls the broadcast for every reader — with no SDIO involvement whatsoever.
 * Therefore: if the SSE smoke stalls, suspect this class before suspecting
 * Hosted. Confirm by porting to include/web_event_stream.h — add
 * +<../src/web/web_event_stream.cpp> to this env's build_src_filter (it is
 * standalone: no Arduino, no vendor type, no clock) and supply the two
 * transport functions at the foot of that header. Do not record NO-GO on a
 * stall until that has been ruled out.
 * Production's concurrent-stream cap is 3 (PA_ADMISSION_MAX_SSE_CLIENTS); this
 * sketch has no cap, so client counts above 3 are observation, not verdict.
 *
 * WiFi credentials NEVER committed (public repo). Resolved in this order:
 *   1. -DBENCH_SSID / -DBENCH_PASS build flags, e.g.
 *      PLATFORMIO_BUILD_FLAGS='-DBENCH_SSID=\"ssid\" -DBENCH_PASS=\"pass\"' \
 *        make build BUILD_ENV=firebeetle2_hosted_bench
 *   2. src/secrets.h (gitignored, 0600, written by `make setup-wifi`) - the
 *      same PA_STA_* credentials the shipping firmware uses, so a bench run
 *      associates with the real network without credentials on a command line
 *      or in shell history.
 *   3. A placeholder that cannot associate, so an unconfigured build fails
 *      visibly rather than looking like a transport fault.
 *
 * Uses weak symbols to coexist with .dummy/sketch.cpp.o in custom_sdkconfig pass.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PsychicHttp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>

#include "esp32-hal-hosted.h"

// esp_hosted.h: ESP_HOSTED_EVENT base + event ID enum (#184 transport-failure
// recovery ladder). esp_wifi.h: raw esp_wifi_init/set_mode/start, needed
// because Arduino's own WiFi.begin() cannot restart the WiFi driver on a
// freshly-rebooted co-processor - see the recovery ladder section below.
#include "esp_hosted.h"
#include "esp_wifi.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Provisioned WiFi credentials - never committed. Build flags win; otherwise
// fall back to the operator's local src/secrets.h, then to a placeholder.
// This file is built with build_src_filter excluding src/, so secrets.h is
// reached by relative path rather than via the include path.
#if defined(__has_include)
#if __has_include("../src/secrets.h")
#include "../src/secrets.h"
#define BENCH_HAVE_LOCAL_SECRETS 1
#endif
#endif

#ifndef BENCH_SSID
#if defined(BENCH_HAVE_LOCAL_SECRETS) && defined(PA_STA_SSID)
#define BENCH_SSID PA_STA_SSID
#else
#define BENCH_SSID "protoArtoo-bench"
#endif
#endif

#ifndef BENCH_PASS
#if defined(BENCH_HAVE_LOCAL_SECRETS) && defined(PA_STA_PASSWORD)
#define BENCH_PASS PA_STA_PASSWORD
#else
#define BENCH_PASS "protoArtoo-bench"
#endif
#endif

// An empty PA_STA_SSID is legal in secrets.h (STA is optional there) but is a
// dead bench run: WiFi.begin("") cannot associate, and the resulting silence
// looks exactly like the SDIO fault this harness exists to detect.
#if defined(BENCH_HAVE_LOCAL_SECRETS) && defined(PA_STA_SSID)
static_assert(sizeof(BENCH_SSID) > 1,
              "src/secrets.h has an empty PA_STA_SSID; run `make setup-wifi` or "
              "pass -DBENCH_SSID/-DBENCH_PASS. An empty SSID cannot associate and "
              "would be misread as a Hosted transport failure.");
#endif

// Timing constants in milliseconds.
static constexpr uint32_t BENCH_SSE_CADENCE_MS = 1000;
static constexpr uint32_t WIFI_CHECK_INTERVAL_MS = 5000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
static constexpr uint32_t RESET_RESPONSE_GRACE_MS = 1000;
static constexpr uint32_t RESET_PULSE_MS = 100;

// Recovery ladder bounds (#184). Each attempt's own SDIO card-init timeout
// (sdio_drv.c CARD_INIT_TIMEOUT_MS = 1500ms, with internal retries) needs to
// fully settle before the next attempt starts, so the interval is well above
// that; five attempts over roughly 25-35s is long enough to ride out a
// transient co-processor glitch without looking like the harness has wedged.
static constexpr unsigned int RECOVERY_MAX_ATTEMPTS = 5;
static constexpr uint32_t RECOVERY_ATTEMPT_INTERVAL_MS = 5000;

// benchBootCount is RTC_DATA_ATTR so it survives a CPU reset (not power cycle).
RTC_DATA_ATTR static unsigned int benchBootCount = 0;

static struct {
  uint32_t bootTimeMs = 0;
  unsigned int linkFaultCount = 0;
  uint32_t lastWiFiCheckMs = 0;
  uint32_t lastWiFiBeginAtMs = 0;
  bool wifiConnected = false;
  bool hasConnectedOnce = false;
  wl_status_t firstWiFiBeginStatus = WL_IDLE_STATUS;
  bool firstHostedInitialized = false;
  bool firstHostedWiFiActiveFlag = false;
  uint32_t firstAttemptAtMs = 0;
  unsigned int wifiBeginAttemptCount = 0;
  unsigned int wifiRetryCount = 0;
  wl_status_t lastWiFiBeginStatus = WL_IDLE_STATUS;
  bool httpStartAttempted = false;
  esp_err_t httpStartResult = ESP_OK;
  bool httpStarted = false;
  uint32_t sseFrameCount = 0;
  unsigned int sseClientCount = 0;
  uint32_t lastSseFrameMs = 0;
} benchState;

enum class ResetPhase : uint8_t {
  IDLE,
  RESERVED,
  RESPONSE_GRACE,
  LOW_ASSERTED,
};

static struct {
  ResetPhase phase = ResetPhase::IDLE;
  uint32_t nextRequestId = 1;
  uint32_t currentRequestId = 0;
  unsigned int scheduledCount = 0;
  unsigned int acceptedCount = 0;
  unsigned int rejectedCount = 0;
  unsigned int responseSendFailureCount = 0;
  unsigned int executedCount = 0;
  unsigned int completedCount = 0;
  uint32_t scheduledAtMs = 0;
  uint32_t responseSentAtMs = 0;
  uint32_t lowAttemptAtMs = 0;
  uint32_t highAttemptAtMs = 0;
  uint32_t completedAtMs = 0;
  bool lowWriteAttempted = false;
  bool highWriteAttempted = false;
  esp_err_t lastResponseSendResult = ESP_OK;
  esp_err_t lastLowWriteResult = ESP_OK;
  esp_err_t lastHighWriteResult = ESP_OK;
} resetState;

static portMUX_TYPE benchStateMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE resetMux = portMUX_INITIALIZER_UNLOCKED;

enum class RecoveryPhase : uint8_t {
  IDLE,
  ARMED,
  ATTEMPTING,
  DEGRADED,
};

// State for the bounded ESP_HOSTED_EVENT_TRANSPORT_FAILURE recovery ladder
// (#184). IDLE: no failure outstanding. ARMED: a failure was observed and the
// recovery task has been notified but has not started its first attempt yet
// (a window of a few ticks). ATTEMPTING: a deinit/re-init cycle is in flight -
// superviseLink() must not touch WiFi/Hosted while this holds, see its guard
// clause below. DEGRADED: the ladder exhausted RECOVERY_MAX_ATTEMPTS; terminal
// for this boot by design (ADR 0032 forbids restarting the host to clear it,
// and the ladder itself must not retry forever).
static struct {
  RecoveryPhase phase = RecoveryPhase::IDLE;
  unsigned int transportFailureEventCount = 0;
  unsigned int transportUpEventCount = 0;
  unsigned int attemptCount = 0;       // attempts made in the current/most-recent ladder run
  unsigned int totalAttemptCount = 0;  // lifetime attempts across all ladder runs
  unsigned int recoveredCount = 0;     // number of ladder runs that reached IDLE again
  bool hasAttempted = false;
  bool lastAttemptSucceeded = false;
  bool lastRejoinStaConnectAccepted = false;
  uint32_t lastFailureAtMs = 0;
  uint32_t lastAttemptAtMs = 0;
  uint32_t lastRejoinAtMs = 0;
  uint32_t degradedAtMs = 0;
} recoveryState;

static portMUX_TYPE recoveryMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t recoveryTaskHandle = nullptr;

// HTTP server and SSE source.
PsychicHttpServer http;
PsychicEventSource events;

static uint32_t benchUptimeMs() {
  return millis() - benchState.bootTimeMs;
}

static bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

static const char *resetPhaseName(ResetPhase phase) {
  switch (phase) {
    case ResetPhase::IDLE:
      return "idle";
    case ResetPhase::RESERVED:
      return "reserved";
    case ResetPhase::RESPONSE_GRACE:
      return "responseGrace";
    case ResetPhase::LOW_ASSERTED:
      return "lowAsserted";
  }
  return "unknown";
}

static const char *recoveryPhaseName(RecoveryPhase phase) {
  switch (phase) {
    case RecoveryPhase::IDLE:
      return "idle";
    case RecoveryPhase::ARMED:
      return "armed";
    case RecoveryPhase::ATTEMPTING:
      return "attempting";
    case RecoveryPhase::DEGRADED:
      return "degraded";
  }
  return "unknown";
}

// ============================================================================
// Link Supervision
// ============================================================================

static void startHttpIfReady() {
  bool httpStartAttempted;
  esp_err_t httpStartResult;

  portENTER_CRITICAL(&benchStateMux);
  httpStartAttempted = benchState.httpStartAttempted;
  portEXIT_CRITICAL(&benchStateMux);

  if (httpStartAttempted || WiFi.status() != WL_CONNECTED) {
    return;
  }

  httpStartResult = http.begin();
  const bool httpStarted = (httpStartResult == ESP_OK);

  portENTER_CRITICAL(&benchStateMux);
  benchState.httpStartAttempted = true;
  benchState.httpStartResult = httpStartResult;
  benchState.httpStarted = httpStarted;
  portEXIT_CRITICAL(&benchStateMux);

  Serial.printf(
    "[BENCH] HTTP_START result=%d (%s), started=%s. A client response is still required to prove reachability.\n",
    static_cast<int>(httpStartResult), esp_err_to_name(httpStartResult), httpStarted ? "true" : "false"
  );
}

// ---------------------------------------------------------------------------
// One-shot ESP32-C6 slave OTA over the live SDIO link (#189 probe).
//
// Disabled unless C6_OTA_URL is defined at build time, e.g.
//   PLATFORMIO_BUILD_FLAGS='-DC6_OTA_URL=\"http://10.0.0.44:8000/esp32c6-v2.12.11.bin\"'
// Serve the hash-verified artifact from tasks/c6-backup/c6-reflash-kit/ rather
// than hostedGetUpdateURL(): the bytes are then known, and no TLS stack is
// needed on the bench.
//
// Safety: the slave keeps a dual-OTA layout, so a failed or abandoned write
// leaves otadata pointing at the intact factory slot. A slave older than 2.6.0
// may not implement the OTA RPCs at all, in which case begin/write simply
// fails and NOTHING is written - which is itself the answer.
// ---------------------------------------------------------------------------
#ifdef C6_OTA_URL
static void runC6SlaveOtaOnce() {
  static bool attempted = false;
  if (attempted) {
    return;
  }
  attempted = true;

  uint32_t maj = 0, min = 0, pat = 0;
  hostedGetSlaveVersion(&maj, &min, &pat);
  Serial.printf("[C6OTA] start url=%s slaveVersionBefore=%lu.%lu.%lu\n", C6_OTA_URL,
                (unsigned long)maj, (unsigned long)min, (unsigned long)pat);

  HTTPClient http;
  if (!http.begin(C6_OTA_URL)) {
    Serial.println("[C6OTA] FAIL http.begin");
    return;
  }
  const int code = http.GET();
  const int total = http.getSize();
  if (code != HTTP_CODE_OK || total <= 0) {
    Serial.printf("[C6OTA] FAIL httpCode=%d size=%d\n", code, total);
    http.end();
    return;
  }
  Serial.printf("[C6OTA] fetching %d bytes\n", total);

  if (!hostedBeginUpdate()) {
    Serial.println("[C6OTA] FAIL hostedBeginUpdate - slave likely predates the OTA RPCs. Nothing written.");
    http.end();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  static uint8_t chunk[4096];
  int remaining = total;
  int written = 0;
  bool ok = true;
  uint32_t lastLogAt = 0;

  while (remaining > 0 && http.connected()) {
    const size_t avail = stream->available();
    if (avail == 0) {
      delay(1);
      continue;
    }
    const int n = stream->readBytes(chunk, avail > sizeof(chunk) ? sizeof(chunk) : avail);
    if (n <= 0) {
      continue;
    }
    if (!hostedWriteUpdate(chunk, (uint32_t)n)) {
      Serial.printf("[C6OTA] FAIL hostedWriteUpdate at offset %d\n", written);
      ok = false;
      break;
    }
    written += n;
    remaining -= n;
    if (millis() - lastLogAt > 2000) {
      lastLogAt = millis();
      Serial.printf("[C6OTA] %d/%d bytes\n", written, total);
    }
  }
  http.end();

  if (!ok || written != total) {
    Serial.printf("[C6OTA] ABORT written=%d expected=%d - otadata still points at the factory slot\n", written, total);
    return;
  }
  Serial.printf("[C6OTA] wrote %d bytes; calling hostedEndUpdate\n", written);
  if (!hostedEndUpdate()) {
    Serial.println("[C6OTA] FAIL hostedEndUpdate");
    return;
  }
  Serial.println("[C6OTA] hostedEndUpdate OK; activating (slave reboots, SDIO link drops by design)");
  const bool activated = hostedActivateUpdate();
  Serial.printf("[C6OTA] hostedActivateUpdate=%s. Reboot the P4 and re-read hostedSlaveVersion.\n",
                activated ? "true" : "false");
}
#endif  // C6_OTA_URL

static void superviseLink() {
  const uint32_t now = millis();

  RecoveryPhase recoveryPhaseNow;
  portENTER_CRITICAL(&recoveryMux);
  recoveryPhaseNow = recoveryState.phase;
  portEXIT_CRITICAL(&recoveryMux);
  if (recoveryPhaseNow == RecoveryPhase::ATTEMPTING) {
    // A recovery attempt owns the Hosted/WiFi layer right now
    // (hostedDeinitWiFi()/hostedInitWiFi() are mid-flight, see the recovery
    // ladder section below); do not race it with an independent WiFi.begin()
    // from this periodic supervisor.
    return;
  }

  uint32_t lastWiFiCheckMs;
  bool wasConnected;
  bool hasConnectedOnce;
  uint32_t lastWiFiBeginAtMs;

  portENTER_CRITICAL(&benchStateMux);
  lastWiFiCheckMs = benchState.lastWiFiCheckMs;
  portEXIT_CRITICAL(&benchStateMux);

  if (now - lastWiFiCheckMs < WIFI_CHECK_INTERVAL_MS) {
    return;
  }

  const bool isConnected = (WiFi.status() == WL_CONNECTED);

  portENTER_CRITICAL(&benchStateMux);
  benchState.lastWiFiCheckMs = now;
  wasConnected = benchState.wifiConnected;
  benchState.wifiConnected = isConnected;
  hasConnectedOnce = benchState.hasConnectedOnce;
  portEXIT_CRITICAL(&benchStateMux);

  if (isConnected) {
    if (!hasConnectedOnce) {
      portENTER_CRITICAL(&benchStateMux);
      benchState.hasConnectedOnce = true;
      portEXIT_CRITICAL(&benchStateMux);
      // Print the address the harness is reachable at. Without it the
      // "a client response is still required" instruction below is
      // unactionable: the operator has an associated board and no way to
      // address it short of scanning the network.
      Serial.printf(
        "[BENCH] WiFi reached WL_CONNECTED for the first time. ip=%s rssi=%ddBm "
        "channel=%d\n",
        WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), (int)WiFi.channel());
    }
    startHttpIfReady();
#ifdef C6_OTA_URL
    runC6SlaveOtaOnce();
#endif
    return;
  }

  if (wasConnected) {
    portENTER_CRITICAL(&benchStateMux);
    benchState.linkFaultCount++;
    const unsigned int faultCount = benchState.linkFaultCount;
    portEXIT_CRITICAL(&benchStateMux);
    Serial.printf("[BENCH] WiFi link fault detected. Total faults: %u\n", faultCount);
  }

  // A failed first attempt stays quiescent so its UART evidence is not blurred.
  // Retrying is a later resilience test and is enabled only after one connection.
  portENTER_CRITICAL(&benchStateMux);
  hasConnectedOnce = benchState.hasConnectedOnce;
  lastWiFiBeginAtMs = benchState.lastWiFiBeginAtMs;
  portEXIT_CRITICAL(&benchStateMux);

  if (!hasConnectedOnce || now - lastWiFiBeginAtMs < WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  const wl_status_t newStatus = WiFi.begin(BENCH_SSID, BENCH_PASS);
  const uint32_t newAttemptAtMs = millis();

  portENTER_CRITICAL(&benchStateMux);
  benchState.wifiRetryCount++;
  benchState.wifiBeginAttemptCount++;
  benchState.lastWiFiBeginStatus = newStatus;
  benchState.lastWiFiBeginAtMs = newAttemptAtMs;
  const unsigned int retryCount = benchState.wifiRetryCount;
  portEXIT_CRITICAL(&benchStateMux);

  Serial.printf(
    "[BENCH] RETRY %u WiFi.begin immediateStatus=%d; this is not eventual association proof.\n",
    retryCount, static_cast<int>(newStatus)
  );
}

// ============================================================================
// Hosted Transport-Failure Recovery Ladder
//
// ESP-Hosted's SDIO driver posts ESP_HOSTED_EVENT_TRANSPORT_FAILURE
// unconditionally when MAX_SDIO_WRITE_RETRY writes fail
// (managed_components/espressif__esp_hosted/host/drivers/transport/sdio/
// sdio_drv.c:748-780), then restarts the host under
// #if H_TRANSPORT_RESTART_ON_FAILURE. This env's custom_sdkconfig
// (platformio.ini, [env:firebeetle2_hosted_bench]) leaves that symbol
// undefined, so the event fires and the host survives - but nothing
// reconnects on its own (bench session 2026-08-29, "Confirmed on hardware").
// This is that missing subscriber.
//
// The transport half mirrors the vendor's own reference sequence
// (examples/host_hosted_events/main/main.c): tear the transport down and
// bring it back up. hostedInitWiFi() -> hostedInit() calls esp_hosted_init()
// then esp_hosted_connect_to_slave(), and with
// CONFIG_ESP_HOSTED_SLAVE_RESET_ON_EVERY_HOST_BOOTUP=y (this env's config)
// that path always resets the slave over GPIO54 first
// (sdio_drv.c ensure_slave_bus_ready(), "Always reset slave on host boot
// up") before re-running card init - so a plain deinit+init cycle already
// performs the "reset slave over GPIO54" step; nothing here writes GPIO54
// directly, avoiding any conflict with /api/c6/reset's own raw GPIO use.
//
// The WiFi half is NOT just another WiFi.begin() call. The C6 physically
// rebooted, so its WiFi driver was never (re)started this session - but
// Arduino's own driver-started bookkeeping (WiFiGeneric.cpp
// espWiFiStart()/_esp_wifi_started, STA.cpp ESP_NETIF_STARTED_BIT) is
// stale-true from before the failure (read on disk: neither flag is ever
// cleared by hostedDeinitWiFi()/hostedInitWiFi(), only by the WiFi.mode(
// WIFI_MODE_NULL) teardown path, which itself talks to the dead transport
// and fails during the outage). A bare WiFi.begin() short-circuits on that
// stale state and never calls esp_wifi_start() again, so esp_wifi_connect()
// would be sent to a driver that was never started on the freshly-rebooted
// slave. So the rejoin step below bypasses that shortcut and mirrors the
// vendor's own recovery sequence (examples/host_hosted_events/main/
// station_example.c example_wifi_init_sta()) directly: raw esp_wifi_init()
// + esp_wifi_set_mode() + esp_wifi_start(), then WiFi.STA.connect() (public,
// not gated on the stale flags) to set fresh credentials and connect.
//
// Runs on its own task, not the Arduino loop() or the esp_event default-loop
// task: hostedDeinitWiFi()/hostedInitWiFi() and the WiFi calls above can
// block for seconds (CARD_INIT_TIMEOUT_MS retries, RPC teardown/setup), and
// blocking loop() for that long would stall SSE emission during the exact
// window the SSE soak cares about most. The event handler only sets state
// and notifies; all the blocking work happens in hostedRecoveryTaskFn().
// ============================================================================

static void hostedTransportFailureHandler(void *arg, esp_event_base_t base, int32_t id, void *eventData) {
  (void)arg;
  (void)base;
  (void)id;
  (void)eventData;

  bool shouldNotify = false;
  unsigned int failureCount = 0;
  RecoveryPhase phaseNow = RecoveryPhase::IDLE;

  portENTER_CRITICAL(&recoveryMux);
  recoveryState.transportFailureEventCount++;
  recoveryState.lastFailureAtMs = millis();
  failureCount = recoveryState.transportFailureEventCount;
  if (recoveryState.phase == RecoveryPhase::IDLE) {
    // Only arm a fresh ladder from IDLE. If ARMED/ATTEMPTING, a run is
    // already in flight and this failure is folded into it. If DEGRADED,
    // the ladder is exhausted and stays terminal by design - see the banner
    // above.
    recoveryState.phase = RecoveryPhase::ARMED;
    recoveryState.attemptCount = 0;
    shouldNotify = true;
  }
  phaseNow = recoveryState.phase;
  portEXIT_CRITICAL(&recoveryMux);

  Serial.printf(
    "[BENCH] HOSTED_EVENT transport-failure #%u phase=%s%s\n",
    failureCount, recoveryPhaseName(phaseNow),
    shouldNotify ? "; recovery task notified" : "; folded into the run already in flight, or degraded and terminal"
  );

  if (shouldNotify && recoveryTaskHandle != nullptr) {
    xTaskNotifyGive(recoveryTaskHandle);
  }
}

static void hostedTransportUpHandler(void *arg, esp_event_base_t base, int32_t id, void *eventData) {
  (void)arg;
  (void)base;
  (void)id;
  (void)eventData;
  // Secondary, purely observational counter: ESP_HOSTED_EVENT_TRANSPORT_UP
  // is posted by the SDIO driver itself (esp_hosted_api.c
  // transport_active_cb()) whenever the transport reaches
  // TRANSPORT_RX_ACTIVE, independent of anything this sketch believes. It
  // corroborates the ladder's own attempt outcome without being derived from
  // WiFi.status(), which the 2026-08-29 bench run proved keeps reporting
  // CONNECTED through a dead transport.
  portENTER_CRITICAL(&recoveryMux);
  recoveryState.transportUpEventCount++;
  portEXIT_CRITICAL(&recoveryMux);
}

static void hostedRecoveryTaskFn(void *arg) {
  (void)arg;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    portENTER_CRITICAL(&recoveryMux);
    recoveryState.phase = RecoveryPhase::ATTEMPTING;
    portEXIT_CRITICAL(&recoveryMux);

    bool recovered = false;
    unsigned int attemptsThisRun = 0;

    for (unsigned int attempt = 1; attempt <= RECOVERY_MAX_ATTEMPTS && !recovered; attempt++) {
      vTaskDelay(pdMS_TO_TICKS(RECOVERY_ATTEMPT_INTERVAL_MS));

      Serial.printf("[BENCH] RECOVERY attempt %u/%u: hostedDeinitWiFi + hostedInitWiFi\n", attempt, RECOVERY_MAX_ATTEMPTS);

      const bool deinitOk = hostedDeinitWiFi();
      const bool initOk = hostedInitWiFi();
      const bool transportUp = hostedIsInitialized();
      const uint32_t outcomeAtMs = millis();
      attemptsThisRun = attempt;

      Serial.printf(
        "[BENCH] RECOVERY attempt %u/%u result: deinit=%s init=%s hostedIsInitialized=%s\n",
        attempt, RECOVERY_MAX_ATTEMPTS, deinitOk ? "ok" : "FAIL", initOk ? "ok" : "FAIL", transportUp ? "true" : "false"
      );

      portENTER_CRITICAL(&recoveryMux);
      recoveryState.attemptCount = attempt;
      recoveryState.totalAttemptCount++;
      recoveryState.hasAttempted = true;
      recoveryState.lastAttemptSucceeded = transportUp;
      recoveryState.lastAttemptAtMs = outcomeAtMs;
      portEXIT_CRITICAL(&recoveryMux);

      recovered = transportUp;
    }

    if (recovered) {
      // See the banner above for why this is not just another WiFi.begin().
      wifi_init_config_t wifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
      const esp_err_t wifiInitResult = esp_wifi_init(&wifiInitCfg);
      const esp_err_t wifiModeResult = esp_wifi_set_mode(WIFI_MODE_STA);
      const esp_err_t wifiStartResult = esp_wifi_start();
      const bool connectAccepted = WiFi.STA.connect(BENCH_SSID, BENCH_PASS);
      const wl_status_t rejoinStatus = WiFi.status();
      const uint32_t rejoinAtMs = millis();

      Serial.printf(
        "[BENCH] RECOVERY transport restored after %u attempt(s); WiFi rejoin: wifiInit=%d(%s) "
        "wifiSetMode=%d(%s) wifiStart=%d(%s) staConnect=%s immediateStatus=%d "
        "(none of this is eventual association proof).\n",
        attemptsThisRun, static_cast<int>(wifiInitResult), esp_err_to_name(wifiInitResult), static_cast<int>(wifiModeResult),
        esp_err_to_name(wifiModeResult), static_cast<int>(wifiStartResult), esp_err_to_name(wifiStartResult),
        connectAccepted ? "accepted" : "FAILED", static_cast<int>(rejoinStatus)
      );

      portENTER_CRITICAL(&benchStateMux);
      benchState.wifiBeginAttemptCount++;
      benchState.wifiRetryCount++;
      benchState.lastWiFiBeginStatus = rejoinStatus;
      benchState.lastWiFiBeginAtMs = rejoinAtMs;
      portEXIT_CRITICAL(&benchStateMux);

      portENTER_CRITICAL(&recoveryMux);
      recoveryState.recoveredCount++;
      recoveryState.lastRejoinStaConnectAccepted = connectAccepted;
      recoveryState.lastRejoinAtMs = rejoinAtMs;
      recoveryState.phase = RecoveryPhase::IDLE;
      portEXIT_CRITICAL(&recoveryMux);
    } else {
      Serial.printf(
        "[BENCH] RECOVERY ladder exhausted after %u attempts; settling in a degraded state for "
        "the rest of this boot. No further automatic recovery will be attempted.\n",
        RECOVERY_MAX_ATTEMPTS
      );

      portENTER_CRITICAL(&recoveryMux);
      recoveryState.phase = RecoveryPhase::DEGRADED;
      recoveryState.degradedAtMs = millis();
      portEXIT_CRITICAL(&recoveryMux);

      // Falls through to the top of the loop and parks on the next
      // ulTaskNotifyTake(); the event handler refuses to re-arm from
      // DEGRADED, so this task now blocks forever - the "must not retry
      // forever" bound.
    }
  }
}

// ============================================================================
// Abrupt C6 Reset State Machine
// ============================================================================

static void serviceScheduledReset() {
  const uint32_t now = benchUptimeMs();
  uint32_t requestId = 0;
  ResetPhase phase = ResetPhase::IDLE;
  uint32_t phaseDeadline = 0;

  portENTER_CRITICAL(&resetMux);
  phase = resetState.phase;
  requestId = resetState.currentRequestId;
  if (phase == ResetPhase::RESPONSE_GRACE) {
    phaseDeadline = resetState.responseSentAtMs + RESET_RESPONSE_GRACE_MS;
  } else if (phase == ResetPhase::LOW_ASSERTED) {
    phaseDeadline = resetState.lowAttemptAtMs + RESET_PULSE_MS;
  }
  portEXIT_CRITICAL(&resetMux);

  if (phase == ResetPhase::RESPONSE_GRACE && deadlineReached(now, phaseDeadline)) {
    const esp_err_t result = gpio_set_level(static_cast<gpio_num_t>(BOARD_SDIO_ESP_HOSTED_RESET), 0);
    const uint32_t attemptedAt = benchUptimeMs();

    portENTER_CRITICAL(&resetMux);
    if (resetState.phase == ResetPhase::RESPONSE_GRACE && resetState.currentRequestId == requestId) {
      resetState.executedCount++;
      resetState.lowWriteAttempted = true;
      resetState.lowAttemptAtMs = attemptedAt;
      resetState.lastLowWriteResult = result;
      // Even an API error does not prove the pad stayed high. Always make the
      // corresponding HIGH release attempt after the bounded pulse interval.
      resetState.phase = ResetPhase::LOW_ASSERTED;
    }
    portEXIT_CRITICAL(&resetMux);

    Serial.printf(
      "[BENCH] RESET request=%lu low-write result=%d (%s) at=%lums; API acceptance is not electrical proof.\n",
      static_cast<unsigned long>(requestId), static_cast<int>(result), esp_err_to_name(result), static_cast<unsigned long>(attemptedAt)
    );
    return;
  }

  if (phase == ResetPhase::LOW_ASSERTED && deadlineReached(now, phaseDeadline)) {
    const esp_err_t result = gpio_set_level(static_cast<gpio_num_t>(BOARD_SDIO_ESP_HOSTED_RESET), 1);
    const uint32_t attemptedAt = benchUptimeMs();

    portENTER_CRITICAL(&resetMux);
    if (resetState.phase == ResetPhase::LOW_ASSERTED && resetState.currentRequestId == requestId) {
      resetState.highWriteAttempted = true;
      resetState.highAttemptAtMs = attemptedAt;
      resetState.lastHighWriteResult = result;
      resetState.completedCount++;
      resetState.completedAtMs = attemptedAt;
      resetState.phase = ResetPhase::IDLE;
    }
    portEXIT_CRITICAL(&resetMux);

    Serial.printf(
      "[BENCH] RESET request=%lu high-write result=%d (%s) at=%lums; verify the pulse and C6 reboot externally.\n",
      static_cast<unsigned long>(requestId), static_cast<int>(result), esp_err_to_name(result), static_cast<unsigned long>(attemptedAt)
    );
  }
}

// ============================================================================
// SSE Event Stream
// ============================================================================

static void emitSseFrame() {
  const uint32_t now = millis();
  bool httpStarted;
  uint32_t lastSseFrameMs;

  portENTER_CRITICAL(&benchStateMux);
  httpStarted = benchState.httpStarted;
  lastSseFrameMs = benchState.lastSseFrameMs;
  portEXIT_CRITICAL(&benchStateMux);

  if (!httpStarted || now - lastSseFrameMs < BENCH_SSE_CADENCE_MS) {
    return;
  }

  char frame[64];
  uint32_t frameCount;
  portENTER_CRITICAL(&benchStateMux);
  benchState.lastSseFrameMs = now;
  frameCount = benchState.sseFrameCount;
  benchState.sseFrameCount++;
  portEXIT_CRITICAL(&benchStateMux);

  snprintf(frame, sizeof(frame), "%lu", static_cast<unsigned long>(frameCount));
  events.send(frame);
}

static void handleSseOpen(PsychicEventSourceClient *client) {
  (void)client;
  portENTER_CRITICAL(&benchStateMux);
  benchState.sseClientCount++;
  const unsigned int sseClientCount = benchState.sseClientCount;
  portEXIT_CRITICAL(&benchStateMux);
  Serial.printf("[BENCH] SSE client connected. Total: %u\n", sseClientCount);
}

static void handleSseClose(PsychicEventSourceClient *client) {
  (void)client;
  portENTER_CRITICAL(&benchStateMux);
  if (benchState.sseClientCount > 0) {
    benchState.sseClientCount--;
  }
  const unsigned int sseClientCount = benchState.sseClientCount;
  portEXIT_CRITICAL(&benchStateMux);
  Serial.printf("[BENCH] SSE client disconnected. Total: %u\n", sseClientCount);
}

// ============================================================================
// HTTP Endpoints
// ============================================================================

static esp_err_t handleStatus(PsychicRequest *request, PsychicResponse *response) {
  (void)request;
  JsonDocument doc;

  // Canonical runtime identity, same contract as the production firmware's
  // /api/status. PA_FIRMWARE_VERSION is injected by tools/extract_version.py,
  // which this env inherits even though build_src_filter excludes src/. The
  // ESP-IDF app descriptor carries a different, framework-generated string and
  // is not this project's version contract.
  doc["firmwareVersion"] = PA_FIRMWARE_VERSION;
  doc["bootCount"] = benchBootCount;
  doc["resetReason"] = static_cast<int>(esp_reset_reason());
  doc["uptimeMs"] = benchUptimeMs();

  // Take a consistent snapshot of benchState under the critical section to avoid torn reads.
  decltype(benchState) benchSnapshot;
  portENTER_CRITICAL(&benchStateMux);
  benchSnapshot = benchState;
  portEXIT_CRITICAL(&benchStateMux);

  doc["linkFaultCount"] = benchSnapshot.linkFaultCount;
  doc["wifiConnected"] = benchSnapshot.wifiConnected;
  doc["wifiStatus"] = static_cast<int>(WiFi.status());
  doc["wifiRSSI"] = benchSnapshot.wifiConnected ? WiFi.RSSI() : -999;
  doc["firstWiFiBeginStatus"] = static_cast<int>(benchSnapshot.firstWiFiBeginStatus);
  doc["firstHostedInitialized"] = benchSnapshot.firstHostedInitialized;
  doc["firstHostedWiFiActiveFlag"] = benchSnapshot.firstHostedWiFiActiveFlag;
  doc["firstAttemptAtMs"] = benchSnapshot.firstAttemptAtMs;
  doc["wifiBeginAttemptCount"] = benchSnapshot.wifiBeginAttemptCount;
  doc["wifiRetryCount"] = benchSnapshot.wifiRetryCount;
  doc["lastWiFiBeginStatus"] = static_cast<int>(benchSnapshot.lastWiFiBeginStatus);

  doc["httpStartAttempted"] = benchSnapshot.httpStartAttempted;
  doc["httpStartResult"] = static_cast<int>(benchSnapshot.httpStartResult);
  doc["httpStarted"] = benchSnapshot.httpStarted;

  doc["freeHeapBytes"] = ESP.getFreeHeap();
  doc["largestFree8bitBlock"] = static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  doc["sseFramesSent"] = benchSnapshot.sseFrameCount;
  doc["sseClientsConnected"] = benchSnapshot.sseClientCount;

  uint32_t hostMajor = 0;
  uint32_t hostMinor = 0;
  uint32_t hostPatch = 0;
  uint32_t slaveMajor = 0;
  uint32_t slaveMinor = 0;
  uint32_t slavePatch = 0;
  hostedGetHostVersion(&hostMajor, &hostMinor, &hostPatch);
  hostedGetSlaveVersion(&slaveMajor, &slaveMinor, &slavePatch);

  doc["hostedIsInitialized"] = hostedIsInitialized();
  doc["hostedIsWiFiActive"] = hostedIsWiFiActive();
  doc["hostedIsWiFiActiveMeaning"] = "requested/usage flag; not initialization or association proof";
  char hostVersionStr[32];
  char slaveVersionStr[32];
  snprintf(hostVersionStr, sizeof(hostVersionStr), "%lu.%lu.%lu", hostMajor, hostMinor, hostPatch);
  snprintf(slaveVersionStr, sizeof(slaveVersionStr), "%lu.%lu.%lu", slaveMajor, slaveMinor, slavePatch);
  doc["hostedHostVersion"] = hostVersionStr;
  doc["hostedSlaveVersion"] = slaveVersionStr;
  doc["hostedSlaveTargetName"] = hostedGetSlaveTargetName();

  decltype(resetState) resetSnapshot;
  portENTER_CRITICAL(&resetMux);
  resetSnapshot = resetState;
  portEXIT_CRITICAL(&resetMux);

  doc["resetState"] = resetPhaseName(resetSnapshot.phase);
  doc["resetPending"] = resetSnapshot.phase != ResetPhase::IDLE;
  doc["resetRequestId"] = resetSnapshot.currentRequestId;
  doc["resetScheduledCount"] = resetSnapshot.scheduledCount;
  doc["resetAcceptedCount"] = resetSnapshot.acceptedCount;
  doc["resetRejectedCount"] = resetSnapshot.rejectedCount;
  doc["resetResponseSendFailureCount"] = resetSnapshot.responseSendFailureCount;
  doc["resetResponseSendResult"] = static_cast<int>(resetSnapshot.lastResponseSendResult);
  doc["resetExecutedCount"] = resetSnapshot.executedCount;
  doc["resetCompletedCount"] = resetSnapshot.completedCount;
  doc["resetScheduledAtMs"] = resetSnapshot.scheduledAtMs;
  doc["resetResponseSentAtMs"] = resetSnapshot.responseSentAtMs;
  doc["resetLowWriteAttempted"] = resetSnapshot.lowWriteAttempted;
  doc["resetLowAttemptAtMs"] = resetSnapshot.lowAttemptAtMs;
  doc["resetLowWriteResult"] = static_cast<int>(resetSnapshot.lastLowWriteResult);
  doc["resetHighWriteAttempted"] = resetSnapshot.highWriteAttempted;
  doc["resetHighAttemptAtMs"] = resetSnapshot.highAttemptAtMs;
  doc["resetHighWriteResult"] = static_cast<int>(resetSnapshot.lastHighWriteResult);
  doc["resetCompletedAtMs"] = resetSnapshot.completedAtMs;
  doc["resetResponseGraceMs"] = RESET_RESPONSE_GRACE_MS;
  doc["resetPulseMs"] = RESET_PULSE_MS;
  doc["resetEvidenceBoundary"] = "GPIO API results require external logic capture plus C6 UART reboot proof";

  // Hosted transport-failure recovery ladder (#184). Every field here comes
  // from the Hosted event stream or this ladder's own attempt bookkeeping,
  // never from WiFi.status() - see the recovery ladder section for why.
  decltype(recoveryState) recoverySnapshot;
  portENTER_CRITICAL(&recoveryMux);
  recoverySnapshot = recoveryState;
  portEXIT_CRITICAL(&recoveryMux);

  doc["hostedTransportFailureCount"] = recoverySnapshot.transportFailureEventCount;
  doc["hostedTransportUpEventCount"] = recoverySnapshot.transportUpEventCount;
  doc["recoveryLadderState"] = recoveryPhaseName(recoverySnapshot.phase);
  doc["recoveryAttemptCount"] = recoverySnapshot.attemptCount;
  doc["recoveryTotalAttemptCount"] = recoverySnapshot.totalAttemptCount;
  doc["recoveryMaxAttempts"] = RECOVERY_MAX_ATTEMPTS;
  doc["recoveryAttemptIntervalMs"] = RECOVERY_ATTEMPT_INTERVAL_MS;
  doc["recoveryRecoveredCount"] = recoverySnapshot.recoveredCount;
  doc["recoveryHasAttempted"] = recoverySnapshot.hasAttempted;
  doc["recoveryLastAttemptSucceeded"] = recoverySnapshot.lastAttemptSucceeded;
  doc["recoveryLastRejoinStaConnectAccepted"] = recoverySnapshot.lastRejoinStaConnectAccepted;
  doc["recoveryLastFailureAtMs"] = recoverySnapshot.lastFailureAtMs;
  doc["recoveryLastAttemptAtMs"] = recoverySnapshot.lastAttemptAtMs;
  doc["recoveryLastRejoinAtMs"] = recoverySnapshot.lastRejoinAtMs;
  doc["recoveryDegradedAtMs"] = recoverySnapshot.degradedAtMs;

  doc["chipModel"] = ESP.getChipModel();
  doc["chipRevision"] = ESP.getChipRevision();

  String body;
  serializeJson(doc, body);
  return response->send(200, "application/json", body.c_str());
}

static esp_err_t sendResetRejection(PsychicResponse *response, int status, const char *reason) {
  JsonDocument doc;
  doc["resetScheduled"] = false;
  doc["reason"] = reason;
  String body;
  serializeJson(doc, body);
  return response->send(status, "application/json", body.c_str());
}

static esp_err_t handleC6Reset(PsychicRequest *request, PsychicResponse *response) {
  (void)request;
  const bool hostedInitialized = hostedIsInitialized();
  const uint32_t requestedAt = benchUptimeMs();
  uint32_t requestId = 0;
  const char *rejectionReason = nullptr;
  int rejectionStatus = 503;

  portENTER_CRITICAL(&resetMux);
  if (!hostedInitialized) {
    resetState.rejectedCount++;
    rejectionReason = "ESP-Hosted is not initialized";
  } else if (resetState.phase != ResetPhase::IDLE) {
    resetState.rejectedCount++;
    rejectionReason = "another reset is pending";
    rejectionStatus = 409;
  } else {
    requestId = resetState.nextRequestId++;
    resetState.currentRequestId = requestId;
    resetState.phase = ResetPhase::RESERVED;
    resetState.scheduledCount++;
    resetState.scheduledAtMs = requestedAt;
    resetState.responseSentAtMs = 0;
    resetState.lowAttemptAtMs = 0;
    resetState.highAttemptAtMs = 0;
    resetState.completedAtMs = 0;
    resetState.lowWriteAttempted = false;
    resetState.highWriteAttempted = false;
    resetState.lastResponseSendResult = ESP_OK;
    resetState.lastLowWriteResult = ESP_OK;
    resetState.lastHighWriteResult = ESP_OK;
  }
  portEXIT_CRITICAL(&resetMux);

  if (rejectionReason != nullptr) {
    Serial.printf("[BENCH] RESET rejected: %s.\n", rejectionReason);
    return sendResetRejection(response, rejectionStatus, rejectionReason);
  }

  JsonDocument doc;
  doc["requestId"] = requestId;
  doc["resetScheduled"] = true;
  doc["responseGraceMs"] = RESET_RESPONSE_GRACE_MS;
  String body;
  serializeJson(doc, body);

  // httpd_resp_send() completes the response send call before this returns. The
  // additional grace gives the client time to receive it; only synchronized
  // client/logic/UART capture can prove the response preceded the physical edge.
  const esp_err_t sendResult = response->send(202, "application/json", body.c_str());
  const uint32_t responseSentAt = benchUptimeMs();

  portENTER_CRITICAL(&resetMux);
  if (resetState.phase == ResetPhase::RESERVED && resetState.currentRequestId == requestId) {
    resetState.lastResponseSendResult = sendResult;
    if (sendResult == ESP_OK) {
      resetState.acceptedCount++;
      resetState.responseSentAtMs = responseSentAt;
      resetState.phase = ResetPhase::RESPONSE_GRACE;
    } else {
      resetState.responseSendFailureCount++;
      resetState.completedAtMs = responseSentAt;
      resetState.phase = ResetPhase::IDLE;
    }
  }
  portEXIT_CRITICAL(&resetMux);

  Serial.printf(
    "[BENCH] RESET request=%lu response-send result=%d (%s) at=%lums; %s.\n",
    static_cast<unsigned long>(requestId), static_cast<int>(sendResult), esp_err_to_name(sendResult), static_cast<unsigned long>(responseSentAt),
    sendResult == ESP_OK ? "grace interval started" : "reset canceled"
  );
  return sendResult;
}

static esp_err_t handleHealth(PsychicRequest *request, PsychicResponse *response) {
  (void)request;
  return response->send(200, "text/plain", "OK");
}

// ============================================================================
// Setup and Initialization
// ============================================================================

static void registerHttpEndpoints() {
  http.on("/api/health", HTTP_GET,
    [](PsychicRequest *req, PsychicResponse *resp) -> esp_err_t {
      return handleHealth(req, resp);
    });
  http.on("/api/status", HTTP_GET,
    [](PsychicRequest *req, PsychicResponse *resp) -> esp_err_t {
      return handleStatus(req, resp);
    });
  http.on("/api/c6/reset", HTTP_POST,
    [](PsychicRequest *req, PsychicResponse *resp) -> esp_err_t {
      return handleC6Reset(req, resp);
    });

  events.onOpen(handleSseOpen);
  events.onClose(handleSseClose);
  http.on("/api/events", &events);
}

// Subscribes to the Hosted transport-failure recovery ladder (#184). Must run
// before WiFi.begin(): registering first guarantees the handler cannot miss
// an early event, and esp_event_loop_create_default() here is defensive/
// idempotent - WiFi.begin() would create the default loop anyway (Arduino's
// own NetworkEvents::initNetworkEvents() does exactly this, tolerating
// ESP_ERR_INVALID_STATE the same way).
static void registerHostedTransportRecovery() {
  const esp_err_t loopResult = esp_event_loop_create_default();
  if (loopResult != ESP_OK && loopResult != ESP_ERR_INVALID_STATE) {
    Serial.printf(
      "[BENCH] esp_event_loop_create_default failed: %d (%s)\n", static_cast<int>(loopResult), esp_err_to_name(loopResult)
    );
  }

  static esp_event_handler_instance_t transportFailureInstance;
  static esp_event_handler_instance_t transportUpInstance;

  esp_err_t err = esp_event_handler_instance_register(
    ESP_HOSTED_EVENT, ESP_HOSTED_EVENT_TRANSPORT_FAILURE, &hostedTransportFailureHandler, nullptr, &transportFailureInstance
  );
  if (err != ESP_OK) {
    Serial.printf(
      "[BENCH] Failed to register ESP_HOSTED_EVENT_TRANSPORT_FAILURE handler: %d (%s)\n", static_cast<int>(err), esp_err_to_name(err)
    );
  }

  err = esp_event_handler_instance_register(ESP_HOSTED_EVENT, ESP_HOSTED_EVENT_TRANSPORT_UP, &hostedTransportUpHandler, nullptr, &transportUpInstance);
  if (err != ESP_OK) {
    Serial.printf("[BENCH] Failed to register ESP_HOSTED_EVENT_TRANSPORT_UP handler: %d (%s)\n", static_cast<int>(err), esp_err_to_name(err));
  }

  const BaseType_t taskResult = xTaskCreatePinnedToCore(hostedRecoveryTaskFn, "HostedRecovery", 4096, nullptr, 2, &recoveryTaskHandle, 0);
  if (taskResult != pdPASS) {
    Serial.println("[BENCH] Failed to create HostedRecovery task; transport-failure events will not be handled.");
    recoveryTaskHandle = nullptr;
  }
}

static void updateHeartbeatLed() {
#if defined(LED_BUILTIN)
  static uint32_t lastToggleAtMs = 0;
  static bool lit = false;
  const uint32_t now = millis();
  if (now - lastToggleAtMs < 500) {
    return;
  }
  lastToggleAtMs = now;
  lit = !lit;
  digitalWrite(LED_BUILTIN, lit ? HIGH : LOW);
#endif
}

__attribute__((weak))
void setup() {
  benchBootCount++;
  // benchState.bootTimeMs is the origin for benchUptimeMs(). Set it immediately,
  // before any time-sensitive operations (like Hosted init and WiFi.begin()).
  // No lock needed: written once in setup() before any other task exists.
  benchState.bootTimeMs = millis();

  Serial.begin(115200);

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
    digitalWrite(LED_BUILTIN, LOW);
    delay(200);
  }
#else
  Serial.println("[BENCH] LED_BUILTIN is unavailable in this build pass; heartbeat disabled.");
#endif

  Serial.println("\n[BENCH] protoArtoo P4 ESP-Hosted bench initialized.");
  // Printed before anything else identifying: the pre-flight identity check for
  // an acceptance run reads this line, not the ESP-IDF app descriptor.
  Serial.printf("[BENCH] Firmware: %s\n", PA_FIRMWARE_VERSION);
  Serial.printf("[BENCH] Chip: %s\n", ESP.getChipModel());
  Serial.printf("[BENCH] Revision: %u\n", ESP.getChipRevision());
  Serial.printf("[BENCH] Boot count: %u\n", benchBootCount);
  Serial.printf("[BENCH] Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  Serial.printf("[BENCH] Free heap: %lu bytes\n", static_cast<unsigned long>(ESP.getFreeHeap()));

  Serial.println("[BENCH] Registering HTTP endpoints; server start remains deferred until WL_CONNECTED.");
  registerHttpEndpoints();

  Serial.println("[BENCH] Registering ESP_HOSTED_EVENT transport-failure recovery ladder.");
  registerHostedTransportRecovery();

  Serial.printf("[BENCH] WiFi.begin(\"%s\", <pass>)...\n", BENCH_SSID);
  const wl_status_t firstWiFiStatus = WiFi.begin(BENCH_SSID, BENCH_PASS);
  const uint32_t firstAttemptAt = benchUptimeMs();
  const bool firstHostedInitialized = hostedIsInitialized();
  const bool firstHostedWiFiActive = hostedIsWiFiActive();

  portENTER_CRITICAL(&benchStateMux);
  benchState.firstWiFiBeginStatus = firstWiFiStatus;
  benchState.firstAttemptAtMs = firstAttemptAt;
  benchState.firstHostedInitialized = firstHostedInitialized;
  benchState.firstHostedWiFiActiveFlag = firstHostedWiFiActive;
  benchState.wifiBeginAttemptCount = 1;
  benchState.lastWiFiBeginStatus = firstWiFiStatus;
  benchState.lastWiFiBeginAtMs = millis();
  portEXIT_CRITICAL(&benchStateMux);

  Serial.printf(
    "[BENCH] FIRST_ATTEMPT at=%lums WiFi.begin immediateStatus=%d hostedInitialized=%s "
    "hostedWiFiActiveRequestedFlag=%s; immediate status and requested/usage flag are not eventual association proof.\n",
    static_cast<unsigned long>(firstAttemptAt), static_cast<int>(firstWiFiStatus),
    firstHostedInitialized ? "true" : "false", firstHostedWiFiActive ? "true" : "false"
  );
  Serial.println("[BENCH] Initial failure will remain quiescent; retries are enabled only after one successful connection.");
}

__attribute__((weak))
void loop() {
  updateHeartbeatLed();
  serviceScheduledReset();
  superviseLink();
  emitSseFrame();

  static uint32_t lastStatusLog = 0;
  const uint32_t now = millis();
  if (now - lastStatusLog >= 30000) {
    // Take a consistent snapshot of benchState/recoveryState to avoid torn reads in the log.
    decltype(benchState) benchSnapshot;
    portENTER_CRITICAL(&benchStateMux);
    benchSnapshot = benchState;
    portEXIT_CRITICAL(&benchStateMux);

    decltype(recoveryState) recoverySnapshot;
    portENTER_CRITICAL(&recoveryMux);
    recoverySnapshot = recoveryState;
    portEXIT_CRITICAL(&recoveryMux);

    Serial.printf(
      "[BENCH] STATUS uptime=%lus boot=%u wifi=%s everConnected=%s faults=%u retries=%u "
      "httpAttempted=%s httpStarted=%s sseClients=%u sseFrames=%lu freeHeap=%lu bytes "
      "recovery=%s transportFailures=%u attempts=%u recovered=%u\n",
      static_cast<unsigned long>(benchUptimeMs() / 1000), benchBootCount, benchSnapshot.wifiConnected ? "CONNECTED" : "DISCONNECTED",
      benchSnapshot.hasConnectedOnce ? "true" : "false", benchSnapshot.linkFaultCount, benchSnapshot.wifiRetryCount,
      benchSnapshot.httpStartAttempted ? "true" : "false", benchSnapshot.httpStarted ? "true" : "false", benchSnapshot.sseClientCount,
      static_cast<unsigned long>(benchSnapshot.sseFrameCount), static_cast<unsigned long>(ESP.getFreeHeap()),
      recoveryPhaseName(recoverySnapshot.phase), recoverySnapshot.transportFailureEventCount, recoverySnapshot.totalAttemptCount,
      recoverySnapshot.recoveredCount
    );
    lastStatusLog = now;
  }

  delay(1);  // Yield without blocking the reset timing state machine.
}

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
 * WiFi credentials NEVER committed (public repo). Build with explicit creds:
 *   PLATFORMIO_BUILD_FLAGS='-DBENCH_SSID=\"ssid\" -DBENCH_PASS=\"pass\"' \
 *     make build BUILD_ENV=firebeetle2_hosted_bench
 *
 * Uses weak symbols to coexist with .dummy/sketch.cpp.o in custom_sdkconfig pass.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PsychicHttp.h>
#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <driver/gpio.h>

#include "esp32-hal-hosted.h"

// Provisioned WiFi credentials - overridden by build flags, not committed
#ifndef BENCH_SSID
#define BENCH_SSID "protoArtoo-bench"
#endif
#ifndef BENCH_PASS
#define BENCH_PASS "protoArtoo-bench"
#endif

// Timing constants in milliseconds.
static constexpr uint32_t BENCH_SSE_CADENCE_MS = 1000;
static constexpr uint32_t WIFI_CHECK_INTERVAL_MS = 5000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
static constexpr uint32_t RESET_RESPONSE_GRACE_MS = 1000;
static constexpr uint32_t RESET_PULSE_MS = 100;

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

static void superviseLink() {
  const uint32_t now = millis();
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
      Serial.println("[BENCH] WiFi reached WL_CONNECTED for the first time.");
    }
    startHttpIfReady();
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
  Serial.printf("[BENCH] Chip: %s\n", ESP.getChipModel());
  Serial.printf("[BENCH] Revision: %u\n", ESP.getChipRevision());
  Serial.printf("[BENCH] Boot count: %u\n", benchBootCount);
  Serial.printf("[BENCH] Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  Serial.printf("[BENCH] Free heap: %lu bytes\n", static_cast<unsigned long>(ESP.getFreeHeap()));

  Serial.println("[BENCH] Registering HTTP endpoints; server start remains deferred until WL_CONNECTED.");
  registerHttpEndpoints();

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
    // Take a consistent snapshot of benchState to avoid torn reads in the log.
    decltype(benchState) benchSnapshot;
    portENTER_CRITICAL(&benchStateMux);
    benchSnapshot = benchState;
    portEXIT_CRITICAL(&benchStateMux);

    Serial.printf(
      "[BENCH] STATUS uptime=%lus boot=%u wifi=%s everConnected=%s faults=%u retries=%u "
      "httpAttempted=%s httpStarted=%s sseClients=%u sseFrames=%lu freeHeap=%lu bytes\n",
      static_cast<unsigned long>(benchUptimeMs() / 1000), benchBootCount, benchSnapshot.wifiConnected ? "CONNECTED" : "DISCONNECTED",
      benchSnapshot.hasConnectedOnce ? "true" : "false", benchSnapshot.linkFaultCount, benchSnapshot.wifiRetryCount,
      benchSnapshot.httpStartAttempted ? "true" : "false", benchSnapshot.httpStarted ? "true" : "false", benchSnapshot.sseClientCount,
      static_cast<unsigned long>(benchSnapshot.sseFrameCount), static_cast<unsigned long>(ESP.getFreeHeap())
    );
    lastStatusLog = now;
  }

  delay(1);  // Yield without blocking the reset timing state machine.
}

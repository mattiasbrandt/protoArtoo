/**
 * P4 ESP-Hosted WiFi reliability bench sketch — #184
 * Lives in bringup/ (fenced outside src/) as a throwaway test image.
 *
 * Tests SDIO WiFi transport stability using PsychicHttp server.
 * Provides endpoints for:
 *  - /api/health: Simple liveness check
 *  - /api/status: Link and health metrics with full transport visibility
 *  - /api/c6/reset: Trigger C6 co-processor reset
 *  - /api/events: SSE stream of monotonic counter at fixed cadence
 *
 * Critical constraints (from prepared research #184):
 *  - WiFi credential persistence is DISABLED under ESP-Hosted
 *    Must call WiFi.begin(ssid, pass) explicitly, no NVS fallback
 *  - No liveness API for SDIO link, supervise it ourselves via transport
 *  - Never WiFi.scan() unguarded (EHM-257 NULL memcpy crash)
 *  - Avoid BLE entirely (EHM-238 P4+C6 coexistence broken)
 *
 * WiFi credentials NEVER committed (public repo). Build with explicit creds:
 *   make build BUILD_ENV=protoArtoo_p4_hosted_bench \
 *     PLATFORMIO_BUILD_FLAGS="-DBENCH_SSID=ssid -DBENCH_PASS=pass"
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

// ESP-Hosted transport visibility (Arduino core esp32-hal-hosted.h)
extern "C" {
  bool hostedIsInitialized();
  bool hostedIsWiFiActive();
  void hostedGetHostVersion(uint32_t *major, uint32_t *minor, uint32_t *patch);
  void hostedGetSlaveVersion(uint32_t *major, uint32_t *minor, uint32_t *patch);
  const char *hostedGetSlaveTargetName();
}

// GPIO 3 is LED_BUILTIN on FireBeetle 2 ESP32-P4 (DFR1172)
#define BENCH_LED 3

// C6 co-processor reset line (GPIO 54, from spec sheet)
#define C6_RESET_GPIO 54

// Provisioned WiFi credentials - overridden by build flags, not committed
#ifndef BENCH_SSID
#define BENCH_SSID "protoArtoo-bench"
#endif
#ifndef BENCH_PASS
#define BENCH_PASS "protoArtoo-bench"
#endif

// SSE stream cadence (milliseconds between monotonic counter frames)
#define BENCH_SSE_CADENCE_MS 1000

// Global state for link supervision and metrics.
// benchBootCount is RTC_DATA_ATTR so it survives a CPU reset (not power cycle).
RTC_DATA_ATTR static unsigned int benchBootCount = 0;

static struct {
  unsigned long bootTimeMs = 0;
  unsigned int linkFaultCount = 0;
  unsigned long lastWiFiCheckMs = 0;
  unsigned long wifiConnectAttemptMs = 0;
  bool wifiConnected = false;
  uint32_t sseFrameCount = 0;
  unsigned int sseClientCount = 0;
  unsigned long lastSseFrameMs = 0;
} benchState;

// HTTP server instance
PsychicHttpServer http;

// SSE event source
PsychicEventSource events;

// ============================================================================
// Link Supervision
// ============================================================================

void superviseLink() {
  unsigned long now = millis();

  if (now - benchState.lastWiFiCheckMs < 5000) {
    return;
  }
  benchState.lastWiFiCheckMs = now;

  bool wasConnected = benchState.wifiConnected;
  bool isConnected = (WiFi.status() == WL_CONNECTED);
  benchState.wifiConnected = isConnected;

  if (wasConnected && !isConnected) {
    benchState.linkFaultCount++;
    Serial.print("[BENCH] WiFi link fault detected. Total faults: ");
    Serial.println(benchState.linkFaultCount);
  }

  if (!isConnected) {
    if (now - benchState.wifiConnectAttemptMs >= 10000) {
      Serial.println("[BENCH] WiFi not connected, calling begin() with explicit creds...");
      WiFi.begin(BENCH_SSID, BENCH_PASS);
      benchState.wifiConnectAttemptMs = now;
    }
  }
}

// ============================================================================
// SSE Event Stream
// ============================================================================

void emitSseFrame() {
  unsigned long now = millis();

  if (now - benchState.lastSseFrameMs < BENCH_SSE_CADENCE_MS) {
    return;
  }
  benchState.lastSseFrameMs = now;

  char frame[64];
  snprintf(frame, sizeof(frame), "data: %lu\n", benchState.sseFrameCount);
  benchState.sseFrameCount++;

  events.send(frame);
}

void handleSseOpen(PsychicEventSourceClient* client) {
  benchState.sseClientCount++;
  Serial.print("[BENCH] SSE client connected. Total: ");
  Serial.println(benchState.sseClientCount);
}

void handleSseClose(PsychicEventSourceClient* client) {
  if (benchState.sseClientCount > 0) {
    benchState.sseClientCount--;
  }
  Serial.print("[BENCH] SSE client disconnected. Total: ");
  Serial.println(benchState.sseClientCount);
}

// ============================================================================
// HTTP Endpoints
// ============================================================================

static esp_err_t handleStatus(PsychicRequest *request, PsychicResponse *response) {
  JsonDocument doc;

  // Boot and reset info
  doc["bootCount"] = benchBootCount;
  doc["resetReason"] = (int)esp_reset_reason();
  doc["uptimeMs"] = millis() - benchState.bootTimeMs;

  // Link supervision
  doc["linkFaultCount"] = benchState.linkFaultCount;
  doc["wifiConnected"] = benchState.wifiConnected;
  doc["wifiStatus"] = (int)WiFi.status();
  doc["wifiRSSI"] = benchState.wifiConnected ? WiFi.RSSI() : -999;

  // Heap metrics
  doc["freeHeapBytes"] = ESP.getFreeHeap();
  doc["largestFree8bitBlock"] = (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

  // SSE metrics
  doc["sseFramesSent"] = benchState.sseFrameCount;
  doc["sseClientsConnected"] = benchState.sseClientCount;

  // ESP-Hosted transport visibility
  uint32_t hostMajor = 0, hostMinor = 0, hostPatch = 0;
  uint32_t slaveMajor = 0, slaveMinor = 0, slavePatch = 0;
  hostedGetHostVersion(&hostMajor, &hostMinor, &hostPatch);
  hostedGetSlaveVersion(&slaveMajor, &slaveMinor, &slavePatch);

  doc["hostedIsInitialized"] = hostedIsInitialized();
  doc["hostedIsWiFiActive"] = hostedIsWiFiActive();
  char hostVersionStr[32], slaveVersionStr[32];
  snprintf(hostVersionStr, sizeof(hostVersionStr), "%lu.%lu.%lu", hostMajor, hostMinor, hostPatch);
  snprintf(slaveVersionStr, sizeof(slaveVersionStr), "%lu.%lu.%lu", slaveMajor, slaveMinor, slavePatch);
  doc["hostedHostVersion"] = hostVersionStr;
  doc["hostedSlaveVersion"] = slaveVersionStr;
  doc["hostedSlaveTargetName"] = hostedGetSlaveTargetName();

  // Chip info
  doc["chipModel"] = ESP.getChipModel();
  doc["chipRevision"] = ESP.getChipRevision();

  String body;
  serializeJson(doc, body);

  return response->send(200, "application/json", body.c_str());
}

static esp_err_t handleC6Reset(PsychicRequest *request, PsychicResponse *response) {
  Serial.println("[BENCH] C6 reset requested via HTTP endpoint.");

  digitalWrite(C6_RESET_GPIO, LOW);
  delay(100);
  digitalWrite(C6_RESET_GPIO, HIGH);

  JsonDocument doc;
  doc["resetTriggeredAtMs"] = millis() - benchState.bootTimeMs;
  doc["linkFaultCountAfterReset"] = benchState.linkFaultCount;

  String body;
  serializeJson(doc, body);

  return response->send(200, "application/json", body.c_str());
}

static esp_err_t handleHealth(PsychicRequest *request, PsychicResponse *response) {
  return response->send(200, "text/plain", "OK");
}

// ============================================================================
// Setup and Initialization
// ============================================================================

__attribute__((weak))
void setup() {
  benchBootCount++;
  benchState.bootTimeMs = millis();

  pinMode(BENCH_LED, OUTPUT);
  pinMode(C6_RESET_GPIO, OUTPUT);
  digitalWrite(C6_RESET_GPIO, HIGH);

  Serial.begin(115200);

  for (int i = 0; i < 3; i++) {
    digitalWrite(BENCH_LED, HIGH);
    delay(200);
    digitalWrite(BENCH_LED, LOW);
    delay(200);
  }

  Serial.println("\n[BENCH] protoArtoo P4 ESP-Hosted bench initialized.");
  Serial.print("[BENCH] Chip: ");
  Serial.println(ESP.getChipModel());
  Serial.print("[BENCH] Revision: ");
  Serial.println(ESP.getChipRevision());
  Serial.print("[BENCH] Boot count: ");
  Serial.println(benchBootCount);
  Serial.print("[BENCH] Reset reason: ");
  Serial.println((int)esp_reset_reason());
  Serial.print("[BENCH] Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  Serial.print("[BENCH] WiFi.begin(\"");
  Serial.print(BENCH_SSID);
  Serial.println("\", <pass>)...");
  WiFi.begin(BENCH_SSID, BENCH_PASS);
  benchState.wifiConnectAttemptMs = millis();

  Serial.println("[BENCH] Registering HTTP endpoints...");
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

  // Register SSE event source with callbacks
  events.onOpen(handleSseOpen);
  events.onClose(handleSseClose);
  http.on("/api/events", &events);

  if (http.begin()) {
    Serial.println("[BENCH] HTTP server started on port 80.");
  } else {
    Serial.println("[BENCH] ERROR: HTTP server failed to start!");
  }

  Serial.println("[BENCH] Setup complete. Entering loop...");
}

__attribute__((weak))
void loop() {
  digitalWrite(BENCH_LED, HIGH);
  delay(200);
  digitalWrite(BENCH_LED, LOW);

  superviseLink();
  emitSseFrame();

  delay(100);

  static unsigned long lastStatusLog = 0;
  unsigned long now = millis();
  if (now - lastStatusLog >= 30000) {
    Serial.print("[BENCH] Uptime: ");
    Serial.print((now - benchState.bootTimeMs) / 1000);
    Serial.print("s, Boot#: ");
    Serial.print(benchBootCount);
    Serial.print(", WiFi: ");
    Serial.print(benchState.wifiConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.print(", Link faults: ");
    Serial.print(benchState.linkFaultCount);
    Serial.print(", SSE clients: ");
    Serial.print(benchState.sseClientCount);
    Serial.print(", SSE frames: ");
    Serial.print(benchState.sseFrameCount);
    Serial.print(", Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    lastStatusLog = now;
  }
}

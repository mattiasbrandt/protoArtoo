/**
 * P4 ESP-Hosted WiFi reliability bench sketch — #184
 * Lives in bringup/ (fenced outside src/) as a throwaway test image.
 *
 * Tests SDIO WiFi transport stability using PsychicHttp server.
 * Provides endpoints for:
 *  - /api/status: Link and health metrics (boot counter, link faults)
 *  - /api/c6/reset: Trigger C6 co-processor reset
 *
 * Critical constraints (from prepared research #184):
 *  - WiFi credential persistence is DISABLED under ESP-Hosted
 *    → Must call WiFi.begin(ssid, pass) explicitly, no NVS fallback
 *  - No liveness API for SDIO link → supervise it ourselves
 *  - Never WiFi.scan() unguarded (EHM-257 NULL memcpy crash)
 *  - Avoid BLE entirely (EHM-238 P4+C6 coexistence broken)
 *
 * Uses weak symbols (like p4_bringup.cpp) to coexist with .dummy/sketch.cpp.o
 * in the custom_sdkconfig library pass.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PsychicHttp.h>

// GPIO 3 is LED_BUILTIN on FireBeetle 2 ESP32-P4 (DFR1172)
#define BENCH_LED 3

// C6 co-processor reset line (GPIO 54, from spec sheet)
#define C6_RESET_GPIO 54

// Provisioned WiFi credentials — set environment-specific defaults.
// BENCH_SSID and BENCH_PASS can be overridden via build flags.
#ifndef BENCH_SSID
#define BENCH_SSID "protoArtoo-bench"
#endif
#ifndef BENCH_PASS
#define BENCH_PASS "protoArtoo-bench"
#endif

// Global state for link supervision and metrics
static struct {
  unsigned long bootTimeMs = 0;
  unsigned int bootCount = 0;
  unsigned int linkFaultCount = 0;
  unsigned long lastWiFiCheckMs = 0;
  unsigned long wifiConnectAttemptMs = 0;
  bool wifiConnected = false;
} benchState;

// HTTP server instance
PsychicHttpServer http;

// ============================================================================
// Link Supervision
// ============================================================================

/**
 * Check WiFi link status and supervise SDIO transport.
 * Called periodically from loop() to detect link faults.
 */
void superviseLink() {
  unsigned long now = millis();

  // Check every 5 seconds if we're connected; supervise transport health.
  if (now - benchState.lastWiFiCheckMs < 5000) {
    return;
  }
  benchState.lastWiFiCheckMs = now;

  bool wasConnected = benchState.wifiConnected;
  bool isConnected = (WiFi.status() == WL_CONNECTED);
  benchState.wifiConnected = isConnected;

  // Detect link fault: transition from connected to not connected.
  if (wasConnected && !isConnected) {
    benchState.linkFaultCount++;
    Serial.print("[BENCH] WiFi link fault detected. Total faults: ");
    Serial.println(benchState.linkFaultCount);
  }

  // If not connected, initiate reconnection with explicit credentials.
  // This is required under ESP-Hosted: WiFi state is not persisted in NVS.
  if (!isConnected) {
    // Only attempt reconnect every 10 seconds to avoid log spam.
    if (now - benchState.wifiConnectAttemptMs >= 10000) {
      Serial.println("[BENCH] WiFi not connected, calling begin() with explicit creds...");
      // Must provide SSID and pass explicitly — NVS persistence is disabled.
      WiFi.begin(BENCH_SSID, BENCH_PASS);
      benchState.wifiConnectAttemptMs = now;
    }
  }
}

// ============================================================================
// HTTP Endpoints
// ============================================================================

/**
 * GET /api/status
 * Return JSON with bench metrics: boot count, uptime, link faults, WiFi state.
 */
void handleStatus(PsychicRequest *request) {
  DynamicJsonDocument doc(256);

  doc["bootCount"] = benchState.bootCount;
  doc["uptimeMs"] = millis() - benchState.bootTimeMs;
  doc["linkFaultCount"] = benchState.linkFaultCount;
  doc["wifiConnected"] = benchState.wifiConnected;
  doc["wifiStatus"] = (int)WiFi.status();
  doc["freeHeapBytes"] = ESP.getFreeHeap();
  doc["chipModel"] = ESP.getChipModel();
  doc["chipRevision"] = ESP.getChipRevision();

  String response;
  serializeJson(doc, response);

  request->send(200, "application/json", response);
}

/**
 * POST /api/c6/reset
 * Trigger a C6 co-processor reset by toggling GPIO54.
 * Response includes the reset timestamp and new link fault count.
 */
void handleC6Reset(PsychicRequest *request) {
  Serial.println("[BENCH] C6 reset requested via HTTP endpoint.");

  // Drive reset line low for 100ms, then release.
  digitalWrite(C6_RESET_GPIO, LOW);
  delay(100);
  digitalWrite(C6_RESET_GPIO, HIGH);

  // Return JSON with reset result.
  DynamicJsonDocument doc(128);
  doc["resetTriggeredAtMs"] = millis() - benchState.bootTimeMs;
  doc["linkFaultCountAfterReset"] = benchState.linkFaultCount;

  String response;
  serializeJson(doc, response);

  request->send(200, "application/json", response);
}

/**
 * GET /api/health
 * Simple liveness check for the bench firmware.
 */
void handleHealth(PsychicRequest *request) {
  request->send(200, "text/plain", "OK");
}

// ============================================================================
// Setup and Initialization
// ============================================================================

// Weak symbols to coexist with .dummy/sketch.cpp.o in the library pass
__attribute__((weak))
void setup() {
  // Initialize boot state
  benchState.bootTimeMs = millis();
  benchState.bootCount++;

  // GPIO setup
  pinMode(BENCH_LED, OUTPUT);
  pinMode(C6_RESET_GPIO, OUTPUT);
  digitalWrite(C6_RESET_GPIO, HIGH);  // Release reset line (active low)

  // Serial console via USB CDC
  Serial.begin(115200);

  // Blink pattern: 3 blinks to signal boot
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
  Serial.print("[BENCH] Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  // Initialize WiFi with explicit SSID and password.
  // ESP-Hosted does NOT persist WiFi state in NVS; every boot must call begin() explicitly.
  Serial.print("[BENCH] WiFi.begin(\"");
  Serial.print(BENCH_SSID);
  Serial.println("\", <pass>)...");
  WiFi.begin(BENCH_SSID, BENCH_PASS);
  benchState.wifiConnectAttemptMs = millis();

  // Register HTTP endpoints
  Serial.println("[BENCH] Registering HTTP endpoints...");
  http.on("/api/health", HTTP_GET, handleHealth);
  http.on("/api/status", HTTP_GET, handleStatus);
  http.on("/api/c6/reset", HTTP_POST, handleC6Reset);

  // Start the HTTP server on port 80
  if (http.begin()) {
    Serial.println("[BENCH] HTTP server started on port 80.");
  } else {
    Serial.println("[BENCH] ERROR: HTTP server failed to start!");
  }

  Serial.println("[BENCH] Setup complete. Entering loop...");
}

__attribute__((weak))
void loop() {
  // Blink heartbeat: 200ms on, 4800ms off (5s cycle)
  digitalWrite(BENCH_LED, HIGH);
  delay(200);
  digitalWrite(BENCH_LED, LOW);

  // Supervise the WiFi link and handle reconnection.
  superviseLink();

  // Small delay to avoid busy-spinning.
  delay(100);

  // Every 30 seconds, log a status line.
  static unsigned long lastStatusLog = 0;
  unsigned long now = millis();
  if (now - lastStatusLog >= 30000) {
    Serial.print("[BENCH] Uptime: ");
    Serial.print((now - benchState.bootTimeMs) / 1000);
    Serial.print("s, WiFi: ");
    Serial.print(benchState.wifiConnected ? "CONNECTED" : "DISCONNECTED");
    Serial.print(", Link faults: ");
    Serial.print(benchState.linkFaultCount);
    Serial.print(", Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    lastStatusLog = now;
  }
}

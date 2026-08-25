// =============================================================================
// src/web/web_network_bootstrap.cpp
//
// WiFi boot posture decision and application for protoArtoo.
// Handles boot posture decisions and WiFi configuration application,
// network recovery gesture evaluation, and OTA registration. WiFi event
// handling and registration are delegated to the network manager seam
// (web_network_manager.h), which ensures the HTTP server is started from
// the WiFi event callback path via the backend implementation.
// =============================================================================

#include "../../include/web_network_bootstrap.h"

#include <Arduino.h>
#include <stddef.h>

#include "../../include/config.h"
#include "../../include/config_cache.h"
#include "../../include/config_store.h"
#include "../../include/logging.h"
#include "../../include/web_server.h"
#include "../../include/web_network_manager.h"
#include "../../include/wifi_boot_decision.h"
#include "../../include/wifi_recovery_gesture.h"

// PA_HAS_SECRETS_HEADER and PA_ENABLE_STA_WIFI are needed by buildDeveloperShortcut()
// even in native test builds, so they are defined at the top level, not inside #ifdef ARDUINO.
// src/secrets.h is the Developer WiFi Shortcut (ADR 0015): local/self-build-only
// compile-time WiFi defaults. It is never required to compile or boot - public
// release binaries (protoArtoo_chirp, protoArtoo_mp3trigger) ship without it and
// boot into WiFi Provisioning via wifiDecideBootPosture() instead.
#ifdef ARDUINO
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>

#if __has_include("secrets.h")
#include "secrets.h"
#define PA_HAS_SECRETS_HEADER 1
#else
#define PA_HAS_SECRETS_HEADER 0
#endif
#else
// Native test build: no secrets.h available
#define PA_HAS_SECRETS_HEADER 0

// ESP-IDF reset reason enum for native test builds — evaluateNetworkRecoveryGesture()
// needs esp_reset_reason(). Device builds include this from esp_system.h via Arduino.h.
enum esp_reset_reason_t {
    ESP_RST_UNKNOWN   = 0,
    ESP_RST_POWERON   = 1,
    ESP_RST_EXT       = 2,
    ESP_RST_SW        = 3,
    ESP_RST_PANIC     = 4,
    ESP_RST_INT_WDT   = 5,
    ESP_RST_TASK_WDT  = 6,
    ESP_RST_WDT       = 7,
    ESP_RST_DEEPSLEEP = 8,
    ESP_RST_BROWNOUT  = 9,
    ESP_RST_SDIO      = 10,
};

esp_reset_reason_t esp_reset_reason();
#endif  // ARDUINO

// PA_ENABLE_STA_WIFI selects which posture the Developer WiFi Shortcut resolves to
// when secrets.h is present: 1 (default) = WiFi Client Mode, 0 = Standalone AP Mode.
// It has no effect once Device WiFi Settings are provisioned (runtime settings win).
#ifndef PA_ENABLE_STA_WIFI
#define PA_ENABLE_STA_WIFI 1
#endif

static const char* TAG = "WebServer";

// buildDeveloperShortcut: the Developer WiFi Shortcut (ADR 0015) resolved from
// src/secrets.h, source-build-only. Never populated in public release binaries -
// `available` stays false whenever secrets.h is absent, leaves its expected
// macros undefined, or defines only the blank placeholder values.
static WifiDeveloperShortcut buildDeveloperShortcut() {
    WifiDeveloperShortcut shortcut;
#if PA_HAS_SECRETS_HEADER
#if PA_ENABLE_STA_WIFI
#if defined(PA_STA_SSID) && defined(PA_STA_PASSWORD)
    // A blank PA_STA_SSID (the secrets.h.example placeholder) is not a usable
    // shortcut: honoring it would boot an unprovisioned controller into
    // WiFi.begin("", ...) instead of WiFi Provisioning (ADR 0015), with no
    // AP and no web server to recover through.
    if (PA_STA_SSID[0] != '\0') {
        shortcut.available = true;
        shortcut.mode = WifiMode::CLIENT;
    }
#endif
#else
#if defined(PA_AP_PASSWORD)
    shortcut.available = true;
    shortcut.mode = WifiMode::STANDALONE_AP;
#endif
#endif  // PA_ENABLE_STA_WIFI
#endif  // PA_HAS_SECRETS_HEADER
    return shortcut;
}

// evaluateNetworkRecoveryGesture: reads the persisted power-cycle count,
// runs it through the pure gesture rule (wifi_recovery_gesture.h), and
// persists the updated count. Only a true power-on reset advances the
// gesture; watchdog/panic/brownout/software resets fall through to
// wifiEvaluateRecoveryGesture()'s "reset the count" branch. Returns true
// only on the boot that latches Network Recovery Mode.
static bool evaluateNetworkRecoveryGesture() {
    Preferences recoveryPrefs;
    if (!recoveryPrefs.begin(NVS_NAMESPACE, false)) {
        PA_LOG_WARN(TAG, "recovery gesture NVS open failed; gesture unavailable this boot");
        return false;
    }

    WifiRecoveryGestureInput gestureInput;
    gestureInput.wasPowerOnReset = (esp_reset_reason() == ESP_RST_POWERON);
    gestureInput.priorCycleCount = recoveryPrefs.getUChar(kWifiRecoveryCycleKey, 0);

    WifiRecoveryGestureResult gestureResult = wifiEvaluateRecoveryGesture(gestureInput);
    recoveryPrefs.putUChar(kWifiRecoveryCycleKey, gestureResult.nextCycleCount);
    recoveryPrefs.end();

    if (gestureResult.recoveryRequested) {
        PA_LOG_WARN(TAG,
                    "Network Recovery Mode gesture detected (%u power cycles) - "
                    "starting WiFi Provisioning",
                    (unsigned)WIFI_RECOVERY_GESTURE_THRESHOLD);
    } else if (gestureInput.wasPowerOnReset && gestureResult.nextCycleCount > 0) {
        PA_LOG_DEBUG(TAG, "power-on reset cycle count = %u/%u",
                     (unsigned)gestureResult.nextCycleCount,
                     (unsigned)WIFI_RECOVERY_GESTURE_THRESHOLD);
    }
    return gestureResult.recoveryRequested;
}

// Network bootstrap logic: decide and apply WiFi boot posture.
// Called from webServerInit() in web_server.cpp after LittleFS and
// event stream setup. Evaluates the network recovery gesture, builds the
// developer WiFi shortcut, decides the boot posture, and applies it.
void webNetworkBootstrap() {
    // ADR 0015: boot posture is decided once from Device WiFi Settings plus the
    // Developer WiFi Shortcut, through the same pure decision layer the native
    // tests exercise (test_wifi_boot_decision). networkRecoveryRequested comes
    // from the explicit local power-cycle gesture (wifi_recovery_gesture.h)
    // - this shell classifies the reset reason and
    // owns the persisted counter, but never infers recovery from ordinary STA
    // connection trouble.
    WifiConfig wifiSettings = {};
    configCacheReadWifi(&wifiSettings);

    WifiBootDecisionInput decisionInput;
    decisionInput.settings = wifiSettings;
    decisionInput.networkRecoveryRequested = evaluateNetworkRecoveryGesture();
    decisionInput.developerShortcut = buildDeveloperShortcut();

    WifiBootPosture posture = wifiDecideBootPosture(decisionInput);
    networkManagerApplyBootPosture(posture, wifiSettings);

    // Record what was actually applied so the read surface can distinguish
    // active settings from any pending (saved-but-not-yet-applied) settings
    // for a Staged Network Switch (ADR 0015).
    configCacheSetActiveWifi(wifiSettings);
    configCacheSetActiveWifiRecovery(posture == WifiBootPosture::NETWORK_RECOVERY);
}

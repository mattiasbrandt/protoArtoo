// =============================================================================
// src/web/web_network_bootstrap.cpp
//
// WiFi, mDNS, OTA, and network recovery bootstrap for protoArtoo.
// Handles WiFi event dispatch, boot posture decisions, network recovery
// gesture evaluation, and OTA registration. The HTTP server is started
// from the WiFi event callback path (handleWiFiEvent -> startHttpServerOnce).
// =============================================================================

#include "../../include/web_network_bootstrap.h"

#include <Arduino.h>
#include <stddef.h>

#include "../../include/config.h"
#include "../../include/config_store.h"
#include "../../include/logging.h"
#include "../../include/web_server.h"
#include "../../include/wifi_boot_decision.h"
#include "../../include/wifi_recovery_gesture.h"

#ifdef ARDUINO
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>

// src/secrets.h is the Developer WiFi Shortcut (ADR 0015): local/self-build-only
// compile-time WiFi defaults. It is never required to compile or boot — public
// release binaries (protoArtoo_chirp, protoArtoo_mp3trigger) ship without it and
// boot into WiFi Provisioning via wifiDecideBootPosture() instead.
#if __has_include("secrets.h")
#include "secrets.h"
#define PA_HAS_SECRETS_HEADER 1
#else
#define PA_HAS_SECRETS_HEADER 0
#endif

// PA_ENABLE_STA_WIFI selects which posture the Developer WiFi Shortcut resolves to
// when secrets.h is present: 1 (default) = WiFi Client Mode, 0 = Standalone AP Mode.
// It has no effect once Device WiFi Settings are provisioned (runtime settings win).
#ifndef PA_ENABLE_STA_WIFI
#define PA_ENABLE_STA_WIFI 1
#endif
#endif  // ARDUINO

static const char* TAG = "WebServer";

#ifdef ARDUINO

void handleWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START:
            PA_LOG_INFO(TAG, "Hotspot started - SSID: %s  IP: %s", WiFi.softAPSSID().c_str(),
                        WiFi.softAPIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            PA_LOG_INFO(TAG, "Connecting to WiFi network...");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            PA_LOG_INFO(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            // Ordinary WiFi Client Mode connection trouble stays visible as a
            // client-mode problem (ADR 0015). It must never trigger automatic
            // AP fallback here — wifiDecideBootPosture() has no connectivity
            // input, so there is nothing to re-decide on disconnect.
            PA_LOG_INFO(TAG, "WiFi connection lost");
            break;
        default:
            break;
    }
}

// buildDeveloperShortcut: the Developer WiFi Shortcut (ADR 0015) resolved from
// src/secrets.h, source-build-only. Never populated in public release binaries —
// `available` stays false whenever secrets.h is absent or leaves its expected
// macros undefined, which is always true for protoArtoo_chirp/protoArtoo_mp3trigger.
static WifiDeveloperShortcut buildDeveloperShortcut() {
    WifiDeveloperShortcut shortcut;
#if PA_HAS_SECRETS_HEADER
#if PA_ENABLE_STA_WIFI
#if defined(PA_STA_SSID) && defined(PA_STA_PASSWORD)
    shortcut.available = true;
    shortcut.mode = WifiMode::CLIENT;
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

// executeWifiBootPosture: enters the posture wifiDecideBootPosture() returned.
// This function decides HOW to enter a posture; it never re-derives WHICH
// posture to enter (that decision already happened, and stays pure/testable).
static void executeWifiBootPosture(WifiBootPosture posture, const WifiConfig& settings) {
    switch (posture) {
        case WifiBootPosture::PROVISIONING:
        case WifiBootPosture::NETWORK_RECOVERY:
            // Both postures expose WiFi Provisioning with the documented Default AP
            // Credential — recovery must stay reachable even if the operator no
            // longer remembers a custom Standalone AP Mode password.
            WiFi.mode(WIFI_AP);
            WiFi.softAP(WIFI_AP_SSID, WIFI_DEFAULT_AP_PASSWORD);
            PA_LOG_INFO(TAG, "WiFi bootstrap: %s (AP %s)",
                        posture == WifiBootPosture::NETWORK_RECOVERY ? "network recovery"
                                                                      : "provisioning",
                        WIFI_AP_SSID);
            break;
        case WifiBootPosture::CLIENT_MODE: {
            const char* ssid = settings.sta_ssid;
            const char* password = settings.sta_password;
#if PA_HAS_SECRETS_HEADER && defined(PA_STA_SSID) && defined(PA_STA_PASSWORD)
            // Developer WiFi Shortcut: an unprovisioned controller has no saved
            // STA credentials, so a self-build falls back to secrets.h defaults.
            if (ssid[0] == '\0') {
                ssid = PA_STA_SSID;
                password = PA_STA_PASSWORD;
            }
#endif
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid, password);
            PA_LOG_INFO(TAG, "WiFi bootstrap: client mode (SSID %s)", ssid);
            break;
        }
        case WifiBootPosture::STANDALONE_AP_MODE:
            WiFi.mode(WIFI_AP);
            WiFi.softAP(settings.ap_ssid, settings.ap_password);
            PA_LOG_INFO(TAG, "WiFi bootstrap: standalone AP mode (SSID %s)", settings.ap_ssid);
            break;
    }
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
    // — this shell classifies the reset reason and
    // owns the persisted counter, but never infers recovery from ordinary STA
    // connection trouble.
    WifiConfig wifiSettings = {};
    configCacheReadWifi(&wifiSettings);

    WifiBootDecisionInput decisionInput;
    decisionInput.settings = wifiSettings;
    decisionInput.networkRecoveryRequested = evaluateNetworkRecoveryGesture();
    decisionInput.developerShortcut = buildDeveloperShortcut();

    WifiBootPosture posture = wifiDecideBootPosture(decisionInput);
    executeWifiBootPosture(posture, wifiSettings);

    // Record what was actually applied so the read surface can distinguish
    // active settings from any pending (saved-but-not-yet-applied) settings
    // for a Staged Network Switch (ADR 0015).
    configCacheSetActiveWifi(wifiSettings);
    configCacheSetActiveWifiRecovery(posture == WifiBootPosture::NETWORK_RECOVERY);
}

#endif  // ARDUINO

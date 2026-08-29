// =============================================================================
// src/web/web_network_manager_hosted.cpp
//
// ESP-Hosted (ESP32-P4 + ESP32-C6 over SDIO) backend for the network manager
// seam (#188/#189). Implements WiFi event handling, registration, and boot
// posture application on top of the same Arduino WiFi API the native backend
// uses.
//
// Boot posture only (#189 slice 1): WiFi.mode()/WiFi.begin()/WiFi.softAP()
// transparently bring the SDIO transport up via hostedInitWiFi() inside the
// Arduino core's wifiLowLevelInit() (WiFiGeneric.cpp, gated on
// CONFIG_ESP_HOSTED_ENABLED) and post the same ARDUINO_EVENT_WIFI_* events as
// the native radio, so the boot-time posture code below is identical to the
// native backend's. The rejoin-after-co-processor-reboot path is different
// (#189 slice 2, not yet implemented): Arduino's own WiFi.begin() cannot
// restart a freshly-reset C6 because its _esp_wifi_started/connected()
// bookkeeping is stale-true after the reboot and is not cleared by
// hostedDeinitWiFi() (device-proven, #184). That path bypasses WiFi.begin()
// entirely and goes through raw esp_wifi_* calls instead; it does not touch
// this file's boot posture code.
// =============================================================================

#include "../../include/web_network_manager.h"

#include <Arduino.h>
#include "../../include/config.h"

// Hosted radio backend. Whole-file guard, no #else: when this board's backend
// is not ESP-Hosted, this translation unit contributes nothing, and the
// composition that does apply lives in its own file (ADR 0021 shape). Keeping
// a "not selected" definition here is what let a stale signature survive as a
// silent overload and break the firebeetle2 link once already (see
// web_network_manager_native.cpp).
#if PA_CAP_HOSTED_WIFI

#include <WiFi.h>

#include "../../include/api_status.h"
#include "../../include/logging.h"
#include "../../include/web_server.h"
#include "../../include/wifi_boot_decision.h"

// src/secrets.h is the Developer WiFi Shortcut (ADR 0015): local/self-build-only
// compile-time WiFi defaults. It is never required to compile or boot - public
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

static const char* TAG = "WebServer";

// Event-cached STA connection status for networkManagerStationConnected().
// Updated by handleWiFiEventBackend(); read by Core 1 dome-link loop.
// Using volatile bool avoids heap allocation and vendor calls on Core 1.
static volatile bool g_staConnected = false;

// Event handler for the Arduino WiFi driver. Translates Arduino WiFiEvent_t
// to application behavior: logging and HTTP server startup. Fires identically
// under ESP-Hosted: the Arduino core registers its WIFI_EVENT/IP_EVENT
// handlers unconditionally of CONFIG_ESP_HOSTED_ENABLED (WiFiGeneric.cpp
// initWiFiEvents()), so STA_GOT_IP/STA_DISCONNECTED/AP_START are the same
// events whether the radio is native or relayed over SDIO to the C6.
static void handleWiFiEventBackend(WiFiEvent_t event) {
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
            g_staConnected = true;
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            // Ordinary WiFi Client Mode connection trouble stays visible as a
            // client-mode problem (ADR 0015). It must never trigger automatic
            // AP fallback here - wifiDecideBootPosture() has no connectivity
            // input, so there is nothing to re-decide on disconnect. A dead
            // SDIO transport is a distinct condition handled by the #189
            // slice 2 supervisor (ESP_HOSTED_EVENT_TRANSPORT_FAILURE), not by
            // this Arduino-level disconnect event.
            PA_LOG_INFO(TAG, "WiFi connection lost");
            g_staConnected = false;
            break;
        default:
            break;
    }
}

// Initialize network manager: register WiFi event handler with the Arduino driver.
void networkManagerInitialize() {
    WiFi.onEvent(handleWiFiEventBackend);
}

// Apply WiFi boot posture. Called from webNetworkBootstrap().
void networkManagerApplyBootPosture(WifiBootPosture posture, const WifiConfig& settings) {

    // Same posture code as the native backend (see the file header comment):
    // WiFi.mode()/WiFi.begin()/WiFi.softAP() bring the Hosted transport up
    // implicitly on first use, and this is the boot path, not the
    // co-processor-recovery rejoin path #189 slice 2 must handle differently.
    switch (posture) {
        case WifiBootPosture::PROVISIONING:
        case WifiBootPosture::NETWORK_RECOVERY:
            // Both postures expose WiFi Provisioning with the documented Default AP
            // Credential - recovery must stay reachable even if the operator no
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

// Query WiFi connectivity status. Reads hardware state and derives connectivity fields.
//
// Vendor-boundary note: WiFi.status() itself is safe to read here - it is
// only unsafe as the sole liveness signal for the SDIO transport (#184's
// hardware finding: a dead transport still reads WL_CONNECTED forever). This
// function answers "what does the radio believe", the same question the
// native backend answers; #189 slice 2 adds the transport-truth signal
// (ESP_HOSTED_EVENT_TRANSPORT_FAILURE/_UP) as a separate, additional status
// surface rather than folding it into this query.
WifiConnectivityStatus networkManagerQueryConnectivity() {
    int wifiMode = WiFi.getMode();
    bool apEnabled = wifiMode == WIFI_AP || wifiMode == WIFI_AP_STA;
    bool staConnected = WiFi.status() == WL_CONNECTED;
    bool staEnabled = wifiMode == WIFI_STA || wifiMode == WIFI_AP_STA;
    unsigned int apStationCount = apEnabled ? (unsigned int)WiFi.softAPgetStationNum() : 0U;
    WiFiConnectivityFields wifi =
        deriveWiFiConnectivityFields(apEnabled, staConnected, apStationCount, WiFi.RSSI());

    // Copy-out strings to caller-supplied buffers (ADR 0021 pattern)
    WifiConnectivityStatus result = {};  // Default-initialize all fields
    result.wifiConnected = wifi.wifiConnected;
    result.wifiClientConnected = wifi.wifiClientConnected;
    result.wifiRssi = wifi.wifiRssi;
    result.staConnected = staConnected;
    result.staEnabled = staEnabled;

    // AP IP (always read, returns "0.0.0.0" if AP not active - matches original behavior)
    snprintf(result.apIp, sizeof(result.apIp), "%s", WiFi.softAPIP().toString().c_str());

    // STA IP and SSID (empty if not connected)
    if (staConnected) {
        snprintf(result.staIp, sizeof(result.staIp), "%s", WiFi.localIP().toString().c_str());
        snprintf(result.staSsid, sizeof(result.staSsid), "%s", WiFi.SSID().c_str());
    }

    return result;
}

// Query STA connection status via event cache (Core 1 safe).
bool networkManagerStationConnected() {
    return g_staConnected;
}

#endif  // PA_CAP_HOSTED_WIFI

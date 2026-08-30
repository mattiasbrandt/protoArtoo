// =============================================================================
// src/web/web_network_manager_common.cpp
//
// Shared implementation for the network manager seam's Arduino-WiFi-API
// radio backends. See include/web_network_manager_common.h for why this
// file exists (#189's de-duplication obligation).
// =============================================================================

#include "../../include/web_network_manager_common.h"

#if PA_CAP_NATIVE_WIFI || PA_CAP_HOSTED_WIFI

#include "../../include/api_status.h"
#include "../../include/logging.h"
#include "../../include/web_server.h"

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

void wifiNetworkManagerResolveStaCredentialsCommon(const WifiConfig& settings,
                                                     const char** ssidOut,
                                                     const char** passwordOut) {
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
    *ssidOut = ssid;
    *passwordOut = password;
}

void wifiNetworkManagerApplyBootPostureCommon(WifiBootPosture posture,
                                                const WifiConfig& settings,
                                                const char* logTag) {
    switch (posture) {
        case WifiBootPosture::PROVISIONING:
        case WifiBootPosture::NETWORK_RECOVERY:
            // Both postures expose WiFi Provisioning with the documented Default AP
            // Credential - recovery must stay reachable even if the operator no
            // longer remembers a custom Standalone AP Mode password.
            WiFi.mode(WIFI_AP);
            WiFi.softAP(WIFI_AP_SSID, WIFI_DEFAULT_AP_PASSWORD);
            PA_LOG_INFO(logTag, "WiFi bootstrap: %s (AP %s)",
                        posture == WifiBootPosture::NETWORK_RECOVERY ? "network recovery"
                                                                      : "provisioning",
                        WIFI_AP_SSID);
            break;
        case WifiBootPosture::CLIENT_MODE: {
            const char* ssid;
            const char* password;
            wifiNetworkManagerResolveStaCredentialsCommon(settings, &ssid, &password);
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid, password);
            PA_LOG_INFO(logTag, "WiFi bootstrap: client mode (SSID %s)", ssid);
            break;
        }
        case WifiBootPosture::STANDALONE_AP_MODE:
            WiFi.mode(WIFI_AP);
            WiFi.softAP(settings.ap_ssid, settings.ap_password);
            PA_LOG_INFO(logTag, "WiFi bootstrap: standalone AP mode (SSID %s)", settings.ap_ssid);
            break;
    }
}

WifiConnectivityStatus wifiNetworkManagerQueryConnectivityCommon() {
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

void wifiNetworkManagerHandleEventCommon(WiFiEvent_t event, const char* logTag,
                                           volatile bool* staConnected) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_START:
            PA_LOG_INFO(logTag, "Hotspot started - SSID: %s  IP: %s", WiFi.softAPSSID().c_str(),
                        WiFi.softAPIP().toString().c_str());
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_START:
            PA_LOG_INFO(logTag, "Connecting to WiFi network...");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            PA_LOG_INFO(logTag, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
            *staConnected = true;
            startHttpServerOnce();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            // Ordinary WiFi Client Mode connection trouble stays visible as a
            // client-mode problem (ADR 0015). It must never trigger automatic
            // AP fallback here - wifiDecideBootPosture() has no connectivity
            // input, so there is nothing to re-decide on disconnect. A dead
            // SDIO transport under the hosted backend is a distinct condition
            // handled by the #189 supervisor
            // (ESP_HOSTED_EVENT_TRANSPORT_FAILURE), not by this Arduino-level
            // disconnect event.
            PA_LOG_INFO(logTag, "WiFi connection lost");
            *staConnected = false;
            break;
        default:
            break;
    }
}

#endif  // PA_CAP_NATIVE_WIFI || PA_CAP_HOSTED_WIFI

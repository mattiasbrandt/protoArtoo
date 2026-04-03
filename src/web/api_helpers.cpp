// =============================================================================
// src/web/api_helpers.cpp
//
// Pure parsing helpers for web API parameter validation.
// No Arduino, no FreeRTOS, no hardware dependencies.
// =============================================================================

#include "api_helpers.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool parseDriveValue(const char* raw, int16_t* out) {
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    long value = strtol(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0') {
        return false;
    }

    if (value < INT16_MIN || value > INT16_MAX) {
        return false;
    }

    *out = (int16_t)value;
    return true;
}

bool parseUint32Value(const char* raw, uint32_t* out) {
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }

    // Reject leading minus sign — strtoul accepts it (wraps around)
    if (raw[0] == '-') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    unsigned long value = strtoul(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0') {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

bool parseBoolValue(const char* raw, bool* out) {
    if (raw == nullptr) {
        return false;
    }

    if (strcmp(raw, "true") == 0 || strcmp(raw, "1") == 0) {
        *out = true;
        return true;
    }

    if (strcmp(raw, "false") == 0 || strcmp(raw, "0") == 0) {
        *out = false;
        return true;
    }

    return false;
}


void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax, uint32_t webDriveTimeoutMs,
                      bool ch8ModeLock) {
    snprintf(buf, bufSize, "{\"speedLimitMax\":%d,\"webDriveTimeoutMs\":%lu,\"ch8ModeLock\":%s}",
             (int)speedLimitMax, (unsigned long)webDriveTimeoutMs, ch8ModeLock ? "true" : "false");
}

void formatWifiJson(char* buf, size_t bufSize, const char* apSsid, const char* apIp,
                    bool staEnabled, bool staConnected, const char* staIp, long wifiRssi) {
    snprintf(buf, bufSize,
             "{\"apSsid\":\"%s\",\"apIp\":\"%s\",\"staEnabled\":%s,\"staConnected\":%s,\"staIp\":"
             "\"%s\",\"wifiRssi\":%ld}",
             apSsid, apIp, staEnabled ? "true" : "false", staConnected ? "true" : "false", staIp,
             wifiRssi);
}

void formatSerialJson(char* buf, size_t bufSize, bool domeLinkActive, unsigned long domeHbRx,
                      unsigned long bodyHbTx) {
    snprintf(buf, bufSize,
             "{"
             "\"debug\":{\"label\":\"S0\",\"name\":\"ESP debug\",\"active\":true,\"note\":\"USB "
             "debug serial\"},"
             "\"hoverboard\":{\"label\":\"S1\",\"name\":\"Hoverboard\",\"active\":true,"
             "\"hardwareRequired\":true,\"note\":\"Firmware path active; full behavior needs Artoo "
             "PCB + hoverboard chain\"},"
             "\"sound\":{\"label\":\"S2\",\"name\":\"Sound\",\"active\":false,\"hardwareRequired\":"
             "true,\"note\":\"Electrically defined, phase 4 audio integration\"},"
             "\"dome\":{\"label\":\"S3\",\"name\":\"Dome "
             "Control\",\"active\":%s,\"heartbeatRx\":%lu,\"heartbeatTx\":%lu,\"hardwareRequired\":"
             "true,\"note\":\"Body-dome link reserved for later phase integration\"}"
             "}",
             domeLinkActive ? "true" : "false", domeHbRx, bodyHbTx);
}

WiFiConnectivityFields deriveWiFiConnectivityFields(bool apEnabled, bool staConnected,
                                                    unsigned int apStationCount, long staRssi) {
    WiFiConnectivityFields fields{};
    fields.wifiConnected = apEnabled || staConnected;
    fields.wifiClientConnected = apEnabled && apStationCount > 0U;
    fields.wifiRssi = staConnected ? staRssi : 0;
    return fields;
}

void formatHealthJson(char* buf, size_t bufSize, bool estop, bool sbusSignalLost,
                      bool sbusHwFailsafe, bool webControlEnabled, bool wifiConnected,
                      bool wifiClientConnected, bool fsReady, unsigned long heapFree,
                      unsigned long heapMin, unsigned long heapLargestBlock, long wifiRssi) {
    snprintf(buf, bufSize,
             "{\"estop\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,\"webControlEnabled\":%s,"
             "\"wifiConnected\":%s,\"wifiClientConnected\":%s,\"littleFsReady\":%s,"
             "\"heapFree\":%lu,\"heapMin\":%lu,\"heapLargestBlock\":%lu,\"wifiRssi\":%ld}",
             estop ? "true" : "false", sbusSignalLost ? "true" : "false",
             sbusHwFailsafe ? "true" : "false", webControlEnabled ? "true" : "false",
             wifiConnected ? "true" : "false", wifiClientConnected ? "true" : "false",
             fsReady ? "true" : "false", heapFree, heapMin, heapLargestBlock, wifiRssi);
}

void formatAudioStatusJson(char* buf, size_t bufSize, const char* driverName,
                           uint8_t capabilities, bool linkOk, bool active,
                           uint8_t playState, uint8_t device, uint16_t totalTracks,
                           uint16_t currentTrack) {
    // playState labels per datasheet: 0=stop 1=playing 2=paused 0xFF=unknown
    const char* playSt = (playState == 0x00)   ? "stop"
                         : (playState == 0x01) ? "playing"
                         : (playState == 0x02) ? "paused"
                                               : "unknown";
    // device labels: 0=USB 1=SD/TF 2=FLASH 3=Flash+SD(CHIRP) 0xFF=none/unknown
    const char* devStr = (device == 0x00)   ? "USB"
                         : (device == 0x01) ? "SD/TF"
                         : (device == 0x02) ? "FLASH"
                         : (device == 0x03) ? "Flash+SD"
                         : (device == 0xFF) ? "none"
                                            : "unknown";
    snprintf(buf, bufSize,
             "{\"driver\":\"%s\",\"capabilities\":%u,\"link_ok\":%s,\"active\":%s,"
             "\"play_state\":\"%s\",\"device\":\"%s\","
             "\"total_tracks\":%u,\"current_track\":%u}",
             driverName, (unsigned)capabilities, linkOk ? "true" : "false",
             active ? "true" : "false", playSt, devStr, (unsigned)totalTracks,
             (unsigned)currentTrack);
}


bool formatSleepControlJson(char* buf, size_t bufSize, bool sleepMode, bool changed) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }

    int written = snprintf(buf, bufSize, "{\"ok\":true,\"sleepMode\":%s,\"changed\":%s}",
                         sleepMode ? "true" : "false", changed ? "true" : "false");
    return written > 0 && (size_t)written < bufSize;
}

bool formatAuxLedStateJson(char* buf, size_t bufSize, uint8_t pin, uint8_t r, uint8_t g,
                           uint8_t b, const char* effect) {
    if (buf == nullptr || bufSize == 0 || effect == nullptr) {
        return false;
    }

    int written = snprintf(buf, bufSize,
                         "{\"ok\":true,\"auxLed\":{\"pin\":%u,\"r\":%u,\"g\":%u,\"b\":%u,\"effect\":\"%s\"}}",
                         (unsigned)pin, (unsigned)r, (unsigned)g, (unsigned)b, effect);
    return written > 0 && (size_t)written < bufSize;
}
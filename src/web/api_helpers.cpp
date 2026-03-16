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

ManualCommand resolveManualCommand(const char* command) {
    if (command == nullptr) {
        return MC_UNKNOWN;
    }

    if (strcmp(command, "estop") == 0) {
        return MC_ESTOP;
    }
    if (strcmp(command, "clear_estop") == 0) {
        return MC_CLEAR_ESTOP;
    }
    if (strcmp(command, "enable_web_control") == 0) {
        return MC_ENABLE_WEB_CONTROL;
    }
    if (strcmp(command, "disable_web_control") == 0) {
        return MC_DISABLE_WEB_CONTROL;
    }
    if (strcmp(command, "reboot") == 0) {
        return MC_REBOOT;
    }
    if (strcmp(command, "#st") == 0) {
        return MC_STATIONARY_MODE;
    }
    if (strcmp(command, "#sm") == 0) {
        return MC_DRIVING_MODE;
    }

    return MC_UNKNOWN;
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

void formatHealthJson(char* buf, size_t bufSize, bool estop, bool sbusSignalLost,
                      bool sbusHwFailsafe, bool webControlEnabled, bool wifiConnected,
                      bool wifiClientConnected, bool fsReady, unsigned long heapFree,
                      unsigned long heapMin, long wifiRssi) {
    snprintf(buf, bufSize,
             "{\"estop\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,\"webControlEnabled\":%s,"
             "\"wifiConnected\":%s,\"wifiClientConnected\":%s,\"littleFsReady\":%s,"
             "\"heapFree\":%lu,\"heapMin\":%lu,\"wifiRssi\":%ld}",
             estop ? "true" : "false", sbusSignalLost ? "true" : "false",
             sbusHwFailsafe ? "true" : "false", webControlEnabled ? "true" : "false",
             wifiConnected ? "true" : "false", wifiClientConnected ? "true" : "false",
             fsReady ? "true" : "false", heapFree, heapMin, wifiRssi);
}

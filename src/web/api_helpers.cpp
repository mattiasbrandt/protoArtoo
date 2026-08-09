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

#include "config.h"

void trimAsciiWhitespace(char* s) {
    if (s == nullptr) {
        return;
    }
    char* start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    size_t len = strlen(start);
    while (len > 0) {
        const char c = start[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        --len;
    }
    memmove(s, start, len);
    s[len] = '\0';
}

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

bool normalizeDroidName(const char* raw, char* out, size_t outSize) {
    if (raw == nullptr || out == nullptr || outSize == 0) {
        return false;
    }

    size_t len = 0;
    for (const char* p = raw; *p != '\0'; ++p) {
        const char c = *p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            return false;
        }
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!allowed) {
            return false;
        }

        if (len + 1 >= outSize || len >= DROID_NAME_MAX_LEN) {
            return false;
        }
        out[len++] = c;
    }
    out[len] = '\0';
    return len > 0;
}

void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax,
                      uint32_t webDriveTimeoutMs) {
    snprintf(buf, bufSize, "{\"speedLimitMax\":%d,\"webDriveTimeoutMs\":%lu}", (int)speedLimitMax,
             (unsigned long)webDriveTimeoutMs);
}

void formatAudioStatusJson(char* buf, size_t bufSize, const char* driverName, uint8_t capabilities,
                           bool linkOk, bool active, uint8_t playState, uint8_t device,
                           uint16_t totalTracks, uint16_t currentTrack, const char* rxStatus,
                           const char* rxDetail) {
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
             "\"total_tracks\":%u,\"current_track\":%u,"
             "\"rx_status\":\"%s\",\"rx_detail\":\"%s\"}",
             driverName, (unsigned)capabilities, linkOk ? "true" : "false",
             active ? "true" : "false", playSt, devStr, (unsigned)totalTracks,
             (unsigned)currentTrack, rxStatus, rxDetail);
}

bool formatSleepControlJson(char* buf, size_t bufSize, bool sleepMode, bool changed) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }

    int written = snprintf(buf, bufSize, "{\"ok\":true,\"sleepMode\":%s,\"changed\":%s}",
                           sleepMode ? "true" : "false", changed ? "true" : "false");
    return written > 0 && (size_t)written < bufSize;
}

bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName) {
    if (buf == nullptr || bufSize == 0 || droidName == nullptr) {
        return false;
    }

    int written = snprintf(buf, bufSize, "{\"droidName\":\"%s\",\"mdnsUseName\":%s}",
                           droidName, mdnsUseName ? "true" : "false");
    return written > 0 && (size_t)written < bufSize;
}

bool formatSpeedPresetResponseJson(char* buf, size_t bufSize, SpeedPresetId preset,
                                   int16_t speedLimitMax) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }
    if (!speedPresetIdIsValid(preset)) {
        return false;
    }
    if (speedLimitMax < 0 || speedLimitMax > SPEED_LIMIT_MAX) {
        return false;
    }

    int written = snprintf(buf, bufSize, "{\"ok\":true,\"preset\":\"%s\",\"speedLimitMax\":%d}",
                           speedPresetIdToString(preset), (int)speedLimitMax);
    return written > 0 && (size_t)written < bufSize;
}

bool formatAuxLedStateJson(char* buf, size_t bufSize, uint8_t pin, uint8_t r, uint8_t g, uint8_t b,
                           const char* effect) {
    if (buf == nullptr || bufSize == 0 || effect == nullptr) {
        return false;
    }

    int written = snprintf(
        buf, bufSize,
        "{\"ok\":true,\"auxLed\":{\"pin\":%u,\"r\":%u,\"g\":%u,\"b\":%u,\"effect\":\"%s\"}}",
        (unsigned)pin, (unsigned)r, (unsigned)g, (unsigned)b, effect);
    return written > 0 && (size_t)written < bufSize;
}

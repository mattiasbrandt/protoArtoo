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

    // Reject leading minus sign - strtoul accepts it (wraps around)
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

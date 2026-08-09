// =============================================================================
// src/web/api_identity_serializers.cpp
//
// Pure JSON serialization helper for identity endpoints.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_identity.h"

#include <cstdio>

bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName) {
    if (buf == nullptr || bufSize == 0 || droidName == nullptr) {
        return false;
    }

    int written = snprintf(buf, bufSize, "{\"droidName\":\"%s\",\"mdnsUseName\":%s}",
                           droidName, mdnsUseName ? "true" : "false");
    return written > 0 && (size_t)written < bufSize;
}

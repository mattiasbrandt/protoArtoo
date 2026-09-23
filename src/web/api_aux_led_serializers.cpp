// =============================================================================
// src/web/api_aux_led_serializers.cpp
//
// Pure JSON serialization helper for the droid's lit wires.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_aux_led.h"

#include <cstdio>

bool formatLitWiresJson(char* buf, size_t bufSize, const LitWireReading* wires, size_t count,
                        size_t* written) {
    if (buf == nullptr || bufSize == 0) {
        return false;
    }
    if (wires == nullptr && count > 0) {
        return false;
    }

    size_t used = 0;
    const int opened = snprintf(buf, bufSize, "{");
    if (opened < 0 || (size_t)opened >= bufSize) {
        return false;
    }
    used = (size_t)opened;

    for (size_t i = 0; i < count; ++i) {
        const LitWireReading& wire = wires[i];
        if (wire.id == nullptr || wire.effect == nullptr) {
            return false;
        }
        const int n = snprintf(buf + used, bufSize - used,
                               "%s\"%s\":{\"r\":%u,\"g\":%u,\"b\":%u,\"effect\":\"%s\","
                               "\"available\":%s}",
                               i == 0 ? "" : ",", wire.id, (unsigned)wire.r, (unsigned)wire.g,
                               (unsigned)wire.b, wire.effect, wire.available ? "true" : "false");
        if (n < 0 || (size_t)n >= bufSize - used) {
            return false;
        }
        used += (size_t)n;
    }

    const int closed = snprintf(buf + used, bufSize - used, "}");
    if (closed < 0 || (size_t)closed >= bufSize - used) {
        return false;
    }
    used += (size_t)closed;

    if (written != nullptr) {
        *written = used;
    }
    return true;
}

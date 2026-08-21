// =============================================================================
// src/web/api_aux_led_serializers.cpp
//
// Pure JSON serialization helper for AUX LED status.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_aux_led.h"

#include <cstdio>

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

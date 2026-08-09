// =============================================================================
// src/web/api_aux_led.cpp
//
// AUX LED REST API
//   POST /api/aux-led/color   body: {"r":0,"g":0,"b":0}
//   POST /api/aux-led/effect  body: {"effect":"solid|blink|pulse|off"}
//
// Both endpoints accept either a JSON body or ordinary form fields; a JSON body
// wins when present. Written against the project-owned WebRequest seam
// (ADR 0021) and bound by the seam route table.
// =============================================================================

#include "api_aux_led.h"

#include <ArduinoJson.h>

#include <cstdint>

#include "api_helpers.h"
#include "api_json_response.h"
#include "aux_led.h"
#include "robot_state.h"

namespace {

// Parses the request's raw (non-form) body as JSON. hasBody distinguishes "no
// JSON body, fall through to form fields" from "a JSON body that did not
// parse", which are the same false return but different outcomes.
bool parseJsonBody(WebRequest& req, JsonDocument* outDoc, bool* hasBody) {
    if (outDoc == nullptr || hasBody == nullptr) {
        return false;
    }
    *hasBody = false;

    // Borrowed, not copied: an aux-LED body is small, but sizing a buffer for
    // it here would be a second place to keep in step with the payload.
    const char* raw = req.body();
    if (raw == nullptr) {
        return true;
    }

    *hasBody = true;
    DeserializationError err = deserializeJson(*outDoc, raw);
    return !err;
}

bool parseUint8FormField(WebRequest& req, const char* key, uint8_t* out) {
    if (key == nullptr || out == nullptr) {
        return false;
    }

    // Wider than any valid 0..255 value, so an over-long input is rejected by
    // the parser rather than truncated into a valid one (web_request.h).
    char raw[16] = {};
    if (!req.param(key, raw, sizeof(raw))) {
        return false;
    }

    uint32_t parsed = 0;
    if (!parseUint32Value(raw, &parsed) || parsed > 255U) {
        return false;
    }

    *out = (uint8_t)parsed;
    return true;
}

bool parseUint8JsonField(const JsonDocument& doc, const char* key, uint8_t* out) {
    if (key == nullptr || out == nullptr) {
        return false;
    }

    if (!doc[key].is<uint32_t>()) {
        return false;
    }

    uint32_t parsed = doc[key].as<uint32_t>();
    if (parsed > 255U) {
        return false;
    }

    *out = (uint8_t)parsed;
    return true;
}

bool parseColorPayload(WebRequest& req, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (r == nullptr || g == nullptr || b == nullptr) {
        return false;
    }

    JsonDocument body;
    bool hasJson = false;
    if (!parseJsonBody(req, &body, &hasJson)) {
        return false;
    }

    if (hasJson) {
        return parseUint8JsonField(body, "r", r) && parseUint8JsonField(body, "g", g) &&
               parseUint8JsonField(body, "b", b);
    }

    return parseUint8FormField(req, "r", r) && parseUint8FormField(req, "g", g) &&
           parseUint8FormField(req, "b", b);
}

bool parseEffectPayload(WebRequest& req, AuxLedEffect* outEffect) {
    if (outEffect == nullptr) {
        return false;
    }

    JsonDocument body;
    bool hasJson = false;
    if (!parseJsonBody(req, &body, &hasJson)) {
        return false;
    }

    if (hasJson) {
        if (!body["effect"].is<const char*>()) {
            return false;
        }
        return parseAuxLedEffect(body["effect"].as<const char*>(), outEffect);
    }

    // Wider than the longest effect name, so an over-long value reaches
    // parseAuxLedEffect() as an unknown effect instead of a truncated match.
    char raw[16] = {};
    if (!req.param("effect", raw, sizeof(raw))) {
        return false;
    }

    return parseAuxLedEffect(raw, outEffect);
}

bool isAuxLedAvailable() {
    taskENTER_CRITICAL(&robotStateMux);
    const bool available = robotState.auxLed.available;
    const uint8_t pin = robotState.auxLed.pin;
    taskEXIT_CRITICAL(&robotStateMux);

    return available && pin != 0;
}

void sendAuxLedStateResponse(WebRequest& req) {
    uint8_t pin = 0;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    AuxLedEffect effect = AUX_LED_EFFECT_OFF;

    taskENTER_CRITICAL(&robotStateMux);
    pin = robotState.auxLed.pin;
    r = robotState.auxLed.r;
    g = robotState.auxLed.g;
    b = robotState.auxLed.b;
    effect = robotState.auxLed.effect;
    taskEXIT_CRITICAL(&robotStateMux);

    char body[160] = {};
    if (!formatAuxLedStateJson(body, sizeof(body), pin, r, g, b, auxLedEffectToString(effect))) {
        webSendJsonError(req, 500, "aux LED response overflow");
        return;
    }

    req.send(200, "application/json", body);
}

// Both endpoints reject a refused queue the same way, and the distinction the
// operator needs is why: an absent strip is a wiring/config answer, a full
// queue is a retry.
void sendAuxLedQueueRefusal(WebRequest& req) {
    if (!isAuxLedAvailable()) {
        webSendJsonError(req, 503, "aux LED unavailable");
        return;
    }
    webSendJsonError(req, 503, "aux LED command queue full");
}

}  // namespace

void handleAuxLedColorPost(WebRequest& req) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (!parseColorPayload(req, &r, &g, &b)) {
        webSendJsonError(req, 400, "payload must contain r,g,b integers 0..255");
        return;
    }

    if (!auxLedQueueSetColor(r, g, b, SRC_WEB_API)) {
        sendAuxLedQueueRefusal(req);
        return;
    }

    sendAuxLedStateResponse(req);
}

void handleAuxLedEffectPost(WebRequest& req) {
    AuxLedEffect effect = AUX_LED_EFFECT_OFF;
    if (!parseEffectPayload(req, &effect)) {
        webSendJsonError(req, 400, "effect must be one of off|solid|blink|pulse");
        return;
    }

    if (!auxLedQueueSetEffect(effect, SRC_WEB_API)) {
        sendAuxLedQueueRefusal(req);
        return;
    }

    sendAuxLedStateResponse(req);
}

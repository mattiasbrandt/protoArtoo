// =============================================================================
// src/web/api_aux_led.cpp
//
// The lit wires' REST API
//   POST /api/aux-led/color   body: {"r":0,"g":0,"b":0[,"output":"ledc:9"]}
//   POST /api/aux-led/effect  body: {"effect":"solid|blink|pulse|off"[,"output":"ledc:9"]}
//
// `output` names ONE wire by its Output Address, the same word GET
// /api/servo/outputs answers with. Leaving it out means every lit wire, which
// is what it has always meant and what a sequence step and an RC action send:
// they mean "the droid's body lights" (ADR 0067, #413). A surface controlling
// one Part's light sends the address of the Output that Part is on.
//
// Both endpoints accept either a JSON body or ordinary form fields; a JSON body
// wins when present. Written against the project-owned WebRequest seam
// (ADR 0021) and bound by the seam route table.
// =============================================================================

#include "api_aux_led.h"

#include <ArduinoJson.h>

#include <cstdint>
#include <cstdio>

#include "api_helpers.h"
#include "api_json_response.h"
#include "aux_led.h"
#include "board_outputs.h"
#include "robot_state.h"
#include "servo_output_row.h"

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

// Which wire a request is about. An absent `output` is every lit wire; an
// `output` naming an Output Address this board does not have is a 400 rather
// than a silent broadcast, because a surface sending an address it did not read
// must not quietly command the whole droid.
//
// *outTarget is only written on success.
bool parseTargetPayload(WebRequest& req, uint8_t* outTarget) {
    if (outTarget == nullptr) {
        return false;
    }

    // Wider than the longest Output Address, so an over-long value fails to
    // parse as an address rather than being truncated into a valid one.
    char raw[SERVO_OUTPUT_ADDRESS_STR_MAX + 8] = {};
    bool named = false;

    JsonDocument body;
    bool hasJson = false;
    if (!parseJsonBody(req, &body, &hasJson)) {
        return false;
    }
    if (hasJson) {
        if (body["output"].is<const char*>()) {
            snprintf(raw, sizeof(raw), "%s", body["output"].as<const char*>());
            named = true;
        }
    } else if (req.param("output", raw, sizeof(raw))) {
        named = true;
    }

    if (!named || raw[0] == '\0') {
        *outTarget = AUX_LED_TARGET_ALL;
        return true;
    }

    ServoOutputDriver driver = SERVO_DRIVER_LEDC;
    uint8_t channel = 0;
    if (!servoOutputParseAddress(raw, &driver, &channel) || driver != SERVO_DRIVER_LEDC) {
        return false;
    }
    const BoardOutput* output = boardOutputOnChannel(channel);
    if (output == nullptr) {
        return false;
    }
    *outTarget = (uint8_t)(output - BOARD_OUTPUTS);
    return true;
}

// Every lit wire and what it is showing, as the status frame reports them. It
// is the whole set rather than just the wire commanded: a builder who asked all
// of them to go red has changed several, and a receipt naming one would be a
// narrower answer than the request.
void sendLitWiresResponse(WebRequest& req) {
    LitWireReading readings[BOARD_OUTPUT_COUNT] = {};
    size_t count = 0;

    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        AuxLedState state = {};
        taskENTER_CRITICAL(&robotStateMux);
        state = robotState.auxLed[i];
        taskEXIT_CRITICAL(&robotStateMux);
        if (!state.lit) {
            continue;
        }
        readings[count].id = BOARD_OUTPUTS[i].id;
        readings[count].r = state.r;
        readings[count].g = state.g;
        readings[count].b = state.b;
        readings[count].effect = auxLedEffectToString(state.effect);
        readings[count].available = state.available;
        ++count;
    }

    char wiresJson[BOARD_OUTPUT_COUNT * 96 + 8] = {};
    if (!formatLitWiresJson(wiresJson, sizeof(wiresJson), readings, count, nullptr)) {
        webSendJsonError(req, 500, "lights response overflow");
        return;
    }

    char body[sizeof(wiresJson) + 32] = {};
    const int written = snprintf(body, sizeof(body), "{\"ok\":true,\"lights\":%s}", wiresJson);
    if (written <= 0 || (size_t)written >= sizeof(body)) {
        webSendJsonError(req, 500, "lights response overflow");
        return;
    }

    req.send(200, "application/json", body);
}

// Both endpoints reject a refused queue the same way, and the distinction the
// operator needs is why: a wire with no light on it is a wiring/config answer,
// a full queue is a retry.
void sendAuxLedQueueRefusal(WebRequest& req, uint8_t target) {
    if (!auxLedTargetIsLit(target)) {
        webSendJsonError(req, 503, "no light on that wire");
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

    uint8_t target = AUX_LED_TARGET_ALL;
    if (!parseTargetPayload(req, &target)) {
        webSendJsonError(req, 400, "output must be an Output Address this droid has");
        return;
    }

    if (!auxLedQueueSetColor(target, r, g, b, SRC_WEB_API)) {
        sendAuxLedQueueRefusal(req, target);
        return;
    }

    sendLitWiresResponse(req);
}

void handleAuxLedEffectPost(WebRequest& req) {
    AuxLedEffect effect = AUX_LED_EFFECT_OFF;
    if (!parseEffectPayload(req, &effect)) {
        webSendJsonError(req, 400, "effect must be one of off|solid|blink|pulse");
        return;
    }

    uint8_t target = AUX_LED_TARGET_ALL;
    if (!parseTargetPayload(req, &target)) {
        webSendJsonError(req, 400, "output must be an Output Address this droid has");
        return;
    }

    if (!auxLedQueueSetEffect(target, effect, SRC_WEB_API)) {
        sendAuxLedQueueRefusal(req, target);
        return;
    }

    sendLitWiresResponse(req);
}

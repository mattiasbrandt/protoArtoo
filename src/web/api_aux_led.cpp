// =============================================================================
// src/web/api_aux_led.cpp
//
// AUX LED REST API
//   POST /api/aux-led/color   body: {"r":0,"g":0,"b":0}
//   POST /api/aux-led/effect  body: {"effect":"solid|blink|pulse|off"}
// =============================================================================

#include "api_aux_led.h"

#include <cstdint>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "api_helpers.h"
#include "aux_led.h"
#include "robot_state.h"

namespace {

bool parseJsonBody(AsyncWebServerRequest* req, JsonDocument* outDoc, bool* hasBody) {
    if (outDoc == nullptr || hasBody == nullptr) {
        return false;
    }
    *hasBody = false;

    if (req == nullptr || !req->hasParam("plain", true)) {
        return true;
    }

    *hasBody = true;
    DeserializationError err = deserializeJson(*outDoc, req->getParam("plain", true)->value().c_str());
    return !err;
}

bool parseUint8FormField(AsyncWebServerRequest* req, const char* key, uint8_t* out) {
    if (req == nullptr || key == nullptr || out == nullptr || !req->hasParam(key, true)) {
        return false;
    }

    uint32_t parsed = 0;
    if (!parseUint32Value(req->getParam(key, true)->value().c_str(), &parsed) || parsed > 255U) {
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

bool parseColorPayload(AsyncWebServerRequest* req, uint8_t* r, uint8_t* g, uint8_t* b) {
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

bool parseEffectPayload(AsyncWebServerRequest* req, AuxLedEffect* outEffect) {
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

    if (!req->hasParam("effect", true)) {
        return false;
    }

    return parseAuxLedEffect(req->getParam("effect", true)->value().c_str(), outEffect);
}

void sendAuxLedStateResponse(AsyncWebServerRequest* req) {
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
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"aux LED response overflow\"}");
        return;
    }

    req->send(200, "application/json", body);
}

}  // namespace

void registerAuxLedRoutes(AsyncWebServer& server) {
    server.on("/api/aux-led/color", HTTP_POST, [](AsyncWebServerRequest* req) {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        if (!parseColorPayload(req, &r, &g, &b)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"payload must contain r,g,b integers 0..255\"}");
            return;
        }

        if (!auxLedQueueSetColor(r, g, b, SRC_WEB_API)) {
            req->send(503, "application/json", "{\"ok\":false,\"error\":\"aux LED command queue full\"}");
            return;
        }

        sendAuxLedStateResponse(req);
    });

    server.on("/api/aux-led/effect", HTTP_POST, [](AsyncWebServerRequest* req) {
        AuxLedEffect effect = AUX_LED_EFFECT_OFF;
        if (!parseEffectPayload(req, &effect)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"effect must be one of off|solid|blink|pulse\"}");
            return;
        }

        if (!auxLedQueueSetEffect(effect, SRC_WEB_API)) {
            req->send(503, "application/json", "{\"ok\":false,\"error\":\"aux LED command queue full\"}");
            return;
        }

        sendAuxLedStateResponse(req);
    });
}

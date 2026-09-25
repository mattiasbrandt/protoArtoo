// =============================================================================
// src/web/api_identity.cpp
//
// Droid identity API endpoints
//   GET  /api/identity             - cosmetic droid name, mDNS opt-in, and the
//                                    compile-time Feature Availability manifest
//   POST /api/identity             - persist validated droid name and mDNS opt-in
//   GET  /api/identity/components  - the Component Registry lineup: every
//                                    product the project supports or plans,
//                                    and what this image can drive
//
// First route ported to the WebRequest seam (ADR 0021): the same handler
// source compiles and serves under every backend and names no vendor type.
// =============================================================================

#include "api_identity.h"

#include <stdio.h>

#include "api_helpers.h"
#include "api_json_response.h"
#include "component_registry.h"
#include "config.h"
#include "config_store.h"
#include "config_cache.h"
#include "config_write_lock.h"  // identitySetWriteWindow() lives here
#include "logging.h"
#include "web_request.h"

static const char* TAG = "WebServer";

namespace {

void sendIdentityResponse(WebRequest& req, const SystemConfig& system) {
    // Fixed buffer for identity JSON serialization including the manifest.
    // IDENTITY_JSON_MAX_BYTES = 512 B; usable JSON is 511 B (1 byte for NUL).
    // Worst case is a 32-char droid name (DROID_NAME_MAX_LEN), mdnsUseName false,
    // and every manifest value false (false is 5 chars, true is 4). With today's
    // manifest -- 4 capabilities, 3 flags, 3 Board Lanes, and the Learned
    // Sequence cap -- that worst case is 487 B of JSON on firebeetle2, leaving
    // 511 - 487 = 24 B of headroom; the artoo-esp32 is one byte shorter, its cap
    // being one digit (5) where firebeetle2's is two (10). Every lane's UART
    // index is one digit and every lane pin is two on both boards.
    // A capability or flag row emits ,"<name>":false, so it costs name_len + 9
    // bytes at worst (name_len + 8 for the first row in an object, which has no
    // leading comma). A Board Lane row emits
    // ,"<name>":{"uart":N,"tx":NN,"rx":NN} and costs name_len + 29 at worst,
    // one more for each extra digit in a pin or controller index.
    // Every capability, flag or lane added grows this payload toward the ceiling.
    char body[IDENTITY_JSON_MAX_BYTES] = {};
    if (!formatIdentityJson(body, sizeof(body), system.droid_name, system.mdns_use_name)) {
        webSendJsonError(req, 500, "identity response overflow");
        return;
    }
    req.send(200, "application/json", body);
}

}  // namespace

void handleIdentityGet(WebRequest& req) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    sendIdentityResponse(req, snap.system);
}

// See include/api_identity.h for the full contract.
IdentitySetCommitOutcome identitySetCommitApplied(ConfigSnapshot* working) {
    IdentitySetCommitOutcome outcome;
    configCacheApply(*working);

    if (!configPersistSystem(working->system)) {
        return outcome;
    }

    PA_LOG_INFO(TAG, "[IDENTITY] name=%s mdnsUseName=%s", working->system.droid_name,
                working->system.mdns_use_name ? "true" : "false");
    outcome.persisted = true;
    return outcome;
}

// See include/api_identity.h for the full contract.
bool identitySetWriteWindow(const char* droidName, bool mdnsUseName, ConfigSnapshot* working,
                            IdentitySetCommitOutcome* commit) {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return false;
    }
    configCacheRead(working);
    snprintf(working->system.droid_name, sizeof(working->system.droid_name), "%s", droidName);
    working->system.mdns_use_name = mdnsUseName;
    *commit = identitySetCommitApplied(working);
    return true;
}

// GET /api/identity/components -- the Component Registry lineup.
//
// Every row, including the parts nothing drives: a builder sees a product we
// have not written a driver for as PLANNED rather than as silently absent, and
// a Component Picker reads one lineup from the controller instead of keeping
// its own (ADR 0042 as amended 2026-09-09).
void handleComponentsGet(WebRequest& req) {
    // Pin the active members before the send. Sound and the Radio Controller
    // have one; a family without a member setting reports active_member null,
    // which is what never pinning it gives.
    componentRegistryJsonPinActiveMember(COMPONENT_CATEGORY_SOUND,
                                         configCacheReadActiveSoundMember());
    // The radio member drives nothing on the controller, so there is no boot
    // latch to report: the saved choice is the active one.
    {
        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        const ComponentPartEntry* radio =
            componentResolveMember(COMPONENT_CATEGORY_RADIO_CONTROLLER, snap.system.rc_member);
        componentRegistryJsonPinActiveMember(COMPONENT_CATEGORY_RADIO_CONTROLLER,
                                             radio != nullptr ? radio->value : 0);
    }

    if (!req.sendChunked("application/json", fillComponentRegistryJson)) {
        webSendJsonError(req, 500, "response alloc failed");
        return;
    }
    PA_LOG_DEBUG(TAG, "GET /api/identity/components (%u parts)", (unsigned)COMPONENT_PART_COUNT);
}

void handleIdentityPost(WebRequest& req) {
    // Oversized relative to DROID_NAME_MAX_LEN so an over-long submission
    // still reaches normalizeDroidName() as an over-long string and is
    // rejected, instead of being truncated into a silently valid name.
    char rawName[DROID_NAME_MAX_LEN * 2 + 2] = {};
    if (!req.param("droidName", rawName, sizeof(rawName))) {
        webSendJsonError(req, 400, "droidName is required");
        return;
    }

    char normalized[DROID_NAME_MAX_LEN + 1] = {};
    if (!normalizeDroidName(rawName, normalized, sizeof(normalized))) {
        webSendJsonError(req, 400, "droidName must be 1..32 lowercase letters, numbers, or hyphens; spaces are not allowed");
        return;
    }

    bool mdnsUseName = false;
    char rawMdns[16] = {};
    if (req.param("mdnsUseName", rawMdns, sizeof(rawMdns))) {
        if (!parseBoolValue(rawMdns, &mdnsUseName)) {
            webSendJsonError(req, 400, "mdnsUseName must be true/false or 1/0");
            return;
        }
    }

    ConfigSnapshot working = {};
    IdentitySetCommitOutcome commit;
    if (!identitySetWriteWindow(normalized, mdnsUseName, &working, &commit)) {
        webSendJsonError(req, 503, "config write busy");
        return;
    }
    if (!commit.persisted) {
        webSendJsonError(req, 500, "failed to persist identity");
        return;
    }

    sendIdentityResponse(req, working.system);
}

// =============================================================================
// src/web/api_servo.cpp
//
// Servo control API endpoint
//   POST /api/servo         - Move one Output, named by its board's label
//                             (open/close/position/stop/nudge/travel/hold/release)
//   POST /api/servo/centre  - Put every Servo Output back to centre, paced by
//                             the Sequence Coordinator (#318, #365)
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table. The command goes onto servoCmdQueue with a zero wait, so
// this never blocks on ServoTask.
// =============================================================================

#include "api_servo.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>  // strcasecmp()

#include "api_helpers.h"
#include "api_json_response.h"
#include "board_outputs.h"  // boardOutputForWord(), boardOutputWordList()
#include "ledc_pwm.h"
#include "logging.h"
#include "robot_state.h"
#include "servo_helpers.h"  // servo_ledc_channel_to_arm_id()

extern QueueHandle_t servoCmdQueue;

static const char* TAG = "SERVO_API";

// See include/api_servo.h for the full contract - exported so the Controller
// Console's servo.action.* executors (include/console_direct_action_servo.h)
// reuse the same target<->id mapping.
int16_t parseArmId(const char* arm) {
    if (arm == nullptr) {
        return -1;
    }
    if (strcasecmp(arm, "both") == 0) {
        return 255;
    }
    const BoardOutput* output = boardOutputForWord(arm);
    uint8_t armId = 0;
    if (output == nullptr || !servo_ledc_channel_to_arm_id(output->channel, &armId)) {
        return -1;
    }
    return armId;
}

// See include/api_servo.h for the full contract. `cmd` is zero-initialised
// here: the pre-port handler left fields of an uninitialised local unset that
// ServoTask never read for OPEN/CLOSE/POSITION, so this closes that latent UB
// without changing anything ServoTask observes.
ServoSubmitOutcome servoSubmitCommand(uint8_t armId, ServoCommandType type, uint16_t positionUs,
                                       CommandSource source) {
    ServoSubmitOutcome outcome;
    ServoCommand cmd = {};
    cmd.armId = armId;
    cmd.type = type;
    cmd.positionUs = positionUs;
    cmd.source = source;
    cmd.timestampMs = millis();
    outcome.ok = (xQueueSend(servoCmdQueue, &cmd, 0) == pdTRUE);
    return outcome;
}

namespace {

// What one action name means. Three rules travel with the name rather than
// being re-derived from it at each use: the command type, the width the name
// fixes where it fixes one, and whether the request has to name a width itself.
//
// `oneOutputOnly` is the fourth, and it is the reason this is a table. A nudge
// and a hold are each about ONE Output by definition -- a builder watching
// which part twitches, and a dial standing on one row -- so the `both`
// broadcast (the first two Outputs, include/robot_state.h ServoCommand::armId)
// is refused for both, at the door, where the caller hears why.
struct ServoActionSpec {
    const char* name;
    ServoCommandType type;
    uint16_t positionUs;  // the width the action fixes; 0 when it fixes none
    bool needsWidth;      // the request must carry positionUs
    bool oneOutputOnly;   // arm=both is refused
};

constexpr ServoActionSpec kServoActions[] = {
    {"open", SERVO_CMD_OPEN, 0, false, false},
    {"close", SERVO_CMD_CLOSE, 0, false, false},
    // "stop" drives to neutral; it does not hold position (include/
    // console_direct_action_servo.h's header comment has the full note).
    {"stop", SERVO_CMD_POSITION, SERVO_PULSE_NEUTRAL_US, false, false},
    {"position", SERVO_CMD_POSITION, 0, true, false},
    // Find by Moving (ADR 0050, #363): no width travels with it. ServoTask
    // computes the bounded pair from the width on the pin, so this route
    // cannot be handed a big nudge however the request is spelled.
    {"nudge", SERVO_CMD_NUDGE, 0, false, true},
    // The calibration dial's hold (ADR 0064, #364): drive there and keep the
    // pulse on it. Every hold refreshes the short expiry; the first one starts
    // the ten-minute ceiling, which nothing sent here can move.
    {"hold", SERVO_CMD_HOLD, 0, true, true},
    // Pulses off (ADR 0043, ADR 0064, #364): the Output goes limp where it is.
    // No width, because a release commands no position at all.
    {"release", SERVO_CMD_RELEASE, 0, false, false},
    // A Part run through its travel and back (ADR 0063, #352): the one command
    // a deliberate press on a body view sends. No width travels with it either
    // -- ServoTask reads the ends off the Output's own row (include/
    // servo_travel.h) -- and one output only, because a press is about one
    // Part and the broadcast would run two parts through their travel at once.
    {"travel", SERVO_CMD_TRAVEL, 0, false, true},
};

const ServoActionSpec* findAction(const char* action) {
    for (const ServoActionSpec& spec : kServoActions) {
        if (strcmp(action, spec.name) == 0) {
            return &spec;
        }
    }
    return nullptr;
}

}  // namespace

void handleServoPost(WebRequest& req) {
    // Both buffers are wider than the longest value either name accepts, so an
    // over-long input arrives at the parsers as an over-long string and is
    // rejected, rather than being truncated into a valid one (web_request.h).
    char arm[16] = {};
    char action[16] = {};
    if (!req.param("arm", arm, sizeof(arm)) || !req.param("action", action, sizeof(action))) {
        webSendJsonError(req, 400, "Missing arm or action parameter");
        return;
    }

    // The word is the board's own label for the Output (ADR 0033 Amendment
    // 2026-09-19), so a refusal names this board's words rather than a list
    // that is right for one board only.
    char words[64] = {};
    boardOutputWordList(", ", words, sizeof(words));

    int16_t armId = parseArmId(arm);
    if (armId < 0) {
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "No output called %s on this board. Use %s, or both", arm,
                 words);
        webSendJsonError(req, 400, errMsg);
        return;
    }

    const ServoActionSpec* spec = findAction(action);
    if (spec == nullptr) {
        webSendJsonError(
            req, 400,
            "Invalid action. Use: open, close, stop, position, nudge, travel, hold, or release");
        return;
    }

    // One output at a time where the action is about one output. Refused here,
    // where the caller hears why, rather than only in ServoTask's log.
    if (spec->oneOutputOnly && armId == 255) {
        char errMsg[112];
        snprintf(errMsg, sizeof(errMsg), "A %s moves one output. Use %s", spec->name, words);
        webSendJsonError(req, 400, errMsg);
        return;
    }

    uint16_t positionUs = spec->positionUs;
    if (spec->needsWidth) {
        char positionRaw[16] = {};
        if (!req.param("positionUs", positionRaw, sizeof(positionRaw))) {
            char errMsg[80];
            snprintf(errMsg, sizeof(errMsg), "Missing positionUs parameter for %s action",
                     spec->name);
            webSendJsonError(req, 400, errMsg);
            return;
        }
        // Unparseable input lands on the same range error a numerically
        // out-of-range value gets, which is what this endpoint has always
        // answered -- the vendor's toInt() read garbage as 0.
        uint32_t parsed = 0;
        if (!parseUint32Value(positionRaw, &parsed) || parsed < SERVO_PULSE_MIN_US ||
            parsed > SERVO_PULSE_MAX_US) {
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg),
                     "positionUs must be between %u and %u",
                     (unsigned)SERVO_PULSE_MIN_US, (unsigned)SERVO_PULSE_MAX_US);
            webSendJsonError(req, 400, errMsg);
            return;
        }
        positionUs = (uint16_t)parsed;
    }

    // Commit Step (ADR 0036 criterion 1, include/api_servo.h): the same
    // servoCmdQueue submission both this handler and the Console's
    // servo.action.* executors now make.
    ServoSubmitOutcome outcome =
        servoSubmitCommand((uint8_t)armId, spec->type, positionUs, SRC_WEB_API);
    if (!outcome.ok) {
        webSendJsonError(req, 503, "Servo command queue full");
        return;
    }

    PA_LOG_INFO(TAG, "[WEB] Servo command queued: arm=%s, action=%s", arm, action);
    req.send(200, "application/json", "{\"ok\":true}");
}

// =============================================================================
// POST /api/servo/centre  --  put every Servo Output back to centre (#318, #365)
//
// One operator standing at the bench chooses this, which is what makes it
// legitimate: it is never emitted automatically. What arrives here is the whole
// of their press. The EXPANSION -- which Outputs, in what order, how far apart
// -- belongs to the Sequence Coordinator and is not in this handler, in the
// request, or in the browser that sent it, because a safe pace a page held is
// one a hand-edited or imported client could walk around (CONTEXT.md "Cadence
// Floor", "Sequence Coordinator").
//
// So the handler validates nothing and queues no servo command. It sets the
// transient flag the Coordinator reads on its next tick -- the same shape
// POST /api/seq/stop uses -- and answers. Idempotent: a second press while a
// sweep is in flight restarts it rather than queueing a second one.
//
// The Coordinator refuses to start under a latched estop or in Sleep Mode and
// says so in the log. Its moves are SRC_SEQ, which ServoTask refuses under
// either halt, so a sweep's move still queued when one lands is dropped there
// too. This route does not duplicate that judgement, which would put the same
// rule in two places and let them disagree.
// =============================================================================
void handleServoCentrePost(WebRequest& req) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.bulkCentreRequest = SRC_WEB_API;
    taskEXIT_CRITICAL(&robotStateMux);

    PA_LOG_INFO(TAG, "[WEB] back to centre requested");
    req.send(200, "application/json", "{\"ok\":true}");
}

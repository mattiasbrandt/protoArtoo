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
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>  // strcasecmp()

#include "api_helpers.h"
#include "api_json_response.h"
#include "board_outputs.h"  // boardOutputForWord(), boardOutputWordList()
#include "config_cache.h"   // configCacheOutputIsWired(), configCacheReadServoOutputComponent()
#include "ledc_pwm.h"
#include "logging.h"
#include "output_wire.h"    // outputWirePinKeptForLight()
#include "robot_state.h"
#include "sequence_bulk_centre.h"  // sequenceBulkCentreHasTravel(), sequenceBodyCentrePlan()
#include "servo_backend.h"  // boardOutputAddress(), boardOutputIndexOf()
#include "servo_output_row.h"  // servoOutputParseExpanderAddress() - an expander Output's name
#include "servo_task.h"     // servoTaskDrivesOutput() - what ServoTask started with

extern QueueHandle_t servoCmdQueue;

static const char* TAG = "SERVO_API";

// See include/api_servo.h for the full contract - exported so the Controller
// Console's servo.action.* executors (include/console_direct_action_servo.h)
// reuse the same word<->Output mapping.
bool servoParseTarget(const char* word, ServoOutputAddress* output) {
    if (word == nullptr || output == nullptr) {
        return false;
    }
    if (strcasecmp(word, "both") == 0) {
        *output = SERVO_OUTPUT_BOTH_ARMS;
        return true;
    }
    const BoardOutput* board = boardOutputForWord(word);
    if (board != nullptr) {
        *output = boardOutputAddress((size_t)(board - BOARD_OUTPUTS));
        return true;
    }
    return servoOutputParseExpanderAddress(word, output);
}

// See include/api_servo.h for the full contract. `cmd` is zero-initialised
// here: the pre-port handler left fields of an uninitialised local unset that
// ServoTask never read for OPEN/CLOSE/POSITION, so this closes that latent UB
// without changing anything ServoTask observes.
ServoSubmitOutcome servoSubmitCommand(ServoOutputAddress output, ServoCommandType type,
                                      uint16_t positionUs, CommandSource source) {
    ServoSubmitOutcome outcome;
    ServoCommand cmd = {};
    cmd.output = output;
    cmd.type = type;
    cmd.positionUs = positionUs;
    cmd.source = source;
    outcome.ok = (xQueueSend(servoCmdQueue, &cmd, 0) == pdTRUE);
    return outcome;
}

namespace {

// One Output's half of servoOutputUndriven(). The saved tick and component are
// read only to say what a restart would do; whether anything drives it NOW is
// ServoTask's snapshot alone. The tick is stored per board Output, so an
// address that is not one of the board's has none saved.
//
// An expander's Output is named by its address, and says first what keeps its
// expander from driving anything (#444): not chosen as the body servo
// controller, or not answering - the words every surface shows for it
// (data/outputs.js, "unreachable"). Past those, it is wired the way a board
// Output is, by having a Part on it, read at start.
bool oneOutputUndriven(ServoOutputAddress output, char* reason, size_t reasonSize) {
    if (servoTaskDrivesOutput(output)) {
        return false;
    }
    char address[SERVO_OUTPUT_ADDRESS_STR_MAX + 1] = {};
    const char* name = servoOutputAddressName(output.driver, output.channel);
    if (name[0] == '\0') {
        servoOutputFormatAddress(address, sizeof(address), output.driver, output.channel);
        name = address;
    }
    if (output.driver == SERVO_DRIVER_PCA9685) {
        const ServoExpanderFacts expander = servoTaskExpanderFacts();
        if (!expander.chosen) {
            snprintf(reason, reasonSize,
                     "%s is on the PCA9685. Choose it as the body servo controller to use it.", name);
        } else if (!expander.answering) {
            snprintf(reason, reasonSize, "%s is unreachable - the PCA9685 is not answering.", name);
        } else if (configCacheServoOutputPartCountAt(output.driver, output.channel) > 0) {
            snprintf(reason, reasonSize, "Restart the droid to use %s.", name);
        } else {
            snprintf(reason, reasonSize, "%s has no Part on it. Put one on it on Wiring.", name);
        }
        return true;
    }
    const size_t boardIndex = boardOutputIndexOf(output);
    const OutputWireInputs saved = {
        boardIndex < BOARD_OUTPUT_COUNT && configCacheOutputIsWired(boardIndex),
        configCacheReadServoOutputComponent(output.driver, output.channel),
    };
    if (outputWirePinKeptForLight(saved, boardIndex)) {
        snprintf(reason, reasonSize, "%s carries a light, not a servo.", name);
    } else if (saved.wired) {
        snprintf(reason, reasonSize, "Restart the droid to use %s.", name);
    } else {
        snprintf(reason, reasonSize, "%s has no Part on it. Put one on it on Wiring.", name);
    }
    return true;
}

}  // namespace

bool servoOutputUndriven(ServoOutputAddress output, char* reason, size_t reasonSize) {
    if (output == SERVO_OUTPUT_BOTH_ARMS) {
        return oneOutputUndriven(boardOutputAddress(0), reason, reasonSize) ||
               oneOutputUndriven(boardOutputAddress(1), reason, reasonSize);
    }
    return oneOutputUndriven(output, reason, reasonSize);
}

// The two ServoTask questions answer false for `both` and for any address it
// has no slot for, so neither needs testing for here.
bool servoCommandIsARunsOnAFreeOutput(ServoOutputAddress output, ServoCommandType type) {
    if (type == SERVO_CMD_NUDGE) {
        return servoTaskMayTakeForRun(output);
    }
    // A release is Stop, and Stop always wins: it is taken for an Output the
    // run holds, and for a free one it may be about to, because the nudge that
    // takes it can still be queued ahead of this release (include/api_servo.h).
    return type == SERVO_CMD_RELEASE &&
           (servoTaskRunHolds(output) || servoTaskMayTakeForRun(output));
}

namespace {

// What one action name means. Three rules travel with the name rather than
// being re-derived from it at each use: the command type, the width the name
// fixes where it fixes one, and whether the request has to name a width itself.
//
// `oneOutputOnly` is the fourth, and it is the reason this is a table. A nudge
// and a hold are each about ONE Output by definition -- a builder watching
// which part twitches, and a dial standing on one row -- so the `both`
// broadcast (the first two Outputs, SERVO_OUTPUT_BOTH_ARMS)
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
    // pulse on it. Every hold refreshes the short expiry; the one that takes
    // the Output starts the ten-minute ceiling, which nothing sent here can
    // move. `refresh=1` makes it a refresh only (#417): see handleServoPost().
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

    ServoOutputAddress output = SERVO_OUTPUT_NONE;
    if (!servoParseTarget(arm, &output)) {
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
    if (spec->oneOutputOnly && output == SERVO_OUTPUT_BOTH_ARMS) {
        char errMsg[112];
        snprintf(errMsg, sizeof(errMsg), "A %s moves one output. Use %s", spec->name, words);
        webSendJsonError(req, 400, errMsg);
        return;
    }

    // A hold is a press unless it says it is a refresh (#417). The dial's
    // keepalive and its own moves send `refresh=1`, which ServoTask honours
    // only while the hold still stands, so nothing but a press -- opening the
    // dial, "take it again", a test sweep -- takes an Output a bound, the
    // estop or pulses off let go. Any other value is refused rather than read
    // as a press, which would be the one wrong way to read it.
    ServoCommandType type = spec->type;
    char refreshRaw[4] = {};
    if (req.param("refresh", refreshRaw, sizeof(refreshRaw))) {
        if (spec->type != SERVO_CMD_HOLD || strcmp(refreshRaw, "1") != 0) {
            webSendJsonError(req, 400, "refresh=1 is for a hold only");
            return;
        }
        type = SERVO_CMD_HOLD_REFRESH;
    }

    uint16_t positionUs = spec->positionUs;
    if (spec->needsWidth) {
        char positionRaw[16] = {};
        if (!req.param("positionUs", positionRaw, sizeof(positionRaw))) {
            char errMsg[80];
            snprintf(errMsg, sizeof(errMsg), "Missing positionUs parameter for %s action",
                     spec->name);
            ApplyRefusal refusal;
            applyRefusalSet(&refusal, ApplyRefusalReason::MissingArgument, "positionUs");
            webSendApplyRefusal(req, 400, errMsg, refusal);
            return;
        }
        // Unparseable input lands on the same range error a numerically
        // out-of-range value gets, which is what this endpoint has always
        // answered -- the vendor's toInt() read garbage as 0.
        uint32_t parsed = 0;
        if (!parseUint32Value(positionRaw, &parsed) || parsed < SERVO_PULSE_MIN_US ||
            parsed > SERVO_PULSE_MAX_US) {
            // With its field, reason and range as data (#425), so a page words
            // the refusal itself rather than keeping its own copy of the range
            // (ADR 0068, amended 2026-09-26).
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg),
                     "positionUs must be between %u and %u",
                     (unsigned)SERVO_PULSE_MIN_US, (unsigned)SERVO_PULSE_MAX_US);
            ApplyRefusal refusal;
            applyRefusalSetRange(&refusal, "positionUs", SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
            webSendApplyRefusal(req, 400, errMsg, refusal);
            return;
        }
        positionUs = (uint16_t)parsed;
    }

    // Last, after every check on the request itself: a malformed request is
    // still told what is wrong with it. ServoTask drops a command for an Output
    // it does not drive without a word, so this is the only place the caller
    // can hear it - and a queued command would answer `ok` for nothing (#364).
    // A Find by Moving run's nudge or release on a free Output is the one
    // exception: ServoTask takes that Output for the run (#411).
    char undriven[96] = {};
    if (!servoCommandIsARunsOnAFreeOutput(output, type) &&
        servoOutputUndriven(output, undriven, sizeof(undriven))) {
        webSendJsonError(req, 409, undriven);
        return;
    }

    // Commit Step (ADR 0036 criterion 1, include/api_servo.h): the same
    // servoCmdQueue submission both this handler and the Console's
    // servo.action.* executors now make.
    ServoSubmitOutcome outcome = servoSubmitCommand(output, type, positionUs, SRC_WEB_API);
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
size_t servoCentreSkipped(void (*visit)(const char* name, void* ctx), void* ctx) {
    size_t skipped = 0;
    const uint8_t count = configCacheServoOutputCount();
    for (uint8_t i = 0; i < count; ++i) {
        ServoOutputRow row = {};
        if (!configCacheReadServoOutput(i, &row) || !sequenceBulkCentreHasTravel(row)) {
            continue;  // a light is passed over by design, not for want of a drive
        }
        const SeqBodyStepPlan plan = sequenceBodyCentrePlan(row);
        if (plan.drive && servoTaskDrivesOutput(plan.output)) {
            continue;
        }
        char address[SERVO_OUTPUT_ADDRESS_STR_MAX + 1] = {};
        const char* name = servoOutputAddressName(row.driver, row.channel);
        if (name[0] == '\0') {
            servoOutputFormatAddress(address, sizeof(address), row.driver, row.channel);
            name = address;
        }
        if (visit != nullptr) {
            visit(name, ctx);
        }
        ++skipped;
    }
    return skipped;
}

void handleServoCentrePost(WebRequest& req) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.bulkCentreRequest = SRC_WEB_API;
    taskEXIT_CRITICAL(&robotStateMux);

    // What the sweep will pass over, named now: the Coordinator skips these
    // rows without spending a slot on them, and this is the one answer the
    // caller gets (#364).
    JsonDocument doc;
    doc["ok"] = true;
    JsonArray skipped = doc["skipped"].to<JsonArray>();
    servoCentreSkipped([](const char* name, void* ctx) { static_cast<JsonArray*>(ctx)->add(name); },
                       &skipped);

    PA_LOG_INFO(TAG, "[WEB] back to centre requested");
    // A ceiling, not a size: twenty-four rows all skipped, each named by an
    // eight-character address, is under 300 B.
    webSendJsonDocument(req, doc, 512, TAG);
}

// =============================================================================
// src/web/api_drive.cpp
//
// Drive and web control API endpoints
//   POST /api/mode                    - stationary / driving mode
//   POST /api/drive                   - browser drive command (timeout-protected)
//   POST /api/drive/speed-preset      - apply Slow/Normal/Turbo speed preset
//   POST /api/web-control/enable      - enable browser control
//   POST /api/web-control/disable     - disable browser control
//   POST /api/dome/cmd                - forward a raw Marcduino line to the dome
//   POST /api/dome                    - dome rotation speed
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table.
//
// None of these handlers touch a motor. A drive command is validated, clamped
// to the configured cap and handed to the drive arbiter, which DriveTask reads
// on its own 50 Hz cadence -- so the zero-frame continuity and speed cap the
// arbiter and DriveTask enforce are untouched by anything here. Every queue
// send uses a zero wait, so no real-time loop can be blocked from this file.
// =============================================================================

#include "api_drive.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "api_helpers.h"
#include "api_json_response.h"
#include "audio_task.h"
#include "commanded_modes.h"
#include "config_cache.h"
#include "dome_link.h"
#include "dome_rx_parser.h"
#include "drive_arbiter.h"
#include "drive_speed_preset.h"
#include "failsafe_gate.h"
#include "logging.h"
#include "mood.h"
#include "queue_drop_tracker.h"
#include "robot_state.h"
#include "sequence_dispatcher.h"
#include "web_server.h"

// External declaration for saveConfigToNvs
extern bool saveConfigToNvs();

static const char* TAG = "WebServer";

static bool isSleepModeActive() {
    taskENTER_CRITICAL(&robotStateMux);
    bool sleeping = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    return sleeping;
}

namespace {

// ManualCommand - recognized command tokens for POST /api/manual-command.
// Internal to this translation unit; not exposed in any header.
enum ManualCommand : uint8_t {
    MC_UNKNOWN = 0,
    MC_ESTOP,
    MC_CLEAR_ESTOP,
    MC_ENABLE_WEB_CONTROL,
    MC_DISABLE_WEB_CONTROL,
    MC_REBOOT,
    MC_STATIONARY_MODE,
    MC_DRIVING_MODE,
};

ManualCommand resolveManualCommand(const char* command) {
    if (command == nullptr) {
        return MC_UNKNOWN;
    }
    if (strcmp(command, "estop") == 0) {
        return MC_ESTOP;
    }
    if (strcmp(command, "clear_estop") == 0) {
        return MC_CLEAR_ESTOP;
    }
    if (strcmp(command, "enable_web_control") == 0) {
        return MC_ENABLE_WEB_CONTROL;
    }
    if (strcmp(command, "disable_web_control") == 0) {
        return MC_DISABLE_WEB_CONTROL;
    }
    if (strcmp(command, "reboot") == 0) {
        return MC_REBOOT;
    }
    // The two mode keywords. Nothing asks for them any more:
    // executeManualCommand() refuses "#st"/"#sm" before the Marcduino routing
    // that shadows them ever claims the line (isShadowedModeKeyword() below,
    // #379). They stay here because that guard reads its definition of the
    // keywords from this function rather than from a second copy of the two
    // strings.
    if (strcmp(command, "#st") == 0) {
        return MC_STATIONARY_MODE;
    }
    if (strcmp(command, "#sm") == 0) {
        return MC_DRIVING_MODE;
    }
    return MC_UNKNOWN;
}

// Lowercase raw into out. Returns false when raw does not fit, which for the
// keyword path means it cannot be a keyword -- the longest is 19 characters.
bool copyLowercase(const char* raw, char* out, size_t outSize) {
    size_t i = 0;
    for (; raw[i] != '\0'; i++) {
        if (i + 1 >= outSize) {
            return false;
        }
        out[i] = (char)tolower((unsigned char)raw[i]);
    }
    out[i] = '\0';
    return true;
}

// True when raw is one of the two mode keywords the Marcduino '#' routing
// shadows, in any case: "#st" and "#sm" (#379).
//
// Case-insensitive because that is the question resolveManualCommand()
// answers -- it runs on a lowercased copy, so "#ST" would have resolved to
// MC_STATIONARY_MODE exactly as "#st" does, and a refusal that missed the
// uppercase spelling would answer {"ok":true} to the same non-event. Asked of
// that resolver rather than of a second copy of the two strings, so there is
// one definition of what the keywords are and the refusal cannot drift from
// it.
//
// The buffer clears both keywords with room to spare; a longer line cannot be
// one of them, which is what copyLowercase() returning false means here.
bool isShadowedModeKeyword(const char* raw) {
    char lowered[8] = {};
    if (!copyLowercase(raw, lowered, sizeof(lowered))) {
        return false;
    }
    const ManualCommand cmd = resolveManualCommand(lowered);
    return cmd == MC_STATIONARY_MODE || cmd == MC_DRIVING_MODE;
}

void lowercaseInPlace(char* text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        text[i] = (char)tolower((unsigned char)text[i]);
    }
}

bool parseDomeSpeedValue(const char* raw, float* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }

    char* end = nullptr;
    float value = strtof(raw, &end);
    if (end == raw || end == nullptr || *end != '\0') {
        return false;
    }
    if (value < -1.0f || value > 1.0f) {
        return false;
    }

    *out = value;
    return true;
}

// Reads a form parameter and lowercases it in one step, which is what every
// keyword-valued parameter in this group wants. False means absent, and only
// absent: an over-long value still arrives, so it reaches the caller's keyword
// comparison and is answered as the invalid value it is rather than as a
// missing parameter. Callers size out well past their longest valid keyword,
// so no truncation can produce a match (web_request.h).
bool paramLowercase(WebRequest& req, const char* name, char* out, size_t outSize) {
    if (!req.param(name, out, outSize)) {
        return false;
    }
    lowercaseInPlace(out);
    return true;
}

// Store the mode the droid has just been put into, and say whether it reached
// flash. saveConfigToNvs() writes the whole config cache, the new mode
// included -- commandedSetStationary() has already synced it there.
//
// One helper rather than four bare calls because there is a decision behind it
// (#376), and the four mode paths in this file have to take the same one:
//
//   A failed save is REPORTED. The runtime mode is not reverted.
//
// The other consumer of this return in the tree does revert:
// applySpeedPresetPersisted() (src/drive_speed_preset.cpp) puts the previous
// cap back and warns. That is right there and wrong here. A speed preset is a
// stored preference whose runtime value IS the stored number, so restoring the
// old one leaves a coherent pair and costs the operator nothing. A mode is a
// state the droid is already in: commandedSetStationary() has moved the drive
// gating and, on the way out of Stationary, queued the drive-on cue. Undoing
// that on the strength of a flash error would re-enable drive nobody asked
// for -- the wrong direction for this toggle to fail in -- and would run the
// transition a second time.
//
// It also keeps the two adapters for one action answering alike: the Console's
// drive.action.set-mode executor already reports rather than reverts
// (include/console_direct_action_system.h).
//
// What "reported" looks like is the caller's to decide, because each surface
// has its own vocabulary. What none of them may do is answer plain success.
bool saveCommandedMode() {
    return saveConfigToNvs();
}

// POST /api/mode's two branches, which differ only in the mode they command.
// The answer on a failed save reads the way POST /api/audio's volume branch
// already words the same outcome ("volume applied but NVS save failed",
// src/web/api_audio.cpp): applied is true, stored is not.
void applyModeAndAnswer(WebRequest& req, bool stationary) {
    commandedSetStationary(stationary, SRC_WEB_API);
    const bool stored = saveCommandedMode();
    requestStatusBroadcastNow();
    PA_LOG_INFO(TAG, "[WEB] Mode set to %s (stored=%s)", stationary ? "stationary" : "driving",
                stored ? "yes" : "no");
    if (!stored) {
        webSendJsonError(req, 500, "mode applied but NVS save failed");
        return;
    }
    req.send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

ManualCommandResult executeManualCommand(const char* raw) {
    if (raw == nullptr || raw[0] == '\0') {
        return ManualCommandResult::Unsupported;
    }

    // Marcduino commands are case-sensitive - route them directly on raw
    // WITHOUT copying or case-folding. Only the keyword commands below need
    // lowercasing, and we defer that copy until we actually need it.
    const char prefix = raw[0];

    // $ - audio commands: route to AudioTask
    if (prefix == '$') {
        // A full audio queue lands on Unsupported, which is what this branch has
        // always answered: the bool it returns covers "not a $ command I know"
        // and "queue full" alike, and both reached the caller's single failure
        // shape. #376 split the SAVE outcome out, not this one -- the
        // conflation is pre-existing and stays here rather than being widened
        // into.
        return audioQueueDollar(raw, SRC_WEB_API) ? ManualCommandResult::Applied
                                                  : ManualCommandResult::Unsupported;
    }

    // "#st"/"#sm" - refused, and asked BEFORE the branch below, which is the
    // whole point: that branch claims every '#'-prefixed line, so these two
    // never reached the keyword resolver and the route answered {"ok":true}
    // for a mode change that never happened (#379). The caller names
    // POST /api/mode when it answers this.
    if (isShadowedModeKeyword(raw)) {
        return ManualCommandResult::ShadowedModeKeyword;
    }

    // : and # - body-processed Marcduino: servo sequences, panel cmds, config
    if (prefix == ':' || prefix == '#') {
        // Mood commands (:SE10/11/13/14) are not valid body sequences so
        // parseMarcduinoCommand() would silently discard them. Intercept first.
        uint8_t moodId = moodIdFromSeCommand(raw);
        if (moodId != 0) {
            applyMood(moodId);
            return ManualCommandResult::Applied;
        }
        parseMarcduinoCommand(raw);
        // Always accept - body handles or discards per routing table
        return ManualCommandResult::Applied;
    }

    // * @ % & ! - dome-bound Marcduino: forward to dome TX queue
    if (prefix == '*' || prefix == '@' || prefix == '%' || prefix == '&' || prefix == '!') {
        domeQueueTx(raw);
        return ManualCommandResult::Applied;
    }

    // Keyword commands (estop, reboot, etc.) - case-insensitive. Only build the
    // lowercase copy here, not for every Marcduino command. The buffer clears
    // the longest keyword ("disable_web_control", 19); anything longer cannot
    // match one and is resolved as unknown.
    char command[24] = {};
    if (!copyLowercase(raw, command, sizeof(command))) {
        return ManualCommandResult::Unsupported;
    }
    ManualCommand cmd = resolveManualCommand(command);

    switch (cmd) {
        case MC_ESTOP:
            failsafeTrigger(FailsafeLayer::ESTOP);
            return ManualCommandResult::Applied;

        case MC_CLEAR_ESTOP:
            failsafeClearEstop();
            requestStatusBroadcastNow();
            return ManualCommandResult::Applied;

        case MC_ENABLE_WEB_CONTROL:
            commandedSetWebControl(true, SRC_WEB_API);
            return ManualCommandResult::Applied;

        case MC_DISABLE_WEB_CONTROL:
            commandedSetWebControl(false, SRC_WEB_API);
            driveArbiterSubmit(DriveSource::WEB_API, 0, 0, millis());
            return ManualCommandResult::Applied;

        case MC_REBOOT:
            requestSystemRestart(500);
            return ManualCommandResult::Applied;

        // The two mode keywords, and the only branches here that persist
        // anything: saveCommandedMode() above carries the decision they and
        // handleModePost() take together.
        //
        // NEITHER IS REACHABLE, AND THAT IS NOW A DECISION RATHER THAN A
        // DEFECT. The ':'/'#' Marcduino branch above claims every
        // '#'-prefixed line before the keyword resolver ever runs, so "#st"
        // and "#sm" went to parseMarcduinoCommand(), whose own '#' case
        // matches neither and logs "unhandled body command"
        // (src/drivers/dome_rx_parser.cpp) -- shadowed since the Marcduino
        // prefix routing landed the day after them (4f10228f, 2026-03-17).
        // #379 settled it by refusing the two lines ahead of that branch
        // (isShadowedModeKeyword() above) rather than by moving them in front
        // of it: POST /api/mode with mode=stationary|driving already does
        // exactly this, so a second door into the same room is not worth the
        // Marcduino namespace it would cost.
        //
        // So these arms are dead by construction, and they stay anyway: they
        // are the definition the refusal reads (through resolveManualCommand())
        // and the record of what it refuses. They consume the save result as
        // POST /api/mode does, so a future un-shadowing inherits the right
        // answer rather than the discarded one this file used to have.
        // test_api_motion_routes.cpp pins both halves.
        case MC_STATIONARY_MODE:
            commandedSetStationary(true, SRC_WEB_API);
            return saveCommandedMode() ? ManualCommandResult::Applied
                                       : ManualCommandResult::SaveFailed;

        case MC_DRIVING_MODE:
            commandedSetStationary(false, SRC_WEB_API);
            return saveCommandedMode() ? ManualCommandResult::Applied
                                       : ManualCommandResult::SaveFailed;

        case MC_UNKNOWN:
        default:
            return ManualCommandResult::Unsupported;
    }
}

void handleModePost(WebRequest& req) {
    char mode[32] = {};
    if (!paramLowercase(req, "mode", mode, sizeof(mode))) {
        webSendJsonError(req, 400, "missing mode parameter");
        return;
    }

    if (strcmp(mode, "stationary") == 0) {
        applyModeAndAnswer(req, true);
    } else if (strcmp(mode, "driving") == 0) {
        applyModeAndAnswer(req, false);
    } else {
        webSendJsonError(req, 400, "invalid mode - use 'stationary' or 'driving'");
    }
}

void handleSpeedPresetPost(WebRequest& req) {
    char presetRaw[32] = {};
    if (!paramLowercase(req, "preset", presetRaw, sizeof(presetRaw))) {
        webSendJsonError(req, 400, "missing preset");
        return;
    }

    SpeedPresetId preset = SpeedPresetId::Normal;
    if (!parseSpeedPresetId(presetRaw, &preset)) {
        webSendJsonError(req, 400, "invalid preset - use slow, normal, or turbo");
        return;
    }

    if (!applySpeedPresetPersisted(preset)) {
        webSendJsonError(req, 500, "failed to persist speed preset");
        return;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    const int16_t speedLimitMax = cfg.drive.speedLimitMax;
    const SpeedPresetId activePreset = normalizeSpeedPresetId((uint8_t)cfg.drive.speedPresetActive);

    char response[96];
    if (!formatSpeedPresetResponseJson(response, sizeof(response), activePreset, speedLimitMax)) {
        webSendJsonError(req, 500, "speed preset response overflow");
        return;
    }
    req.send(200, "application/json", response);
}

void handleWebControlEnablePost(WebRequest& req) {
    commandedSetWebControl(true, SRC_WEB_API);
    PA_LOG_INFO(TAG, "[WEB] POST /api/web-control/enable - browser control enabled");
    req.send(200, "application/json", "{\"ok\":true}");
}

void handleWebControlDisablePost(WebRequest& req) {
    commandedSetWebControl(false, SRC_WEB_API);
    driveArbiterSubmit(DriveSource::WEB_API, 0, 0, millis());
    PA_LOG_INFO(TAG, "[WEB] POST /api/web-control/disable - browser control disabled");
    req.send(200, "application/json", "{\"ok\":true}");
}

void handleDrivePost(WebRequest& req) {
    // Wider than any valid signed 16-bit decimal, so an over-long value is
    // rejected by parseDriveValue() rather than truncated into a valid one.
    char speedRaw[16] = {};
    char steerRaw[16] = {};
    if (!req.param("speed", speedRaw, sizeof(speedRaw)) ||
        !req.param("steer", steerRaw, sizeof(steerRaw))) {
        webSendJsonError(req, 400, "missing speed or steer");
        return;
    }

    int16_t speed = 0;
    int16_t steer = 0;
    if (!parseDriveValue(speedRaw, &speed) || !parseDriveValue(steerRaw, &steer)) {
        webSendJsonError(req, 400, "speed and steer must be integers");
        return;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    taskENTER_CRITICAL(&robotStateMux);
    const bool sbusHealthy = !robotState.sbusSignalLost && !robotState.sbusHwFailsafe;
    const bool blocked = robotState.estop || robotState.stationary ||
                         (!sbusHealthy && !robotState.webControlEnabled);
    taskEXIT_CRITICAL(&robotStateMux);
    const int16_t maxOut = cfg.drive.speedLimitMax;

    if (blocked) {
        PA_LOG_WARN(TAG, "[WEB] POST /api/drive - rejected: blocked by safety state");
        webSendJsonError(req, 409, "drive blocked by safety state");
        return;
    }

    // Widened to int on all three arguments: Arduino's constrain() is a macro
    // on the device but a same-type template on the host, and the clamp has to
    // be the identical arithmetic in both builds for the host test to mean
    // anything about the device.
    const int16_t clampedSpeed = (int16_t)constrain((int)speed, (int)-maxOut, (int)maxOut);
    const int16_t clampedSteer = (int16_t)constrain((int)steer, (int)-maxOut, (int)maxOut);
    driveArbiterSubmit(DriveSource::WEB_API, clampedSpeed, clampedSteer, millis());

    req.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/dome/cmd - forward a raw Marcduino command verbatim to the dome
// over the dome link TX queue (UART2 or WiFi/UDP), bypassing the body's
// Marcduino prefix router. Use this for dome-native prefixes (:, *, @, etc.)
// that the body would otherwise consume or reject.
void handleDomeCmdPost(WebRequest& req) {
    // Borrowed rather than copied: the length limit below is the contract this
    // endpoint enforces, and a copy-out buffer would silently enforce its own
    // first -- turning an over-long command into a truncated valid one.
    const char* raw = req.paramRef("cmd");
    if (raw == nullptr || raw[0] == '\0') {
        webSendJsonError(req, 400, "missing cmd parameter");
        return;
    }
    if (strlen(raw) > 127) {
        webSendJsonError(req, 400, "cmd too long (max 127)");
        return;
    }
    if (strncmp(raw, "DM:", 3) == 0) {
        if (!sequenceStart(raw, SRC_WEB_API)) {
            webSendJsonError(req, 503, "sequence queue full");
            return;
        }
    } else if (!domeQueueTx(raw)) {
        webSendJsonError(req, 503, "dome TX queue full or link not ready");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] POST /api/dome/cmd cmd=%s", raw);
    req.send(200, "application/json", "{\"ok\":true}");
}

void handleDomeSpeedPost(WebRequest& req) {
    // Wider than any valid -1.0..1.0 literal, so an over-long value reaches
    // parseDomeSpeedValue() and is rejected instead of being truncated.
    char speedRaw[32] = {};
    if (!req.param("speed", speedRaw, sizeof(speedRaw))) {
        webSendJsonError(req, 400, "missing speed");
        return;
    }

    if (isSleepModeActive()) {
        webSendJsonError(req, 423, "sleeping", "POST /api/wake");
        return;
    }

    float speed = 0.0f;
    if (!parseDomeSpeedValue(speedRaw, &speed)) {
        webSendJsonError(req, 400, "speed must be a float in range -1.0..1.0");
        return;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    if (!cfg.system.enable_dome_esc) {
        webSendJsonError(req, 409, "dome output is disabled");
        return;
    }

    DomeCommand cmd = {};
    cmd.speed = constrain(speed, -1.0f, 1.0f);
    cmd.source = SRC_WEB_API;
    cmd.timestampMs = millis();
    if (xQueueSend(domeCmdQueue, &cmd, 0) != pdTRUE) {
        logQueueDrop(QUEUE_DOME_CMD, "POST /api/dome");
        webSendJsonError(req, 503, "dome command queue full");
        return;
    }

    PA_LOG_INFO(TAG, "[WEB] POST /api/dome speed=%.2f", (double)cmd.speed);
    req.send(200, "application/json", "{\"ok\":true}");
}

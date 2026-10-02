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
//   POST /api/dome/front              - the dome points front now (#445)
//   POST /api/dome/home               - turn the dome to the believed front (#445)
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

#include "config_write_lock.h"  // saveCommandedMode() is the mode save's Write Window
#include "api_helpers.h"
#include "api_json_response.h"
#include "audio_dollar_parser.h"  // audioDollarBankForm()
#include "audio_task.h"
#include "commanded_modes.h"
#include "config_cache.h"
#include "config_store.h"  // saveConfigToNvs()
#include "dome_link.h"
#include "drive_arbiter.h"
#include "drive_speed_preset.h"
#include "failsafe_gate.h"
#include "logging.h"
#include "marcduino_router.h"  // routeMarcduinoLine(), marcduinoForwardToDome()
#include "mood.h"              // applyMood(), moodIdFromSeCommand()
#include "queue_drop_tracker.h"
#include "robot_state.h"
#include "sequence_dispatcher.h"
#include "web_server.h"

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
//
// It is the mode save's Write Window (ADR 0011, amended 2026-09-24): the save
// reads the whole cache and writes it, rows included, so it runs holding the
// config write lock, and POST /api/mode, the manual command paths and the
// Console's drive.action.set-mode all call it rather than taking the lock. A
// lock that cannot be taken is a save that did not happen, and is reported as
// one.
bool saveCommandedMode() {
    ConfigWriteLock lock;
    if (!lock.acquired()) {
        return false;
    }
    return saveConfigToNvs();
}


// A routed Marcduino line's outcome, in this dispatcher's vocabulary. NotRun is
// a line the body owns but cannot run - an :MV with no value - which is what
// Unsupported already answers for a command nothing here will execute.
static ManualCommandResult manualCommandResultFor(MarcduinoRouteOutcome outcome) {
    switch (outcome) {
        case MarcduinoRouteOutcome::Applied:
            return ManualCommandResult::Applied;
        case MarcduinoRouteOutcome::Forwarded:
            return ManualCommandResult::Forwarded;
        case MarcduinoRouteOutcome::DomeLinkDown:
            return ManualCommandResult::DomeLinkDown;
        case MarcduinoRouteOutcome::DomeQueueFull:
            return ManualCommandResult::DomeQueueFull;
        case MarcduinoRouteOutcome::BlockedByEstop:
            return ManualCommandResult::BlockedByEstop;
        case MarcduinoRouteOutcome::OutputUndriven:
            return ManualCommandResult::OutputUndriven;
        case MarcduinoRouteOutcome::QueueFull:
            return ManualCommandResult::QueueFull;
        case MarcduinoRouteOutcome::LineTooLong:
            return ManualCommandResult::LineTooLong;
        case MarcduinoRouteOutcome::NotRun:
            break;
    }
    return ManualCommandResult::Unsupported;
}

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
        // $8nn is bank 8, sound nn. Where the module has no bank 8 it is refused
        // here, so the sender hears why; AudioTask asks the same of the same
        // line from the paths that do not come through this door.
        uint8_t bank = 0;
        uint16_t sound = 0;
        if (audioDollarBankForm(raw, &bank, &sound)) {
            if (sound == 0) {
                return ManualCommandResult::BankSoundMissing;
            }
            switch (audioBankFitted(bank)) {
                case AudioBankFit::Fitted:
                    break;
                case AudioBankFit::NotFitted:
                    return ManualCommandResult::BankNotFitted;
                case AudioBankFit::CatalogBusy:
                    return ManualCommandResult::SoundCatalogBusy;
            }
        }
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

    // : and # - Command Ownership (ADR 0055): the body runs the lines naming
    // things it models and forwards the rest to the dome
    // (include/marcduino_router.h). Prefix no longer decides who answers. A
    // Mood is the body's and is applied here, ahead of the router, which never
    // applies one (its header says why).
    if (prefix == ':' || prefix == '#') {
        const uint8_t moodId = moodIdFromSeCommand(raw);
        if (moodId != 0) {
            applyMood(moodId);
            return ManualCommandResult::Applied;
        }
        return manualCommandResultFor(routeMarcduinoLine(raw));
    }

    // * @ % & ! - dome-bound Marcduino, forwarded uninterpreted (ADR 0045) and
    // answered with what the forward did, never plain success.
    if (prefix == '*' || prefix == '@' || prefix == '%' || prefix == '&' || prefix == '!') {
        return manualCommandResultFor(marcduinoForwardToDome(raw));
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

        // Both estop commands leave the publish to the gate, which asks on the
        // edge (src/failsafe_gate.cpp, #346). MC_ESTOP never asked here at all,
        // so a latch entered this way used to reach no browser; now both do,
        // and neither publishes a command that changed nothing.
        case MC_CLEAR_ESTOP:
            failsafeClearEstop();
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
// over the dome link TX queue (UART2 or WiFi/UDP), bypassing Command Ownership
// (include/marcduino_router.h). Use this for a line the body owns that is meant
// for the dome instead - :OP01 is body arm 1 on the manual-command route and
// dome panel 1 here (ADR 0055).
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
    } else if (strlen(raw) > DOME_TX_LINE_MAX) {
        // domeQueueTx() would queue it cut to DOME_TX_LINE_MAX characters and
        // answer true: a different line from the one sent (#449). The 127 above
        // bounds every cmd; a line forwarded to the dome is bounded by the queue.
        webSendJsonError(req, 400, "cmd too long (max 63)");
        return;
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

DomeBearingActOutcome domeBearingActRequest(DomeBearingAct act, CommandSource source) {
    taskENTER_CRITICAL(&robotStateMux);
    const bool estopLatched = robotState.estop;
    const bool sleepMode = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    DomeConfig dome = {};
    configCacheReadDome(&dome);
    const DomeTurnCalibration cal = {dome.dome_neutral_us,   dome.dome_min_pulse_us,
                                     dome.dome_max_pulse_us, dome.dome_full_turn_ms,
                                     dome.dome_full_turn_pct, dome.dome_positive_turn};

    // The Dome ESC as this boot runs it, not as saved: a switch saved since is
    // staged until the restart (ADR 0027), and DomeTask runs or not by the
    // boot's answer.
    DomeBearingActOutcome outcome = {
        domeBearingActRefusal(act, estopLatched, sleepMode, configCacheReadActiveDomeEnabled(),
                              domeTurnCalibrated(cal), domeBearingRead().believed),
        false};
    if (outcome.refusal != DOME_BEARING_OK) {
        return outcome;
    }

    DomeCommand cmd = {};
    cmd.kind = (act == DOME_BEARING_ACT_FRONT_IS_HERE) ? DOME_CMD_FRONT_IS_HERE : DOME_CMD_TURN_TO;
    cmd.targetTenths = 0;  // home is front
    cmd.source = source;
    cmd.timestampMs = millis();
    outcome.queued = xQueueSend(domeCmdQueue, &cmd, 0) == pdTRUE;
    if (!outcome.queued) {
        logQueueDrop(QUEUE_DOME_CMD, act == DOME_BEARING_ACT_FRONT_IS_HERE ? "dome front" : "dome home");
    }
    return outcome;
}

namespace {

// One answer for both routes: the refusal's clause, under 409 - or 423 for
// Sleep Mode, with the way to wake, as POST /api/dome answers it - and 503 for
// a full queue.
void sendDomeBearingAct(WebRequest& req, DomeBearingAct act, const char* route) {
    const DomeBearingActOutcome outcome = domeBearingActRequest(act, SRC_WEB_API);
    if (outcome.refusal == DOME_BEARING_ASLEEP) {
        webSendJsonError(req, 423, domeBearingRefusalWords(outcome.refusal), "POST /api/wake");
        return;
    }
    if (outcome.refusal != DOME_BEARING_OK) {
        webSendJsonError(req, 409, domeBearingRefusalWords(outcome.refusal));
        return;
    }
    if (!outcome.queued) {
        webSendJsonError(req, 503, "dome command queue full");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] POST %s", route);
    req.send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

// POST /api/dome/front - the builder turned the dome to front, by hand or by
// stick, and says so (ADR 0051, #445). The recovery act: it makes an unknown
// bearing believed again, at 0. Refused under a halt, with the Dome ESC off,
// and before the full turn is timed - with no calibration no turn could be
// followed, so no belief held.
void handleDomeFrontPost(WebRequest& req) {
    sendDomeBearingAct(req, DOME_BEARING_ACT_FRONT_IS_HERE, "/api/dome/front");
}

// POST /api/dome/home - turn the dome the short way to the believed front, at
// the speed its full turn was timed at, and stop on time (#445). The
// end-of-show act, never the recovery one: with the bearing unknown it does not
// move the dome and says so, because a wrong belief would drive it away from
// home with confidence (ADR 0051).
void handleDomeHomePost(WebRequest& req) {
    sendDomeBearingAct(req, DOME_BEARING_ACT_TURN, "/api/dome/home");
}

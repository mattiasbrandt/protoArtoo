// =============================================================================
// include/api_drive.h
//
// Drive, mode, web-control and dome-motion API endpoints, written against the
// project-owned WebRequest seam (ADR 0021) and bound by the seam route table.
// Exposed so native tests can drive them directly through the host-test
// backend.
//
// The dome layout relay lives in api_dome.cpp; the dome endpoints here are the
// motion and command paths, which share this group's safety gating.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "web_request.h"

#include "dome_bearing_act.h"  // DomeBearingAct, DomeBearingRefusal
#include "drive_speed_preset.h"
#include "robot_state.h"       // CommandSource

// Format JSON response for speed preset endpoint.
// Output: {"ok":true,"preset":"slow|normal|turbo","speedLimitMax":<0..600>}
// Returns false if the payload does not fit in buf or preset is invalid.
bool formatSpeedPresetResponseJson(char* buf, size_t bufSize, SpeedPresetId preset,
                                   int16_t speedLimitMax);

void handleModePost(WebRequest& req);
void handleDrivePost(WebRequest& req);
void handleSpeedPresetPost(WebRequest& req);
void handleWebControlEnablePost(WebRequest& req);
void handleWebControlDisablePost(WebRequest& req);
void handleDomeCmdPost(WebRequest& req);
void handleDomeSpeedPost(WebRequest& req);
void handleDomeFrontPost(WebRequest& req);
void handleDomeHomePost(WebRequest& req);

// Front is here or go home, asked for by `source` (#445): the one way either
// press reaches DomeTask, for the web routes above and the Console's two
// actions alike. Decides with domeBearingActRefusal()
// (include/dome_bearing_act.h) from the droid as it is now, and only on OK
// sends the command, without waiting. `queued` false with OK is a full queue.
struct DomeBearingActOutcome {
    DomeBearingRefusal refusal;
    bool queued;
};
DomeBearingActOutcome domeBearingActRequest(DomeBearingAct act, CommandSource source);

// What executeManualCommand() did with a command, which is four things and
// not two (#376, #379).
//
// It was a bool -- recognized or not -- and the branches that persist the
// commanded mode had no way to say that the persisting failed, so a mode
// change whose NVS write never landed was indistinguishable from one that
// stored and every caller answered success for both. Unsupported and
// SaveFailed are both "do not answer ok", but they are not the same answer:
// one is the operator's command being wrong, the other is the flash being
// full.
//
// SaveFailed means the command DID take effect on the droid; only the store
// missed. Reverting instead is the other option in this tree, and
// saveCommandedMode() (src/web/api_drive.cpp) says why the mode paths do not
// take it.
//
// ShadowedModeKeyword is the answer #379 settled on for "#st"/"#sm". Both
// resolve to a mode branch that the Marcduino prefix routing has claimed since
// the day after they landed, so neither ever ran and the caller was told
// {"ok":true} for a mode change that never happened. They are refused up front
// instead of being moved in front of the routing: POST /api/mode already does
// this job, so a second door into the same room is not worth the Marcduino
// namespace it costs. Every caller must name the working route when it answers
// this one -- a refusal that does not say what to do instead is the failure
// #348 D1 closed.
//
// No caller can observe SaveFailed today, and that is the same shadowing: the
// only two branches that produce it are those mode keywords, which the guard
// above them now refuses. Callers keep handling it because the two arms stay
// (they are the evidence of what the refusal refuses) and because the answer
// is then already right rather than discarded -- see the comment on them in
// src/web/api_drive.cpp.
//
// The rest are the answers Command Ownership gives a Marcduino line (ADR 0055,
// #449; include/marcduino_router.h). Forwarded is its own answer and never
// Applied: the line was handed to the dome and the body cannot say what the
// dome did with it. Each "no" names what stopped it, so a caller can say why.
enum class ManualCommandResult : uint8_t {
    Unsupported,          // not a command this dispatcher owns -- nothing happened
    Applied,              // executed; either nothing to persist, or the save landed
    SaveFailed,           // executed, but the config save did not reach flash
    ShadowedModeKeyword,  // "#st"/"#sm": refused, because the Marcduino '#'
                          // routing claims the line and no mode can change
    Forwarded,            // queued for the dome verbatim; what it does there is the dome's
    DomeLinkDown,         // for the dome, and protoR2link is not connected: not queued
    DomeQueueFull,        // for the dome, and the dome TX queue refused it: not queued
    BlockedByEstop,       // the body's, refused while estop is latched
    OutputUndriven,       // the body's, and nothing drives the Output it names this boot
                          // (servoOutputUndriven(), include/api_servo.h, says why)
    QueueFull,            // the body's, and the queue that runs it refused it
    BankNotFitted,        // "$8nn": bank 8, sound nn (ShadowMD's $Bnn), and the fitted
                          // sound module has no bank 8 -- refused, never raw track 8nn
    BankSoundMissing,     // "$800": the bank form naming sound 00, which no bank has
    SoundCatalogBusy,     // "$8nn" while a catalog refresh holds the bank table
    LineTooLong,          // longer than the dome TX buffer holds (DOME_TX_LINE_MAX):
                          // refused before any of it ran, never forwarded cut short
};

// Execute one manual command: a Marcduino line, or one of the keyword commands
// (estop, reboot, ...). Answers Unsupported for an unrecognized keyword.
//
// A ':' or '#' line goes where Command Ownership sends it: the body runs the
// lines naming things it models and forwards the rest to the dome, and a
// full-droid sequence (:SE01-:SE09, :SE15, :SE16) does both
// (include/marcduino_router.h). The raw families '*', '@', '%', '&' and '!' are
// forwarded uninterpreted (ADR 0045). Neither kind is answered Applied unless
// the body acted; a forward that could not be queued is not answered success.
// "#st"/"#sm" look like keywords but are refused ahead of all of it --
// ShadowedModeKeyword, nothing executed.
//
// Takes a plain C string rather than an Arduino String so the one cross-file
// caller (POST /api/manual-command, api_system.cpp) can hand over a borrowed
// seam parameter without a copy.
ManualCommandResult executeManualCommand(const char* raw);

// The mode save's Write Window (ADR 0011, amended 2026-09-24): store the
// commanded mode the droid is already in and say whether it reached flash.
// False covers both a busy config write lock and a failed NVS write; either
// way the mode stays applied and the caller reports the save as failed. Every
// caller that stores a commanded mode - POST /api/mode, the manual command
// paths and the Console's drive.action.set-mode - calls this and holds no lock
// of its own. The decision behind reporting rather than reverting is on the
// definition (src/web/api_drive.cpp).
bool saveCommandedMode();

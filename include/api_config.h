// =============================================================================
// include/api_config.h
//
// Config, RC-map and WiFi API endpoints, written against the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table. Exposed so
// native tests can drive them directly through the host-test backend.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api_config_apply.h"
#include "config_store.h"
#include "robot_state.h"  // CommandSource
#include "web_request.h"

// Write a JSON config object into a caller-supplied buffer.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: buf               - output buffer (must not be null)
//         bufSize           - size of buf in bytes
//         speedLimitMax     - current speed limit cap
//         webDriveTimeoutMs - current web drive timeout in ms
// thread-safe: yes (pure function, no globals)
void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax, uint32_t webDriveTimeoutMs);

// Commit Step for the POST /api/config Apply Core (ADR 0036): the complete
// transport-independent tail of a config write - replay the core's applied-
// field log lines, sync the config cache, resync stationary mode (with its
// existing edge-detect/drive-on-cue logic, ADR 0012), fire the dome-on-cue
// action, persist to NVS, and (on success) broadcast the new status. This is
// "persistSystemConfig(WebRequest&, ...)"'s sibling named in ADR 0036's
// Consequences as the first extraction target. configWriteWindow() below runs
// it for both adapters - the HTTP handler (handleConfigPost) and the
// Controller Console (src/console/console_module.cpp) - so neither carries its
// own copy of the sequence, and it runs holding the config write lock.
// `working` must already hold configApply()'s output;
// `source` is forwarded to commandedSetStationary() for its Command Source
// provenance (#221) - the web route always passes SRC_WEB_API, the Console
// passes SRC_SERIAL_CONSOLE/SRC_WEB_CONSOLE.
//
// The outcome is the plain verdict only. The post-commit snapshot is written
// back through `working`, which is how identitySetCommitApplied()
// (include/api_identity.h) and the audio Commit Steps (include/api_audio.h)
// already hand a snapshot back, and what ADR 0011's 2026-09-04 amendment
// settles for this one: the Apply Core contract is the response bytes and the
// plain outcome, not the calling convention.
//
// It used to carry a whole ConfigSnapshot under a comment calling the struct
// small. ConfigSnapshot measured 944 B then (916 B today, static_assert in
// config_store.h), so that one by-value crossing put ~1892 B of snapshot
// copies on the serial config-write path and helped overflow the Console task
// on both chips (#226). `working` already holds a snapshot the caller owns; writing the
// post-commit state back into it costs no second copy.
struct ConfigCommitOutcome {
    bool persisted = false;  // false -> caller reports "failed to persist config"
    // Non-null when the request carried a Part move the live table refused - the
    // Part is not on the Output the move named, the destination is full, or no
    // row is addressed there (servoOutputTableMovePart()). Nothing in the request
    // was applied and nothing was persisted; the caller answers 409 with this
    // sentence, and `working` is not the committed state so is not rendered.
    // Only a request naming movePart can be refused, and the Console's scalar
    // config adapter carries exactly one field that is never movePart, so today
    // the REST route is the one caller that meets it.
    const char* refusal = nullptr;
    // Rows whose open end, close end or stated centre the component band moved
    // on the way in, by row index (ServoOutputRepairReport::openMovedRows). The
    // clamp is deliberate (#286); what the REST answer owes is saying so, which
    // sendConfigSnapshot() does as `clamped` (#417, ADR 0068).
    uint32_t openClampedRows = 0;
    uint32_t closeClampedRows = 0;
    uint32_t centreClampedRows = 0;
};

// Write Window for a config write (ADR 0011, amended 2026-09-24; CONTEXT.md
// "Write Window"): the one guarded span of POST /api/config and the Console's
// scalar config write alike - take the config write lock
// (include/config_write_lock.h), read the cache into `*working`, run
// configApply(), and when that carries no error run configCommitApplied(),
// then release. Neither adapter holds the lock; both call this, and render
// their answer after it returns, outside the window.
//
// Busy: another Write Window held the lock past its bound. Nothing was read,
// applied or committed, and `*result`, `*commit` and `*working` are
// untouched; the REST route answers 503 and the Console
// temporarily-unavailable.
// Refused: configApply() refused the request; `*result` says why and nothing
// was committed. `*refused`, when the caller passes one, holds a copy of the
// refusal's field, reason and accepts, taken inside the window.
// Committed: `*commit` holds the Commit Step's outcome and `*working` the
// post-commit snapshot.
//
// Three answers rather than a bool, because the verdict has to be taken
// inside the window: the Console passes a ConfigApplyResult its two adapters
// share (2,060 B on artoo-esp32, too big for either task's stack), and the
// other adapter may overwrite it the moment the lock is released. For the
// same reason the Console reads why a write was refused from `*refused` (81 B,
// on its own stack) rather than from `*result`.
//
// The Working Snapshot is the caller's (916 B), as it was when each adapter
// held the lock itself, so no adapter's stack moves for this.
enum class ConfigWriteWindowAnswer : uint8_t { Busy, Refused, Committed };
ConfigWriteWindowAnswer configWriteWindow(const ConfigParamSource& params, ConfigSnapshot* working,
                                          ConfigApplyResult* result, CommandSource source,
                                          ConfigCommitOutcome* commit,
                                          ApplyRefusal* refused = nullptr);

// On return `*working` holds the post-apply, post-cache-resync snapshot - the
// bytes the REST handler renders - whether or not persistence succeeded.
ConfigCommitOutcome configCommitApplied(ConfigSnapshot* working, const ConfigApplyResult& result,
                                         CommandSource source);

// The largest body POST /api/config buffers. A restore posts a whole
// Configuration back in the shape it was read (ADR 0068): GET /api/config,
// measured at about 3.4 KB at its worst (test_api_config_get), with an `outputs`
// row set beside it. The five rows this controller drives add about 2 KB as
// GET /api/servo/outputs reads them; a full expander's twenty-four, sent as
// the rows a restore posts - the settings, without the readings - about 6 KB.
// 12 KB holds that with headroom, and is the ceiling the server already
// buffers for POST /api/seq on this chip, so no route's allocation grows for it.
constexpr size_t kConfigPostMaxBodyBytes = 12288;

void handleConfigGet(WebRequest& req);
void handleConfigPost(WebRequest& req);
void handleServoOutputsGet(WebRequest& req);
void handleRcMapGet(WebRequest& req);
void handleRcMapPost(WebRequest& req);
void handleWifiPost(WebRequest& req);

// =============================================================================
// src/web/web_seam_routes.cpp
//
// The route table for everything already ported to the WebRequest seam
// (ADR 0021). Both device backends call webRegisterSeamRoutes() from their own
// bring-up, so a ported route cannot be present on one stack and missing on
// the other -- which is what per-backend registration lists invite.
//
// While the migration is in flight this is a partial table: routes not listed
// here are still served by the async stack's own registration block in
// web_server.cpp. The cutover deletes that block and leaves this as the whole
// route table. See docs/adr/0021-project-owned-web-request-seam.md.
// =============================================================================

#include "../../include/api_actions.h"
#include "../../include/api_aux_led.h"
#include "../../include/api_config.h"
#include "../../include/api_dome.h"
#include "../../include/api_drive.h"
#include "../../include/api_estop.h"
#include "../../include/api_identity.h"
#include "../../include/api_logs.h"
#include "../../include/api_servo.h"
#include "../../include/api_status.h"
#include "../../include/api_system.h"
#include "../../include/api_upload.h"
#include "../../include/web_request.h"

void webRegisterSeamRoutes() {
    webRegisterRoute("/api/identity", WebMethod::kGet, handleIdentityGet);
    webRegisterRoute("/api/identity", WebMethod::kPost, handleIdentityPost);

    // The three routes data/app.js and data/shell.js fetch on every page load.
    // Until the remaining groups are ported, these plus static serving are what
    // a page load needs from the psychic stack.
    webRegisterRoute("/api/config", WebMethod::kGet, handleConfigGet);
    webRegisterRoute("/api/actions", WebMethod::kGet, handleActionsGet);
    webRegisterRoute("/api/logs", WebMethod::kGet, handleLogsGet);

    // Ported ahead of the rest of its route group: the admission counters and
    // heap readings in this payload are what the load harness polls and what
    // the migration's comparison run is scored against, so the guard's own
    // evidence is unobservable without it.
    webRegisterRoute("/api/status", WebMethod::kGet, handleStatusGet);

    // Config, RC-map and WiFi writes. Their decision logic stays in the
    // ADR 0011 apply cores; these routes only carry values across.
    webRegisterRoute("/api/config", WebMethod::kPost, handleConfigPost);
    webRegisterRoute("/api/rc/map", WebMethod::kGet, handleRcMapGet);
    webRegisterRoute("/api/rc/map", WebMethod::kPost, handleRcMapPost);
    webRegisterRoute("/api/wifi", WebMethod::kGet, handleWifiGet);
    webRegisterRoute("/api/wifi", WebMethod::kPost, handleWifiPost);

    // System control.
    webRegisterRoute("/api/sleep", WebMethod::kPost, handleSleepPost);
    webRegisterRoute("/api/wake", WebMethod::kPost, handleWakePost);
    webRegisterRoute("/api/manual-command", WebMethod::kPost, handleManualCommandPost);
    webRegisterRoute("/api/reboot", WebMethod::kPost, handleRebootPost);
    webRegisterRoute("/api/coredump/status", WebMethod::kGet, handleCoredumpStatusGet);
    webRegisterRoute("/api/coredump", WebMethod::kGet, handleCoredumpGet);
    webRegisterRoute("/api/coredump/erase", WebMethod::kPost, handleCoredumpErasePost);

    // Motion and safety. The estop pair goes first because it is the pair the
    // admission policy exempts by path (webPathIsEstop()), and because the
    // dashboard's E-Stop button is inert on any build where it is missing.
    webRegisterRoute("/api/estop", WebMethod::kPost, handleEstopPost);
    webRegisterRoute("/api/estop/clear", WebMethod::kPost, handleEstopClearPost);

    webRegisterRoute("/api/mode", WebMethod::kPost, handleModePost);
    webRegisterRoute("/api/drive", WebMethod::kPost, handleDrivePost);
    webRegisterRoute("/api/drive/speed-preset", WebMethod::kPost, handleSpeedPresetPost);
    webRegisterRoute("/api/web-control/enable", WebMethod::kPost, handleWebControlEnablePost);
    webRegisterRoute("/api/web-control/disable", WebMethod::kPost, handleWebControlDisablePost);

    webRegisterRoute("/api/dome", WebMethod::kPost, handleDomeSpeedPost);
    webRegisterRoute("/api/dome/cmd", WebMethod::kPost, handleDomeCmdPost);
    webRegisterRoute("/api/dome/layout", WebMethod::kGet, handleDomeLayoutGet);

    webRegisterRoute("/api/servo", WebMethod::kPost, handleServoPost);

    webRegisterRoute("/api/aux-led/color", WebMethod::kPost, handleAuxLedColorPost);
    webRegisterRoute("/api/aux-led/effect", WebMethod::kPost, handleAuxLedEffectPost);

    // Streaming OTA uploads, ported early on purpose: with these working on
    // the psychic build, later work reflashes over the air instead of needing
    // the controller pulled for a serial flash.
    webRegisterUploadRoute("/upload/firmware", handleFirmwareUploadChunk,
                           handleFirmwareUploadDone);
    webRegisterUploadRoute("/upload/filesystem", handleFilesystemUploadChunk,
                           handleFilesystemUploadDone);
}

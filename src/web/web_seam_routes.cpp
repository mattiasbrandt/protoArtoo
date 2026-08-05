// =============================================================================
// src/web/web_seam_routes.cpp
//
// The route table for everything already ported to the WebRequest seam
// (ADR 0021). Both device backends call webRegisterSeamRoutes() from their own
// bring-up, so a ported route cannot be present on one stack and missing on
// the other -- which is what per-backend registration lists invite.
//
// While epic #75 is in flight this is a partial table: routes not listed here
// are still served by the async stack's own registration block in
// web_server.cpp. The #91 cutover deletes that block and leaves this as the
// whole route table.
// =============================================================================

#include "../../include/api_actions.h"
#include "../../include/api_config.h"
#include "../../include/api_identity.h"
#include "../../include/api_logs.h"
#include "../../include/api_upload.h"
#include "../../include/web_request.h"

void webRegisterSeamRoutes() {
    webRegisterRoute("/api/identity", WebMethod::kGet, handleIdentityGet);
    webRegisterRoute("/api/identity", WebMethod::kPost, handleIdentityPost);

    // The three routes data/app.js and data/shell.js fetch on every page load
    // (#79). Until the rest of the groups land (#86-#90) these plus static
    // serving are what a page load needs from the psychic stack.
    webRegisterRoute("/api/config", WebMethod::kGet, handleConfigGet);
    webRegisterRoute("/api/actions", WebMethod::kGet, handleActionsGet);
    webRegisterRoute("/api/logs", WebMethod::kGet, handleLogsGet);

    // Streaming OTA uploads (#80). Ported early in the epic on purpose: with
    // these working on the psychic build, later slices reflash over the air
    // instead of needing the controller pulled for a serial flash.
    webRegisterUploadRoute("/upload/firmware", handleFirmwareUploadChunk,
                           handleFirmwareUploadDone);
    webRegisterUploadRoute("/upload/filesystem", handleFilesystemUploadChunk,
                           handleFilesystemUploadDone);
}

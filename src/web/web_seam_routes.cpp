// =============================================================================
// src/web/web_seam_routes.cpp
//
// The whole route table, against the WebRequest seam (ADR 0021). The server
// bring-up calls webRegisterSeamRoutes() once; nothing registers routes
// anywhere else. See docs/adr/0021-project-owned-web-request-seam.md.
// =============================================================================

#include "../../include/api_actions.h"
#include "../../include/api_audio.h"
#include "../../include/api_aux_led.h"
#include "../../include/api_config.h"
#include "../../include/api_dome.h"
#include "../../include/api_drive.h"
#include "../../include/api_estop.h"
#include "../../include/api_identity.h"
#include "../../include/api_logs.h"
#include "../../include/api_profiler.h"
#include "../../include/api_rc.h"
#include "../../include/api_seq.h"
#include "../../include/api_servo.h"
#include "../../include/api_status.h"
#include "../../include/api_system.h"
#include "../../include/api_upload.h"
#include "../../include/api_validation.h"
#include "../../include/seq_store_util.h"  // SEQ_FILE_MAX_BYTES
#include "../../include/web_request.h"

void webRegisterSeamRoutes() {
    webRegisterRoute("/api/identity", WebMethod::kGet, handleIdentityGet);
    webRegisterRoute("/api/identity", WebMethod::kPost, handleIdentityPost);

    // The routes data/app.js and data/shell.js fetch on every page load.
    webRegisterRoute("/api/config", WebMethod::kGet, handleConfigGet);
    webRegisterRoute("/api/actions", WebMethod::kGet, handleActionsGet);
    webRegisterRoute("/api/actions/test", WebMethod::kPost, handleActionsTestPost);
    webRegisterRoute("/api/logs", WebMethod::kGet, handleLogsGet);

    // The admission counters and heap readings in this payload are what the
    // load harness polls and what a comparison run is scored against, so the
    // guard's own evidence is unobservable without it.
    webRegisterRoute("/api/status", WebMethod::kGet, handleStatusGet);

    // The rest of the status/telemetry group, which followed /api/status.
    webRegisterRoute("/api/health", WebMethod::kGet, handleHealthGet);
    webRegisterRoute("/api/serial", WebMethod::kGet, handleSerialGet);

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

    // RC diagnostics and the validation snapshot. Both read-only payloads come
    // straight from their snapshot cores; /api/rc/map above is the write half of
    // this group and was ported with the config routes.
    webRegisterRoute("/api/rc", WebMethod::kGet, handleRcGet);
    webRegisterRoute("/api/rc/debug", WebMethod::kPost, handleRcDebugPost);
    webRegisterRoute("/api/validation", WebMethod::kGet, handleValidationGet);

    // Audio. The read and control surface, the two track-assignment routes and
    // the mood map, all reusing the ADR 0011 apply cores and the ADR 0013 config
    // map. /api/audio/tracks is registered ahead of /api/audio because the async
    // backend matches in registration order, and the shorter path would
    // otherwise swallow requests for the longer one.
    webRegisterRoute("/api/audio/tracks", WebMethod::kGet, handleAudioTracksGet);
    webRegisterRoute("/api/audio/tracks", WebMethod::kPost, handleAudioTracksPost);
    webRegisterRoute("/api/audio/category-range", WebMethod::kPost,
                     handleAudioCategoryRangePost);
    webRegisterRoute("/api/audio/mood-map", WebMethod::kGet, handleAudioMoodMapGet);
    webRegisterRoute("/api/audio/mood-map", WebMethod::kPost, handleAudioMoodMapPost);
    webRegisterRoute("/api/audio/catalog", WebMethod::kGet, handleAudioCatalogGet);
    webRegisterRoute("/api/audio/catalog/refresh", WebMethod::kPost,
                     handleAudioCatalogRefreshPost);
    webRegisterRoute("/api/audio/query", WebMethod::kPost, handleAudioQueryPost);
    webRegisterRoute("/api/audio/play-banked", WebMethod::kPost, handleAudioPlayBankedPost);
    webRegisterRoute("/api/audio", WebMethod::kGet, handleAudioGet);
    webRegisterRoute("/api/audio", WebMethod::kPost, handleAudioPost);

    // Mood presets. index.html fetches this one on every load.
    webRegisterRoute("/api/mood", WebMethod::kPost, handleMoodPost);

    // Learned Sequences. POST /api/seq names its own body bound because a
    // saved sequence runs to SEQ_FILE_MAX_BYTES, three times what the default
    // allows -- and the store enforces that same number, so the route and the
    // thing it writes to agree on one limit.
    webRegisterRoute("/api/seq/list", WebMethod::kGet, handleSeqListGet);
    webRegisterRoute("/api/seq/builtins", WebMethod::kGet, handleSeqBuiltinsGet);
    webRegisterRoute("/api/seq/test", WebMethod::kPost, handleSeqTestPost);
    webRegisterRoute("/api/seq/stop", WebMethod::kPost, handleSeqStopPost);
    webRegisterRoute("/api/seq/last-run", WebMethod::kGet, handleSeqLastRunGet);
    webRegisterRoute("/api/seq", WebMethod::kGet, handleSeqGet);
    webRegisterRoute("/api/seq", WebMethod::kPost, handleSeqPost, SEQ_FILE_MAX_BYTES);
    webRegisterRoute("/api/seq", WebMethod::kDelete, handleSeqDelete);

#if PA_HEAP_PROFILE
    // Absent entirely on builds without the profiler, which is what setup.js
    // probes for: it shows the profiler panel only if this route answers 200.
    webRegisterRoute("/api/profiler", WebMethod::kGet, handleProfilerGet);
#ifdef CONFIG_HEAP_TRACING
    webRegisterRoute("/api/profiler/trace/start", WebMethod::kPost,
                     handleProfilerTraceStartPost);
    webRegisterRoute("/api/profiler/trace/stop", WebMethod::kPost, handleProfilerTraceStopPost);
#endif
#endif

    // Streaming OTA uploads, ported early on purpose: with these working on
    // the psychic build, later work reflashes over the air instead of needing
    // the controller pulled for a serial flash.
    webRegisterUploadRoute("/upload/firmware", handleFirmwareUploadChunk,
                           handleFirmwareUploadDone);
    webRegisterUploadRoute("/upload/filesystem", handleFilesystemUploadChunk,
                           handleFilesystemUploadDone);
}

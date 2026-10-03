// =============================================================================
// src/web/api_take.cpp
//
// Takes: performing on the sticks and keeping it (#442, ADR 0061). The route
// list is include/api_take.h. Transport only: the capture is src/take.cpp, the
// files src/take_store.cpp.
// =============================================================================

#include "api_take.h"

#include <ArduinoJson.h>
#include <string.h>

#include "api_json_response.h"
#include "logging.h"
#include "marcduino_ownership.h"  // marcduinoCommandOwner() - who a cue's line is for
#include "rc_action_types.h"   // robotActionIdToString()
#include "take.h"
#include "take_store.h"
#include "take_store_util.h"

static const char* TAG = "APITAKE";

namespace {

// Response ceilings. The status is a few hundred bytes with eleven Part ids;
// the receipt carries every cue, each at most its action token, a 15-byte
// payload JSON-escaped to twice that, the time and the keys - about 110 B, so
// 24 of them and the take's Parts fit in 4 KB.
constexpr size_t kTakeStatusMaxBytes = 1536;
constexpr size_t kTakeKeepMaxBytes = 4096;
constexpr size_t kTakeErrorMaxBytes = 384;

void sendError(WebRequest& req, int code, const char* message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    webSendJsonDocument(req, doc, kTakeErrorMaxBytes, TAG, code);
}

const char* stageWord(TakeStage stage) {
    switch (stage) {
        case TAKE_STAGE_PERFORMING:
            return "performing";
        case TAKE_STAGE_STOPPED:
        case TAKE_STAGE_HELD:
            return "stopped";
        case TAKE_STAGE_IDLE:
        default:
            return "idle";
    }
}

// Who answers the Marcduino line a cue sends (Command Ownership, ADR 0055), or
// nullptr for a cue that sends none. The page places a cue as a dome step only
// where the dome is who answers it: a sequence's dome step reaches the dome
// and nothing else, so a body-owned line placed there would move a different
// thing - :OP01 opening a dome panel, not the body's first Output.
const char* cueOwnerWord(const TakeCue& cue) {
    char line[24];
    if (cue.action == DOME_ACTION_MARCDUINO_SEQ) {
        snprintf(line, sizeof(line), ":SE%s", cue.payload);
    } else if (cue.action == DOME_ACTION_MARCDUINO_CMD && (cue.payload[0] == ':' || cue.payload[0] == '#')) {
        snprintf(line, sizeof(line), "%s", cue.payload);
    } else {
        return nullptr;
    }
    switch (marcduinoCommandOwner(line)) {
        case MarcduinoOwner::Body:
            return "body";
        case MarcduinoOwner::BodyAndDome:
            return "both";
        case MarcduinoOwner::Dome:
        default:
            return "dome";
    }
}

const char* stopWord(TakeStop why) {
    switch (why) {
        case TAKE_STOP_KEEP:
            return "kept";
        case TAKE_STOP_FULL:
            return "full";
        case TAKE_STOP_ESTOP:
            return "estop";
        case TAKE_STOP_NONE:
        default:
            return nullptr;
    }
}

// The take file a GET is serving: the filler takes no context, and the server
// dispatches one request at a time (the same shape as GET /api/seq).
char s_fileOwner[17] = {};
char s_fileTake[TAKE_ID_LEN + 1] = {};

size_t takeFileFiller(uint8_t* out, size_t capacity, size_t offset) {
    return takeStoreReadSlice(s_fileOwner, s_fileTake, offset, out, capacity);
}

// Whether a restore's first piece arrived. Its refusal, if it has one, is
// carried by the store to the finish (takeStoreRestoreFinish()).
bool s_uploadBegun = false;

}  // namespace

// GET /api/take - the take in hand, and the figures the droid keeps takes by:
// how many, how big, and the free space a write leaves (take_store_util.h).
void handleTakeGet(WebRequest& req) {
    TakeStatus st;
    takeStatusRead(&st);
    JsonDocument doc;
    doc["state"] = stageWord(st.stage);
    if (const char* why = stopWord(st.why)) doc["stopped"] = why;
    if (st.stage != TAKE_STAGE_IDLE) {
        doc["seq"] = st.seqName;
        doc["elapsedMs"] = st.elapsedMs;
        doc["fill"] = st.fill;
        doc["nearlyFull"] = st.fill >= TAKE_NEARLY_FULL_PERMILLE;
        doc["samples"] = st.samples;
        doc["cues"] = st.cues;
        JsonArray parts = doc["parts"].to<JsonArray>();
        for (uint8_t i = 0; i < st.partCount; ++i) parts.add(st.parts[i]);
    }
    JsonObject store = doc["store"].to<JsonObject>();
    store["cap"] = TAKE_STORE_CAP;
    store["held"] = takeStoreHeld();
    store["maxBytes"] = TAKE_FILE_MAX_BYTES;
    store["floorBytes"] = TAKE_FS_FREE_FLOOR;
    store["rateHz"] = TAKE_RATE_HZ;
    webSendJsonDocument(req, doc, kTakeStatusMaxBytes, TAG);
}

// POST /api/take/arm?seq= - arm a take for a saved sequence. 409 with the
// reason when it cannot be: no string, no frames, the estop, a take running,
// a sequence not saved, no room, no memory. The name rides in the query, so
// the route parses no JSON body.
void handleTakeArmPost(WebRequest& req) {
    char seq[64] = {};
    if (!req.param("seq", seq, sizeof(seq)) || strncmp(seq, "DM:", 3) != 0) {
        sendError(req, 400, "missing or invalid DM:* seq");
        return;
    }
    char refusal[160];
    if (takeArm(seq, refusal, sizeof(refusal)) != nullptr) {
        sendError(req, 409, refusal);
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] take armed for %s", seq);
    req.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/take/keep - stop the take in hand and keep it. The answer is the
// receipt: the take object it produced (none when nothing moved), and each cue
// pressed during it with its time, for the Sequences page to place as steps.
void handleTakeKeepPost(WebRequest& req) {
    char refusal[160];
    TakeKeep keep;
    if (takeKeepBegin(&keep, refusal, sizeof(refusal)) != nullptr) {
        sendError(req, 409, refusal);
        return;
    }
    const TakeCapture& c = *keep.capture;
    char takeId[TAKE_ID_LEN + 1] = {};
    char replaced[TAKE_ID_LEN + 1] = {};
    if (c.sampleCount > 0 &&
        takeStoreWriteNew(keep.ownerId, c, takeId, replaced, refusal, sizeof(refusal)) != nullptr) {
        // Back to STOPPED: the performance is not lost, and keeping it can be
        // asked again once there is room.
        takeKeepEnd(false);
        sendError(req, 409, refusal);
        return;
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["seq"] = keep.seqName;
    if (const char* why = stopWord(keep.why)) doc["stopped"] = why;
    if (takeId[0] != '\0') {
        JsonObject take = doc["take"].to<JsonObject>();
        take["id"] = takeId;
        take["rateHz"] = TAKE_RATE_HZ;
        take["lengthMs"] = takeTicksToMs(c.lengthTicks, TAKE_RATE_HZ);
        take["samples"] = c.sampleCount;
        JsonArray parts = take["parts"].to<JsonArray>();
        for (uint8_t i = 0; i < c.partCount; ++i) parts.add(c.parts[i]);
    } else {
        doc["take"] = nullptr;
    }
    if (replaced[0] != '\0') doc["replaced"] = replaced;
    JsonArray cues = doc["cues"].to<JsonArray>();
    for (uint8_t i = 0; i < c.cueCount; ++i) {
        JsonObject cue = cues.add<JsonObject>();
        cue["t"] = c.cues[i].ms;
        cue["action"] = robotActionIdToString(c.cues[i].action);
        cue["payload"] = c.cues[i].payload;
        if (const char* owner = cueOwnerWord(c.cues[i])) cue["owner"] = owner;
    }
    doc["cuesPast"] = c.cuesPast;
    webSendJsonDocument(req, doc, kTakeKeepMaxBytes, TAG);
    takeKeepEnd(true);
}

// GET /api/take/file?owner=&take= - one take file as it is stored, for the
// backup to carry: `owner` is its sequence's stable id.
void handleTakeFileGet(WebRequest& req) {
    char owner[24] = {};
    char take[16] = {};
    if (!req.param("owner", owner, sizeof(owner)) || !req.param("take", take, sizeof(take)) ||
        strlen(owner) >= sizeof(s_fileOwner) || !takeIdValid(take)) {
        sendError(req, 400, "missing or invalid owner or take");
        return;
    }
    if (!takeStoreHas(owner, take)) {
        sendError(req, 404, "not found");
        return;
    }
    memcpy(s_fileOwner, owner, sizeof(s_fileOwner));
    s_fileOwner[sizeof(s_fileOwner) - 1] = '\0';
    memcpy(s_fileTake, take, sizeof(s_fileTake));
    s_fileTake[sizeof(s_fileTake) - 1] = '\0';
    if (!req.sendChunked("application/octet-stream", takeFileFiller)) {
        sendError(req, 500, "read failed");
    }
}

// POST /api/take/file - a take file put back by a backup's restore, uploaded
// as the file "<owner>.<take>.take" (take_store_util.h). Its sequence must
// already be on the droid; the file is checked as it arrives.
void handleTakeFileUploadChunk(WebRequest& /*req*/, const char* filename, size_t index,
                               const uint8_t* data, size_t len, bool /*final*/) {
    if (index == 0) {
        s_uploadBegun = true;
        char refusal[128];
        takeStoreRestoreBegin(filename, refusal, sizeof(refusal));
    }
    if (s_uploadBegun) {
        takeStoreRestoreAppend(data, len);
    }
}

void handleTakeFileUploadDone(WebRequest& req) {
    const bool begun = s_uploadBegun;
    s_uploadBegun = false;
    char refusal[128];
    if (!begun) {
        sendError(req, 400, "no take file arrived");
        return;
    }
    // A refused begin finishes too: its temporary file and its memory go, and
    // the finish answers with the begin's reason.
    const char* why = takeStoreRestoreFinish(refusal, sizeof(refusal));
    if (why != nullptr) {
        sendError(req, 409, why);
        return;
    }
    req.send(200, "application/json", "{\"ok\":true}");
}

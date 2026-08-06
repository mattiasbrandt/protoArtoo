// =============================================================================
// src/native_test_stubs.cpp
//
// Stub definitions for extern symbols referenced by native-build source files
// (src/web/api_config.cpp, src/drivers/audio_chirp.cpp, etc.) that depend on
// hardware or task-level facilities not available in [env:native].
//
// This file is only listed in [env:native] build_src_filter.
// It is never compiled for device (ESP32) firmware builds.
// =============================================================================
#ifdef PA_NATIVE_TEST_STUBS

#include "Arduino.h"      // SerialStub (from test/stubs/include)
#include "logging.h"      // logging sink declarations
#include "robot_state.h"  // RobotState, portMUX_TYPE, QueueHandle_t

// Zero-initialised global state. Test cases populate cfg_* fields as needed
// before calling captureConfigSnapshot() or populateConfigJson().
RobotState robotState = {};
portMUX_TYPE robotStateMux = 0;

// Arduino Serial instance (referenced by code compiled in native tests)
SerialStub Serial;

// Logging sinks — no-op in native test builds
void paLogInit() {
}

void paLogLine(const char* /*line*/) {
}

void paLogLineRaw(const char* /*line*/) {
}

// millis() stub — used by the failsafe gate for diagnostics, and by anything
// that timestamps work it hands to another task.
//
// Settable, defaulting to 0 so every existing test sees the frozen clock it
// was written against. A test that needs a non-zero timestamp sets this: the
// drive arbiter treats timestamp 0 as "never submitted", so a handler's
// submission is indistinguishable from no submission while the clock reads 0.
unsigned long g_test_millis = 0;

unsigned long millis() {
    return g_test_millis;
}

// NVS save stub — not under test; POST handler calls it but tests call
// populateConfigJson() directly without going through registerConfigRoutes().
bool saveConfigToNvs() {
    return true;
}

// dome_link.cpp is excluded from the native build. Provide a controllable stub
// so audio_chirp.cpp's UART2 ownership guard can be exercised in tests.
// Default: DOME_UART_NONE (no owner). Tests set g_test_dome_uart_owner in
// setUp() and reset it in tearDown().
#include "dome_link.h"
DomeUartOwner g_test_dome_uart_owner = DOME_UART_NONE;
bool domeUartOwnedBy(DomeUartOwner owner) {
    return g_test_dome_uart_owner == owner;
}
bool domeUartAcquire(DomeUartOwner requester) {
    if (requester == DOME_UART_NONE) {
        return false;
    }
    if (g_test_dome_uart_owner != DOME_UART_NONE && g_test_dome_uart_owner != requester) {
        return false;
    }
    g_test_dome_uart_owner = requester;
    return true;
}
void domeUartRelease(DomeUartOwner requester) {
    if (g_test_dome_uart_owner == requester) {
        g_test_dome_uart_owner = DOME_UART_NONE;
    }
}
bool domeConnected() { return true; }

// sequence_dispatcher.cpp needs domeQueueTx and audioQueueDollar.
// No-op stubs: routing tests use sequenceLookup() directly and do not need
// side-effect capture from these functions.
bool domeQueueTx(const char* /*cmd*/) { return true; }

#include "audio_task.h"
bool audioQueueDollar(const char* /*cmd*/, CommandSource /*src*/) { return true; }

// Side effects of the config write path, recorded rather than performed so a
// test can assert that a POST reached them. Zeroed by the test's own setUp().
#include "commanded_modes.h"
bool g_test_commanded_stationary = false;
unsigned g_test_dome_on_cue_count = 0;
unsigned g_test_status_broadcast_count = 0;

void commandedSetStationary(bool stationary, CommandSource /*source*/) {
    g_test_commanded_stationary = stationary;
}

// POST /api/rc/debug's only side effect. Recorded rather than performed: RC
// verbose logging lives in RCInputTask, which is not in the native build.
bool g_test_commanded_rc_debug = false;
unsigned g_test_commanded_rc_debug_calls = 0;

void commandedSetRcDebug(bool enabled, CommandSource /*source*/) {
    g_test_commanded_rc_debug = enabled;
    g_test_commanded_rc_debug_calls++;
}

bool audioQueuePlaySlot(AudioPlaybackSlot /*slot*/, CommandSource /*src*/) {
    g_test_dome_on_cue_count++;
    return true;
}

void requestStatusBroadcastNow() {
    g_test_status_broadcast_count++;
}

// sequenceQueue — defined here so sequence_dispatcher.cpp can reference the
// extern without main.cpp being in the native build.
#include "sequence_dispatcher.h"
QueueHandle_t sequenceQueue = nullptr;

// -----------------------------------------------------------------------------
// Motion and safety route group (#89). The handlers are under test; the tasks
// they hand work to are not in the native build, so each side effect is
// recorded rather than performed and the test asserts on the record.
// -----------------------------------------------------------------------------

// Command queues the handlers post to. The freertos stub's xQueueSend() always
// succeeds, so the queue-full branches are device behaviour, not host.
QueueHandle_t servoCmdQueue = nullptr;
QueueHandle_t domeCmdQueue = nullptr;

bool g_test_commanded_web_control = false;
unsigned g_test_web_control_calls = 0;
unsigned g_test_restart_requests = 0;
unsigned g_test_marcduino_calls = 0;
unsigned g_test_applied_mood = 0;

void commandedSetWebControl(bool enabled, CommandSource /*source*/) {
    g_test_commanded_web_control = enabled;
    g_test_web_control_calls++;
}

void requestSystemRestart(uint32_t /*delayMs*/) {
    g_test_restart_requests++;
}

#include "dome_rx_parser.h"
bool parseMarcduinoCommand(const char* /*line*/) {
    g_test_marcduino_calls++;
    return true;
}

#include "mood.h"
void applyMood(uint8_t moodId, bool /*fromDome*/) {
    g_test_applied_mood = moodId;
}

// The persisted half of the speed-preset write. The pure preset mapping in
// drive_speed_preset.h is exercised directly by test_drive_speed_preset; what
// the handler needs from here is a controllable success/failure.
#include "drive_speed_preset.h"
bool g_test_speed_preset_persist_ok = true;
SpeedPresetId g_test_persisted_speed_preset = SpeedPresetId::Normal;

bool applySpeedPresetPersisted(SpeedPresetId preset) {
    g_test_persisted_speed_preset = preset;
    return g_test_speed_preset_persist_ok;
}

// AUX LED strip. aux_led.cpp is a task translation unit and stays out of the
// native build, so the effect-name mapping the handler depends on is
// reproduced here rather than stubbed away -- the payload assertions would be
// vacuous otherwise.
#include "aux_led.h"
bool g_test_aux_led_queue_ok = true;

const char* auxLedEffectToString(AuxLedEffect effect) {
    switch (effect) {
        case AUX_LED_EFFECT_SOLID:
            return "solid";
        case AUX_LED_EFFECT_BLINK:
            return "blink";
        case AUX_LED_EFFECT_PULSE:
            return "pulse";
        case AUX_LED_EFFECT_OFF:
        default:
            return "off";
    }
}

bool parseAuxLedEffect(const char* raw, AuxLedEffect* out) {
    if (raw == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(raw, "off") == 0) {
        *out = AUX_LED_EFFECT_OFF;
    } else if (strcmp(raw, "solid") == 0) {
        *out = AUX_LED_EFFECT_SOLID;
    } else if (strcmp(raw, "blink") == 0) {
        *out = AUX_LED_EFFECT_BLINK;
    } else if (strcmp(raw, "pulse") == 0) {
        *out = AUX_LED_EFFECT_PULSE;
    } else {
        return false;
    }
    return true;
}

bool auxLedQueueSetColor(uint8_t r, uint8_t g, uint8_t b, CommandSource /*source*/) {
    if (!g_test_aux_led_queue_ok) {
        return false;
    }
    robotState.auxLed.r = r;
    robotState.auxLed.g = g;
    robotState.auxLed.b = b;
    return true;
}

bool auxLedQueueSetEffect(AuxLedEffect effect, CommandSource /*source*/) {
    if (!g_test_aux_led_queue_ok) {
        return false;
    }
    robotState.auxLed.effect = effect;
    return true;
}

// Dome layout cache, normally filled by DomeLinkTask over WiFi. Tests set the
// status and the payload; the handler's job is to relay them.
DomeLayoutCacheStatus g_test_dome_layout_status = {};
const char* g_test_dome_layout_payload = "";
unsigned g_test_dome_layout_refresh_requests = 0;

DomeLayoutCacheStatus domeLayoutCacheGetStatus() {
    return g_test_dome_layout_status;
}

size_t domeLayoutCacheReadChunk(uint8_t* outBuf, size_t maxLen, size_t offset,
                                uint32_t fetchedAtMs) {
    // Generation pinning is the point of this signature: a filler that reads
    // past a refresh must get 0, not bytes from the newer fetch.
    if (fetchedAtMs != g_test_dome_layout_status.fetched_at_ms) {
        return 0;
    }
    const size_t total = strlen(g_test_dome_layout_payload);
    if (offset >= total) {
        return 0;
    }
    size_t remaining = total - offset;
    if (remaining > maxLen) {
        remaining = maxLen;
    }
    memcpy(outBuf, g_test_dome_layout_payload + offset, remaining);
    return remaining;
}

bool domeLayoutCacheRefreshRequested() {
    g_test_dome_layout_refresh_requests++;
    return true;
}

// Log ring stand-in for the one main.cpp owns, which the native build does not
// compile. Backed by the real log_buffer.cpp ring, so /api/logs tests exercise
// the actual copy behavior rather than a canned string. Tests fill it through
// g_test_log_buffer directly (declared extern in the test file).
#include "log_buffer.h"
LogBuffer g_test_log_buffer = {};
size_t copyRecentLogs(char* buffer, size_t bufferSize) {
    return logBufferCopy(&g_test_log_buffer, buffer, bufferSize);
}

// -----------------------------------------------------------------------------
// WebRequest host-test backend (ADR 0021). backend_ holds a
// WebRequestTestBackend (test/stubs/include/web_request_test_backend.h):
// params come from the test's name/value table, send() captures the response
// for assertions. The session escape hatch mirrors the async scaffold's
// unsupported behavior (null/false).
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>

#include "web_request.h"
#include "web_request_test_backend.h"

static const char* testParamLookup(const WebRequestTestBackend* b, const char* name) {
    for (size_t i = 0; i < b->paramCount; i++) {
        if (strcmp(b->params[i].name, name) == 0) {
            return b->params[i].value;
        }
    }
    return nullptr;
}

bool WebRequest::hasParam(const char* name) const {
    return testParamLookup(static_cast<const WebRequestTestBackend*>(backend_), name) != nullptr;
}

bool WebRequest::param(const char* name, char* out, size_t outSize) const {
    const char* value = testParamLookup(static_cast<const WebRequestTestBackend*>(backend_), name);
    if (value == nullptr || out == nullptr || outSize == 0) {
        return false;
    }
    snprintf(out, outSize, "%s", value);
    return true;
}

const char* WebRequest::paramRef(const char* name) const {
    // The table's own storage outlives the request on the host, so borrowing
    // is simply the lookup -- no copy step to get wrong.
    return testParamLookup(static_cast<const WebRequestTestBackend*>(backend_), name);
}

const char* WebRequest::body() const {
    const char* raw = static_cast<const WebRequestTestBackend*>(backend_)->body;
    return (raw != nullptr && raw[0] != '\0') ? raw : nullptr;
}

size_t WebRequest::contentLength() const {
    return static_cast<const WebRequestTestBackend*>(backend_)->contentLength;
}

void WebRequest::addHeader(const char* name, const char* value) {
    WebRequestTestBackend* b = static_cast<WebRequestTestBackend*>(backend_);
    if (b->headerCount >= kMaxStagedHeaders) {
        return;
    }
    WebRequestTestHeader& staged = b->headers[b->headerCount++];
    snprintf(staged.name, sizeof(staged.name), "%s", name);
    snprintf(staged.value, sizeof(staged.value), "%s", value);
}

void WebRequest::send(int code, const char* contentType, const char* body) {
    WebRequestTestBackend* b = static_cast<WebRequestTestBackend*>(backend_);
    b->sentCode = code;
    snprintf(b->sentContentType, sizeof(b->sentContentType), "%s", contentType);
    snprintf(b->sentBody, sizeof(b->sentBody), "%s", body);
    b->sentBodyLength = strlen(body);
    b->sentChunked = false;
    b->sendCalls++;
}

bool WebRequest::sendChunked(const char* contentType, WebResponseBodyFiller filler) {
    WebRequestTestBackend* b = static_cast<WebRequestTestBackend*>(backend_);
    b->sentCode = 200;
    snprintf(b->sentContentType, sizeof(b->sentContentType), "%s", contentType);
    b->sentChunked = true;
    b->sendCalls++;

    // Drive the filler in small chunks the way a device backend does, so a
    // filler that mishandles the offset splits shows up on the host instead of
    // only on hardware. sentBodyLength counts everything produced; sentBody
    // keeps as much as it can hold.
    uint8_t chunk[64];
    size_t offset = 0;
    for (;;) {
        const size_t written = filler(chunk, sizeof(chunk), offset);
        if (written == 0) {
            break;
        }
        if (offset < sizeof(b->sentBody) - 1) {
            size_t room = sizeof(b->sentBody) - 1 - offset;
            size_t copy = written < room ? written : room;
            memcpy(b->sentBody + offset, chunk, copy);
            b->sentBody[offset + copy] = '\0';
        }
        offset += written;
    }
    b->sentBodyLength = offset;
    return true;
}

void* WebRequest::sessionContext() const {
    return nullptr;
}

bool WebRequest::setSessionContext(void* /*ctx*/, void (* /*freeFn*/)(void*)) {
    return false;
}

bool WebRequest::triggerClose() {
    return false;
}

bool WebRequest::beginEventStream() {
    WebRequestTestBackend* b = static_cast<WebRequestTestBackend*>(backend_);
    if (b->eventStreamFails) {
        return false;
    }
    b->eventStreamStarted = true;
    return true;
}

// -----------------------------------------------------------------------------
// Event stream transport (include/web_event_stream.h). The broadcaster's wire
// side is a device concern; what a host test needs is control over how many
// streams the handler believes are open, so the client cap can be exercised.
// -----------------------------------------------------------------------------
#include "web_event_stream.h"

size_t g_test_event_stream_clients = 0;
unsigned g_test_event_stream_broadcasts = 0;

size_t webEventStreamClientCount() {
    return g_test_event_stream_clients;
}

void webEventStreamBroadcast(const char* /*event*/, const char* /*data*/, uint32_t /*id*/) {
    g_test_event_stream_broadcasts++;
}

// -----------------------------------------------------------------------------
// Learned Sequence store (include/seq_store.h). Only the LittleFS half is
// stubbed: the index, the JSON codec and Protocol Check are all real code in
// the native build, so a seq-route test exercises the actual validation path
// and fakes only the filesystem underneath it.
// -----------------------------------------------------------------------------
#include <string>

#include "seq_store.h"

// What seqStoreSave() reports back, and what it was handed. A test sets the
// verdict, calls the handler, and asserts on both the response and the bytes
// the store would have written -- which is how "the body survived the seam
// intact" gets checked without a filesystem.
ProtocolCheckResult g_test_seq_save_result = pcOk();
std::string g_test_seq_saved_body;
unsigned g_test_seq_save_calls = 0;
bool g_test_seq_delete_ok = true;
unsigned g_test_seq_delete_calls = 0;
// Stands in for one stored file's contents, served by the slice reader.
std::string g_test_seq_file_body;

void seqStoreInit() {
}

ProtocolCheckResult seqStorePrepare(const char* /*name*/) {
    return pcOk();
}

bool seqStoreCommit(SequenceEntry& /*out*/) {
    return false;
}

void seqStoreReleaseRun() {
}

ProtocolCheckResult seqStoreSave(const char* json, size_t len) {
    g_test_seq_save_calls++;
    g_test_seq_saved_body.assign(json != nullptr ? json : "", len);
    return g_test_seq_save_result;
}

bool seqStoreDelete(const char* /*name*/) {
    g_test_seq_delete_calls++;
    return g_test_seq_delete_ok;
}

size_t seqStoreReadFileSlice(const char* /*name*/, size_t offset, uint8_t* out, size_t capacity) {
    if (out == nullptr || capacity == 0 || offset >= g_test_seq_file_body.size()) {
        return 0;
    }
    const size_t remaining = g_test_seq_file_body.size() - offset;
    const size_t count = remaining < capacity ? remaining : capacity;
    memcpy(out, g_test_seq_file_body.data() + offset, count);
    return count;
}

// Route registration is a no-op on the host: native tests call the exposed
// handlers directly instead of dispatching through a server.
void webRegisterRoute(const char* /*path*/, WebMethod /*method*/, WebRequestHandler /*handler*/,
                      size_t /*maxBodyBytes*/) {
}

void webRegisterUploadRoute(const char* /*path*/, WebUploadChunkHandler /*onChunk*/,
                            WebRequestHandler /*onDone*/) {
}

#endif

// =============================================================================
// include/status_json.h
//
// The /api/status document, which the WebEvents "status" event also carries.
// Split the ADR 0036 way: buildStatusJson() (src/web/web_server.cpp) captures
// the droid's state into a StatusJsonInputs and formatStatusJson()
// (src/web/status_json.cpp) writes the document from it. The format half holds
// no device dependency, so the native suite builds the real document and can
// ask whether its worst case fits the buffer both senders hold (#428).
//
// The web admission and response-deadline counters are not captured: the
// format reads those project-owned globals itself (include/web_admission.h,
// include/web_response_deadline.h, include/web_event_stream.h), as it always
// has.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "api_aux_led.h"  // LitWireReading
#include "audio_rx_status.h"
#include "audio_sound_member.h"  // SoundStatusIdentity
#include "board_outputs.h"
#include "config.h"
#include "dome_link_transport.h"
#include "drive_speed_preset.h"
#include "robot_state.h"  // FailsafeDiagnostics, RcInputMode, DomeUartOwner

#if PA_CAP_HOSTED_WIFI
#include "hosted_link_status.h"
#endif

// The longest version stamp the version scheme composes, terminator included:
// fs-v<release-tag>-<count>-g<sha>[-dirty][+<branch-suffix>], e.g.
// fs-v1.0.0-alpha.1-837-g401530a+phase-v1.0.0 (43 chars). 128 leaves room for
// longer branch names on both ends. firmwareVersion comes from the same scheme
// (tools/extract_version.py), so it is held to the same bound.
constexpr size_t STATUS_VERSION_STAMP_MAX = 128;

// What buildStatusJson() reads from RobotState, the config cache and the
// platform, in one pass, before a byte of the document is written.
struct StatusJsonInputs {
    FailsafeDiagnostics diag;
    bool webControlEnabled;
    bool sbus2SignalLost;
    int driveSpeed;
    int driveSteer;
    float domeTargetSpeed;
    bool enableDome;
    int speedLimitMax;
    SpeedPresetId speedPresetActive;
    bool stationary;
    unsigned long uptimeMs;
    unsigned long heapFree;
    unsigned long heapMin;
    uint32_t heapLargestBlock;
    uint32_t heapLargest8bit;
    uint32_t failedAllocs;
    unsigned sseClients;
    const char* firmwareVersion;
    const char* fsVersion;
    const char* resetReason;
    bool otaActive;
    uint8_t otaProgressPct;
    char otaLastError[64];
    long wifiRssi;
    bool wifiConnected;
    bool wifiClientConnected;
    bool littleFsReady;
    bool enableArm1, enableArm2, enableAux1, enableAux2, enableAux3;
    bool enableRcCh1, enableRcCh2, enableRcCh3, enableRcCh4, enableRcCh5, enableRcCh6;
    bool enableS1Hoverboard, enableS2Sound, enableS3DomeCtrl;
    bool audioActive;
    bool audioLinkOk;
    AudioRxStatus audioRxStatus;
    SoundStatusIdentity sound;
    bool sleepMode;
    uint8_t activeMood;
    uint32_t sleepSinceMs;
    LitWireReading litWires[BOARD_OUTPUT_LIGHT_CAPABLE_COUNT];
    size_t litWireCount;
    RcInputMode rcInputMode;
    bool singleSbusUseCh2;
    uint16_t arm1TargetUs;
    uint16_t arm2TargetUs;
    uint32_t lastSbus1Ms;
    uint32_t lastSbus2Ms;
    uint32_t sbus1LostFrameCount;
    uint32_t sbus2LostFrameCount;
    uint32_t queueOverflowCount;
    uint32_t domeHbRx;
    uint32_t bodyHbTx;
    uint32_t domeLastSeenMs;
    uint32_t domeRxOverflowCount;
    uint32_t domeRxUnknownCount;
    DomeLinkTransport domeActiveTransport;
    DomeUartOwner domeUartOwner;
    int16_t fbBatteryRaw;
    int16_t fbBoardTempRaw;
    int16_t fbSpeedR;
    int16_t fbSpeedL;
    int16_t fbCurrentL;
    int16_t fbCurrentR;
    bool fbValid;
#if PA_CAP_HOSTED_WIFI
    HostedLinkStatusSnapshot hostedLink;
#endif
};

// The buffer both senders build the document in: GET /api/status in the web
// request scratch (include/web_request_scratch.h) and the WebEvents "status"
// event in that task's own body buffer (src/web/web_server.cpp).
//
// Sized to the document's worst case - every component on and every value at
// its longest, 4,153 B plus its terminator on artoo-esp32, which
// test/test_native/test_status_json measures - rounded up to 4,160. The 3,072 B
// it replaces held a fresh-boot droid with every component on (about 3,176 B)
// only as an overflow answer (#381, #428). A board with ESP-Hosted also carries
// the hostedLink block, which src/web/status_json.cpp formats into 256 B first,
// so it adds at most 255; the native build cannot measure it, so it is added
// rather than measured.
#if PA_CAP_HOSTED_WIFI
constexpr size_t STATUS_JSON_HOSTED_LINK_MAX = 255;
#else
constexpr size_t STATUS_JSON_HOSTED_LINK_MAX = 0;
#endif
constexpr size_t STATUS_JSON_BUFFER_BYTES = 4160 + STATUS_JSON_HOSTED_LINK_MAX;

// Writes the status document into buffer. False when it did not fit, and then
// buffer holds {"ok":false,"error":"status payload overflow"} instead - the
// answer both senders have always given for an overflow.
bool formatStatusJson(char* buffer, size_t bufferSize, const StatusJsonInputs& in);

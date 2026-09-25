// =============================================================================
// test/test_native/test_status_json/test_status_json.cpp
//
// The /api/status document at its worst case fits the buffer both senders
// build it in (STATUS_JSON_BUFFER_BYTES, include/status_json.h; #428).
//
// Past that buffer a droid answers {"ok":false,"error":"status payload
// overflow"} instead of its status, and the dashboard loses every reading at
// once. The document grows with every component a builder switches on, so the
// droid most likely to hit it is the fully built one.
//
// "Worst case" is every component flag on and every value at its longest:
//   - integers at the limit of the type they are read from, as a 32-bit
//     target prints them (long and unsigned long are 32 bits on both chips);
//   - enums at their longest word, strings at their longest source: the
//     version stamps at STATUS_VERSION_STAMP_MAX - 1, OTA's last error at its
//     buffer, the Sound name at the longest Sound part in the registry, the
//     lit wires on every light-capable Output this board has;
//   - domeTargetSpeed across its -1..1 domain (include/robot_state.h).
// The branches the document takes on state - RC mode and receiver states,
// drive, sound, the dome link - are swept together rather than picked by
// hand, so a branch that is longer than it looks cannot be missed.
// =============================================================================

#include <unity.h>

#include <ArduinoJson.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "aux_led.h"
#include "board_outputs.h"
#include "component_registry.h"
#include "drive_motion_test_hooks.h"  // g_test_millis
#include "reset_reason.h"
#include "status_json.h"
#include "web_admission.h"
#include "web_event_stream.h"
#include "web_response_deadline.h"

namespace {

// Room for any document the formatter could write, so its real length is
// measured rather than cut off at the buffer under test.
constexpr size_t kUnbounded = 16384;

char g_fwVersion[STATUS_VERSION_STAMP_MAX];
char g_fsVersion[STATUS_VERSION_STAMP_MAX];

const char* longestOf(const char* a, const char* b) {
    return strlen(b) > strlen(a) ? b : a;
}

const char* longestResetReason() {
    const char* longest = "";
    for (int reason = -1; reason <= 32; ++reason) {
        longest = longestOf(longest, resetReasonName(reason));
    }
    return longest;
}

const char* longestSoundName() {
    const char* longest = "";
    for (size_t i = 0; i < COMPONENT_PART_COUNT; ++i) {
        if (COMPONENT_PARTS[i].category == COMPONENT_CATEGORY_SOUND) {
            longest = longestOf(longest, COMPONENT_PARTS[i].name);
        }
    }
    return longest;
}

SpeedPresetId longestSpeedPreset() {
    SpeedPresetId longest = normalizeSpeedPresetId(0);
    for (int value = 0; value <= UINT8_MAX; ++value) {
        const SpeedPresetId preset = normalizeSpeedPresetId((uint8_t)value);
        if (strlen(speedPresetIdToString(preset)) > strlen(speedPresetIdToString(longest))) {
            longest = preset;
        }
    }
    return longest;
}

const char* longestLedEffect() {
    const char* longest = "";
    for (int effect = 0; effect <= 255; ++effect) {
        longest = longestOf(longest, auxLedEffectToString(static_cast<AuxLedEffect>(effect)));
    }
    return longest;
}

// The ages the document prints are (uint32_t)(now - then) cast to int32_t;
// 0x80000000 is the one that prints as -2147483648, the widest.
constexpr uint32_t kWidestAge = 0x80000000u;
constexpr uint32_t kUptime = UINT32_MAX;

// The web admission and response-deadline counters the formatter reads itself,
// each at its widest.
void setCountersToTheirWidest() {
    g_webAcceptRejectHeap = UINT32_MAX;
    g_webAcceptRejectRate = UINT32_MAX;
    g_webAcceptRejectLastMs = kUptime - kWidestAge;
    g_webInflightRequests = INT32_MIN;
    g_webInflightRequestsPeak = INT32_MIN;
    g_webRefusedInflightCap = UINT32_MAX;
    g_webRefusedHeapFloor = UINT32_MAX;
    g_webRefusedHeapFloorDiag = UINT32_MAX;
    g_webSseClientsPeak = UINT32_MAX;
    g_webAcceptGuardLastUs = UINT32_MAX;
    g_webAcceptGuardMaxUs = UINT32_MAX;
    g_webAcceptRejectLargestBlock = UINT32_MAX;
    g_webAcceptMinLargestBlockSeen = kWidestAge;
    g_webRefusedSseCap = UINT32_MAX;
    g_webSseEvicted = UINT32_MAX;
    g_webSseEvictLastMs = kUptime - kWidestAge;
    g_webBusyRecoveryPagesServed = UINT32_MAX;
    g_webResponseDeadlineClosures = UINT32_MAX;
    g_webResponseDeadlineLastMs = (uint32_t)g_test_millis - kWidestAge;
    g_webSocketsAccepted = UINT32_MAX;
    g_webSocketsOpen = INT32_MIN;
    g_webSocketsOpenPeak = INT32_MIN;
    g_webSocketsUntracked = UINT32_MAX;
    g_webRequestsServed = UINT32_MAX;
    g_webResponseLastMs = UINT32_MAX;
    g_webResponseMaxMs = UINT32_MAX;
    g_webSendRetriesWindow = UINT32_MAX;
    g_webSendRetriesMemory = UINT32_MAX;
    g_webSendRetryMaxMs = UINT32_MAX;
}

// Everything the sweep below does not vary, at its longest.
StatusJsonInputs widestInputs() {
    StatusJsonInputs in = {};
    in.diag.estop = false;  // "false" is the longer word
    in.diag.sbusHwFailsafe = false;
    in.diag.webDriveExpired = false;
    in.diag.failsafeSource = FS_WATCHDOG_RESET;
    in.diag.failsafeTriggerCount = UINT32_MAX;
    in.diag.failsafeLastTriggerMs = UINT32_MAX;
    in.diag.failsafeLastZeroOutputMs = UINT32_MAX;
    in.diag.failsafeLastTriggerToZeroMs = UINT32_MAX;
    in.diag.failsafeLastWatchdogMs = UINT32_MAX;
    in.diag.failsafeLastTriggerSource = FS_WATCHDOG_RESET;
    in.webControlEnabled = false;
    in.speedLimitMax = INT16_MIN;
    in.speedPresetActive = longestSpeedPreset();
    in.stationary = false;
    in.uptimeMs = kUptime;
    in.heapFree = UINT32_MAX;
    in.heapMin = UINT32_MAX;
    in.heapLargestBlock = UINT32_MAX;
    in.heapLargest8bit = UINT32_MAX;
    in.failedAllocs = UINT32_MAX;
    in.sseClients = UINT32_MAX;
    in.firmwareVersion = g_fwVersion;
    in.fsVersion = g_fsVersion;
    in.resetReason = longestResetReason();
    in.otaActive = false;
    in.otaProgressPct = UINT8_MAX;
    memset(in.otaLastError, 'e', sizeof(in.otaLastError) - 1);
    in.wifiRssi = INT32_MIN;
    in.wifiConnected = false;
    in.wifiClientConnected = false;
    in.littleFsReady = false;
    in.enableArm1 = in.enableArm2 = true;
    in.enableAux1 = in.enableAux2 = in.enableAux3 = true;
    in.enableRcCh1 = in.enableRcCh2 = in.enableRcCh3 = true;
    in.enableRcCh4 = in.enableRcCh5 = in.enableRcCh6 = true;
    in.enableS1Hoverboard = in.enableS2Sound = in.enableS3DomeCtrl = true;
    in.enableDome = true;
    in.audioLinkOk = false;
    in.sound.driver = longestSoundName();
    in.sleepMode = false;
    in.activeMood = UINT8_MAX;
    in.sleepSinceMs = UINT32_MAX;
    for (size_t i = 0; i < BOARD_OUTPUT_COUNT; ++i) {
        if (!BOARD_OUTPUTS[i].lightCapable) {
            continue;
        }
        LitWireReading& wire = in.litWires[in.litWireCount++];
        wire.id = BOARD_OUTPUTS[i].id;
        wire.r = wire.g = wire.b = UINT8_MAX;
        wire.effect = longestLedEffect();
        wire.available = false;
    }
    in.arm1TargetUs = UINT16_MAX;
    in.arm2TargetUs = UINT16_MAX;
    in.sbus1LostFrameCount = UINT32_MAX;
    in.sbus2LostFrameCount = UINT32_MAX;
    in.queueOverflowCount = UINT32_MAX;
    in.domeHbRx = UINT32_MAX;
    in.bodyHbTx = UINT32_MAX;
    in.domeRxOverflowCount = UINT32_MAX;
    in.domeRxUnknownCount = UINT32_MAX;
    in.fbValid = true;
    in.fbBatteryRaw = INT16_MIN;
    in.fbBoardTempRaw = INT16_MIN;
    in.fbSpeedR = INT16_MIN;
    in.fbSpeedL = INT16_MIN;
    in.fbCurrentL = INT16_MIN;
    in.fbCurrentR = INT16_MIN;
    return in;
}

}  // namespace

void setUp(void) {
    memset(g_fwVersion, 'v', sizeof(g_fwVersion) - 1);
    memset(g_fsVersion, 'f', sizeof(g_fsVersion) - 1);
    g_test_millis = 1000;
    setCountersToTheirWidest();
}

void tearDown(void) {
}

void test_the_worst_case_status_document_fits_its_buffer(void) {
    static char unbounded[kUnbounded];
    static char bounded[STATUS_JSON_BUFFER_BYTES];
    static char worst[kUnbounded];
    size_t worstLength = 0;
    StatusJsonInputs worstIn = {};

    const RcInputMode modes[] = {RC_INPUT_STANDARD_PWM, RC_INPUT_SINGLE_SBUS, RC_INPUT_ELRS,
                                 RC_INPUT_DUAL_SBUS};
    const AudioRxStatus rxStates[] = {AUDIO_RX_AVAILABLE, AUDIO_RX_BLOCKED_BY_DOME_UART,
                                      AUDIO_RX_NO_RESPONSE, AUDIO_RX_UNKNOWN};
    const DomeLinkTransport transports[] = {DOME_LINK_TRANSPORT_UART, DOME_LINK_TRANSPORT_WIFI,
                                            DOME_LINK_TRANSPORT_DISCONNECTED};
    const DomeUartOwner owners[] = {DOME_UART_NONE, DOME_UART_DOME, DOME_UART_AUDIO};
    // Never seen, seen within the 5 s the link counts as connected, and seen
    // long enough ago that its age prints at its widest.
    const uint32_t domeSeen[] = {0, kUptime - 4999u, kUptime - kWidestAge};

    StatusJsonInputs in = widestInputs();
    for (RcInputMode mode : modes) {
        in.rcInputMode = mode;
        for (unsigned rc = 0; rc < 32; ++rc) {
            in.singleSbusUseCh2 = rc & 1u;
            in.lastSbus1Ms = (rc & 2u) ? 1u : 0u;
            in.diag.sbusSignalLost = rc & 4u;
            in.lastSbus2Ms = (rc & 8u) ? 1u : 0u;
            in.sbus2SignalLost = rc & 16u;
            for (unsigned motion = 0; motion < 4; ++motion) {
                in.driveSpeed = (motion & 1u) ? INT16_MIN : 0;
                in.driveSteer = (motion & 1u) ? INT16_MIN : 0;
                in.domeTargetSpeed = (motion & 2u) ? -1.0f : 0.0f;
                for (unsigned sound = 0; sound < 4; ++sound) {
                    in.sound.on = sound & 1u;
                    in.audioActive = sound & 2u;
                    for (AudioRxStatus rx : rxStates) {
                        in.audioRxStatus = rx;
                        for (DomeLinkTransport transport : transports) {
                            in.domeActiveTransport = transport;
                            for (DomeUartOwner owner : owners) {
                                in.domeUartOwner = owner;
                                for (uint32_t seen : domeSeen) {
                                    in.domeLastSeenMs = seen;
                                    formatStatusJson(unbounded, sizeof(unbounded), in);
                                    const size_t length = strlen(unbounded);
                                    if (length > worstLength) {
                                        worstLength = length;
                                        memcpy(worst, unbounded, length + 1);
                                        worstIn = in;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // The widest document is well-formed JSON, so the sweep measured the
    // document and not an overflow answer.
    JsonDocument doc;
    TEST_ASSERT_TRUE_MESSAGE(deserializeJson(doc, worst) == DeserializationError::Ok,
                             "the widest status document is not valid JSON");
    TEST_ASSERT_FALSE(doc["estop"].isNull());

    char message[96];
    snprintf(message, sizeof(message), "widest status document is %u B plus its terminator",
             (unsigned)worstLength);
    TEST_ASSERT_LESS_THAN_UINT32_MESSAGE((uint32_t)STATUS_JSON_BUFFER_BYTES,
                                         (uint32_t)worstLength, message);
    // Bound by the buffer, the builder writes that same document whole rather
    // than its overflow answer.
    TEST_ASSERT_TRUE_MESSAGE(formatStatusJson(bounded, sizeof(bounded), worstIn), message);
    TEST_ASSERT_EQUAL_STRING(worst, bounded);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_worst_case_status_document_fits_its_buffer);
    return UNITY_END();
}

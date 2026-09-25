// =============================================================================
// src/web/status_json.cpp
//
// formatStatusJson(): the /api/status document written from a captured
// StatusJsonInputs (include/status_json.h). Holds no device dependency, so the
// native suite builds the same document the droid sends (#428).
// =============================================================================

#include "status_json.h"

#include <stdio.h>
#include <string.h>

#include "api_aux_led.h"
#include "audio_sound_member.h"
#include "audio_task.h"
#include "drive_speed_preset.h"
#include "web_admission.h"
#include "web_event_stream.h"
#include "web_response_deadline.h"

namespace {

// The answer both senders give when the document does not fit.
const char kStatusOverflowBody[] = "{\"ok\":false,\"error\":\"status payload overflow\"}";

const char* rcInputModeLabel(RcInputMode mode) {
    switch (mode) {
        case RC_INPUT_STANDARD_PWM:
            return "standard_pwm";
        case RC_INPUT_SINGLE_SBUS:
            return "single_sbus";
        case RC_INPUT_ELRS:
            return "elrs";
        case RC_INPUT_DUAL_SBUS:
        default:
            return "dual_sbus";
    }
}

const char* domeTransportLabel(DomeLinkTransport transport) {
    switch (transport) {
        case DOME_LINK_TRANSPORT_UART:
            return "uart";
        case DOME_LINK_TRANSPORT_WIFI:
            return "wifi";
        case DOME_LINK_TRANSPORT_DISCONNECTED:
        default:
            return "disconnected";
    }
}

bool appendJsonChunk(char*& pos, size_t& remaining, const char* chunk) {
    if (remaining == 0) {
        return false;
    }

    int n = snprintf(pos, remaining, "%s", chunk);
    if (n <= 0 || n >= (int)remaining) {
        return false;
    }

    pos += n;
    remaining -= (size_t)n;
    return true;
}

bool appendPeripheralStatus(char*& pos, size_t& remaining, const char* key, const char* state,
                            const char* detail) {
    if (remaining == 0) {
        return false;
    }

    int n = snprintf(pos, remaining, ",\"%s\":{\"state\":\"%s\",\"detail\":\"%s\"}", key, state,
                     detail);
    if (n <= 0 || n >= (int)remaining) {
        return false;
    }

    pos += n;
    remaining -= (size_t)n;
    return true;
}

}  // namespace

bool formatStatusJson(char* buffer, size_t bufferSize, const StatusJsonInputs& in) {
    if (buffer == nullptr || bufferSize == 0) {
        return false;
    }

    // The lit wires, keyed by Output id, in the one shape the aux-LED endpoints
    // answer with too (include/api_aux_led.h). A droid with no light writes {}.
    char litWiresJson[LIT_WIRES_JSON_MAX] = {};
    if (!formatLitWiresJson(litWiresJson, sizeof(litWiresJson), in.litWires, in.litWireCount, nullptr)) {
        // Cannot happen while LIT_WIRES_JSON_MAX is derived from the entry
        // bound, and answered like every other overflow if it ever does, so
        // no caller sends a buffer this function never wrote.
        snprintf(buffer, bufferSize, "%s", kStatusOverflowBody);
        return false;
    }

    // Admission evidence, read from the project-owned counters the serving
    // backend writes (include/web_admission.h). The JSON field names below are
    // a comparability contract with the recorded baseline and the load
    // harness, not a description of which implementation produced them --
    // which is why they are unchanged by the cutover that removed the other
    // implementation.
    const uint32_t acceptRejectHeap = g_webAcceptRejectHeap;
    const uint32_t acceptRejectRate = g_webAcceptRejectRate;
    const uint32_t acceptRejectLastMs = g_webAcceptRejectLastMs;
    const int inflightRequests = g_webInflightRequests;
    const int inflightRequestsPeak = g_webInflightRequestsPeak;
    const uint32_t refusedInflightCap = g_webRefusedInflightCap;
    const uint32_t refusedHeapFloor = g_webRefusedHeapFloor;
    const uint32_t refusedHeapFloorDiag = g_webRefusedHeapFloorDiag;

    // Build the fixed system-health fields first.
    int written = snprintf(
        buffer, bufferSize,
        "{\"estop\":%s,\"webControlEnabled\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,\"webDriveExpired\":%s,\"failsafeSource\":%d,\"driveSpeed\":%d,\"driveSteer\":%d,\"domeTargetSpeed\":%.3f,\"domeEnabled\":%s,\"speedLimitMax\":%d,\"speedPreset\":\"%s\",\"stationary\":%s,\"failsafeCount\":%lu,\"failsafeTriggerMs\":%lu,\"failsafeZeroMs\":%lu,\"failsafeTriggerToZeroMs\":%lu,\"failsafeWatchdogMs\":%lu,\"failsafeTriggerSource\":%d,\"queueOverflowCount\":%lu,\"uptimeMs\":%lu,\"firmwareVersion\":\"%s\",\"fsVersion\":\"%s\",\"resetReason\":\"%s\",\"heapFree\":%lu,\"heapMin\":%lu,\"heapLargestBlock\":%lu,\"heapLargest8bit\":%lu,\"failedAllocs\":%lu,\"sseClients\":%u,\"sseClientsPeak\":%lu,\"tcpAcceptRejectHeap\":%lu,\"tcpAcceptRejectRate\":%lu,\"tcpAcceptRejectAgeMs\":%ld,\"acceptGuardLastUs\":%lu,\"acceptGuardMaxUs\":%lu,\"acceptRejectLargestBlock\":%lu,\"acceptMinLargestBlockSeen\":%ld,\"inflightRequests\":%d,\"inflightRequestsPeak\":%d,\"refusedInflightCap\":%lu,\"refusedSseCap\":%lu,\"sseEvicted\":%lu,\"sseEvictAgeMs\":%ld,\"refusedHeapFloor\":%lu,\"refusedHeapFloorDiag\":%lu,\"busyRecoveryPagesServed\":%lu,\"otaActive\":%s,\"otaProgress\":%u,\"otaLastError\":\"%s\",\"wifiRssi\":%ld,\"wifiConnected\":%s,\"wifiClientConnected\":%s,\"littleFsReady\":%s,\"sleepMode\":%s,\"sleepSinceMs\":%lu,\"activeMood\":%u,\"lights\":%s",
        in.diag.estop ? "true" : "false", in.webControlEnabled ? "true" : "false",
        in.diag.sbusSignalLost ? "true" : "false", in.diag.sbusHwFailsafe ? "true" : "false",
        in.diag.webDriveExpired ? "true" : "false", (int)in.diag.failsafeSource, in.driveSpeed, in.driveSteer,
        (double)in.domeTargetSpeed, in.enableDome ? "true" : "false",
        in.speedLimitMax, speedPresetIdToString(in.speedPresetActive), in.stationary ? "true" : "false",
        (unsigned long)in.diag.failsafeTriggerCount, (unsigned long)in.diag.failsafeLastTriggerMs, (unsigned long)in.diag.failsafeLastZeroOutputMs, (unsigned long)in.diag.failsafeLastTriggerToZeroMs,
        (unsigned long)in.diag.failsafeLastWatchdogMs, (int)in.diag.failsafeLastTriggerSource,
        // Every non-blocking enqueue that found its queue full, from any task
        // (logQueueDrop(), src/queue_drop_tracker.cpp). Published beside the
        // failsafe counters because it is the other half of "was Core 1
        // degraded across this run" - read here rather than only from a
        // profiler build, which is the build that measurement forbids.
        (unsigned long)in.queueOverflowCount,
        in.uptimeMs, in.firmwareVersion, in.fsVersion,
        in.resetReason,
        in.heapFree, in.heapMin, (unsigned long)in.heapLargestBlock,
        // Same capability mask as every admission guard (MALLOC_CAP_8BIT).
        // heapLargestBlock above uses MALLOC_CAP_INTERNAL and can diverge
        // wildly from what the guards actually see; both are emitted so the
        // divergence itself is observable.
        (unsigned long)in.heapLargest8bit,
        // Failed allocations since boot, from the always-compiled tracker
        // (include/failed_alloc_tracker.h). ADR 0017's heap rule wants this
        // flat across a load wave, and wants it on a production image - the
        // one build /api/profiler, which reports the same counter, is absent
        // from.
        (unsigned long)in.failedAllocs,
        // Open event streams; the client cap keys on this, so stuck or leaked
        // entries become visible instead of silently denying new streams.
        in.sseClients, (unsigned long)g_webSseClientsPeak,
        (unsigned long)acceptRejectHeap, (unsigned long)acceptRejectRate,
        // The ages and the smallest block below go out as a 32-bit signed
        // value, as they always have on both chips (a 32-bit `long`). The
        // cast says so, so a host build writes the same digits the droid
        // does and the native worst-case test measures the real width.
        acceptRejectLastMs == 0 ? -1L
                                : (long)(int32_t)((uint32_t)in.uptimeMs - acceptRejectLastMs),
        // Cost of the connection guard itself. Kept always-on rather than
        // measured once: whether the guard is affordable on a stack that
        // services every connection from one task is a standing property, not
        // a one-off result.
        (unsigned long)g_webAcceptGuardLastUs, (unsigned long)g_webAcceptGuardMaxUs,
        // Depth evidence for the floor. The resting largest-block reading in
        // this same payload is by definition never the one that caused a
        // refusal, so a bare refusal count cannot say how far the floor was
        // crossed -- and crossing depth is what the out-of-scope rule demands
        // before any floor is argued about.
        (unsigned long)g_webAcceptRejectLargestBlock,
        g_webAcceptMinLargestBlockSeen == UINT32_MAX
            ? -1L
            : (long)(int32_t)g_webAcceptMinLargestBlockSeen,
        // Live + peak inflight depth and refusal counts by the same broad
        // classes the admission layer gates on -- current/peak/refused
        // evidence needed before any cap, floor, or weight is retuned.
        inflightRequests, inflightRequestsPeak,
        (unsigned long)refusedInflightCap, (unsigned long)g_webRefusedSseCap,
        // Stalled-client evictions. Rare by design, which is exactly why they
        // are published: a run that never trips the deadline is otherwise
        // indistinguishable from one where the guard silently stopped working.
        // The age separates a boot-time blip from an ongoing problem.
        (unsigned long)g_webSseEvicted,
        g_webSseEvictLastMs == 0 ? -1L
                                 : (long)(int32_t)((uint32_t)in.uptimeMs - g_webSseEvictLastMs),
        (unsigned long)refusedHeapFloor, (unsigned long)refusedHeapFloorDiag,
        (unsigned long)g_webBusyRecoveryPagesServed,
        in.otaActive ? "true" : "false", (unsigned)in.otaProgressPct, in.otaLastError, in.wifiRssi,
        in.wifiConnected ? "true" : "false",
        in.wifiClientConnected ? "true" : "false", in.littleFsReady ? "true" : "false",
        in.sleepMode ? "true" : "false", (unsigned long)in.sleepSinceMs, (unsigned)in.activeMood,
        litWiresJson);

    // Connection lifetime. httpRequestsServed against httpSocketsAccepted is
    // the measurement: their ratio is requests per connection, which is what
    // "does this stack reuse connections" actually means (ADR 0023).
    // httpSocketsOpenPeak is the other half -- reuse is only affordable if
    // occupancy stays inside max_open_sockets.
    //
    // responseMaxMs is the reading the response-phase deadline is calibrated
    // against: the longest response phase seen this boot. It is published on
    // every run rather than measured once, because "does the deadline still
    // clear the slowest legitimate response" is a standing property of the
    // system, not a past result (ADR 0024). responseDeadlineAgeMs follows
    // sseEvictAgeMs: -1 until one has fired.
    //
    // sendRetriesMemory is the one to watch: it counts writes that had to wait
    // because the stack could not allocate a segment, which is the condition
    // that used to abandon a response mid-body and hand the browser a
    // well-formed but truncated file (prior async backend failure mode). It is
    // now retried rather than fatal, so the failure is invisible from the
    // outside -- this counter is the only place the pressure still shows.
    if (written > 0 && written < (int)bufferSize - 1) {
        const uint32_t responseDeadlineLastMs = g_webResponseDeadlineLastMs;
        const long responseDeadlineAgeMs =
            g_webResponseDeadlineClosures == 0 ? -1L
                                        : (long)(int32_t)((uint32_t)millis() - responseDeadlineLastMs);
        const int extra =
            snprintf(buffer + written, bufferSize - (size_t)written,
                     ",\"httpSocketsAccepted\":%lu,\"httpSocketsOpen\":%d,"
                     "\"httpSocketsOpenPeak\":%d,\"httpSocketsUntracked\":%lu,"
                     "\"httpRequestsServed\":%lu,\"responseDeadlineClosures\":%lu,"
                     "\"responseDeadlineAgeMs\":%ld,\"responseLastMs\":%lu,"
                     "\"responseMaxMs\":%lu,\"sendRetriesWindow\":%lu,"
                     "\"sendRetriesMemory\":%lu,\"sendRetryMaxMs\":%lu",
                     (unsigned long)g_webSocketsAccepted, (int)g_webSocketsOpen,
                     (int)g_webSocketsOpenPeak, (unsigned long)g_webSocketsUntracked,
                     (unsigned long)g_webRequestsServed,
                     (unsigned long)g_webResponseDeadlineClosures, responseDeadlineAgeMs,
                     (unsigned long)g_webResponseLastMs, (unsigned long)g_webResponseMaxMs,
                     (unsigned long)g_webSendRetriesWindow,
                     (unsigned long)g_webSendRetriesMemory,
                     (unsigned long)g_webSendRetryMaxMs);
        if (extra > 0) {
            // Truncation leaves written past the buffer, which the bound check
            // below reads as a failed build -- the same way the fixed section
            // above reports its own overflow.
            written += extra;
        }
    }

    // Conditionally append enabled-component keys - disabled components are absent,
    // not emitted as false placeholders (status/dashboard contract).
    bool ok = written > 0 && written < (int)bufferSize - 1;
    if (ok) {
        char* pos = buffer + written;
        size_t remaining = bufferSize - (size_t)written;
        char detail[96];

        if (in.enableArm1) {
            snprintf(detail, sizeof(detail), "Target %u us", (unsigned)in.arm1TargetUs);
            ok = appendPeripheralStatus(pos, remaining, "arm1", "ready", detail) && ok;
        }
        if (in.enableArm2) {
            snprintf(detail, sizeof(detail), "Target %u us", (unsigned)in.arm2TargetUs);
            ok = appendPeripheralStatus(pos, remaining, "arm2", "ready", detail) && ok;
        }
        if (in.enableAux1) {
            ok = appendPeripheralStatus(pos, remaining, "aux1", "ready", "Servo channel enabled") &&
                 ok;
        }
        if (in.enableAux2) {
            ok = appendPeripheralStatus(pos, remaining, "aux2", "ready", "Servo channel enabled") &&
                 ok;
        }
        if (in.enableAux3) {
            ok = appendPeripheralStatus(pos, remaining, "aux3", "ready", "Servo channel enabled") &&
                 ok;
        }
        if (in.enableDome) {
            if (in.domeTargetSpeed > 0.001f || in.domeTargetSpeed < -0.001f) {
                snprintf(detail, sizeof(detail), "Target %.0f%%",
                         (double)(in.domeTargetSpeed * 100.0f));
                ok = appendPeripheralStatus(pos, remaining, "domeEsc", "spinning", detail) && ok;
            } else {
                ok = appendPeripheralStatus(pos, remaining, "domeEsc", "idle", "Target 0%") && ok;
            }
        }
        if (in.enableRcCh1 && !(in.rcInputMode == RC_INPUT_SINGLE_SBUS && in.singleSbusUseCh2)) {
            if (in.rcInputMode == RC_INPUT_STANDARD_PWM) {
                ok = appendPeripheralStatus(
                         pos, remaining, "rcCh1", "ready",
                         "Standard PWM input enabled; routing configurable via /api/config") &&
                     ok;
            } else if (in.lastSbus1Ms == 0) {
                ok = appendPeripheralStatus(pos, remaining, "rcCh1", "not_seen",
                                            "Drive SBUS input waiting for first frame") &&
                     ok;
            } else if (in.diag.sbusSignalLost) {
                snprintf(detail, sizeof(detail),
                         "Drive SBUS lost, last %lu ms ago, lost frames %lu",
                         in.uptimeMs - in.lastSbus1Ms, (unsigned long)in.sbus1LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh1", "signal_lost", detail) && ok;
            } else {
                snprintf(detail, sizeof(detail),
                         "Drive SBUS active, last %lu ms ago, lost frames %lu",
                         in.uptimeMs - in.lastSbus1Ms, (unsigned long)in.sbus1LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh1", "active", detail) && ok;
            }
        }
        if (in.enableRcCh2) {
            if (in.rcInputMode == RC_INPUT_STANDARD_PWM) {
                ok = appendPeripheralStatus(
                         pos, remaining, "rcCh2", "ready",
                         "Standard PWM input enabled; routing configurable via /api/config") &&
                     ok;
            } else if (in.rcInputMode == RC_INPUT_SINGLE_SBUS && !in.singleSbusUseCh2) {
                ok = appendPeripheralStatus(
                         pos, remaining, "rcCh2", "standby",
                         "SBUS2 not selected; using SBUS1 (CH1) in single_sbus mode") &&
                     ok;
            } else if (in.lastSbus2Ms == 0) {
                ok = appendPeripheralStatus(pos, remaining, "rcCh2", "not_seen",
                                            "SBUS2 input waiting for first frame") &&
                     ok;
            } else if (in.sbus2SignalLost) {
                snprintf(detail, sizeof(detail), "SBUS2 lost, last %lu ms ago, lost frames %lu",
                         in.uptimeMs - in.lastSbus2Ms, (unsigned long)in.sbus2LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh2", "signal_lost", detail) && ok;
            } else {
                snprintf(detail, sizeof(detail), "SBUS2 active, last %lu ms ago, lost frames %lu",
                         in.uptimeMs - in.lastSbus2Ms, (unsigned long)in.sbus2LostFrameCount);
                ok = appendPeripheralStatus(pos, remaining, "rcCh2", "active", detail) && ok;
            }
        }
        if (in.enableRcCh3) {
            snprintf(detail, sizeof(detail),
                     "CH3 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(in.rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh3",
                                        in.rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (in.enableRcCh4) {
            snprintf(detail, sizeof(detail),
                     "CH4 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(in.rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh4",
                                        in.rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (in.enableRcCh5) {
            snprintf(detail, sizeof(detail),
                     "CH5 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(in.rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh5",
                                        in.rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (in.enableRcCh6) {
            snprintf(detail, sizeof(detail),
                     "CH6 enabled; %s routing is configurable via /api/config",
                     rcInputModeLabel(in.rcInputMode));
            ok = appendPeripheralStatus(pos, remaining, "rcCh6",
                                        in.rcInputMode == RC_INPUT_STANDARD_PWM ? "ready" : "standby",
                                        detail) &&
                 ok;
        }
        if (in.enableS1Hoverboard) {
            if (in.driveSpeed != 0 || in.driveSteer != 0) {
                snprintf(detail, sizeof(detail), "Command %d/%d", in.driveSpeed, in.driveSteer);
                ok = appendPeripheralStatus(pos, remaining, "drive", "commanding", detail) &&
                     ok;
            } else {
                ok = appendPeripheralStatus(pos, remaining, "drive", "idle",
                                            "No drive command requested") &&
                     ok;
            }
        }
        if (in.enableS2Sound) {
            const char* rxStatusText = audioRxStatusToken(in.audioRxStatus);
            const char* rxDetail = audioRxStatusDetail(in.audioRxStatus);
            // Sound saved on but off this boot has no module behind it: the
            // block names the picked module and says sound is off rather than
            // reporting a driver nobody is using as "idle" (#370).
            const SoundStatusIdentity& sound = in.sound;
            const char* state = !sound.on ? "off" : (in.audioActive ? "playing" : "idle");
            // Off, the detail is the picked name followed by the shared tail
            // (AUDIO_SOUND_OFF_STATUS_TAIL), written straight into the body
            // rather than composed into a buffer on this frame.
            const char* detailName = !sound.on ? sound.driver : "";
            const char* detailText =
                !sound.on ? AUDIO_SOUND_OFF_STATUS_TAIL
                : in.audioRxStatus == AUDIO_RX_BLOCKED_BY_DOME_UART
                    ? rxDetail
                    : (in.audioActive ? "Playback active" : "Ready, no active playback");
            int _n = snprintf(pos, remaining,
                              ",\"audio\":{\"state\":\"%s\",\"detail\":\"%s%s\",\"driver\":\"%s\",\"output\":\"%s\",\"link_ok\":%s,\"rx_status\":\"%s\",\"rx_detail\":\"%s\"}",
                              state, detailName, detailText, sound.driver, sound.on ? "on" : "off",
                              in.audioLinkOk ? "true" : "false", rxStatusText, rxDetail);
            if (_n > 0 && _n < (int)remaining) {
                pos += _n;
                remaining -= (size_t)_n;
            } else {
                ok = false;
            }
        }
        if (in.enableS3DomeCtrl) {
            const char* transportLabel = domeTransportLabel(in.domeActiveTransport);
            if (in.domeLastSeenMs == 0) {
                snprintf(detail, sizeof(detail),
                         "Heartbeat tx %lu, no protoR2link heartbeat seen yet (transport %s)",
                         (unsigned long)in.bodyHbTx, transportLabel);
                ok = appendPeripheralStatus(pos, remaining, "protoR2link", "not_seen", detail) && ok;
            } else if ((in.uptimeMs - in.domeLastSeenMs) < 5000UL) {
                snprintf(detail, sizeof(detail),
                         "Heartbeat rx %lu / tx %lu, last %lu ms ago (transport %s)",
                         (unsigned long)in.domeHbRx, (unsigned long)in.bodyHbTx,
                         in.uptimeMs - in.domeLastSeenMs, transportLabel);
                ok =
                    appendPeripheralStatus(pos, remaining, "protoR2link", "connected", detail) && ok;
            } else {
                snprintf(detail, sizeof(detail),
                         "Heartbeat rx %lu / tx %lu, last %lu ms ago (transport %s)",
                         (unsigned long)in.domeHbRx, (unsigned long)in.bodyHbTx,
                         in.uptimeMs - in.domeLastSeenMs, transportLabel);
                ok = appendPeripheralStatus(pos, remaining, "protoR2link", "lost", detail) && ok;
            }
        }

        // Top-level dome_link block - always present for external tooling,
        // regardless of whether the protoR2link component is enabled.
        // three states: connected (hb seen < 5s), lost (was seen, now > 5s), not_seen (never).
        {
            const char* dlState;
            const char* dlTransport = domeTransportLabel(in.domeActiveTransport);
            const char* dlUartOwner = "none";
            int32_t lastRxMs = -1;
            char dlDetail[96];
            switch (in.domeUartOwner) {
                case DOME_UART_DOME:
                    dlUartOwner = "dome";
                    break;
                case DOME_UART_AUDIO:
                    dlUartOwner = "audio";
                    break;
                case DOME_UART_NONE:
                default:
                    break;
            }
            if (!in.enableS3DomeCtrl) {
                dlState = "disabled";
                dlTransport = "none";
            } else if (in.domeLastSeenMs == 0) {
                dlState = "not_seen";
            } else if ((in.uptimeMs - in.domeLastSeenMs) < 5000UL) {
                dlState = "connected";
                lastRxMs = (int32_t)(in.uptimeMs - in.domeLastSeenMs);
            } else {
                dlState = "lost";
                lastRxMs = (int32_t)(in.uptimeMs - in.domeLastSeenMs);
            }
            snprintf(dlDetail, sizeof(dlDetail), "transport=%s, uart_owned=%s", dlTransport,
                     in.domeUartOwner == DOME_UART_DOME ? "true" : "false");
            char dlBuf[384];
            snprintf(dlBuf, sizeof(dlBuf),
                     ",\"dome_link\":{\"state\":\"%s\",\"transport\":\"%s\",\"detail\":\"%s\",\"hb_tx\":%lu,\"hb_rx\":%lu"
                     ",\"rx_overflow\":%lu,\"rx_unknown\":%lu,\"last_rx_ms\":%ld,\"uart_owner\":\"%s\",\"uart_owned_by_dome\":%s}",
                     dlState, dlTransport, dlDetail, (unsigned long)in.bodyHbTx,
                     (unsigned long)in.domeHbRx, (unsigned long)in.domeRxOverflowCount,
                     (unsigned long)in.domeRxUnknownCount, (long)lastRxMs, dlUartOwner,
                     in.domeUartOwner == DOME_UART_DOME ? "true" : "false");
            ok = appendJsonChunk(pos, remaining, dlBuf) && ok;
        }

        // The drive backend's own feedback. The RobotState fields it is read
        // from are no longer named for one controller, but the published key
        // still is: "hoverboard" has a consumer (data/drive.js
        // renderHoverboard) and a documented contract (docs/api.md), so
        // renaming it is an API change with its own callers to move and not a
        // field rename (#346, #304).
        if (in.fbValid) {
            char fbBuf[128];
            snprintf(fbBuf, sizeof(fbBuf),
                     ",\"hoverboard\":{\"batteryV\":%.2f,\"boardTempC\":%.1f"
                     ",\"speedR\":%d,\"speedL\":%d"
                     ",\"currentL\":%.2f,\"currentR\":%.2f}",
                     (double)(in.fbBatteryRaw / 100.0f), (double)(in.fbBoardTempRaw / 10.0f),
                     (int)in.fbSpeedR, (int)in.fbSpeedL, (double)(in.fbCurrentL / 100.0f),
                     (double)(in.fbCurrentR / 100.0f));
            ok = appendJsonChunk(pos, remaining, fbBuf) && ok;
        }

#if PA_CAP_HOSTED_WIFI
        // ESP-Hosted C6 link supervisor state (#189). Board Capability
        // Gate, not runtime config -- absent entirely on boards with no
        // Hosted backend rather than emitted with placeholder values.
        {
            const HostedLinkStatusSnapshot& hl = in.hostedLink;
            char hlBuf[256];
            snprintf(hlBuf, sizeof(hlBuf),
                     ",\"hostedLink\":{\"phase\":\"%s\",\"terminal\":%s,"
                     "\"transportFailureCount\":%u,\"transportUpEventCount\":%u,"
                     "\"attemptCount\":%u,\"totalAttemptCount\":%u,\"recoveredCount\":%u,"
                     "\"lastFailureAtMs\":%lu,\"lastAttemptAtMs\":%lu,\"degradedAtMs\":%lu}",
                     hostedLinkPhaseName(hl.phase),
                     hl.phase == HostedLinkPhase::Degraded ? "true" : "false",
                     hl.transportFailureEventCount, hl.transportUpEventCount, hl.attemptCount,
                     hl.totalAttemptCount, hl.recoveredCount, (unsigned long)hl.lastFailureAtMs,
                     (unsigned long)hl.lastAttemptAtMs, (unsigned long)hl.degradedAtMs);
            ok = appendJsonChunk(pos, remaining, hlBuf) && ok;
        }
#endif

        ok = appendJsonChunk(pos, remaining, "}") && ok;
    }

    if (!ok) {
        snprintf(buffer, bufferSize, "%s", kStatusOverflowBody);
        return false;
    }
    return true;
}

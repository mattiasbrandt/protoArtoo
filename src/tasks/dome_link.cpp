// =============================================================================
// src/tasks/dome_link.cpp
//
// DomeLinkTask — body-side protoR2link transport.
//
// This file is the imperative shell: it gathers inputs, calls
// domeLinkArbiterStep(), and executes the returned actions through the
// concrete transport functions below. All transport-selection policy
// lives in dome_link_arbiter.cpp / include/dome_link_arbiter.h.
//
// Transport model:
//   - Primary: UART2 over slip ring (GPIO 33 TX / GPIO 34 RX)
//   - Fallback: WiFi (UDP heartbeat + HTTP command forwarding)
//
// UART2 ownership model:
//   - UART transport active: DomeLinkTask owns UART2 on S3 pins.
//   - WiFi transport active: UART2 is released/reconfigured for audio RX
//     (GPIO 35) so audio status queries can reclaim the peripheral.
//
// Real-time safety:
//   - Non-blocking queue receive and bounded poll loop
//   - No heap allocation on Core 1 steady-state path
// =============================================================================

#include "dome_link.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "audio_task.h"
#include "config.h"
#include "config_cache.h"
#include "dome_cue_handler.h"
#include "dome_link_arbiter.h"
#include "dome_link_encoding.h"
#include "dome_rx_parser.h"
#include "logging.h"
#include "marcduino.h"
#include "mood.h"
#include "queue_drop_tracker.h"
#include "robot_state.h"

static const char* TAG = "DomeLink";

static HardwareSerial s_domeSerial(2);
static WiFiUDP s_domeUdp;
static bool s_uartOwned = false;
static IPAddress s_lastMdnsResolvedIp;

namespace {

constexpr uint16_t    kDomeUdpPort      = 4901;
constexpr uint32_t    kMdnsRefreshMs    = 5000;
constexpr const char* kDomeMdnsHost     = "astropixelsplus";
constexpr const char* kDomeCmdEndpoint  = "/api/cmd";
constexpr const char* kDomeLayoutEndpoint = "/api/dome/layout";
constexpr uint8_t     kRxBufLen         = 64;
// Dome layout cache size. Overridable because this buffer is the largest single
// allocation in the firmware, and it competes directly with the largest
// contiguous block the web stack's admission floors have to work within.
// Measured on the controller: reclaiming it took the resting largest free block
// from 10228 to 32756 and stopped six concurrent asset fetches shedding five of
// themselves, with no admission floor changed.
#ifndef PA_DOME_LAYOUT_CACHE_BYTES
#define PA_DOME_LAYOUT_CACHE_BYTES 24576
#endif
constexpr size_t      kDomeLayoutCacheCapacity = PA_DOME_LAYOUT_CACHE_BYTES;
constexpr uint32_t    kDomeLayoutRefreshMinIntervalMs = 30000;  // Don't refresh more than every 30s

enum DomeRxSource : uint8_t {
    DOME_RX_UART = 0,
    DOME_RX_WIFI = 1,
};

// Dome layout cache: stores the JSON response from /api/dome/layout.
// Thread-safe access protected by cacheMux.
//
// Heap, allocated on first use, rather than a static array -- which is what
// ADR 0009 describes ("one reused heap buffer") and what the static array was
// quietly deviating from. The difference is not stylistic: a static array costs
// its full size whether the feature is ever used or not, and the only
// configuration that uses it is a droid with the dome reachable over WiFi.
// A bench controller has no dome board at all, so fetchDomeLayoutOverWifi() is
// never reached and the buffer is never allocated -- the memory stays in the
// heap, where the web stack's admission floors need it.
//
// Never freed once allocated: the transport can drop and return, and a buffer
// this size is far easier to obtain once, early, than to reacquire from a
// fragmented heap later.
static uint8_t* s_domeLayoutCache = nullptr;
static DomeLayoutCacheStatus s_domeLayoutCacheStatus = {false, 0, 0, 0};
static portMUX_TYPE s_domeLayoutCacheMux = portMUX_INITIALIZER_UNLOCKED;

// Obtains the cache buffer, allocating it the first time a fetch actually needs
// one. Returns false if the heap cannot provide it, which the caller reports as
// a failed fetch -- the same outcome, and the same 503 from /api/dome/layout,
// that an unreachable dome already produces.
static bool ensureDomeLayoutCache() {
    if (s_domeLayoutCache != nullptr) {
        return true;
    }
    s_domeLayoutCache = (uint8_t*)malloc(kDomeLayoutCacheCapacity);
    if (s_domeLayoutCache == nullptr) {
        PA_LOG_WARN(TAG, "dome layout cache: could not allocate %u bytes",
                    (unsigned)kDomeLayoutCacheCapacity);
        return false;
    }
    PA_LOG_INFO(TAG, "dome layout cache: allocated %u bytes on first fetch",
                (unsigned)kDomeLayoutCacheCapacity);
    return true;
}

static bool s_domeLayoutRefreshRequested = false;
static uint32_t s_domeLayoutLastFetchMs = 0;

static void setTransportState(DomeLinkTransport transport) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeActiveTransport = transport;
    taskEXIT_CRITICAL(&robotStateMux);
}

static void incrementBodyHeartbeatTx() {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.bodyHbTx++;
    taskEXIT_CRITICAL(&robotStateMux);
}

static void incrementDomeRxOverflow() {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeRxOverflowCount++;
    taskEXIT_CRITICAL(&robotStateMux);
}

static void incrementDomeRxUnknown() {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeRxUnknownCount++;
    taskEXIT_CRITICAL(&robotStateMux);
}

static void recordHeartbeatRx(uint32_t nowMs, DomeRxSource source) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeLastSeenMs = nowMs;
    if (source == DOME_RX_UART) {
        robotState.domeLastSeenUartMs = nowMs;
    } else {
        robotState.domeLastSeenWifiMs = nowMs;
    }
    robotState.domeHbRx++;
    taskEXIT_CRITICAL(&robotStateMux);
}

static bool acquireDomeUart() {
    if (s_uartOwned) {
        return true;
    }

    s_domeSerial.end();
    s_domeSerial.begin(9600, SERIAL_8N1, PIN_DOME_RX, PIN_DOME_TX);
    s_uartOwned = true;
    domeUartAcquire(DOME_UART_DOME);

    PA_LOG_DEBUG(TAG, "UART2 ownership -> dome link (GPIO%d/GPIO%d)", PIN_DOME_TX, PIN_DOME_RX);
    return true;
}

static void releaseUartToAudioRx() {
    if (!s_uartOwned) {
        return;
    }

    s_domeSerial.end();
    // Reconfigure UART2 RX to audio module status pin so AudioTask queries can run.
    s_domeSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, -1);
    s_uartOwned = false;
    domeUartRelease(DOME_UART_DOME);
    // Intentionally leave owner at DOME_UART_NONE. AudioTask acquires
    // DOME_UART_AUDIO before each query and releases after — pre-acquiring
    // here caused a spurious duplicate-acquire WARN on every audio poll cycle.

    PA_LOG_DEBUG(TAG, "UART2 ownership -> audio RX (GPIO%d)", PIN_AUDIO_RX);
}

static bool readConfiguredPeerIp(IPAddress* out) {
    if (out == nullptr) {
        return false;
    }

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    char ipBuf[16] = {0};
    snprintf(ipBuf, sizeof(ipBuf), "%s", cfg.dome.dome_wifi_peer_ip);

    if (ipBuf[0] == '\0') {
        return false;
    }

    IPAddress parsed;
    if (!parsed.fromString(ipBuf)) {
        return false;
    }

    *out = parsed;
    return true;
}

static bool resolveDomePeerIp(uint32_t nowMs, bool staConnected, bool cachedPeerValid,
                              uint32_t* inOutLastMdnsLookupMs, IPAddress* inOutPeerIp,
                              bool* outUsingManualIp) {
    if (inOutLastMdnsLookupMs == nullptr || inOutPeerIp == nullptr || outUsingManualIp == nullptr) {
        return false;
    }

    *outUsingManualIp = false;

    IPAddress manualIp;
    if (readConfiguredPeerIp(&manualIp)) {
        *inOutPeerIp = manualIp;
        *outUsingManualIp = true;
        return true;
    }

    if (!staConnected) {
        return false;
    }

    if ((uint32_t)(nowMs - *inOutLastMdnsLookupMs) < kMdnsRefreshMs) {
        return cachedPeerValid;
    }

    *inOutLastMdnsLookupMs = nowMs;

    IPAddress resolved = MDNS.queryHost(kDomeMdnsHost);
    if (resolved[0] == 0 && resolved[1] == 0 && resolved[2] == 0 && resolved[3] == 0) {
        PA_LOG_DEBUG(TAG, "mDNS lookup failed for %s.local", kDomeMdnsHost);
        return cachedPeerValid;
    }

    *inOutPeerIp = resolved;
    if (resolved != s_lastMdnsResolvedIp) {
        s_lastMdnsResolvedIp = resolved;
        PA_LOG_INFO(TAG, "mDNS resolved %s.local -> %s", kDomeMdnsHost,
                    inOutPeerIp->toString().c_str());
    } else {
        PA_LOG_DEBUG(TAG, "mDNS re-resolved %s.local -> %s (unchanged)", kDomeMdnsHost,
                     inOutPeerIp->toString().c_str());
    }
    return true;
}

static bool sendCommandOverWifi(const IPAddress& peerIp, const char* cmd) {
    if (cmd == nullptr || cmd[0] == '\0') {
        return false;
    }

    char url[80];
    snprintf(url, sizeof(url), "http://%u.%u.%u.%u%s", (unsigned)peerIp[0], (unsigned)peerIp[1],
             (unsigned)peerIp[2], (unsigned)peerIp[3], kDomeCmdEndpoint);

    char body[128];
    int prefixLen = snprintf(body, sizeof(body), "cmd=");
    if (prefixLen < 0 || (size_t)prefixLen >= sizeof(body)) {
        PA_LOG_WARN(TAG, "WiFi cmd body prefix overflow");
        return false;
    }
    const size_t prefixSize = (size_t)prefixLen;
    size_t encodedLen = 0;
    if (!domeLinkUrlEncodeInto(body + prefixSize, sizeof(body) - prefixSize, cmd, &encodedLen)) {
        PA_LOG_WARN(TAG, "WiFi cmd body overflow: %s", cmd);
        return false;
    }
    const size_t bodyLen = prefixSize + encodedLen;

    HTTPClient http;
    http.setConnectTimeout(250);
    http.setTimeout(250);
    if (!http.begin(url)) {
        PA_LOG_WARN(TAG, "WiFi cmd begin failed: %s", url);
        return false;
    }

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int status = http.POST((uint8_t*)body, bodyLen);
    http.end();

    if (status >= 200 && status < 300) {
        PA_LOG_DEBUG(TAG, "TX WIFI HTTP: %s", cmd);
        return true;
    }

    PA_LOG_WARN(TAG, "WiFi cmd POST failed (%d): %s", status, cmd);
    return false;
}

static bool sendHeartbeatOverUdp(const IPAddress& peerIp) {
    if (!s_domeUdp.beginPacket(peerIp, kDomeUdpPort)) {
        return false;
    }
    s_domeUdp.write((const uint8_t*)MD_BODY_HB, strlen(MD_BODY_HB));
    return s_domeUdp.endPacket() == 1;
}

static bool parseDomeRxLine(const char* line, DomeRxSource source, uint32_t nowMs,
                            bool* heartbeatSeenOut) {
    if (heartbeatSeenOut != nullptr) {
        *heartbeatSeenOut = false;
    }

    if (line == nullptr || line[0] == '\0') {
        return false;
    }

    if (strncmp(line, MD_DOME_HB, 5) == 0) {
        recordHeartbeatRx(nowMs, source);
        if (heartbeatSeenOut != nullptr) {
            *heartbeatSeenOut = true;
        }
        return true;
    }

    uint8_t moodId = moodIdFromSeCommand(line);
    if (moodId != 0) {
        applyMood(moodId, true);
        return true;
    }

    if (strncmp(line, "dome=seqon,", 11) == 0) {
        unsigned int secs = (unsigned int)atoi(line + 11);
        taskENTER_CRITICAL(&robotStateMux);
        robotState.domeSeqActive  = true;
        robotState.domeSeqUntilMs = nowMs + ((uint32_t)secs * 1000UL);
        taskEXIT_CRITICAL(&robotStateMux);
        audioQueueDollar("$O", SRC_INTERNAL);
        PA_LOG_INFO(TAG, "dome seq start, timeout %u s", secs);
        return true;
    }
    if (strcmp(line, "dome=seqoff") == 0) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.domeSeqActive  = false;
        robotState.domeSeqUntilMs = 0;
        taskEXIT_CRITICAL(&robotStateMux);
        audioQueueDollar("$R", SRC_INTERNAL);
        PA_LOG_INFO(TAG, "dome seq end");
        return true;
    }
    if (strncmp(line, "dome=rot,", 9) == 0) {
        int speedPct = 0;
        unsigned int durMs = 0;
        if (sscanf(line + 9, "%d,%u", &speedPct, &durMs) == 2) {
            if (speedPct > 100) speedPct = 100;
            if (speedPct < -100) speedPct = -100;
            DomeCommand rotCmd = {};
            rotCmd.speed      = (float)speedPct / 100.0f;
            rotCmd.durationMs = (uint32_t)durMs;
            rotCmd.source     = SRC_INTERNAL;
            rotCmd.timestampMs = nowMs;
            if (xQueueSend(domeCmdQueue, &rotCmd, 0) != pdTRUE) {
                PA_LOG_WARN(TAG, "dome=rot dropped: queue full");
            } else {
                PA_LOG_INFO(TAG, "dome seq rot: %d%% for %u ms", speedPct, durMs);
            }
        }
        return true;
    }
    if (strncmp(line, "BD:", 3) == 0) {
        handleDomeCue(line + 3);
        return true;
    }

    bool handled = parseMarcduinoCommand(line);
    if (!handled) {
        incrementDomeRxUnknown();
        PA_LOG_DEBUG(TAG, "Unknown/ignored dome RX line: %s", line);
    }
    return handled;
}

static void processUartRx(uint32_t nowMs, uint32_t* inOutLastUartHeartbeatMs) {
    static char rxBuf[kRxBufLen] = {0};
    static uint8_t rxLen = 0;

    while (s_uartOwned && s_domeSerial.available()) {
        char c = (char)s_domeSerial.read();
        if (c == '\r' || c == '\n') {
            if (rxLen == 0) {
                continue;
            }
            rxBuf[rxLen] = '\0';
            bool heartbeatSeen = false;
            parseDomeRxLine(rxBuf, DOME_RX_UART, nowMs, &heartbeatSeen);
            if (heartbeatSeen && inOutLastUartHeartbeatMs != nullptr) {
                *inOutLastUartHeartbeatMs = nowMs;
            }
            rxLen = 0;
            continue;
        }

        if (rxLen < (kRxBufLen - 1)) {
            rxBuf[rxLen++] = c;
            continue;
        }

        // Line overflow.
        incrementDomeRxOverflow();
        PA_LOG_WARN(TAG, "UART RX overflow, discarding line");
        rxLen = 0;
    }
}

static void processUdpRx(uint32_t nowMs, uint32_t* inOutLastWifiHeartbeatMs) {
    char lineBuf[kRxBufLen] = {0};

    int packetLen = s_domeUdp.parsePacket();
    while (packetLen > 0) {
        const int maxPayload = (int)sizeof(lineBuf) - 1;
        const int toRead = packetLen > maxPayload ? maxPayload : packetLen;
        int n = s_domeUdp.read((uint8_t*)lineBuf, (size_t)toRead);
        if (n < 0) {
            n = 0;
        }
        lineBuf[n] = '\0';

        if (packetLen > toRead) {
            // Drain remaining bytes from oversized packet.
            uint8_t sink[16];
            int remaining = packetLen - toRead;
            while (remaining > 0) {
                int chunk = remaining > (int)sizeof(sink) ? (int)sizeof(sink) : remaining;
                int drained = s_domeUdp.read(sink, (size_t)chunk);
                if (drained <= 0) {
                    break;
                }
                remaining -= drained;
            }
            incrementDomeRxOverflow();
            PA_LOG_WARN(TAG, "UDP RX overflow (%d bytes), truncated", packetLen);
        }

        while (n > 0 && (lineBuf[n - 1] == '\r' || lineBuf[n - 1] == '\n')) {
            lineBuf[n - 1] = '\0';
            --n;
        }

        bool heartbeatSeen = false;
        parseDomeRxLine(lineBuf, DOME_RX_WIFI, nowMs, &heartbeatSeen);
        if (heartbeatSeen && inOutLastWifiHeartbeatMs != nullptr) {
            *inOutLastWifiHeartbeatMs = nowMs;
        }

        packetLen = s_domeUdp.parsePacket();
    }
}

}  // namespace

bool domeUartAcquire(DomeUartOwner requester) {
    if (requester == DOME_UART_NONE) {
        PA_LOG_WARN(TAG, "UART2 acquire ignored for NONE requester");
        return false;
    }

    taskENTER_CRITICAL(&robotStateMux);
    DomeUartOwner current = robotState.domeUartOwner;
    if (current == requester) {
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_DEBUG(TAG, "UART2 acquire duplicate by owner=%u", (unsigned)requester);
        return true;
    }
    if (current == DOME_UART_AUDIO && requester == DOME_UART_DOME) {
        robotState.domeUartOwner = requester;
        taskEXIT_CRITICAL(&robotStateMux);
        return true;
    }
    if (current != DOME_UART_NONE) {
        taskEXIT_CRITICAL(&robotStateMux);
        if (requester == DOME_UART_AUDIO) {
            PA_LOG_DEBUG(TAG, "UART2 acquire denied owner=%u requester=%u", (unsigned)current,
                         (unsigned)requester);
        } else {
            PA_LOG_WARN(TAG, "UART2 acquire denied owner=%u requester=%u", (unsigned)current,
                        (unsigned)requester);
        }
        return false;
    }
    robotState.domeUartOwner = requester;
    taskEXIT_CRITICAL(&robotStateMux);
    return true;
}

void domeUartRelease(DomeUartOwner requester) {
    if (requester == DOME_UART_NONE) {
        PA_LOG_WARN(TAG, "UART2 release ignored for NONE requester");
        return;
    }

    taskENTER_CRITICAL(&robotStateMux);
    DomeUartOwner current = robotState.domeUartOwner;
    if (current == requester) {
        robotState.domeUartOwner = DOME_UART_NONE;
        taskEXIT_CRITICAL(&robotStateMux);
        return;
    }
    taskEXIT_CRITICAL(&robotStateMux);
    PA_LOG_WARN(TAG, "UART2 release ignored owner=%u requester=%u", (unsigned)current,
                (unsigned)requester);
}

bool domeUartOwnedBy(DomeUartOwner owner) {
    taskENTER_CRITICAL(&robotStateMux);
    bool owned = robotState.domeUartOwner == owner;
    taskEXIT_CRITICAL(&robotStateMux);
    return owned;
}

// -----------------------------------------------------------------------------
// domeQueueTx()
// Non-blocking enqueue. Increments queueOverflowCount on full queue.
// -----------------------------------------------------------------------------
bool domeQueueTx(const char* cmd) {
    if (!cmd || !*cmd) {
        return false;
    }

    DomeTxCmd msg{};
    strncpy(msg.buf, cmd, sizeof(msg.buf) - 1);
    msg.buf[sizeof(msg.buf) - 1] = '\0';
    if (xQueueSend(domeTxQueue, &msg, 0) != pdTRUE) {
        logQueueDrop(QUEUE_DOME_TX, "TX command");
        return false;
    }
    return true;
}

bool domeConnected() {
    taskENTER_CRITICAL(&robotStateMux);
    uint32_t lastSeen = robotState.domeLastSeenMs;
    taskEXIT_CRITICAL(&robotStateMux);
    return lastSeen > 0 && (millis() - lastSeen) < kDomeLinkHeartbeatTimeoutMs;
}

// -----------------------------------------------------------------------------
// domeLayoutCacheReadChunk()
// Copies up to maxLen bytes starting at offset from the cache into outBuf.
// fetchedAtMs pins the read to a specific cache generation: if the cache has
// been overwritten by a newer fetch since the caller observed it via
// domeLayoutCacheGetStatus(), this returns 0 instead of splicing bytes from
// two different fetches. Designed for small per-call maxLen (chunked response
// filler), so callers never need a full-size buffer of their own.
// Thread-safe via s_domeLayoutCacheMux.
// -----------------------------------------------------------------------------
size_t domeLayoutCacheReadChunk(uint8_t* outBuf, size_t maxLen, size_t offset, uint32_t fetchedAtMs) {
    if (outBuf == nullptr || maxLen == 0) {
        return 0;
    }

    taskENTER_CRITICAL(&s_domeLayoutCacheMux);
    size_t copied = 0;
    // The buffer is checked as well as has_data. has_data can only become true
    // after a fetch that allocated it, so the two agree -- but this read is the
    // one path that dereferences the pointer, and it must not depend on that
    // invariant holding somewhere else.
    if (s_domeLayoutCache != nullptr && s_domeLayoutCacheStatus.has_data &&
        s_domeLayoutCacheStatus.fetched_at_ms == fetchedAtMs &&
        offset < s_domeLayoutCacheStatus.length) {
        size_t remaining = s_domeLayoutCacheStatus.length - offset;
        copied = remaining < maxLen ? remaining : maxLen;
        memcpy(outBuf, s_domeLayoutCache + offset, copied);
    }
    taskEXIT_CRITICAL(&s_domeLayoutCacheMux);

    return copied;
}

// -----------------------------------------------------------------------------
// domeLayoutCacheGetStatus()
// Returns the current cache status (has_data, length, fetched_at_ms, last_http_status).
// Thread-safe via s_domeLayoutCacheMux.
// -----------------------------------------------------------------------------
DomeLayoutCacheStatus domeLayoutCacheGetStatus() {
    taskENTER_CRITICAL(&s_domeLayoutCacheMux);
    DomeLayoutCacheStatus status = s_domeLayoutCacheStatus;
    taskEXIT_CRITICAL(&s_domeLayoutCacheMux);
    return status;
}

// -----------------------------------------------------------------------------
// domeLayoutCacheRefreshRequested()
// Called by web handler to request an on-demand refresh.
// Returns true if a refresh should be initiated; false if throttled.
// Thread-safe.
// -----------------------------------------------------------------------------
bool domeLayoutCacheRefreshRequested() {
    uint32_t now = millis();
    if ((uint32_t)(now - s_domeLayoutLastFetchMs) < kDomeLayoutRefreshMinIntervalMs) {
        // Throttle: don't refresh more than every 30s
        return false;
    }
    s_domeLayoutRefreshRequested = true;
    return true;
}

// Internal function: mark refresh as not pending (called by domeLinkTask after fetch attempt)
static void clearDomeLayoutRefreshRequested() {
    s_domeLayoutRefreshRequested = false;
    s_domeLayoutLastFetchMs = millis();
}

// Internal function: invalidate the cache before a fetch touches any bytes.
// domeLayoutCacheReadChunk() checks has_data before ever reading
// s_domeLayoutCache, so once this critical section exits, any reader that
// acquires the mutex afterward bails out immediately instead of reading a
// buffer that's about to be overwritten in place. A reader that already
// completed its critical section before this call is unaffected -- it read
// the complete previous fetch. This makes it safe for fetchDomeLayoutOverWifi()
// to stream new bytes directly into s_domeLayoutCache without a private
// staging buffer (there isn't DRAM budget for a second ~24KB copy) and without
// holding the mutex across the network read (a spinlock must never be held
// across a blocking call).
static void invalidateDomeLayoutCache() {
    taskENTER_CRITICAL(&s_domeLayoutCacheMux);
    s_domeLayoutCacheStatus.has_data = false;
    taskEXIT_CRITICAL(&s_domeLayoutCacheMux);
}

// Internal function: publish a fetch result. On success (length > 0), bytes
// must already be written into s_domeLayoutCache by the caller, after a prior
// invalidateDomeLayoutCache() call. On failure (length == 0), the cache stays
// empty/invalidated and the client-facing endpoint returns 503 -- the browser
// falls back to its own localStorage cache (ADR 0009 tier 2), so this does not
// need to preserve a stale body-side copy.
static void publishDomeLayoutCache(size_t length, int httpStatus) {
    taskENTER_CRITICAL(&s_domeLayoutCacheMux);
    s_domeLayoutCacheStatus.has_data = (length > 0);
    s_domeLayoutCacheStatus.length = length;
    s_domeLayoutCacheStatus.last_http_status = httpStatus;
    s_domeLayoutCacheStatus.fetched_at_ms = millis();
    taskEXIT_CRITICAL(&s_domeLayoutCacheMux);

    if (length > 0) {
        PA_LOG_INFO(TAG, "dome layout cache: stored %u bytes (status=%d)", (unsigned)length, httpStatus);
    }
}

// Internal function: attempt to fetch dome layout over WiFi
static bool fetchDomeLayoutOverWifi(const IPAddress& peerIp) {
    char url[96];
    snprintf(url, sizeof(url), "http://%u.%u.%u.%u%s", (unsigned)peerIp[0], (unsigned)peerIp[1],
             (unsigned)peerIp[2], (unsigned)peerIp[3], kDomeLayoutEndpoint);

    // Layout fetch is off the hot control path (WiFi-fallback transport only,
    // on-demand and throttled to once per 30s), unlike sendCommandOverWifi's
    // latency-sensitive 250ms budget, so it can afford a more generous window
    // for the dome to compose and send the ~12.5KB payload.
    // Before opening the connection: a fetch with nowhere to put the bytes is
    // just load on the dome and on this task. Reported as a failed fetch, which
    // leaves /api/dome/layout answering the same 503 it already answers when the
    // dome is unreachable.
    if (!ensureDomeLayoutCache()) {
        publishDomeLayoutCache(0, 0);
        return false;
    }

    HTTPClient http;
    http.setConnectTimeout(500);
    http.setTimeout(2000);

    if (!http.begin(url)) {
        PA_LOG_WARN(TAG, "dome layout fetch begin failed: %s", url);
        publishDomeLayoutCache(0, 0);
        return false;
    }

    // Invalidate before touching any bytes: see invalidateDomeLayoutCache()
    // comment. Safe from here on to write s_domeLayoutCache directly without a
    // private staging buffer or holding the mutex across the network read.
    invalidateDomeLayoutCache();

    int status = http.GET();
    PA_LOG_DEBUG(TAG, "dome layout fetch: HTTP %d from %s", status, url);

    if (status >= 200 && status < 300) {
        WiFiClient* stream = http.getStreamPtr();
        if (stream == nullptr) {
            PA_LOG_WARN(TAG, "dome layout fetch: no stream for status %d", status);
            http.end();
            publishDomeLayoutCache(0, status);
            return false;
        }

        // stream->available() alone under-reports mid-transfer: the next TCP
        // segment may not have arrived yet even though the connection is still
        // open, so a loop keyed only on available() can stop early with a
        // truncated body. readBytes() blocks internally up to the WiFiClient
        // timeout (set via http.setTimeout above) waiting for requested bytes,
        // so keep reading as long as there is buffered data OR the connection
        // is still open; a read that returns nothing (genuine stall or the
        // peer closing after the last byte) ends the loop.
        size_t bytesRead = 0;
        uint8_t readBuf[256];
        while (bytesRead < kDomeLayoutCacheCapacity &&
               (stream->available() > 0 || http.connected())) {
            size_t toRead = sizeof(readBuf);
            if (bytesRead + toRead > kDomeLayoutCacheCapacity) {
                toRead = kDomeLayoutCacheCapacity - bytesRead;
            }
            int n = stream->readBytes(readBuf, toRead);
            if (n <= 0) break;
            memcpy(s_domeLayoutCache + bytesRead, readBuf, n);
            bytesRead += n;
        }

        const int contentLength = http.getSize();  // -1 if unknown (e.g. chunked)
        http.end();

        if (bytesRead == 0) {
            PA_LOG_WARN(TAG, "dome layout fetch: empty body (status=%d)", status);
            publishDomeLayoutCache(0, status);
            return false;
        }

        // Reject a body that looks truncated rather than publishing a partial/
        // corrupt payload: either it fell short of the declared Content-Length,
        // or it filled the cache exactly (the observed live payload is ~12.5KB
        // against a 24KB cap, so hitting the cap means real bytes were dropped,
        // not that the layout legitimately needs the full capacity).
        const bool shortOfContentLength = contentLength >= 0 && (size_t)contentLength != bytesRead;
        const bool filledCapacity = bytesRead >= kDomeLayoutCacheCapacity;
        if (shortOfContentLength || filledCapacity) {
            PA_LOG_WARN(TAG, "dome layout fetch: truncated body (got %u bytes, content-length %d, cap %u)",
                        (unsigned)bytesRead, contentLength, (unsigned)kDomeLayoutCacheCapacity);
            publishDomeLayoutCache(0, status);
            return false;
        }

        publishDomeLayoutCache(bytesRead, status);
        return true;
    }

    PA_LOG_WARN(TAG, "dome layout fetch failed: status %d", status);
    http.end();
    publishDomeLayoutCache(0, status);
    return false;
}

// -----------------------------------------------------------------------------
// domeLinkTask()
// -----------------------------------------------------------------------------
void domeLinkTask(void* pvParameters) {
    (void)pvParameters;

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    bool enabled = cfg.system.enable_s3_dome_ctrl;

    if (!enabled) {
        setTransportState(DOME_LINK_TRANSPORT_DISCONNECTED);
        PA_LOG_DEBUG(TAG, "dome link disabled (en_s3=false) — task idle");

        DomeTxCmd cmd{};
        for (;;) {
            xQueueReceive(domeTxQueue, &cmd, pdMS_TO_TICKS(5000));
        }
    }

    acquireDomeUart();
    setTransportState(DOME_LINK_TRANSPORT_UART);

    DomeLinkArbiterState arbiter    = {};
    uint32_t lastUartHeartbeatMs    = 0;
    uint32_t lastMdnsLookupMs       = 0;
    DomeLinkTransport lastLoggedTransport = DOME_LINK_TRANSPORT_UART;

    IPAddress peerIp;
    bool peerKnown  = false;
    bool peerManual = false;
    bool udpReady   = false;

    DomeTxCmd txCmd{};

    for (;;) {
        const uint32_t now = millis();
        const bool staConnected = WiFi.status() == WL_CONNECTED;

        if (staConnected && !udpReady) {
            udpReady = s_domeUdp.begin(kDomeUdpPort) == 1;
            if (!udpReady) {
                PA_LOG_WARN(TAG, "UDP bind failed on port %u", (unsigned)kDomeUdpPort);
            }
        }

        peerKnown = resolveDomePeerIp(now, staConnected, peerKnown, &lastMdnsLookupMs, &peerIp,
                                      &peerManual);

        const uint32_t prevUartHbMs = lastUartHeartbeatMs;
        processUartRx(now, &lastUartHeartbeatMs);
        if (udpReady) {
            processUdpRx(now, nullptr);
        }

        // Dome sequence timeout watchdog — untouched; ADR 0004 replaces these.
        {
            taskENTER_CRITICAL(&robotStateMux);
            bool seqActive    = robotState.domeSeqActive;
            uint32_t seqUntil = robotState.domeSeqUntilMs;
            taskEXIT_CRITICAL(&robotStateMux);
            if (seqActive && (int32_t)(now - seqUntil) >= 0) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.domeSeqActive  = false;
                robotState.domeSeqUntilMs = 0;
                taskEXIT_CRITICAL(&robotStateMux);
                audioQueueDollar("$R", SRC_INTERNAL);
                PA_LOG_WARN(TAG, "dome seq timeout -- resuming random");
            }
        }

        // Gather arbiter inputs and step.
        DomeLinkArbiterInputs inp = {};
        inp.nowMs             = now;
        inp.uartHeartbeatSeen = (lastUartHeartbeatMs != prevUartHbMs);
        inp.staConnected      = staConnected;
        inp.peerKnown         = peerKnown;
        inp.domeConnected     = domeConnected();
        {
            taskENTER_CRITICAL(&robotStateMux);
            inp.bodySleeping = robotState.sleepMode;
            taskEXIT_CRITICAL(&robotStateMux);
        }

        DomeLinkArbiterActions act = domeLinkArbiterStep(arbiter, inp);

        // Execute transport actions.
        if (act.acquireUart) {
            acquireDomeUart();
        }
        if (act.sendUartProbe) {
            s_domeSerial.print(MD_BODY_HB);
            PA_LOG_DEBUG(TAG, "UART probe: sent #PAHB, waiting %u ms for #APHB",
                         (unsigned)kDomeLinkUartProbeWindowMs);
        }
        if (act.releaseUartToAudio) {
            releaseUartToAudioRx();
        }

        setTransportState(act.txRoute);
        if (act.txRoute == DOME_LINK_TRANSPORT_WIFI &&
            lastLoggedTransport != DOME_LINK_TRANSPORT_WIFI) {
            PA_LOG_INFO(TAG, "transport fallback: UART unavailable, using WiFi UDP to %u.%u.%u.%u",
                        (unsigned)peerIp[0], (unsigned)peerIp[1],
                        (unsigned)peerIp[2], (unsigned)peerIp[3]);
            // Fetch dome layout on WiFi transition
            fetchDomeLayoutOverWifi(peerIp);
            clearDomeLayoutRefreshRequested();
        } else if (act.txRoute == DOME_LINK_TRANSPORT_UART &&
                   lastLoggedTransport == DOME_LINK_TRANSPORT_WIFI) {
            PA_LOG_INFO(TAG, "transport recovery: UART heartbeat restored, switching from WiFi UDP");
        }
        lastLoggedTransport = act.txRoute;

        // On-demand dome layout refresh (if requested by web handler)
        if (s_domeLayoutRefreshRequested && act.txRoute == DOME_LINK_TRANSPORT_WIFI &&
            staConnected && peerKnown) {
            fetchDomeLayoutOverWifi(peerIp);
            clearDomeLayoutRefreshRequested();
        }

        // Sleep-sync action from arbiter.
        if (act.sleepSync != SleepSyncAction::None) {
            const char* sleepCmd = (act.sleepSync == SleepSyncAction::SendSleep)
                                       ? MD_BODY_SLEEP
                                       : MD_BODY_WAKE;
            const bool  sleeping = (act.sleepSync == SleepSyncAction::SendSleep);

            if (act.txRoute == DOME_LINK_TRANSPORT_UART && s_uartOwned) {
                s_domeSerial.print(sleepCmd);
                PA_LOG_INFO(TAG, "TX UART sleep sync: %s", sleeping ? "sleep" : "wake");
            } else if (act.txRoute == DOME_LINK_TRANSPORT_WIFI && staConnected && peerKnown) {
                if (sendCommandOverWifi(peerIp, sleepCmd)) {
                    PA_LOG_INFO(TAG, "TX WiFi sleep sync (%s): %s",
                                peerManual ? "manual-ip" : "mdns",
                                sleeping ? "sleep" : "wake");
                }
            }
        }

        // Queue drain.
        while (xQueueReceive(domeTxQueue, &txCmd, 0) == pdTRUE) {
            if (act.txRoute == DOME_LINK_TRANSPORT_UART && s_uartOwned) {
                s_domeSerial.print(txCmd.buf);
                s_domeSerial.print('\r');
                PA_LOG_DEBUG(TAG, "TX UART: %s", txCmd.buf);
                continue;
            }
            if (act.txRoute == DOME_LINK_TRANSPORT_WIFI && staConnected && peerKnown) {
                sendCommandOverWifi(peerIp, txCmd.buf);
                continue;
            }
            PA_LOG_WARN(TAG, "TX dropped (no active transport): %s", txCmd.buf);
        }

        // Heartbeat TX.
        if (act.sendHeartbeat) {
            if (act.txRoute == DOME_LINK_TRANSPORT_UART && s_uartOwned) {
                s_domeSerial.print(MD_BODY_HB);
                incrementBodyHeartbeatTx();
                PA_LOG_DEBUG(TAG, "TX UART heartbeat");
            } else if (act.txRoute == DOME_LINK_TRANSPORT_WIFI && staConnected && peerKnown) {
                if (sendHeartbeatOverUdp(peerIp)) {
                    incrementBodyHeartbeatTx();
                    PA_LOG_DEBUG(TAG, "TX WiFi heartbeat (%s)", peerManual ? "manual-ip" : "mdns");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// =============================================================================
// src/tasks/dome_link.cpp
//
// DomeLinkTask — body-side protoR2link transport.
//
// Transport model:
//   - Primary: UART2 over slip ring (GPIO 33 TX / GPIO 34 RX)
//   - Fallback: WiFi (UDP heartbeat + HTTP command forwarding)
//
// UART2 ownership model:
//   - UART transport active: DomeLinkTask owns UART2 on S3 pins.
//   - WiFi transport active: UART2 is released/reconfigured for audio RX (GPIO 35)
//     so audio status queries can reclaim the peripheral.
//
// Real-time safety:
//   - Non-blocking queue receive and bounded poll loop
//   - No heap allocation on Core 1 steady-state path
// =============================================================================

#include "dome_link.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "config.h"
#include "dome_link_encoding.h"
#include "dome_rx_parser.h"
#include "logging.h"
#include "marcduino.h"
#include "mood.h"
#include "robot_state.h"

static const char* TAG = "DomeLink";

static HardwareSerial s_domeSerial(2);
static WiFiUDP s_domeUdp;
static bool s_uartOwned = false;

namespace {

constexpr uint16_t kDomeUdpPort = 4901;
constexpr uint32_t kHeartbeatIntervalMs = 1000;
constexpr uint32_t kHeartbeatTimeoutMs = 5000;
constexpr uint32_t kUartProbeIntervalMs = 1000;
constexpr uint32_t kUartProbeWindowMs = 150;
constexpr uint32_t kMdnsRefreshMs = 5000;
constexpr const char* kDomeMdnsHost = "astropixelsplus";
constexpr const char* kBodyMdnsHost = "protoartoo";
constexpr const char* kDomeCmdEndpoint = "/api/cmd";
constexpr uint8_t kRxBufLen = 64;

enum DomeRxSource : uint8_t {
    DOME_RX_UART = 0,
    DOME_RX_WIFI = 1,
};

static void setTransportState(DomeLinkTransport transport, bool uartOwned) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeActiveTransport = transport;
    robotState.domeUartOwned = uartOwned;
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

    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeUartOwned = true;
    taskEXIT_CRITICAL(&robotStateMux);

    PA_LOG_INFO(TAG, "UART2 ownership -> dome link (GPIO%d/GPIO%d)", PIN_DOME_TX, PIN_DOME_RX);
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

    taskENTER_CRITICAL(&robotStateMux);
    robotState.domeUartOwned = false;
    taskEXIT_CRITICAL(&robotStateMux);

    PA_LOG_INFO(TAG, "UART2 ownership -> audio RX (GPIO%d)", PIN_AUDIO_RX);
}

static bool readConfiguredPeerIp(IPAddress* out) {
    if (out == nullptr) {
        return false;
    }

    char ipBuf[16] = {0};
    taskENTER_CRITICAL(&robotStateMux);
    snprintf(ipBuf, sizeof(ipBuf), "%s", robotState.cfg_dome_wifi_peer_ip);
    taskEXIT_CRITICAL(&robotStateMux);

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
    PA_LOG_INFO(TAG, "mDNS resolved %s.local -> %s", kDomeMdnsHost, inOutPeerIp->toString().c_str());
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
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_WARN(TAG, "TX queue full, dropped: %s", cmd);
        return false;
    }
    return true;
}

bool domeConnected() {
    taskENTER_CRITICAL(&robotStateMux);
    uint32_t lastSeen = robotState.domeLastSeenMs;
    taskEXIT_CRITICAL(&robotStateMux);
    return lastSeen > 0 && (millis() - lastSeen) < kHeartbeatTimeoutMs;
}

// -----------------------------------------------------------------------------
// domeLinkTask()
// -----------------------------------------------------------------------------
void domeLinkTask(void* pvParameters) {
    (void)pvParameters;

    taskENTER_CRITICAL(&robotStateMux);
    bool enabled = robotState.cfg_enable_s3_dome_ctrl;
    taskEXIT_CRITICAL(&robotStateMux);

    if (!enabled) {
        setTransportState(DOME_LINK_TRANSPORT_DISCONNECTED, false);
        PA_LOG_INFO(TAG, "dome link disabled (en_s3=false) — task idle");

        DomeTxCmd cmd{};
        for (;;) {
            xQueueReceive(domeTxQueue, &cmd, pdMS_TO_TICKS(5000));
        }
    }

    acquireDomeUart();
    setTransportState(DOME_LINK_TRANSPORT_UART, true);

    uint32_t lastHeartbeatTxMs = 0;
    uint32_t lastUartHeartbeatMs = 0;
    uint32_t lastUartProbeMs = 0;
    uint32_t uartProbeWindowUntilMs = 0;
    uint32_t lastMdnsLookupMs = 0;

    IPAddress peerIp;
    bool peerKnown = false;
    bool peerManual = false;
    bool mdnsReady = false;
    bool udpReady = false;

    DomeTxCmd txCmd{};

    for (;;) {
        const uint32_t now = millis();
        const bool staConnected = WiFi.status() == WL_CONNECTED;
        if (!staConnected) {
            mdnsReady = false;
        } else if (!mdnsReady) {
            mdnsReady = MDNS.begin(kBodyMdnsHost);
            if (!mdnsReady) {
                PA_LOG_WARN(TAG, "mDNS init failed for host %s", kBodyMdnsHost);
            }
        }

        if (staConnected && !udpReady) {
            udpReady = s_domeUdp.begin(kDomeUdpPort) == 1;
            if (!udpReady) {
                PA_LOG_WARN(TAG, "UDP bind failed on port %u", (unsigned)kDomeUdpPort);
            }
        }

        peerKnown = resolveDomePeerIp(now, staConnected, peerKnown, &lastMdnsLookupMs, &peerIp,
                                      &peerManual);

        processUartRx(now, &lastUartHeartbeatMs);
        if (udpReady) {
            processUdpRx(now, nullptr);
        }

        const bool uartFresh =
            lastUartHeartbeatMs > 0 && (uint32_t)(now - lastUartHeartbeatMs) < kHeartbeatTimeoutMs;
        const bool initialUartGrace =
            lastUartHeartbeatMs == 0 && (uint32_t)now < kHeartbeatTimeoutMs;

        DomeLinkTransport desiredTransport = DOME_LINK_TRANSPORT_UART;
        if (!uartFresh && !initialUartGrace && staConnected && peerKnown) {
            desiredTransport = DOME_LINK_TRANSPORT_WIFI;
        }

        if (desiredTransport == DOME_LINK_TRANSPORT_UART) {
            uartProbeWindowUntilMs = 0;
            acquireDomeUart();
        } else {
            if ((uint32_t)(now - lastUartProbeMs) >= kUartProbeIntervalMs) {
                lastUartProbeMs = now;
                if (acquireDomeUart()) {
                    uartProbeWindowUntilMs = now + kUartProbeWindowMs;
                    // Probe ping for failback detection; not counted as active transport heartbeat.
                    s_domeSerial.print(MD_BODY_HB);
                }
            }

            if (uartFresh) {
                desiredTransport = DOME_LINK_TRANSPORT_UART;
                uartProbeWindowUntilMs = 0;
            } else if (s_uartOwned && uartProbeWindowUntilMs != 0 &&
                       (int32_t)(now - uartProbeWindowUntilMs) >= 0) {
                releaseUartToAudioRx();
                uartProbeWindowUntilMs = 0;
            }
        }

        setTransportState(desiredTransport, s_uartOwned);

        while (xQueueReceive(domeTxQueue, &txCmd, 0) == pdTRUE) {
            if (desiredTransport == DOME_LINK_TRANSPORT_UART && s_uartOwned) {
                s_domeSerial.print(txCmd.buf);
                s_domeSerial.print('\r');
                PA_LOG_DEBUG(TAG, "TX UART: %s", txCmd.buf);
                continue;
            }

            if (desiredTransport == DOME_LINK_TRANSPORT_WIFI && staConnected && peerKnown) {
                sendCommandOverWifi(peerIp, txCmd.buf);
                continue;
            }

            PA_LOG_WARN(TAG, "TX dropped (no active transport): %s", txCmd.buf);
        }

        if ((uint32_t)(now - lastHeartbeatTxMs) >= kHeartbeatIntervalMs) {
            lastHeartbeatTxMs = now;

            if (desiredTransport == DOME_LINK_TRANSPORT_UART && s_uartOwned) {
                s_domeSerial.print(MD_BODY_HB);
                incrementBodyHeartbeatTx();
                PA_LOG_DEBUG(TAG, "TX UART heartbeat");
            } else if (desiredTransport == DOME_LINK_TRANSPORT_WIFI && staConnected && peerKnown) {
                if (sendHeartbeatOverUdp(peerIp)) {
                    incrementBodyHeartbeatTx();
                    PA_LOG_DEBUG(TAG, "TX WiFi heartbeat (%s)",
                                 peerManual ? "manual-ip" : "mdns");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

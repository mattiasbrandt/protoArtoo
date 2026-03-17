// =============================================================================
// src/tasks/dome_link.cpp
//
// DomeLinkTask — bidirectional Marcduino serial link to dome controller.
//
// UART2 (Serial2), 9600 baud 8N1, GPIO 33 TX / GPIO 34 RX.
// This task is the sole writer to UART2 TX. All outbound commands are
// enqueued via domeQueueTx() from other tasks (web API, mood system, etc.).
//
// TX path:
//   - Drain domeTxQueue each loop iteration; write cmd + \r to UART2.
//   - Send #PAHB\r heartbeat to dome once per second.
//   - Increment robotState.bodyHbTx on each heartbeat sent.
//
// RX path:
//   - Read available bytes from UART2 into a static 64-byte line buffer.
//   - On CR (or LF): dispatch complete line via parseDomeRxLine().
//   - Buffer overflow (line > 63 chars): discard and reset — no heap alloc.
//
// parseDomeRxLine() contract:
//   - #APHB → update domeLastSeenMs + domeHbRx; do not forward.
//   - All other prefixes → parseMarcduinoCommand() which routes:
//       $   → audioQueueDollar()       (audio playback)
//       :   → ServoTask queue          (arm sequences and positions)
//       *@%&! → silently discarded     (dome-only, no slave board)
//       #   → stub log                 (ConfigTask in future phase)
//
// Task disabled path: when cfg_enable_s3_dome_ctrl is false the UART is not
// opened, the queue is drained silently, and no heartbeat is sent.
// =============================================================================

#include "dome_link.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "audio_task.h"
#include "config.h"
#include "logging.h"
#include "marcduino_rx.h"
#include "mood.h"
#include "robot_state.h"

static const char* TAG = "DomeLink";

// UART2 — dedicated to the dome serial link (PCB S3 header).
// Sole instance; no other task may call begin() or write() on this object.
static HardwareSerial s_domeSerial(2);

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

// -----------------------------------------------------------------------------
// parseDomeRxLine()
// Dispatch one complete CR-terminated line received from the dome.
//
// #APHB is intercepted here so it never reaches parseMarcduinoCommand() —
// the # stub there would log it as an unhandled config command.
// Everything else goes through the standard Marcduino body parser which
// already routes $ to AudioTask and : to ServoTask.
// -----------------------------------------------------------------------------
static void parseDomeRxLine(const char* line) {
    if (!line || !*line) {
        return;
    }

    // Intercept dome heartbeat before general Marcduino dispatch
    if (strncmp(line, "#APHB", 5) == 0) {
        uint32_t now = millis();
        taskENTER_CRITICAL(&robotStateMux);
        robotState.domeLastSeenMs = now;
        robotState.domeHbRx++;
        uint32_t rxCount = robotState.domeHbRx;
        taskEXIT_CRITICAL(&robotStateMux);
        PA_LOG_DEBUG(TAG, "dome heartbeat rx (#%lu)", (unsigned long)rxCount);
        return;
    }

    // Intercept mood commands (:SE10, :SE11, :SE13, :SE14) before general
    // dispatch. fromDome=true suppresses the dome TX echo.
    uint8_t moodId = moodIdFromSeCommand(line);
    if (moodId != 0) {
        applyMood(moodId, true);
        return;
    }

    // All other commands — route through standard Marcduino body parser.
    // $ → AudioTask, : → ServoTask, dome-only prefixes discarded silently.
    parseMarcduinoCommand(line);
}

// -----------------------------------------------------------------------------
// domeLinkTask()
// -----------------------------------------------------------------------------
void domeLinkTask(void* pvParameters) {
    (void)pvParameters;

    // Read enable flag once at startup; a change requires reboot to take effect.
    taskENTER_CRITICAL(&robotStateMux);
    bool enabled = robotState.cfg_enable_s3_dome_ctrl;
    taskEXIT_CRITICAL(&robotStateMux);

    if (!enabled) {
        PA_LOG_INFO(TAG, "dome serial disabled (en_s3=false) — task idle");
        // Drain queue silently so senders do not block.
        DomeTxCmd cmd{};
        for (;;) {
            xQueueReceive(domeTxQueue, &cmd, pdMS_TO_TICKS(5000));
        }
    }

    // Open UART2 for dome serial link.
    // RX pin first, TX pin second — matches ESP32 Arduino begin() convention.
    s_domeSerial.begin(9600, SERIAL_8N1, PIN_DOME_RX, PIN_DOME_TX);
    PA_LOG_INFO(TAG, "started — UART2 9600 baud, TX GPIO%d RX GPIO%d",
                PIN_DOME_TX, PIN_DOME_RX);

    uint32_t lastHbMs = 0;

    // Static RX line buffer — no heap, overflow-safe.
    static char rxBuf[64];
    static uint8_t rxLen = 0;

    DomeTxCmd txCmd{};

    for (;;) {
        // ----------------------------------------------------------------
        // TX — drain outbound command queue
        // ----------------------------------------------------------------
        while (xQueueReceive(domeTxQueue, &txCmd, 0) == pdTRUE) {
            s_domeSerial.print(txCmd.buf);
            s_domeSerial.print('\r');
            PA_LOG_DEBUG(TAG, "TX: %s", txCmd.buf);
        }

        // ----------------------------------------------------------------
        // TX — 1 Hz heartbeat (#PAHB) to dome
        // ----------------------------------------------------------------
        uint32_t now = millis();
        if ((uint32_t)(now - lastHbMs) >= 1000) {
            lastHbMs = now;
            s_domeSerial.print("#PAHB\r");
            taskENTER_CRITICAL(&robotStateMux);
            robotState.bodyHbTx++;
            taskEXIT_CRITICAL(&robotStateMux);
            PA_LOG_DEBUG(TAG, "TX: #PAHB (hb_tx=%lu)",
                         (unsigned long)robotState.bodyHbTx);
        }

        // ----------------------------------------------------------------
        // RX — read available bytes and dispatch complete lines
        // ----------------------------------------------------------------
        while (s_domeSerial.available()) {
            char c = (char)s_domeSerial.read();

            if (c == '\r' || c == '\n') {
                if (rxLen > 0) {
                    rxBuf[rxLen] = '\0';
                    parseDomeRxLine(rxBuf);
                    rxLen = 0;
                }
            } else if (rxLen < (uint8_t)(sizeof(rxBuf) - 1)) {
                rxBuf[rxLen++] = c;
            } else {
                // Line too long — discard and reset
                PA_LOG_WARN(TAG, "RX line overflow, discarding");
                rxLen = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

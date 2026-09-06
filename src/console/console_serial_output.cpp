// =============================================================================
// src/console/console_serial_output.cpp
//
// Serial output coordinator for Console task (ADR 0036, ADR 0038, ADR 0039)
// This file is the ONLY writer of the serial wire in the firmware: every
// record, log line, echoed character and banner reaches it through
// consoleSerialWriteFrame below, one Serial.write() per unit, and after the
// bind only the Console task calls any of it. Proves the serial output
// coordinator criterion from #217, the single-write-framing / room-wait seam
// from #265, #268's extension of that framing to the interactive redraw, and
// #270's single ownership (test/test_tools/test_one_serial_seam.py enforces
// the "only writer" half against the whole tree).
// =============================================================================

#include "console_serial_output.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "logging.h"
#include "console_record.h"  // CONSOLE_RECORD_LINE_MAX - one of the two line
                              // lengths the framed emitter below must cover;
                              // asserted, not assumed (see the static_asserts)

// The framed emitter truncates at CONSOLE_SERIAL_FRAME_LINE_MAX, so that cap
// must cover BOTH kinds of line that reach it. Raising either source cap
// without raising it fails here rather than silently clipping a line on the
// wire, which is how a record line acquired a second, invisible ceiling
// (#282).
static_assert(CONSOLE_SERIAL_FRAME_LINE_MAX >= CONSOLE_RECORD_LINE_MAX,
              "a Console Record line would be truncated by the framed emitter");
static_assert(CONSOLE_SERIAL_FRAME_LINE_MAX >= PA_LOG_SERIAL_LINE_MAX,
              "a serial log line would be truncated by the framed emitter");

#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
// The USB-Serial-JTAG register block, read (never written) by the #275 probe
// below. Guarded with the same gate as every other CDC-only line in this file:
// artoo-esp32 has no such peripheral and the native build has no board.
#include "soc/usb_serial_jtag_struct.h"
#endif

extern "C" {
#include "embedded_cli.h"
}

// Static reference to the bound CLI instance, set during console task init
static EmbeddedCli* g_boundCli = nullptr;

// One tag for every line this file writes to the Log Ring, and it is the
// Console task's rather than this file's: the sink reports that task's view
// of its own transport, and one tag keeps the #275 probe (edge, intoken,
// attached, drop) and the #276 backpressure lines on one grep in /api/logs
// beside src/tasks/console_task.cpp's own lines.
static const char* const TAG = "ConsoleTask";

#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
// Armed by every frame that reaches the wire, fired by the first frame dropped
// after it (writeFrameCounted below): one register snapshot per drop episode,
// not one per dropped frame -- a run of drops (a wedge, or a host that stopped
// reading) drops every record, and a probe line per record would only evict
// the ring it is trying to fill with evidence (#275). Tagged "drop", not
// "wedge": the snapshot reports that a record was dropped and the registers
// beside it say whether it is the permanent wedge (data_free 0, room 0,
// repeating) or ordinary ADR 0038 backpressure (recovers), rather than
// presuming the diagnosis.
static bool s_cdcDropProbeArmed = true;
#endif

// Serial Backpressure (CONTEXT.md, #276): the run of consecutive frames
// dropped after their room-wait while `Serial` read a host present -- a host
// that is attached but whose transmit path is not draining. Kept beside the
// probe above because it is the same drop event told differently: the probe
// snapshots the peripheral once per episode, at DEBUG, for a diagnosis; this
// reports the episode itself where the operator looks -- one WARN once it is
// sustained, one INFO with the whole count when the wire drains again. The
// threshold, both lines' shapes and every reason are at
// CONSOLE_SERIAL_BACKPRESSURE_REPORT_AT (console_serial_output.h). Nothing is
// refused on such a link (grilling 2026-09-05): what changes is only that the
// operator is told.
//
// Not under the CDC gate, unlike the probe: the counter is what the native
// suite proves, and artoo-esp32 is unchanged by construction -- UART0 cannot
// drop a frame (the header, at the constant), so the count never gets there.
//
// The drop site and the success site only move these three words; the two
// log lines are written by reportBackpressure() below, at the drain point.
// That split is a stack-chain constraint, not a style: a PA_LOG_* call
// carries a 256-byte line buffer and newlib's printf tail (~2.2 KB on the
// Xtensa walk), and writeFrameCounted() is reached STATICALLY from every
// task's paLogLine() through the pre-bind emit path -- the walk cannot see
// that waitForRoom is false there -- so logging from the drop site put that
// cost on all nine walked chains and failed ADR 0040's gate row (measured at
// db310551: +816..1040 B per task, DomeTask's floor holds by 80). The drain's
// entry is reachable from the Console task alone, and it sits beside the
// eviction marker's own snprintf rather than on top of the redraw frame.
static uint32_t s_backpressureDrops = 0;       // consecutive drops, host present
static bool s_backpressureOnsetOwed = false;   // the threshold was reached; WARN owed
static uint32_t s_backpressureOverOwed = 0;    // an episode ended; INFO owed, with this count

static void noteBackpressureDrop(void) {
    s_backpressureDrops++;
    // On the third drop exactly, so a longer episode is reported once.
    if (s_backpressureDrops == CONSOLE_SERIAL_BACKPRESSURE_REPORT_AT) {
        s_backpressureOnsetOwed = true;
    }
}

static void noteRoomWaitedWrite(void) {
    // A reported episode owes its end too, with the count of every frame it
    // cost -- the two silent ones included. Reported or not, a frame reached
    // the wire after waiting for room: the run of consecutive drops is over.
    if (s_backpressureDrops >= CONSOLE_SERIAL_BACKPRESSURE_REPORT_AT) {
        s_backpressureOverOwed = s_backpressureDrops;
    }
    s_backpressureDrops = 0;
}

// Writes the owed line(s) to the Log Ring, WARN before INFO -- both can be
// owed at once when a record boundary's drain drops the third frame and the
// record behind it then fits the ring's residual room. Called first thing in
// consoleSerialDrainLogs(): every room-waited write on this wire is behind a
// drain point (emitRecordLine drains before each record, the drain's own loop
// runs after this), so the WARN is in the ring before the next frame can
// succeed and the INFO within one record boundary or one poll of the frame
// that ended the episode. Log Ring lines, never wire writes: after the bind
// paLogLine() only appends (src/main.cpp), so nothing here recurses into the
// write path it reports on, and the drain loop below carries both out.
//
// noinline, deliberately: the 256-byte PA_LOG_* line buffer must be this
// function's own frame and not consoleSerialDrainLogs()'s, which every
// drained line's redraw (emitLineCounted, 752 B on artoo-esp32) stacks on.
static void __attribute__((noinline)) reportBackpressure(void) {
    if (s_backpressureOnsetOwed) {
        s_backpressureOnsetOwed = false;
        PA_LOG_WARN(TAG, "serial backpressure: host attached but not reading, "
                         "serial output dropped until it drains");
    }
    if (s_backpressureOverOwed != 0) {
        // Formatted by the helper the closing record and the eviction marker
        // use, so the ` dropped=<n>` idiom cannot drift.
        char suffix[24];
        consoleSerialFormatDroppedSuffix(suffix, sizeof(suffix), s_backpressureOverOwed);
        s_backpressureOverOwed = 0;
        PA_LOG_INFO(TAG, "serial backpressure over: serial output flowing again%s", suffix);
    }
}

// The room-wait, the framed emit and the redraw emit, each with the
// milliseconds they spent waiting reported back to the caller. Only
// consoleSerialDrainLogs() asks: it is the one caller that emits a whole run
// of lines and must stop before that run turns into seconds of Core 0 with no
// input read (#229, CONSOLE_DRAIN_ROOM_WAIT_BUDGET_MS). The public entry
// points below are the same functions with nothing to report to.
//
// `waitedMs` ACCUMULATES into the caller's own counter rather than assigning
// to it, so one counter can span a run of emits -- an eviction marker and the
// line behind it are two emits and one unit of the drain's budget. A nullptr
// means "nobody is accounting", never "no wait happened"; a caller that does
// account is charged for every wait, including the ones a dropped frame paid
// for and got nothing.
static bool writeFrameCounted(const char* bytes, size_t len, bool waitForRoom, uint32_t* waitedMs);
static bool emitFramedLineCounted(const char* line, size_t len, bool waitForRoom,
                                  uint32_t* waitedMs);
static void emitLineCounted(const char* line, uint32_t* waitedMs);

// =============================================================================
// Per-Character Writer
// =============================================================================

// Exported for seam testing and embedded-cli binding.
// The ECHO path: embeddedCliProcess() calls this for every character the line
// editor puts on screen while the operator types, from the Console task
// (src/tasks/console_task.cpp's main loop). No lock: the Console task is the
// only writer of this wire (ADR 0039), so an echoed byte has nothing to be
// interleaved with. Best-effort like every other keystroke echo -- a
// character the transport refuses is one the operator retypes, and waiting
// per byte would put the room-wait bound on every keystroke.
void consoleSerialWriteChar(EmbeddedCli* cli, char c) {
    (void)cli;  // Unused
    consoleSerialWriteFrame(&c, 1, /*waitForRoom=*/false);
}

// =============================================================================
// Seam Implementation
// =============================================================================

void consoleSerialBindCli(EmbeddedCli* cli) {
    g_boundCli = cli;
    // The bind IS the switch to ring-only logging (ADR 0039): from here on
    // paLogLine() only appends, and consoleSerialDrainLogs() below is what
    // puts a log line on the wire. Placing the drain cursor and raising the
    // ownership flag happen together, inside the ring's own critical section,
    // so the line-in-flight at this instant is written exactly once
    // (src/main.cpp's paLogWireBindToConsole).
    paLogWireBindToConsole();
}

void consoleSerialWriteText(const char* text) {
    if (text == nullptr) {
        return;
    }
    // Best-effort: see the header. A wait would refuse to write while the CDC
    // reports no host, which is the state a cold-booted FireBeetle 2 is in.
    consoleSerialWriteFrame(text, strlen(text), /*waitForRoom=*/false);
}

void consoleSerialEmitLine(const char* line) {
    emitLineCounted(line, nullptr);
}

static void emitLineCounted(const char* line, uint32_t* waitedMs) {
    if (line == nullptr) {
        return;
    }

    size_t lineLen = strlen(line);
    const char* lineToEmit = line;
    char lineBuf[PA_LOG_SERIAL_LINE_MAX];

    if (lineLen >= PA_LOG_SERIAL_LINE_MAX) {
        // Truncate to max length (excluding null terminator)
        strncpy(lineBuf, line, PA_LOG_SERIAL_LINE_MAX - 1);
        lineBuf[PA_LOG_SERIAL_LINE_MAX - 1] = '\0';
        lineToEmit = lineBuf;
        lineLen = PA_LOG_SERIAL_LINE_MAX - 1;
    }

    if (g_boundCli == nullptr) {
        // Nothing to render a redraw against (no CLI bound). Send the line on
        // its own through the same single-write seam. Reachable only before
        // the bind, and paLogLine() takes the same path there for the same
        // reason; kept as a guard rather than an assumption.
        emitFramedLineCounted(lineToEmit, lineLen, /*waitForRoom=*/false, waitedMs);
        return;
    }

    // Render what embeddedCliPrint() would have written -- clear the input
    // line, the log line, the break, the prompt, the buffered command, the
    // cursor move -- into one buffer (lib/embedded-cli/VENDORED.md Patch 8)
    // and put it on the wire in ONE write. Character-at-a-time is what let
    // another writer on this wire land a byte inside the line (#268).
    //
    // The buffer is a LOCAL, and that is the point of ADR 0039's ownership
    // change. It used to be static, because any task could be inside this
    // function and 448 bytes on every logging task's stack is a cost none of
    // them budgeted for. Only the Console task reaches it now, so the frame
    // lives on that one task's stack and artoo-esp32 gets the .bss back.
    char frame[CONSOLE_SERIAL_FRAME_MAX];
    const size_t frameLen =
        embeddedCliPrintToBuffer(g_boundCli, lineToEmit, frame, sizeof(frame));

    if (frameLen > 0) {
        // Waits for transmit room under the record bound (ADR 0039). If the
        // room never comes the frame is dropped whole and the render has
        // already advanced the editor's screen bookkeeping -- the operator
        // sees a prompt redrawn one line later than the editor believes,
        // which their next keystroke or the next drained line repairs. The
        // line itself is never lost: /api/logs still has it.
        writeFrameCounted(frame, frameLen, /*waitForRoom=*/true, waitedMs);
        return;
    }

    // The redraw did not fit, so nothing was rendered and the line editor
    // was left untouched. Send the line on its own, whole, through the
    // same single-write seam: the operator still sees the line, and the
    // prompt comes back with their next keystroke or the next log line.
    emitFramedLineCounted(lineToEmit, lineLen, /*waitForRoom=*/true, waitedMs);
}

uint32_t consoleSerialDrainLogs(void) {
    // Owed Serial Backpressure lines first (#276), before the host guard
    // below: they are ring lines that /api/logs wants whether or not a host
    // is there to drain them, and they must be in the ring before this call
    // pops anything, so the drain that carries them is this one.
    reportBackpressure();

    // No host on the other end: leave the lines in the ring rather than
    // writing them into a transport that will refuse them.
    //
    // This is what keeps ADR 0039's "the only loss is ring eviction" true on
    // the FireBeetle 2. A drained line waits for room like a record, and the
    // room-wait refuses outright while `Serial` reports no connected host -
    // so draining into a detached CDC would advance the cursor over lines
    // that never reached anyone, silently. Holding the cursor instead means
    // the ring fills, and the operator who attaches gets the marker plus
    // whatever the ring still holds. On artoo-esp32 `Serial` reports only
    // that the UART driver is installed (HardwareSerial::operator bool()),
    // which is true from setup() onwards, so this guard is inert there.
    //
    // The residual, stated rather than hidden: a host that is attached but has
    // stopped reading can still make a single frame miss its room wait, and
    // that one line is then lost from the wire without a marker. That is ADR
    // 0038's record policy applied to logs, which is what ADR 0039 asked for;
    // /api/logs still has the line either way.
    if (!Serial) {
        return 0;
    }

    uint32_t written = 0;
    uint32_t reads = 0;
    char line[LOG_LINE_MAX];
    uint32_t evicted = 0;
    uint32_t roomWaitMs = 0;

    // Bounded at the ring's own depth. The ring cannot hold more lines than
    // that, so one call always catches up with everything already written;
    // what the bound refuses is being held here indefinitely by writers
    // appending as fast as the wire drains, which at a record boundary would
    // stall the command that is mid-answer. Whatever is left waits for the
    // next of this function's three call sites.
    //
    // And bounded a second time, in TIME, at CONSOLE_DRAIN_ROOM_WAIT_BUDGET_MS
    // of room-waiting accumulated across this call (#229). The depth bound
    // alone let a transport that had stopped taking bytes charge every line
    // the full CONSOLE_RECORD_ROOM_WAIT_BOUND_MS, so one drain call could hold
    // the Console task for LOG_RING_MAX_LINES times that -- seconds during
    // which it read no input at all, long enough for an over-length input line
    // to overrun the transport's own receive queue and lose the CR that is the
    // only thing that triggers its refusal. The budget is checked BEFORE the
    // next line is popped, so the drain never takes a line off the ring that
    // it is not going to write: the cursor and the wire stay in step.
    while (reads < LOG_RING_MAX_LINES && roomWaitMs < CONSOLE_DRAIN_ROOM_WAIT_BUDGET_MS &&
           paLogDrainNextLine(line, sizeof(line), &evicted)) {
        reads++;
        uint32_t waitedMs = 0;
        if (evicted > 0) {
            // Ring eviction is the only remaining way a log line is lost from
            // the wire, and it is marked rather than silent (ADR 0039). The
            // count is formatted through the same helper a record's closing
            // line uses, so the `dropped=<n>` idiom cannot drift between the
            // two things that can be dropped.
            char marker[48];
            char suffix[24];
            consoleSerialFormatDroppedSuffix(suffix, sizeof(suffix), evicted);
            snprintf(marker, sizeof(marker), "[log]%s", suffix);
            emitLineCounted(marker, &waitedMs);
            written++;
        }
        emitLineCounted(line, &waitedMs);
        written++;
        // The marker and its line are one unit -- the marker exists to say
        // what is missing in front of the line that follows it -- so both are
        // always emitted and their waits are charged together, after the fact.
        // Splitting the budget check between them would be the one way to put
        // a marker on the wire and leave its line for the next call.
        roomWaitMs += waitedMs;
    }
    return written;
}

// =============================================================================
// Single-write framed emitter (ADR 0038)
// =============================================================================

bool consoleSerialEmitFramedLine(const char* line, size_t len, bool waitForRoom) {
    return emitFramedLineCounted(line, len, waitForRoom, nullptr);
}

static bool emitFramedLineCounted(const char* line, size_t len, bool waitForRoom,
                                  uint32_t* waitedMs) {
    if (line == nullptr) {
        return true;  // nothing to drop
    }

    // One buffer holding the line AND its CR LF terminator, so the wire only
    // ever sees them delivered together by a single Serial.write() call --
    // see this function's header comment for the two-call defect this
    // replaces.
    //
    // CR LF, not a bare LF (#267): a Console session must be attached in raw
    // mode so Tab and cursor bytes reach the firmware unedited
    // (docs/console-protocol.md 8), and raw mode disables the kernel's ONLCR
    // NL->CR-NL translation. A bare LF then feeds the line down without
    // returning the carriage, so every line starts one column further right
    // than the last. embedded-cli already terminates the interactive log path
    // with "\r\n" (lib/embedded-cli/src/embedded_cli.c:235's lineBreak), so
    // this is what makes records and logs agree on the one wire they share.
    // The buffer is one byte longer than the content cap for the same reason:
    // the truncate-to-the-cap content rule is unchanged, only the terminator
    // grew.
    //
    // The cap is CONSOLE_SERIAL_FRAME_LINE_MAX, not the log cap: a record line
    // is the longer of the two kinds of line that arrive here, and capping
    // both at the log line's length put an invisible second ceiling under the
    // record buffer (#282). A log line is still capped at
    // PA_LOG_SERIAL_LINE_MAX - 1 where it is formatted, so nothing about the
    // log path changes.
    size_t lineLen = len;
    if (lineLen > CONSOLE_SERIAL_FRAME_LINE_MAX - 1) {
        lineLen = CONSOLE_SERIAL_FRAME_LINE_MAX - 1;
    }
    char buf[CONSOLE_SERIAL_FRAME_LINE_MAX + 1];
    memcpy(buf, line, lineLen);
    buf[lineLen] = '\r';
    buf[lineLen + 1] = '\n';
    // ADR 0038 reserves room for the WHOLE line including its terminator, so
    // the reservation follows the extra byte: a record must never be written
    // short for want of the CR.
    size_t total = lineLen + 2;

    return writeFrameCounted(buf, total, waitForRoom, waitedMs);
}

bool consoleSerialWriteFrame(const char* bytes, size_t len, bool waitForRoom) {
    return writeFrameCounted(bytes, len, waitForRoom, nullptr);
}

static bool writeFrameCounted(const char* bytes, size_t len, bool waitForRoom,
                              uint32_t* waitedMs) {
    if (bytes == nullptr || len == 0) {
        return true;  // nothing to send, so nothing to drop
    }

    if (waitForRoom) {
        // Never reserve more room than this transport can ever report having
        // (CONSOLE_SERIAL_TX_ROOM_MAX, console_serial_output.h). Asking for
        // more is a wait that cannot succeed: it burns the whole bound and
        // then drops a frame the transport would have accepted. Above the
        // ceiling the reservation means "as empty as this transport gets",
        // and the transport's own write path carries the remainder -- blocking
        // on UART0, chunked through the TX ring on the CDC.
        const size_t reserve = len < CONSOLE_SERIAL_TX_ROOM_MAX ? len : CONSOLE_SERIAL_TX_ROOM_MAX;

        // Only the Console task ever spends this wait, and it is not
        // TWDT-subscribed (src/tasks/console_task.cpp's file header). That is
        // what ADR 0039 changed: the wait used to be reachable from any
        // logging task's context, which is why ADR 0038 had to keep it
        // strictly outside the lock.
        uint32_t spentMs = 0;
        while (Serial && Serial.availableForWrite() < (int)reserve &&
               spentMs < CONSOLE_RECORD_ROOM_WAIT_BOUND_MS) {
            vTaskDelay(pdMS_TO_TICKS(1));
            spentMs++;
        }
        // Charged to the caller's running total whether or not the room ever
        // came: a frame dropped after the full bound cost the Console task
        // exactly as much time as one that was written after it, and time
        // away from the transport is what the drain's budget is counting.
        if (waitedMs != nullptr) {
            *waitedMs += spentMs;
        }
        // Read once, so the drop below is classified by the same answer that
        // decided it: a host present whose room never cleared is Serial
        // Backpressure; no host at all is the detached state (CONTEXT.md).
        const bool hostPresent = static_cast<bool>(Serial);
        if (!hostPresent || Serial.availableForWrite() < (int)reserve) {
            // Room never cleared (or the host was never connected to begin
            // with): drop the frame whole. Nothing is written -- a half-sent
            // line is exactly the #245-era failure mode this function exists
            // to remove.
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
            // The first drop after a successful write is where a wedge
            // announces itself (#275): snapshot the peripheral once, here,
            // before the ring evicts what happened. The line goes to the Log
            // Ring only (post-bind paLogLine never touches the wire), so this
            // cannot recurse into the write path it is reporting on.
            if (s_cdcDropProbeArmed) {
                s_cdcDropProbeArmed = false;
                consoleCdcProbeLog("drop");
            }
#endif
            // Counted only while a host is present (#276): a drop into no host
            // is not backpressure and leaves the run of consecutive drops as
            // it was, so a detach in the middle of an episode neither ends it
            // nor lengthens it -- the first room-waited write after the host
            // is back and reading is what ends it. Counting only; the report
            // is written at the drain point (reportBackpressure above).
            if (hostPresent) {
                noteBackpressureDrop();
            }
            return false;
        }
    }

    // The one Serial.write() in the firmware. One call per unit is the whole
    // point (ADR 0038, #268): a unit delivered in one call cannot be
    // half-delivered by a transport that short-writes, and with a single
    // owner (ADR 0039) there is nothing else on this wire to interleave it.
    Serial.write((const uint8_t*)bytes, len);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    s_cdcDropProbeArmed = true;
#endif
    if (waitForRoom) {
        // Only a frame that waited for room and got it is known to have
        // reached the wire; a non-waiting write is attempted blind (the
        // header, at CONSOLE_SERIAL_BACKPRESSURE_REPORT_AT). The probe above
        // re-arms on every write on purpose -- it wants one snapshot per burst
        // of drops, and an echoed keystroke between bursts is a fine place to
        // re-arm -- but the report wants an episode, and an echo is not
        // evidence that one ended.
        noteRoomWaitedWrite();
    }
    return true;
}

#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
void consoleCdcProbeLog(const char* where) {
    // Registers first, driver second -- see the header. Every read here is a
    // plain volatile load; nothing below commits a packet or arms an interrupt.
    const uint32_t ep1Conf = USB_SERIAL_JTAG.ep1_conf.val;
    const uint32_t intRaw = USB_SERIAL_JTAG.int_raw.val;
    const uint32_t intEna = USB_SERIAL_JTAG.int_ena.val;
    const uint32_t intSt = USB_SERIAL_JTAG.int_st.val;
    const uint32_t fram = USB_SERIAL_JTAG.fram_num.val;
    const uint32_t inEp1 = USB_SERIAL_JTAG.in_ep1_st.val;
    const bool plugged = HWCDC::isPlugged();
    const int room = Serial.availableForWrite();
    // TAG is the Console task's, not this file's: see its definition above.
    PA_LOG_DEBUG(TAG,
                 "cdc %s conf=%lx raw=%lx ena=%lx st=%lx fram=%lu room=%d plg=%d inep1=%lx", where,
                 (unsigned long)ep1Conf, (unsigned long)intRaw, (unsigned long)intEna,
                 (unsigned long)intSt, (unsigned long)fram, room, plugged ? 1 : 0,
                 (unsigned long)inEp1);
}
#endif

size_t consoleSerialFormatDroppedSuffix(char* buffer, size_t bufferSize, uint32_t droppedCount) {
    if (buffer == nullptr || bufferSize == 0) {
        return 0;
    }
    if (droppedCount == 0) {
        buffer[0] = '\0';
        return 0;
    }
    int written = snprintf(buffer, bufferSize, " dropped=%lu", (unsigned long)droppedCount);
    if (written < 0 || (size_t)written >= bufferSize) {
        // Truncation would corrupt the record envelope worse than omitting
        // the field; drop it rather than emit a torn `dropped=` token.
        buffer[0] = '\0';
        return 0;
    }
    return (size_t)written;
}

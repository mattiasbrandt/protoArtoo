// =============================================================================
// src/tasks/console_task.cpp
//
// ConsoleTask - Serial console adapter using embedded-cli (ADR 0034)
// Core 0, non-real-time, created in setup() regardless of network state.
// No dynamic allocation in its loop; uses static buffers.
//
// Responsibilities:
//  - Initialize embedded-cli with static buffer (no dynamic allocation)
//  - Accept user input from UART0 (USB CDC on P4, serial bridge on artoo)
//  - Execute commands through the Console module
//  - OWN THE SERIAL WIRE (ADR 0037). After consoleSerialBindCli() below, no
//    other task writes it: a log line is written once, to the Log Ring, and
//    this task drains it (consoleSerialDrainLogs). There is no serial mutex -
//    it coordinated writers that no longer exist. #219 R1's per-line locking
//    is superseded for the log path by the same change; what it was protecting
//    against, a 190-entry `operations` listing (10985 B, ~0.95 s @115200 8N1)
//    blocking every PA_LOG_* caller including Core 1's rcInputTask and
//    driveTask, cannot happen at all now: a Core 1 task's PA_LOG_* costs one
//    bounded critical section and a copy into the ring, and never touches the
//    wire (AGENTS.md Architecture Guardrails: Core 1 is real-time, real-time
//    paths must not block; and drive zero-frame continuity at 50 Hz).
//  - Drain the ring at three points, which is what keeps wire order to within
//    one record or one poll: each 10 ms poll, before dispatching a command,
//    and at every record boundary while a command runs. Ring eviction - the
//    only remaining way a log line is lost from the wire - is marked with one
//    counted `dropped=<n>` line before the drain continues.
//  - Emit Console Records one line at a time. docs/console-protocol.md section
//    3.1 says records of one request "may be separated by other lines" (the
//    Request ID reassembles them), and section 2.1 records the no-paging
//    decision this line-level design makes affordable. The only invariant that
//    survives is section 6's "no line is ever interleaved inside another",
//    which single ownership plus one-write framing makes structural.
//  - A drained log line clears the input line, writes the log, then redraws
//    the prompt and buffered command - all of it composed by
//    consoleSerialEmitLine() into ONE frame and written in ONE Serial.write()
//    call (#268).
//  - An input line that lost bytes - past the fixed command buffer, or to an
//    rx FIFO overrun - is refused whole by the library and answered here with
//    the same `invalid reason=line-too-long` record the browser adapter emits
//    (#262; lib/embedded-cli/VENDORED.md Patch 7, include/console_line_overflow.h).
//    No truncated command executes on either adapter.
// =============================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <cstdio>

#include "logging.h"
#include "console_module.h"
#include "console_record.h"
#include "console_serial_output.h"
#include "console_cli_line.h"
#include "console_completion.h"
#include "console_write_exclusion.h"
#include "console_host_attach.h"
#include "console_line_overflow.h"

// Include embedded-cli (vendored at lib/embedded-cli/)
extern "C" {
#include "embedded_cli.h"
}

static const char* TAG = "ConsoleTask";

// =============================================================================
// Forward declarations for record sink callbacks
// =============================================================================
static void onRecordBegin(uint32_t requestId, const char* operationType);
static void onRecordField(uint32_t requestId, const char* name, const char* value);
static void onRecordItem(uint32_t requestId, const char* value);
static void onRecordResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                          ConsoleReason reason);
static void onRecordEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason);

// =============================================================================
// Static Configuration and State
// =============================================================================

// Static buffer for embedded-cli instance (no dynamic allocation per criterion)
// Per embedded_cli.h:202, embeddedCliRequiredSize() computes the required size.
// This buffer must be large enough for the config (verified at init time).
static CLI_UINT embeddedCliBuffer[512];  // CLI_UINT is size-aligned per embedded_cli.h
static EmbeddedCliConfig* embeddedCliConfig = nullptr;
static EmbeddedCli* embeddedCli = nullptr;

// Output buffer for a single Console Record line
static char recordBuffer[CONSOLE_RECORD_LINE_MAX] = {};

// Current request ID for this command (for stack HWM measurement after first command)
static uint32_t currentRequestId = 0;

// Records this request could not send within CONSOLE_RECORD_ROOM_WAIT_BOUND_MS
// of waiting (ADR 0036). Reset at the top of both entry points that build a
// consoleTaskRecordSink() answer (onCliCommand, onCliLineTooLong) AND drained
// back to 0 every time a closing record (onRecordResult/onRecordEnd) is
// emitted, so a request whose sink never reaches a closing callback at all
// -- an exotic early return inside consoleExecuteCommand -- cannot leave a
// stale count for the NEXT request to inherit. Serial-only: the browser
// adapter builds its JSON response whole and cannot drop a field once
// dispatch starts, so it has no equivalent counter.
static uint32_t g_recordsDroppedThisRequest = 0;

// Ready banner text, shared by the boot-time print (setup) and the
// re-attach print (#260, consoleTask's main loop). The banner names the
// detach key (#219 D4) - an operator attaching cold has no other way to
// learn it. CONSOLE_DETACH_KEY_SERIAL is the same literal the bare `help`
// meta-command's detach_key field uses (console_module.cpp), so the two
// can't drift apart. Deliberately excludes the "> " invitation: at boot
// that is printed alongside it manually (embedded-cli has not started
// processing input yet); on re-attach it is supplied by
// consoleResetInputForAttach()'s queued synthetic Enter instead, so the
// caller does not print it twice.
//
// Terminated CR LF, not a bare LF (#267): a Console session is attached in raw
// mode, which disables the kernel's ONLCR NL->CR-NL translation, so a bare LF
// left the banner's carriage where it was and started the invitation one
// column in. Every other line on this wire already agrees on CR LF.
static const char CONSOLE_READY_BANNER[] =
    "Controller Console ready. Type 'help' for commands, "
    CONSOLE_DETACH_KEY_SERIAL " to leave.\r\n";

// =============================================================================
// Embedded-CLI Callbacks
// =============================================================================

// The serial adapter's record sink: the five callbacks below, in one place.
// Both entry points that answer the operator - a dispatched command and a
// refused over-length line - build it from here, so neither can drift into
// answering through a different set of writers than the other.
static ConsoleRecordSink consoleTaskRecordSink() {
    ConsoleRecordSink sink = {
        .onRecordBegin = onRecordBegin,
        .onRecordField = onRecordField,
        .onRecordItem = onRecordItem,
        .onRecordResult = onRecordResult,
        .onRecordEnd = onRecordEnd,
    };
    return sink;
}

// Called by embedded-cli when a complete command line is ready
static void onCliCommand(EmbeddedCli* cli, CliCommand* cmd) {
    (void)cli;  // Unused

    if (cmd == nullptr || cmd->name == nullptr) {
        return;
    }

    // Parse command (T1: system.status.health, help, operations, unknown)
    const char* commandName = cmd->name;

    // Reconstruct the full command line ("operation key=value ...") from
    // embedded-cli's split name/args pair - consoleExecuteCommand() expects
    // one combined string (matching what the web adapter has always handed
    // it, src/web/api_console.cpp) and splits it right back into a bare
    // operation name plus a raw argument remainder as its own first step
    // (consoleSplitCommandLine(), include/console_args.h) before tokenizing
    // the remainder. This used to reconstruct only for "help"/"operations"
    // (#219 R2's fix for "operations type=action" losing its filter), which
    // meant every OTHER command's arguments were silently dropped before
    // consoleExecuteCommand() ever saw them - #221 widens reconstruction to
    // every command so that stops happening. Pulled out to a portable
    // function (console_cli_line.cpp) specifically so this reconstruction is
    // unit-testable without the Arduino/FreeRTOS dependencies the rest of
    // this file carries - see test/test_native/test_console_cli_line/.
    static char operationNameBuf[128];
    const char* operationName =
        consoleBuildCommandLine(commandName, cmd->args, operationNameBuf, sizeof(operationNameBuf));

    // Get request ID (global across both adapters)
    uint32_t requestId = consoleGetNextRequestId();
    currentRequestId = requestId;

    // Belt-and-suspenders reset (ADR 0036): the closing-record callbacks
    // already drain this to 0, but that only runs if dispatch reaches one.
    // Starting every new request at 0 here means an early return inside
    // consoleExecuteCommand() that skips the sink entirely still leaves the
    // NEXT request's count honest.
    g_recordsDroppedThisRequest = 0;

    // Create console request
    ConsoleRequest request = {
        .requestId = requestId,
        .source = CONSOLE_SOURCE_SERIAL,
        .operationName = operationName,
    };

    // Create sink for record output (implemented inline below)
    ConsoleRecordSink sink = consoleTaskRecordSink();

    // Drain before dispatch (ADR 0037): anything logged between the last poll
    // and this Enter belongs on the wire ahead of the answer to the command,
    // not behind it.
    consoleSerialDrainLogs();

    // Execute through Console module (ADR 0034)
    consoleExecuteCommand(&request, &sink);
}

// Called by embedded-cli instead of onCliCommand when the line the operator
// just submitted lost bytes before it could be stored, and has therefore been
// discarded whole (lib/embedded-cli/VENDORED.md Patch 7). The library owns
// the refusal; this turns it into the same wire answer the browser adapter
// gives for the same failure - see include/console_line_overflow.h for the
// divergence this closes and the one it deliberately leaves recorded.
static void onCliLineTooLong(EmbeddedCli* cli) {
    (void)cli;  // one Console, one answer - nothing per-instance to consult
    g_recordsDroppedThisRequest = 0;  // see onCliCommand's reset for why
    ConsoleRecordSink sink = consoleTaskRecordSink();
    consoleEmitLineTooLong(&sink);
}

// Called by embedded-cli before a submitted line reaches the Up-arrow history
// ring (lib/embedded-cli/VENDORED.md Patch 6). A line assigning a value to a
// write-excluded parameter - a secret (docs/console-protocol.md s.4.1) - is
// refused by consoleExecuteCommand() with reason=secret-not-settable, and
// must not be recoverable afterwards either: the whole guarantee is "nothing
// secret is accepted, so nothing secret can be logged, returned, completed
// into the line, or kept in history" (#227).
//
// The decision is made from the same catalog flag the browser adapter uses
// (include/console_write_exclusion.h), so both adapters refuse the same
// lines, and it is made HERE rather than after dispatch because the ring
// write happens before the executor ever sees the line.
static bool onCliShouldStoreHistory(EmbeddedCli* cli, const char* line) {
    (void)cli;  // one Console, one rule - nothing per-instance to consult
    return !consoleLineAssignsWriteExcludedValue(line);
}

// =============================================================================
// Console Record Sink Callbacks (output formatting)
// =============================================================================

// Emit one fully-formatted record line atomically: wait for transmit room
// (ADR 0036), write the line + terminator in one call. That single write is
// the ONLY unit of atomicity the wire format needs
// (docs/console-protocol.md section 6: "no line is ever interleaved inside
// another"), and with one owner on the wire (ADR 0037) nothing is competing
// for it: a multi-record response (begin -> field/item* -> end) is a sequence
// of independent lines by design, not a block that has to be held -- see the
// file header (#219 R1).
//
// The wait and the single-write framing live in consoleSerialEmitFramedLine()
// (src/console/console_serial_output.cpp) -- the one emit helper both the
// record sink here and the pre-bind log path call, per ADR 0036. This
// function's own job is to drain first and then count what that helper
// reports dropped.
//
// Every record boundary is also a drain point (ADR 0037): whatever this task
// has logged since the last one goes out FIRST, so a log line emitted while a
// command runs lands between records rather than after the whole answer. That
// is what bounds wire order to "within one record". The drain cannot recurse
// back in here - it emits log lines, never records - and nothing in the sink
// callbacks below logs while formatting a record.
static void emitRecordLine(const char* line, size_t len) {
    consoleSerialDrainLogs();
    if (!consoleSerialEmitFramedLine(line, len, /*waitForRoom=*/true)) {
        g_recordsDroppedThisRequest++;
    }
}

static void onRecordBegin(uint32_t requestId, const char* operationType) {
    // Emit: < id=<n> type=begin operation=system.status.health
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=begin operation=%s",
                         (unsigned long)requestId, operationType);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
}

static void onRecordField(uint32_t requestId, const char* name, const char* value) {
    // Emit: < id=<n> type=field name=<key> value=<value>
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=field name=%s value=%s",
                         (unsigned long)requestId, name, value);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
}

static void onRecordItem(uint32_t requestId, const char* value) {
    // Emit: < id=<n> type=item value=<value>
    // Each item is its own locked line (emitRecordLine above); this is what
    // lets a 190-entry `operations` listing (#219 R1: 10985 B, ~0.95 s
    // @115200 8N1) share the wire with other tasks' log lines instead of
    // blocking them for the whole listing.
    size_t len = snprintf(recordBuffer, sizeof(recordBuffer),
                         "< id=%lu type=item value=%s",
                         (unsigned long)requestId, value);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
}

static void onRecordResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                          ConsoleReason reason) {
    // Guard path: emit single result record for error/unknown/unsupported operations
    // Emit: < id=<n> type=result status=ok outcome=queued [reason=...] [dropped=<n>]

    // Present exactly when there is a reason. This is the record an unavailable
    // operation answers with, so the reason must survive: the previous guard
    // excluded CONSOLE_REASON_NOT_IN_THIS_BUILD and would have dropped it.
    char reasonStr[64] = {};
    if (consoleReasonIsPresent(reason)) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }

    // dropped= (ADR 0036, docs/console-protocol.md 3.1/3.6): stamped on this
    // request's closing record exactly when nonzero, counting only the
    // records that preceded it -- captured before emitRecordLine() below,
    // which may itself drop THIS line without that counting as a further
    // drop. `< id=%lu ...` alone is a single-record answer (no begin), so
    // g_recordsDroppedThisRequest is always 0 here unless a caller changes
    // that shape; the field only ever fires on a multi-record group. The
    // formatting itself lives in consoleSerialFormatDroppedSuffix() so the
    // wire rule is provable on the host (this file is not native-compiled).
    char droppedStr[24] = {};
    consoleSerialFormatDroppedSuffix(droppedStr, sizeof(droppedStr), g_recordsDroppedThisRequest);

    size_t len =
        snprintf(recordBuffer, sizeof(recordBuffer),
                 "< id=%lu type=result status=%s outcome=%s%s%s", (unsigned long)requestId,
                 consoleStatusString(status), consoleOutcomeString(outcome), reasonStr,
                 droppedStr);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
    g_recordsDroppedThisRequest = 0;  // this request is answered either way
}

static void onRecordEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason) {
    // Emit: < id=<n> type=end status=ok outcome=completed [reason=...] [dropped=<n>]
    //
    // The reason field is present exactly when there is a reason. Testing
    // against NONE rather than the status keeps a genuine availability answer
    // intact: the previous guard also excluded CONSOLE_REASON_NOT_IN_THIS_BUILD
    // (then the enum's zero value, used as filler on success paths), which
    // would have silently dropped `reason=not-in-this-build` from a real
    // `unavailable` answer.
    char reasonStr[64] = {};
    if (consoleReasonIsPresent(reason)) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }

    // dropped= (ADR 0036): see onRecordResult's comment above -- same
    // capture-before-emit, same drain-after-emit discipline.
    char droppedStr[24] = {};
    consoleSerialFormatDroppedSuffix(droppedStr, sizeof(droppedStr), g_recordsDroppedThisRequest);

    size_t len =
        snprintf(recordBuffer, sizeof(recordBuffer),
                 "< id=%lu type=end status=%s outcome=%s%s%s", (unsigned long)requestId,
                 consoleStatusString(status), consoleOutcomeString(outcome), reasonStr,
                 droppedStr);
    if (len < sizeof(recordBuffer)) {
        emitRecordLine(recordBuffer, len);
    }
    g_recordsDroppedThisRequest = 0;  // this request is answered either way
}

// =============================================================================
// Task Implementation
// =============================================================================

void consoleTask(void* pvParameters) {
    (void)pvParameters;

    PA_LOG_INFO(TAG, "active");

    // Initialize embedded-cli with static buffer configuration (no dynamic allocation)
    // Per embedded_cli.h documentation, derive config from embeddedCliDefaultConfig()
    // and check the required size against our static buffer
    embeddedCliConfig = embeddedCliDefaultConfig();
    embeddedCliConfig->cliBuffer = embeddedCliBuffer;
    embeddedCliConfig->cliBufferSize = sizeof(embeddedCliBuffer);
    // Use default rxBufferSize, cmdBufferSize, historyBufferSize

    // Disable live autocompletion (enableAutoComplete). Live autocompletion
    // emits cursor save/restore escape sequences on every keystroke to show
    // an inline suggestion as the operator types - a per-keystroke wire and
    // CPU cost for a feature docs/console-protocol.md never asks for.
    // Explicit Tab completion (#238) is a separate mechanism
    // (lib/embedded-cli.c's onControlInput: `else if (c == '\t')`, not
    // gated by this flag) and works with it left off; only the live,
    // every-keystroke suggestion is disabled here. #238 completes from the
    // runtime catalog via cli->getCompletionCandidate (set below), not from
    // registered bindings (bindingsCount stays 0: 175 catalog entries as
    // CliCommandBindings would cost ~3.5 KB of static RAM, most of the
    // artoo-esp32 board's remaining headroom - see
    // include/console_completion.h and lib/embedded-cli/VENDORED.md Patch 5).
    embeddedCliConfig->enableAutoComplete = false;

    // Verify buffer is large enough for the configuration
    uint16_t requiredSize = embeddedCliRequiredSize(embeddedCliConfig);
    if (requiredSize > sizeof(embeddedCliBuffer)) {
        PA_LOG_ERROR(TAG, "embedded-cli buffer too small: need %u bytes, have %zu",
                     requiredSize, sizeof(embeddedCliBuffer));
        vTaskDelete(nullptr);
        return;
    }

    embeddedCli = embeddedCliNew(embeddedCliConfig);
    if (embeddedCli == nullptr) {
        PA_LOG_ERROR(TAG, "failed to initialize embedded-cli with static buffer");
        vTaskDelete(nullptr);
        return;
    }

    // Set up embedded-cli callbacks.
    //
    // writeChar is the ECHO path only: embeddedCliProcess() calls it as the
    // operator types, from this task. It takes no lock, and needs none - this
    // task is the wire's only writer (ADR 0037), so an echoed byte has
    // nothing to be interleaved with. Drained log lines do not come through
    // here at all: they are rendered whole into one frame
    // (consoleSerialEmitLine) and written in one call.
    embeddedCli->writeChar = consoleSerialWriteChar;
    embeddedCli->onCommand = onCliCommand;
    // Tab completion (#238): operation names and argument keys from the
    // runtime catalog, via the catalog completion callback patch
    // (lib/embedded-cli/VENDORED.md Patch 5). See
    // include/console_completion.h for the candidate source itself.
    embeddedCli->getCompletionCandidate = consoleCompletionCandidate;
    // History filter (#227): keeps a refused secret out of the Up-arrow ring
    // (lib/embedded-cli/VENDORED.md Patch 6). See onCliShouldStoreHistory().
    embeddedCli->shouldStoreHistory = onCliShouldStoreHistory;
    // Explicit line-too-long (#262): an over-length line is refused whole and
    // answered, instead of executing as whatever prefix fit the fixed command
    // buffer (lib/embedded-cli/VENDORED.md Patch 7). The refusal happens with
    // or without this callback; the callback is how the operator hears about
    // it. See onCliLineTooLong().
    embeddedCli->onLineTooLong = onCliLineTooLong;

    // Bind the CLI to the serial output coordinator (routes log/event/record lines)
    consoleSerialBindCli(embeddedCli);

    // Print the ready banner and initial prompt through the one seam that
    // writes this wire (ADR 0037) rather than straight at `Serial` under a
    // hand-taken mutex. See CONSOLE_READY_BANNER's own comment for why the
    // two print sites (here and the re-attach path below) split banner text
    // from the "> " invitation differently.
    consoleSerialWriteText(CONSOLE_READY_BANNER);
    consoleSerialWriteText("> ");
#if !(ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE)
    // artoo-esp32 only (ADR 0036's flush() decision, #265). On UART0 this is
    // a real, harmless wait for the driver to finish draining the banner
    // just printed above -- kept, matching the ticket's requirement that
    // artoo-esp32's behavior is unchanged.
    //
    // Deliberately SKIPPED on a CDC-on-boot build (main.cpp's
    // setTxTimeoutMs(0)): read HWCDC::flush() (HWCDC.cpp) and it cannot wait
    // at all with a zero timeout -- `tries` starts at tx_timeout_ms == 0, so
    // the "keep polling while bytes remain queued" loop never runs even
    // once -- and BOTH of its exit paths end by calling
    // flushTXBuffer(NULL, 0), which discards whatever is still sitting in
    // the TX ring, and forcing HWCDC's `connected` latch to false. Calling
    // it here would silently drop whatever of the banner/prompt the host
    // has not yet picked up, and would report the host as disconnected to
    // this same task's own host-attach read of `Serial` a few lines below --
    // strictly worse than not calling it. src/main.cpp had two calls with the
    // identical defect, reported as out of scope by #265 and removed by #270:
    // the wire has an owner now, and neither of them was this task.
    //
    // This one stays, and it is the wire owner's own call: the scan test
    // (test/test_tools/test_one_serial_seam.py) allowlists it on that basis.
    Serial.flush();
#endif

    // Measure stack high water mark after first command is processed
    // This allows us to measure the stack usage for command parsing + execution + record emission
    bool hwmLogged = false;

    // Debounced host-presence tracking for USB CDC (re)attach detection
    // (#260). `Serial` (bool) is board-portable on purpose, not gated by
    // #ifdef:
    //  - FireBeetle 2 (P4): Serial resolves to HWCDCSerial
    //    (HardwareSerial.h's `#define Serial HWCDCSerial`, active when
    //    ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT - platformio.ini's P4
    //    envs). HWCDC::operator bool() is HWCDC::isCDC_Connected(), an
    //    ISR-driven flag (HWCDC.cpp) that is true only once the host is
    //    actually driving the link - a genuine attach/detach signal, not
    //    just cable presence.
    //  - artoo-esp32: Serial resolves to Serial0 (classic UART).
    //    HardwareSerial::operator bool() reports only that the UART driver
    //    is installed (HardwareSerial.cpp) - true forever once Serial.begin()
    //    runs in setup(), long before this task starts. The debounce below
    //    can therefore never see a second false->true transition on this
    //    board: a UART bridge has no attach/detach concept, so this logic is
    //    inert there by construction, matching #260's own framing (the fault
    //    is P4/native-USB-CDC-specific) without a board #if.
    // main.cpp's setup() (ARDUINO_USB_CDC_ON_BOOT block) documents the same
    // HWCDC `connected` flag as capable of a "few-ms" flap even on a healthy
    // link (the SOF watchdog behind isPlugged()). Requiring two consecutive
    // 10 ms polls before committing an attach filters that out: one extra
    // tick (<=10 ms) of reprint latency on a genuine attach, versus
    // resetting a line an already-connected operator is mid-typing on a
    // spurious blip.
    bool hostConfirmedConnected = Serial;
    bool hostRawPrevConnected = hostConfirmedConnected;

    // Main loop: read from UART and process through embedded-cli
    while (true) {
        // Drain the Log Ring first (ADR 0037). Anything any task logged while
        // this one slept belongs on the wire before this poll's keystrokes
        // echo back, and the redraw each drained line carries repaints the
        // prompt and whatever the operator had half-typed.
        consoleSerialDrainLogs();

        // Log stack high water mark once after first command is processed
        // Measured value guides stack depth sizing for future runs (ADR 0034)
        //
        // uxTaskGetStackHighWaterMark() returns WORDS, so the reading is
        // scaled here. What this line used to say - "ESP-IDF's
        // uxTaskGetStackHighWaterMark() returns bytes, unlike vanilla
        // FreeRTOS which returns words" - is what the vendored task.h still
        // claims in its own doc comment ("in bytes (as opposed to words in
        // the standard FreeRTOS documentation)"), and that doc comment is
        // stale: the implementation it sits above divides the byte count by
        // sizeof(StackType_t) before returning
        // (components/freertos/FreeRTOS-Kernel/tasks.c,
        // prvTaskCheckFreeStackSpace(); the SMP kernel's copy does the same;
        // checked in both toolchains at ESP-IDF 5.5.5, #270). Unscaled, this
        // line under-reported the free stack by 4x on the one task whose
        // stack a bench operator reads straight off the wire.
        // src/web/api_profiler.cpp scales the same reading the same way.
        if (!hwmLogged && currentRequestId > 0) {
            UBaseType_t freeStackWords = uxTaskGetStackHighWaterMark(nullptr);
            PA_LOG_INFO(TAG, "stack HWM: %u bytes free after first command",
                        (unsigned)((uint32_t)freeStackWords * sizeof(StackType_t)));
            hwmLogged = true;
        }

        // Debounced (re)attach edge: two consecutive "connected" polls after
        // being unconfirmed. Reset the input line and reprint the ready
        // banner + invitation (#260) so a reattaching operator is never left
        // staring at a blank screen with a stale, half-typed command behind
        // it. This check and the reset it triggers run BEFORE the byte-drain
        // loop below so the synthetic Enter consoleResetInputForAttach()
        // queues is always ahead, in embedded-cli's FIFO, of any real bytes
        // this same poll reads from the transport (embeddedCliReceiveChar is
        // documented single-caller/ordered, see console_host_attach.h).
        bool hostRawNowConnected = Serial;
        if (hostRawNowConnected) {
            if (hostRawPrevConnected && !hostConfirmedConnected) {
                hostConfirmedConnected = true;

                consoleSerialWriteText(CONSOLE_READY_BANNER);

                consoleResetInputForAttach(embeddedCli);
                PA_LOG_INFO(TAG, "host attached: line reset, prompt reprinted");
            }
        } else {
            // Eager on the way down: a false sample just means the next
            // attach needs to re-confirm over two polls again. Getting this
            // "wrong" on a momentary blip only costs one extra tick of
            // reprint latency, never a missed or duplicated reset.
            hostConfirmedConnected = false;
        }
        hostRawPrevConnected = hostRawNowConnected;

        // Process any available serial data.
        //
        // One byte in, one drain out. embeddedCliReceiveChar() pushes into a
        // fixed rx FIFO (rxBufferSize, 64 B at embeddedCliDefaultConfig()'s
        // defaults) and embeddedCliProcess() is what empties it, so feeding a
        // whole Serial.available() batch first would overrun the FIFO for any
        // burst larger than 64 bytes - a pasted command, or a fast typist's
        // key-repeat - and the library would then discard the partial line.
        // Processing per byte keeps the FIFO at one entry and that branch
        // unreachable. It is not free-standing safety, though: Patch 7 also
        // makes an rx overflow answer `line-too-long` rather than vanish
        // (lib/embedded-cli/VENDORED.md), so a future caller that batches
        // fails loudly instead of silently.
        //
        // The cost is one extra embeddedCliProcess() call per byte. That
        // function's own per-character work (the escape/control/displayable
        // decision, and printLiveAutocompletion) already ran once per byte
        // inside its drain loop, so what is added is the loop preamble, not
        // the per-character handling.
        while (Serial.available()) {
            int byte = Serial.read();
            if (byte >= 0) {
                // Feed character to embedded-cli (non-blocking)
                // Calls onCommand when a complete line is ready
                embeddedCliReceiveChar(embeddedCli, (char)byte);
                embeddedCliProcess(embeddedCli);
            }
        }

        // Process embedded-cli state machine once more even when no byte
        // arrived this poll: the first call is what prints the invitation
        // (CLI_FLAG_INIT_COMPLETE), and consoleResetInputForAttach() above
        // queues a synthetic Enter that needs draining on a poll where the
        // operator typed nothing.
        embeddedCliProcess(embeddedCli);

        // Yield to prevent starving other tasks
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

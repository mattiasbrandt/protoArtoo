# Controller Console protocol

The Controller Console is one command language shared by two operator surfaces:
the **Live Logs** command box in the browser dashboard and a **serial terminal**
attached to the controller's USB port. Both surfaces send the same lines and
receive the same results; this page is the reference for that language and its
result format. Architecture and rationale: ADR 0034. Vocabulary: `CONTEXT.md`
(Controller Console, Operation, Console Record, Request ID, Availability
Reason, Non-RC Control).

Everything the firmware already implements is reachable here - every action,
every status and profiling query, every non-secret configuration value. Nothing
is reachable here that the firmware does not already implement, and nothing is
hidden behind a per-board subset.

## 1. Command line

A command is one line: an **operation name** followed by zero or more
**arguments**.

```text
sound.action.random-humming
drive.action.move speed=200 steer=0
system.status.health
system.config.log-level
system.config.log-level value=debug
wifi.config.settings mode=client sta-ssid="Workshop WiFi"
```

### 1.1 Operation names

- The canonical name of a registry entry is the command:
  `<domain>.<type>.<verb-noun>`, lowercase, kebab-case within a segment.
- The registry **type** decides what the line does:

  | Type | With no arguments | With arguments |
  |---|---|---|
  | `action` | executes | executes with parameters |
  | `status` | returns a snapshot | invalid (queries take no arguments) |
  | `config` | reads the current value(s) | validates and applies the write |
  | `event` | listed by `help`/`operations`, **not executable** - events are output | - |

- Existing short RC tokens remain accepted **aliases** (`sound_rand_humming`);
  help, completion, responses and documentation always show the canonical
  name. An alias resolves through the same operation and never creates a
  second path.
- Names, argument keys, aliases and meta-commands are **case-sensitive
  lowercase**. Argument *values* preserve case.

### 1.2 Arguments

- Whitespace-separated `key=value` pairs. Keys are kebab-case and belong to the
  operation's argument schema; an unknown or missing required key is
  `invalid` with the key named.
- Double quotes preserve spaces and `=` inside a value:
  `sta-ssid="Workshop WiFi"`. Backslash escapes a quote or a backslash inside
  quotes. Single quotes have no special meaning.
- Quoted values may contain valid UTF-8 **only where the operation's schema
  permits human text** (an SSID, a display label). Raw Marcduino / manual
  command operations keep their existing prefix, length, character-set and case
  rules; the Console does not broaden that protocol.
- Limits are measured in **bytes**, matching the firmware's fixed buffers (for
  example the 32-byte SSID limit).

### 1.3 Line ending and malformed input

- CR, LF or CRLF terminates a command; CRLF is one ending.
- A line longer than the input buffer is **discarded whole** and answered with
  `invalid reason=line-too-long`; a truncated command never executes.
- **The two adapters enforce that at different lengths, and both refuse
  explicitly.** The browser accepts 255 bytes (`src/web/api_console.cpp`'s
  command buffer); serial accepts 62 (embedded-cli's default `cmdBufferSize`
  of 64, less the two bytes tokenisation reserves). Serial used to truncate
  silently and run the prefix instead - closed by the vendored library's
  Patch 7 (`lib/embedded-cli/VENDORED.md`), which refuses the line and calls
  back, and `include/console_line_overflow.h`, which answers with the same
  record the browser emits. Aligning the two *lengths* is a separate question:
  it means growing the Console task's fixed embedded-cli buffer, and task and
  static buffer sizes on this project are measured on real boards, not chosen
  from a host. Until that measurement exists, a command between 63 and 255
  bytes is refused on serial and accepted in the browser - visibly, on both.
- A NUL byte or malformed UTF-8 in a quoted value fails explicitly
  (`invalid` with a reason); nothing is silently dropped or "fixed".

## 2. Meta-commands

Meta-commands are Console vocabulary, not registry entries; they never appear
in the catalog and cannot be bound or aliased.

```text
help
help sound.action.random-humming
operations
operations type=action
```

- `help` - the command language in brief and how to list operations. On
  serial, carries a `detach_key` field (Ctrl-C - see section 8); the browser
  adapter has no detach convention and this field is absent there.
- `help <operation>` - description, argument schema, aliases, current
  availability and its reason. See section 3.4 for the `help_file_status`
  field that reports help file availability.
- `operations` - every catalog entry with its type and availability;
  `type=<action|status|config|event>` filters. Known-but-unavailable entries
  are listed with their reason - discovery shows what exists, not only what can
  run right now.

### 2.1 Operations listing output volume - no paging (decided, #219 R1)

The `operations` command lists all 190 catalog entries. Measured, not
estimated, against the shipped catalog table and `artoo_esp32`'s actual build
flags (four profiler/admission-trace entries answer `not-in-this-build` on
that board, each with a longer item line):

```
entries: 190
bytes on the wire: 10985
seconds @115200 8N1 (10 bits/byte): 0.95
```

**Decision: no paging.** On both serial and web transports the listing is
emitted in full, in one request. Justification:

- The cost is wire time, not a safety cost. `operations` used to hold the
  shared serial mutex for that whole window, blocking every other task's log
  line (#219 R1) - that was the actual defect, and it is fixed by locking
  per record line (section 3.1), not by paging. With the mutex held per
  line, `operations`' ~1 s is Core 0 non-real-time wall-clock time; it never
  touches Core 1 and never delays a log line by more than one record's
  width. Paging would trade that one linear ~1 s wait for a slower,
  stateful, multi-round-trip one, for no remaining safety benefit.
- `operations` is an explicit, operator-typed discovery command, not
  telemetry - it is not issued in a loop, and a bench operator reading a
  catalog can wait under a second for it.
- A paging protocol (chunk size, a `more` continuation, cursor state per
  session) is real design and state to carry on an already resource-constrained
  embedded console, for a command whose entire cost is under a second and
  whose result usefully reassembles by Request ID either way (below).
- This is a function of the byte count, not a fixed exemption: if the
  catalog grows by an order of magnitude, or a board ships at a lower baud
  rate, re-measure (sum each entry's rendered `< id=<n> type=item
  value=<name> (<type>[, <reason>])\n` line length against the built
  `src/console/console_catalog.cpp` and that env's actual build flags) and
  revisit before assuming the answer still holds.

**What "no paging" does NOT mean:** it does not mean the listing is atomic on
the wire. Per section 3.1, records of one request may be separated by other
lines - `operations`' 190 `item` records can have log lines from other tasks
land between them, and a reader reassembles the group by Request ID, not by
assuming contiguity. The invariant that does hold, unconditionally, is
section 6's "no line is ever interleaved inside another": every record and
every log line is whole on the wire, never split mid-line.

## 3. Results: Console Records

The firmware answers every command with one or more **Console Records**, one
per line, prefixed `< ` on serial. Each record is a sequence of `key=value`
pairs; the first pair is always the Request ID.

### 3.1 Record types

```text
> sound.action.random-humming
< id=18 type=result status=ok outcome=queued operation=sound.action.random-humming

> system.status.health
< id=19 type=begin operation=system.status.health
< id=19 type=field name=heapFree value=42120
< id=19 type=field name=heapMin value=31840
< id=19 type=field name=estop value=false
< id=19 type=end status=ok

> system.config.log-level value=debug
< id=20 type=result status=ok outcome=applied operation=system.config.log-level

> profiler.action.start
< id=21 type=result status=err outcome=unavailable reason=not-in-this-build operation=profiler.action.start
```

| `type=` | Meaning |
|---|---|
| `result` | The whole answer to an action or a config write, in one record |
| `begin` | Opens a multi-record answer (a query) |
| `field` | One scalar of a query: `name=<api-json-key> value=<value>` |
| `item` | One element of a list; repeated `item` records represent lists |
| `end` | Closes the group with `status=ok` or `status=err` |

Every record carries `id=<n>`. Because logs and events keep streaming, records
of one request may be separated by other lines; the Request ID is how a reader
reassembles them.

On serial only, a `result` or `end` record carries `dropped=<n>` when the sink
could not secure USB CDC transmit room for `<n>` earlier records of that same
request (ADR 0036); the field is absent when nothing was dropped. A dropped
closing record itself just leaves the group unterminated, which a reader
already treats as loss - the two together make every drop visible on the
wire. The browser adapter builds its JSON response whole and never emits this
field.

### 3.2 Request ID

Assigned by the firmware, monotonic, and **one counter across both surfaces**:
a browser session may see gaps while a serial session is active. Operators
never type an ID.

### 3.3 Status, outcome and reason

- `status=ok|err` - whether the request was accepted.
- `outcome=` - what happened, from a **stable** set:

  | Outcome | Meaning |
  |---|---|
  | `queued` | accepted by the responsible non-blocking queue; **not** "physically completed" |
  | `completed` | answered in full before the record was written - queries (`help`, status) run synchronously, so nothing was queued and nothing changed |
  | `applied` | a configuration value changed and, where the value persists, was persisted |
  | `staged-until-reboot` | accepted and saved; takes effect at the next boot (Component Toggles, staged network switch) |
  | `unavailable` | the operation exists but cannot run now - see `reason=` |
  | `blocked` | a safety or state rule refused it - see `reason=` |
  | `queue-full` | the owning queue could not accept it |
  | `invalid` | the line, arguments or values failed validation - see `reason=` |
  | `internal-error` | the firmware could not complete the request for a reason of its own |

- `reason=` - a stable token explaining `unavailable`, `blocked` or `invalid`.
  **The field is present exactly when there is a reason and absent otherwise**, so
  a successful record carries no `reason=`. Both adapters decide this the same way,
  by testing the reason itself rather than the status, which keeps an availability
  answer's reason intact on every path.
  The **Availability Reasons** are:

  | Reason | Meaning |
  |---|---|
  | `not-in-this-build` | the feature was built out of this image (operators see "Not included") |
  | `not-on-this-board` | this board's hardware cannot support it |
  | `component-disabled` | the owning Component Toggle is off |
  | `blocked-by-state` | estop, sleep, stationary or another state rule holds it |
  | `temporarily-unavailable` | busy right now; try again |

  Other reasons name the specific failure: `line-too-long`,
  `secret-not-settable`, `unknown-operation`, `unknown-argument`,
  `missing-argument`, `out-of-range`, `malformed-argument` (a quoted value's
  escaping/quoting/UTF-8 did not parse, or a bare word appeared where
  `key=value` was required - section 1.2/1.3), `not-executable` (an event),
  and during development only `executor-not-ready` (an operation whose
  executor is not yet wired; the count must be zero when the feature is
  complete).

- `not-in-this-build` and `not-on-this-board` are the same tokens the browser
  already uses for Feature Availability; the Console never invents a synonym.
- Availability is **re-checked at execution**, not cached from discovery.

### 3.4 Help file status

When a `help` command returns full help text from the LittleFS-backed help file,
all expected fields are populated. When the help file is unavailable or cannot
be read:

- If the help reader is not initialized (LittleFS unavailable, or native test
  without a mock reader), the `help_file_status` field is emitted with value
  `unavailable`. All other help fields except `type` are omitted.
- If the help file exists and the reader is initialized but a seek or read
  operation fails (e.g., file truncated or stale offsets), the `help_file_status`
  field is emitted with value `unreadable`. All other help fields except `type`
  are omitted.

This field is only present when help text could not be retrieved in full; a
successful help response contains no `help_file_status` field. This allows a
reader to distinguish "help not available" from "no help given" (which does not
occur on a normal path).

### 3.5 Token and field naming

- Every token the protocol *defines* - operation names, argument keys, record
  types, outcomes, reasons, meta-commands - is **kebab-case**.
- A `field` record's `name=` is the API's **JSON key verbatim** (`heapFree`,
  `sbusSignalLost`): field names are existing schema identifiers carried from
  the REST API, not protocol tokens, so a serial transcript and `/api/health`
  name the same thing the same way. Each query's field list is owned by the
  action registry (`fields:`), and the JSON builder, the registry and the
  record emitter are checked against each other.
- Booleans are `true` / `false`; numbers are plain decimal; strings that
  contain spaces, `=`, or quotes are double-quoted with the same escaping as
  input.

## 4. Configuration

- A `config` operation with no arguments **reads**; with arguments it
  **writes** through the same validation and apply path the web page uses, so
  range, type, enum and grouped rules are identical.
- Grouped settings are validated as one configuration: `wifi.config.settings`
  rejects a `mode=client` without a usable SSID as a whole, naming the failing
  field, exactly as the setup page does.
- A write reports `applied`, `staged-until-reboot`, or an explicit persistence
  failure; a restart-required condition is reported, never assumed.
- There is no raw key/value escape hatch into the settings store.

### 4.1 Secrets

Password writes are **excluded**. Every non-secret WiFi field is settable; a
password argument is rejected with `invalid reason=secret-not-settable`, `help`
marks the field as write-excluded, and passwords are entered through the WiFi
provisioning page. No read ever returns a secret value.

## 5. Actions, safety and provenance

- Success for an action means **accepted or queued** - valid, permitted, and
  taken by the owning non-blocking queue. It does not mean the physical action
  finished; later logs do not retroactively change the result.
- Every existing safety rule applies unchanged: estop, stationary and sleep,
  component availability, queue limits, speed caps, input validation.
- **Non-RC Control** is the only consent gate, and it gates only what it gates
  today: a non-RC source commanding motion while the RC link is unhealthy. It
  never gates queries, configuration, diagnostics or unrelated actions. The
  Console can turn it on or off like any other Commanded Mode.
- Commands carry their origin - serial console or web console - so logs and
  state can tell them from RC, REST forms, sequences and internal writes.
- Physical serial is a trusted local operator source: no unlock, no runtime
  enable, no dependency on the network being up.

## 6. Logs and events keep streaming

Firmware log lines and events continue in their existing formats, on both
surfaces, before, during and after a command. They are never held back while a
command is typed or executed, and there is no Console command that mutes them;
the ordinary log level is a configuration value like any other.

On serial, a log line arriving while a command is being typed clears the
visible input line, prints the log line whole, and redraws the prompt with the
buffered command. No line is ever interleaved inside another.

That last sentence is a property of how the redraw is sent, not a hope about
timing: the clear, the line, the prompt and the buffered command are composed
into one sequence and handed to the port in a **single write**, so nothing the
controller writes concurrently - your own keystrokes echoing back, a Console
Record from a command already running - can land inside it.

## 7. Transport-safe text

- The record envelope - keys, record types, tokens, numbers, booleans - is
  printable ASCII. Quoted human-text values may carry UTF-8.
- Output escapes control and invalid bytes visibly and **never emits ANSI**
  inside a record.
- Editor-only cursor sequences (section 8) are distinct from records and are
  never part of a result.
- **Every line the serial sink writes ends CR LF** - Console Records, and the
  boot-time log lines that share the same framed writer - which is the
  terminator embedded-cli's interactive log path already uses. A Console
  session attaches in raw mode (section 8), and raw mode turns off the host
  kernel's NL->CR-NL translation: a bare LF would feed the line down without
  returning the carriage, so records would staircase down-right across the
  terminal while log lines on the same wire stayed at column 0. Every
  supported client treats the CR as line-terminator whitespace, so a
  transcript is unchanged by it. The browser adapter is unaffected: it builds
  a JSON response and has no line terminator on the wire at all.
- A serial record waits briefly for USB CDC transmit room before it is
  written, and is dropped whole (never split, never sent short) if that room
  never clears (ADR 0036); log lines stay best-effort and never wait. **For
  records**, this is one write or none - a record that starts on the wire
  always finishes on it. A runtime log line does not carry the same
  guarantee: once the console task has bound its CLI, a log line is echoed
  through embedded-cli's interactive redraw path one byte per `Serial.write()`
  call (needed for the input-line clear/redraw behavior in section 6, not for
  delivery atomicity), and can still tear under the same zero-timeout
  backpressure. Only the boot-time fallback used before that binding (early
  setup log lines) got the same single-write fix as records; ADR 0036
  deliberately left the interactive log path's best-effort contract (#245)
  unchanged.

## 8. Serial terminal

- The serial adapter uses the embedded-cli line editor as shipped: Enter,
  Backspace/Delete, Tab, history Up/Down, Left/Right/Home/End, with
  editor-only VT100 cursor sequences.
- Pressing **Enter never autocompletes**: a typed prefix runs only if it is a
  complete name or alias.
- **Tab** completes operation names and argument keys from the catalog: one
  match completes fully; several extend to the longest common prefix; a
  further Tab lists the candidates and restores the line. Canonical lowercase
  spellings are inserted; known-but-unavailable operations complete too.
- The host must attach in **raw mode** so Tab and cursor bytes reach the
  controller unchanged (`pio device monitor --raw`, or picocom); a plain
  terminal can still submit complete lines, with imperfect in-line editing.
- Firmware echoes; host local echo should be off. The prompt is `> `.
- On connect, before the first prompt, the firmware prints a one-line ready
  banner naming the detach key: `Controller Console ready. Type 'help' for
  commands, Ctrl-C to leave.` The firmware states the convention; it never
  attempts to close a terminal it does not own, and there is no `quit`/`exit`
  operation. Bare `help` carries the same key as a `detach_key` field
  (section 2) - serial only, since the browser adapter has no detach
  convention and must not claim one.
- 115200 baud on both boards. Attach rules for each board (and the artoo-esp32
  reset-on-open caveat) are in `docs/troubleshooting.md`.

## 9. Browser

The Live Logs command box sends the raw line to one endpoint and receives the
same records, rendered inline between the live log lines. History, Tab
completion and ambiguity listing behave as on serial; the browser additionally
keeps its own persistent session history.

## 10. What is not on this interface

- Uploading or downloading documents and images: sequence and RC-map documents,
  full configuration documents, firmware and filesystem images, coredumps. Each
  keeps its dedicated mechanism.
- Factory reset (not on the web either), a raw settings-store escape hatch, a
  console-specific log mute, a runtime enable toggle, or any operation the
  firmware does not already implement.

## 11. Measured, not chosen

Task stack size, exact input and output buffer sizes, and whether a bounded
output queue is needed are measurement outcomes recorded by the implementing
tickets, not values fixed by this page.

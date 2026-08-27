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

- `help` - the command language in brief and how to list operations.
- `help <operation>` - description, argument schema, aliases, current
  availability and its reason.
- `operations` - every catalog entry with its type and availability;
  `type=<action|status|config|event>` filters. Known-but-unavailable entries
  are listed with their reason - discovery shows what exists, not only what can
  run right now.

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
  | `applied` | a configuration value changed and, where the value persists, was persisted |
  | `staged-until-reboot` | accepted and saved; takes effect at the next boot (Component Toggles, staged network switch) |
  | `unavailable` | the operation exists but cannot run now - see `reason=` |
  | `blocked` | a safety or state rule refused it - see `reason=` |
  | `queue-full` | the owning queue could not accept it |
  | `invalid` | the line, arguments or values failed validation - see `reason=` |
  | `internal-error` | the firmware could not complete the request for a reason of its own |

- `reason=` - a stable token explaining `unavailable`, `blocked` or `invalid`.
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
  `missing-argument`, `out-of-range`, `not-executable` (an event), and during
  development only `executor-not-ready` (an operation whose executor is not yet
  wired; the count must be zero when the feature is complete).

- `not-in-this-build` and `not-on-this-board` are the same tokens the browser
  already uses for Feature Availability; the Console never invents a synonym.
- Availability is **re-checked at execution**, not cached from discovery.

### 3.4 Token and field naming

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

## 7. Transport-safe text

- The record envelope - keys, record types, tokens, numbers, booleans - is
  printable ASCII. Quoted human-text values may carry UTF-8.
- Output escapes control and invalid bytes visibly and **never emits ANSI**
  inside a record.
- Editor-only cursor sequences (section 8) are distinct from records and are
  never part of a result.

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

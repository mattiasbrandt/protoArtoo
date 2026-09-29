# Console Client - `tools/console_client.py`

The Console Client is this repo's own host program for talking to a controller:
it captures a boot log, it opens an interactive Controller Console session you
can type at, and it replays a written sheet of commands and prints exactly what
came back. One program, three modes, and it drives either Console Adapter - the
serial terminal or the dashboard's `POST /api/console` endpoint.

This page is the reference for the **program**. The neighbouring pages own the
other halves and are not repeated here:

- [console.md](console.md) - the Console **language**: what you can type, what
  the answers mean, and how to use the Console from the dashboard.
- [troubleshooting.md](troubleshooting.md#console-interactive-session) - **attach
  safety**: which clients are measured safe to open a port with, the flags that
  make each one safe, and the key that detaches each one.
- [console-protocol.md](console-protocol.md) - the wire format the records below
  are printed in.

> [!WARNING]
> **`system.config.log-level value=<x>` over serial panics the controller today.**
> That is a live firmware defect tracked on #226, not something this client
> causes, and it is in `tools/bench_rows/`'s `scalar-config-and-commanded-modes`
> row - so a replay of a whole sheet will meet it. Reading the value
> (`system.config.log-level` with no argument) is fine; writing one over serial
> is not, until #226 closes.

---

## Which mode you want

| Mode | How you get it | What it is for |
|---|---|---|
| **Capture** (default) | no other mode flag | Watch the log go past, or wait for one line and exit. Read-only: it cannot send anything. |
| **Interactive** | `--interactive` | Sit at the Console and type. Bidirectional, raw-mode local terminal, serial only. |
| **Scripted** | `--script <file>`, or any directive flag (`--send`, `--raw`, ...) | Run a written list of commands, on either adapter, and leave a transcript. This is what a bench row and an agent both use. |

The mode also decides how much the program tells you about itself: only
**scripted** mode prints the [provenance header](#the-provenance-header) and
only scripted mode [colors](#color) record lines.

---

## Capture: watch the log

```bash
python3 tools/console_client.py                         # 10 seconds, default port
python3 tools/console_client.py --port /dev/ttyACM0 --duration 30
python3 tools/console_client.py --until "init complete" --timeout 30
python3 tools/console_client.py --stream                # until you press Ctrl+C
make monitor                                            # resolves the port for you
```

`--until` is the one to reach for in a verification script: it exits as soon as
the string appears rather than burning the whole window. If the string never
appears it prints `TIMEOUT: '<string>' not seen within time limit` to stderr and
**exits 1**. `--timeout` bounds that wait and defaults to `--duration`.

`--baud` defaults to 115200, which is what both boards run at; it applies to
every mode.

This mode opens the port read-only. It cannot send a command, so it is not a way
into the Console: `make monitor` shows you the log going past, and nothing you
type reaches the board. Use `make console` for that.

---

## Interactive: sit at the Console

```bash
python3 tools/console_client.py --interactive
python3 tools/console_client.py --port /dev/ttyACM0 --interactive
make console                                            # resolves the port for you
```

Your terminal goes into raw mode, so Tab, Backspace and the arrow keys reach the
firmware's line editor unedited and the firmware's own echo is what you see.
**Ctrl-C ends the session**; it is consumed locally and never sent to the board,
matching every other supported Console terminal. Raw mode is restored on every
exit path, including a crash or a `SIGTERM` - a tool that leaves your terminal in
raw mode costs you the shell it was running in. The session exits 0 when you
leave it, and 1 if the port could not be opened or the link failed mid-session.

Serial only - the Console over HTTP is a request/response endpoint with no
stream to sit in, so `--interactive` with `--http` is not a thing that exists.
`--interactive --pyserial` is refused outright: that backend resets the board
every time (see [Backends](#backends-and-the-one-unsafe-one)).

This is one of three supported interactive clients; `pio device monitor` and
`picocom` are the other two, and
[troubleshooting.md](troubleshooting.md#console-interactive-session) is the
authoritative list of their flags and detach keys.

---

## Scripted: run a written list of commands

Scripted mode starts as soon as you give it either a `--script` file or any
directive flag. Directives from the file run first, then the ones on the command
line, in the order you wrote them.

```bash
# one command, serial
python3 tools/console_client.py --port /dev/ttyACM0 --send system.status.health

# the same command through the dashboard's endpoint instead
python3 tools/console_client.py --http http://<controller> --send system.status.health

# a whole sheet
python3 tools/console_client.py --port /dev/ttyACM0 --script tools/bench_rows/firebeetle2.txt
make bench-rows BENCH_ROWS=tools/bench_rows/firebeetle2.txt
```

### Finding `<controller>`

A board's mDNS name is **configurable by design**, so guess it in an order rather
than pinning one. The compiled-in default is `WIFI_MDNS_HOST`
(`include/config.h`) - `artoo` on artoo-esp32, `firebeetle2` on a FireBeetle 2,
so the two never contest one LAN. But when an operator sets a Droid Name and
ticks *Use as network hostname (mDNS)* on the **Setup** page (`mdnsUseName` in the
API), *that* name, lowercased, is what the board advertises, and the compiled
default is only the fallback (`configResolvedMdnsHostname()`,
`src/config_store.cpp`).

| # | Try | Where the name comes from |
|---|---|---|
| 1 | `http://artoo.local`, or `http://firebeetle2.local` | The compiled default. This is the right **first** attempt, and the one a tool or an agent should make when it knows nothing else about the board. |
| 2 | `http://<droid-name>.local` | The configured name: `GET /api/identity` -> `droidName`, advertised whenever `mdnsUseName` is true. A droid named `protoartoo` answers at `protoartoo.local` and **not** at `artoo.local`. |
| 3 | `http://<ip>` | The WiFi Client address (`staIp` in `GET /api/wifi`), or your network's lease table. Never guessable, always right. |

**Step 1 failing is not evidence the board is offline.** The ordinary meaning is
that the droid is named and you asked for the wrong name. And if no name answers
at all, the serial adapter needs none: attach with `--port` and ask
`system.status.wifi`, which answers with the same WiFi Client address (`staIp`)
that `GET /api/wifi` returns - which is the whole point of a surface that depends
on no network.
[api.md](api.md) holds the identity endpoints and the override rule;
[troubleshooting.md](troubleshooting.md) holds the base-URL fallback. Neither is
repeated here.

The client agrees with all of that. A name it cannot resolve costs you the
`IMAGE:` line, not the run: the image-identity probe warns, the header degrades to
`IMAGE: UNKNOWN (not evidence)`, and a serial run carries on and exits 0 - a
lookup that could not complete is not a failed run. A command actually *sent* to a
host that is not there is a different thing: that fails, and exits 1.

### Transports

`--port <device>` (default `/dev/ttyUSB0`) is the serial adapter. `--http
<base-url>` is the browser adapter - the same `POST /api/console` the dashboard's
Live Logs box uses. Every `send` and `sendlen` works on both, and the HTTP
transport re-renders the JSON it gets back into the serial line grammar, so a
transcript from one adapter diffs line for line against the other. That is what
makes a parity check a one-program job.

**`raw`, `key` and `listen` are serial only.** There is no continuous byte stream
to write into or watch over HTTP, so they are refused rather than quietly
skipped, and the run stops with exit 1:

```
ERROR: raw/key directives are serial-only (no continuous stream to send bytes into over --http)
ERROR: listen is serial-only (no continuous stream to watch over --http)
```

Scripted mode also refuses `--pyserial`, and refuses to be combined with
`--interactive`, `--stream` or `--until`.

Beside either transport, a sheet can also make plain requests to the droid's own
HTTP routes with the [`http` directive](#the-http-directive-the-droids-own-routes).
That is a side channel, not a third transport: it never carries a Console
command, and it needs the droid's address even on a serial run.

### The directives

One per line in a `--script` file. Blank lines and `#` comments are ignored. An
unknown directive stops the run - nothing is skipped silently.

| Directive | Argument | Adapters | What it does |
|---|---|---|---|
| `send` | the command line | both | Sends the line (serial appends CR) and waits for the whole record group to close. The wait is the current `timeout`. |
| `raw` | escaped bytes | serial | Sends exactly those bytes - no line ending added, no reassembly - then watches for the current `listen` window. Backslash escapes are honoured: `\t`, `\r`, `\x1b`. |
| `key` | `name[,name...]` | serial | Sends the byte sequence for each named key in order, then the same watch window. |
| `sendlen` | `N [prefix]` | both | Builds a line of exactly N bytes - the prefix kept whole and padded with `x`, or cut to N if it is already longer - and sends it as `send` would. This is how an overflow row is written. |
| `listen` | seconds | serial | Watches without sending for that long, **and** sets the window later `raw`/`key` directives use. |
| `settle` | seconds | serial | How long to wait before the **first** send, so an attach reprint lands before your command does. A later `settle` changes nothing once that first send has happened. Ignored over HTTP. |
| `timeout` | seconds | both | How long later `send`/`sendlen` wait for the group to close, and how long an `http` step waits for its answer. Starts at 8.0, or at `--timeout` if you gave one. |
| `pause` | message | both | Prints `[PAUSE] <message>` and waits for Enter on the controlling terminal. With no terminal to wait on it is an error, not a skip. |
| `http` | `GET <path> [> FILE]` or `POST <path> [key=value ...] [> FILE]` | both | One request to the droid's own routes at `--http-base`, printed into the transcript; `> FILE` also keeps the answer in `--run-dir`. See [below](#the-http-directive-the-droids-own-routes). |
| `@row` | `<ticket> <name>` | both | Starts a row block - see [Bench sheets](#bench-sheets-and-row). |

`settle` defaults to 0.3 s (`--settle`), and `listen` to 2.0 s until a `listen`
directive changes it.

**Key names** accepted by `key`, and the bytes each one sends (read from the
vendored line editor in `lib/embedded-cli/`, so they are the bytes it actually
recognises):

| Name | Bytes | | Name | Bytes |
|---|---|---|---|---|
| `tab` | `\t` | | `up` | `ESC [ A` |
| `enter` | `\r` | | `down` | `ESC [ B` |
| `backspace` | `0x7F` | | `right` | `ESC [ C` |
| `delete` | `ESC [ 3 ~` | | `left` | `ESC [ D` |
| `home` | `ESC [ H` | | `end` | `ESC [ F` |

### The same directives on the command line

Every directive has a matching flag, and they interleave in the order you write
them - the command line is the same engine as a script file, not a second one:

```bash
python3 tools/console_client.py --port /dev/ttyACM0 \
  --send system.action.enable-web-control \
  --listen 2 --raw 'dome.action.dr' --key tab \
  --send sound.action.random-humming
```

**`--sendlen` takes one argument, so quote it**: `--sendlen '70 sys.status.'`.
Written as two words the argument parser rejects the second one. The same goes
for `http`, whose flag is `--http-step` (`--http` was already the transport):
`--http-step 'GET /api/status'`.

### The `http` directive: the droid's own routes

A bench row often needs a plain request beside its Console commands: keep
`GET /api/config` before a flash, or `POST /api/config rcMember=rc_radio` before a
reboot. A sheet writes those as `http` steps rather than asking a person to make
them at a `pause`:

```
http GET /api/config > before-flash-config.json
http POST /api/config rcMember=rc_radio
http POST /api/manual-command command=#st
http GET /api/seq?name=wave
```

| Part | Rule |
|---|---|
| Method | `GET` or `POST`, in capitals. Nothing else is accepted. |
| Path | Starts with a single `/`. The host never appears in the sheet; it comes from `--http-base`. A query rides in the path. |
| `key=value` pairs | POST only, sent `application/x-www-form-urlencoded` in the order written - the way the droid's routes read their parameters and the dashboard sends them. A repeated key is sent twice. Tokens split with shell quoting, so a value with a space is written `droidName="R2 D2"`. A POST with no pairs sends an empty body. |
| `> FILE` | Optional, last on the line. Also writes the raw answer body to `FILE` inside `--run-dir`. The name is letters, digits, `.`, `_` and `-` only - no path, no leading dot. |

A malformed step is refused when the sheet loads, before anything is sent.

**Where it goes.** `--http-base http://<droid>` names the droid (see
[Finding `<controller>`](#finding-controller)); on an `--http` run it defaults to
that same base. A kept answer needs `--run-dir <directory>`, which is created if
it is missing. A file that is already there is **never overwritten**: a replayed
row cannot quietly replace a before-the-flash copy with an after one, so give
each run its own directory. `tasks/` is gitignored and a natural home for one.

**What it prints.** The step's marker, the answer's status line, then the body
as the droid sent it:

```
--- http POST http://10.0.0.22/api/config body=b'rcMember=rc_radio' ---
HTTP 200 OK (1834 bytes, application/json)
{"ok":true,...}
--- kept /home/me/protoArtoo/tasks/bench-run/after.json (1834 bytes, HTTP 200) ---
```

**No verdict, ever.** Any HTTP status - a `404`, a `400` refusal, a `500` - is the
droid answering, is printed like any other answer, can be kept, and never changes
the exit code, exactly as a `status=err` record does not. Only *no answer at all*
is a failed step: the connection refused, no answer within the current
`timeout`, the peer hanging up mid-answer, or an answer that could not be kept.
That prints `[HTTP-FAILED] <method> <url>: <why>`, the run **carries on** to the
next directive, and the run exits `5`.

**Refused before anything runs.** The checks are made on the rows actually
selected, before the port is opened, and the refusal names the rows so you know
what to pass or what to leave out with `--rows`:

```
ERROR: these rows send `http` steps and there is no --http-base to send them to: 413 before-the-flash, 355 the-image-is-what-we-think. Pass --http-base http://<droid>, or --no-http on a board with no network, or leave those rows out with --rows.
ERROR: these rows keep HTTP answers with `> FILE` and there is no --run-dir to keep them in: 413 before-the-flash. Pass --run-dir <directory>.
```

**A board with no network.** `--no-http` asserts it (FireBeetle board 1, whose
C6 cannot be flashed). Every `http` step then prints
`[HTTP-SKIPPED] <method> <path>: --no-http (this board has no network)` and sends
nothing, so the row's Console commands still run and the transcript says what was
not done. It is refused alongside `--http` or `--http-base`, which name a network.

**It is a request, not an assertion.** Like the rest of a sheet, an `http` step
carries only what to ask; what the answer must say stays on the ticket.

### What the run prints

Everything below goes to stdout, so a redirect captures the whole transcript;
the `[console] <port> @ ... (scripted)` attach line and any error go to stderr.

| Line | Means |
|---|---|
| `--- send b'...' ---` | The exact bytes about to go out, before they go out. |
| `< id=<n> type=...` | A Console Record. On serial this is the wire text verbatim; over HTTP it is the JSON re-rendered into the same grammar. |
| `=== row <ticket> <name> ===` | A row block started. |
| `[PAUSE] <message>` | Waiting for you to press Enter. |
| `[TIMEOUT] send '<line>' did not close within <n>s` | Nothing closed the group in time. |
| `[LOSS] dropped=<n> on closing record id=<n>` | The firmware said it dropped records from this answer. |
| `[ADAPTER-CAPPED] ...` | The adapter said it could not carry the whole answer. |
| `[ANOMALY] blank line inside record group id=<n>` | A wire oddity, reported where it happened - never treated as the loss signal itself. |
| `--- gap: ... ---` / `--- reattached: ... ---` | The port vanished mid-run and came back; see [Detach](#a-detach-mid-run). |
| `--- http <method> <url> [body=b'...'] ---` | An `http` step about to go out, with the exact form body a POST sends. |
| `HTTP <code> <reason> (<n> bytes, <type>)` | The droid's answer to it, followed by the body as sent. Any status is an answer. |
| `--- kept <path> (<n> bytes, HTTP <code>) ---` | The answer was written to `<path>` in `--run-dir`. |
| `[HTTP-FAILED] <method> <url>: <why>` | No answer at all, or one that could not be kept. The run carries on. |
| `[HTTP-SKIPPED] <method> <path>: --no-http ...` | Nothing was sent: `--no-http` said this board has no network. |

### Exit codes

| Code | Meaning |
|---|---|
| `0` | Every request closed, nothing was dropped, nothing was capped. |
| `1` | The run could not proceed at all: a malformed directive or an unreadable sheet, one used on the wrong adapter, a port that would not open, an unreachable `--http` Console host, a `--rows` name the sheet does not have, or selected `http` rows with no `--http-base` (or no `--run-dir` for the answers they keep). |
| `2` | At least one request never closed within its timeout. |
| `3` | A closing record carried `dropped=`, so the firmware itself reported loss. |
| `4` | The adapter reported it could not carry the whole answer: HTTP 500 `response too large for this adapter`, or a 200 whose envelope carries `"truncated":true`. |
| `5` | At least one `http` step got no answer at all, or its answer could not be kept (`[HTTP-FAILED]`). |

One run reports **one** code: the highest one it saw. Up to 4, the higher number
is the more specific finding; 5 ranks above them because it means evidence a row
exists to collect is missing from the transcript entirely, where 2-4 describe an
answer that is there but incomplete.

> [!IMPORTANT]
> **A `status=err` record is data, and never changes the exit code** - nor does
> an `http` step's `404` or `400`. A record reading
> `status=err outcome=unavailable reason=not-in-this-build` is the
> Console answering your question correctly - the question just had a negative
> answer. The run still exits `0`. The exit codes above are about whether the
> *transport* delivered the conversation, never about whether the droid liked
> what you asked. Read the outcome and reason for that; they are documented in
> [console.md](console.md#reading-the-answer).

One wrinkle worth knowing if you are branching on the code: a usage error from
the argument parser itself also exits 2, by Python's own convention. It prints
`usage:` to stderr and never opens a port, so the two are easy to tell apart.

---

## Bench sheets and `@row`

`tools/bench_rows/` holds one sheet per board - `artoo_esp32.txt` and
`firebeetle2.txt` - written in exactly the directive grammar above, so a bench
runbook row is a tracked file you replay rather than a session someone typed
once. Each sheet's own header says which ticket owns it.

A sheet is divided by `@row` markers:

```
timeout 8
settle 0.5

@row 219 discovery
send operations
send help system.status.health

@row 240 response-caps
send operations
send system.status.logs
```

The first token after `@row` is the ticket the row answers to and the second is
the row's **name**, which is how you select it. Anything before the first `@row`
is preamble - file-wide `timeout`/`settle` setup - and always runs, whichever
rows you pick.

```bash
# just these two rows, in this order (not the order they appear in the file)
python3 tools/console_client.py --port /dev/ttyACM0 \
  --script tools/bench_rows/firebeetle2.txt --rows discovery,response-caps

# every row that needs no human at the bench
python3 tools/console_client.py --port /dev/ttyACM0 \
  --script tools/bench_rows/firebeetle2.txt --skip-manual

# the same two, through make, which resolves the port
make bench-rows BENCH_ROWS=tools/bench_rows/firebeetle2.txt ROWS=discovery,response-caps
make bench-rows BENCH_ROWS=tools/bench_rows/firebeetle2.txt SKIP_MANUAL=1
```

`--skip-manual` drops every row containing a `pause`, which is what makes a sheet
runnable by an agent with nobody at the bench. It applies on top of `--rows`. An
`http` step needs no person, so it never makes a row manual - but a selected row
that has one needs `--http-base` (and `--run-dir` if it keeps an answer), or
`--no-http` on a board with no network; `make bench-rows` passes them as
`HTTP_BASE=`, `RUN_DIR=` and `NO_HTTP=1`:

```bash
make bench-rows BENCH_ROWS=tools/bench_rows/artoo_esp32.txt SKIP_MANUAL=1 \
  HTTP_BASE=http://10.0.0.22 RUN_DIR=tasks/bench-run-artoo
make bench-rows BENCH_ROWS=tools/bench_rows/firebeetle2.txt SKIP_MANUAL=1 NO_HTTP=1
```

A name that is not in the sheet is refused with the list of names that are:

```
ERROR: --rows: unknown row name(s) nope; available: detach-replug, discovery, ...
```

The automated half of a bench session - these rows, the console sweep and
every Playwright script, with a memory log beside them - is one command,
`make bench-auto` (`tools/bench_auto.py`). It replays the sheet with
`--skip-manual` exactly as above, then reports what each step did to the
controller's heap:

```bash
make bench-auto BENCH_ROWS=tools/bench_rows/artoo_esp32.txt HTTP_BASE=http://10.0.0.22 IMAGE=artoo
```

A sheet carries **commands only**. What a row is expected to answer stays on the
runbook ticket, deliberately - there is no `expect` directive, and a reviewer
reads the transcript against the ticket. The same holds for `http` steps: a
`pause` stays only where a person must act (a flash, a replug, a browser page)
or where the request needs a value only the droid knows, and a "compare with"
judgement is the ticket's, never a step.

---

## The provenance header

Scripted mode prints these lines before the first send, so a transcript says
what it is a transcript *of*. Capture and interactive modes print none of them.

```
PORT: /dev/ttyUSB0 (usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0)
BAUD: 115200
HOST-TIME: 2026-09-03T22:07:10Z
REPO: ed500ab6
BOARD: artoo-esp32 (Bench-Mode) (asserted)
IMAGE: firmwareVersion=v1.0.0 fsVersion=fs-v1.0.0
```

| Line | Where it comes from |
|---|---|
| `PORT:` / `HTTP:` | The transport. On serial the stable by-id name is added when the port has one. |
| `BAUD:` | Serial only. |
| `HOST-TIME:` | The **host's** UTC clock at the start of the run. |
| `REPO:` | The short HEAD sha of this checkout, plus `(dirty)` if the tree has uncommitted changes. `REPO: UNKNOWN` if there is no git metadata to read. |
| `BOARD:` | Whatever you passed to `--board`, marked `(asserted)`, or `BOARD: (not asserted)`. The tool never decides this itself - a CP2102 bridge fronts any board and there is nothing to check an assertion against. |
| `HTTP-BASE:` | Where `http` steps go, when the run has a base (`--http-base`, or `--http`'s). `HTTP-BASE: none (--no-http asserted: http steps are skipped)` under `--no-http`. Absent otherwise. |
| `RUN-DIR:` | The absolute `--run-dir`, when one was given: where kept answers are. |
| `IMAGE:` | `firmwareVersion` and `fsVersion` fetched from `/api/status`: automatically when the transport is `--http`, or from `--status <base-url>` while staying on serial, or else from `--http-base` when the run has one. Failing that, the label you passed to `--image`. Failing that, `IMAGE: UNKNOWN (not evidence)`. |

**`IMAGE: UNKNOWN (not evidence)` means what it says.** `REPO:` is the sha of the
source tree on your laptop, not of the code running on the board; a transcript
whose image line is UNKNOWN proves the conversation happened but proves nothing
about which firmware answered it. Pass `--status http://<host>` when the board is
on the network, or `--image <label>` when it is not, and prefer the first - it is
read from the board rather than typed by a human.

---

## Color

Scripted mode tints Console Record lines: red when the record carries
`status=err`, cyan otherwise. Nothing else is touched - log lines, the send
markers and the verdict markers stay plain.

Color is **on only when stdout is a terminal**. Redirect the output and the
transcript is clean text with no escape sequences in it, which is the point:
a transcript pasted into a ticket should not carry ANSI. `--color` forces it on
anyway and `--no-color` forces it off on a terminal.

Capture and interactive modes never color anything.

---

## Backends, and the one unsafe one

The default backend opens the port without becoming its controlling terminal and
without touching DTR or RTS. It measured **0/5 resets** in the attach matrix in
[troubleshooting.md](troubleshooting.md#serial-monitor-caveat).

`--pyserial` is the old backend and is **not safe**: it drives DTR and RTS low in
two separate steps after opening, which resets the board every time - 7/7
measured, and two of those seven left the board stranded in the ROM download
stub, silent on serial and absent from WiFi. It is refused for `--interactive`
and for scripted mode, and survives only for read-only comparison captures.
troubleshooting.md has the recovery procedure for a stranded board.

---

## A detach mid-run

Pull the cable during a scripted serial run and the client does not die. It notes
the gap, waits for the port's **by-id** name to come back - not the device path,
which can renumber from `ttyACM0` to `ttyACM1` on replug - and carries on:

```
--- gap: /dev/ttyACM0 detached, waiting up to 30.0s for by-id 'usb-...-if00' to return ---
--- reattached: /dev/ttyACM1 ---
```

`--reattach-timeout` bounds that wait (30 s by default); past it the run stops
with exit 1. A request that was in flight when the port vanished is reported as
an ordinary timeout and **is never re-sent** - the board may already have run it,
and running an action twice is worse than an honest gap in the transcript.

---

## See also

- [console.md](console.md) - the command language, and using the Console from
  the dashboard.
- [console-protocol.md](console-protocol.md) - record shapes, quoting, and the
  two adapters' line limits.
- [troubleshooting.md](troubleshooting.md) - attach safety, the supported-client
  list, and Console error tokens.
- [api.md](api.md) - the `POST /api/console` endpoint the `--http` transport
  speaks to.

# Controller Console — operator guide

The Controller Console lets you type commands straight at the controller and
read back what happened, in plain `key=value` lines. It is the same command
language in two places: a **serial terminal** plugged into the controller's
USB port, and the **Live Logs** command box on the dashboard. Whichever one
you use, you type the same commands and get the same answers back.

This page is for a builder at the bench: how to open a terminal, what to
type, and how to read what comes back. The full wire-format reference (for
anyone extending the firmware) is [console-protocol.md](console-protocol.md).

> [!NOTE]
> **The Console isn't finished yet.** Reading status (`system.status.*`),
> discovering commands (`help`, `operations`), running most sound/dome
> actions, and a growing list of `config` reads and writes all work today.
> Drive and dome-speed motion still don't run through the Console — see
> [What doesn't work here yet](#what-doesnt-work-here-yet) for exactly which
> `config` settings are wired up and which still route through the
> **Setup** page instead.

## Two ways in

| | Serial terminal | Dashboard Live Logs |
|---|---|---|
| Where | USB cable + a terminal program | The **📋 Live Logs** panel on the dashboard, in the command box below the log |
| Needs the network up? | No | Yes |
| Leaves a session history? | Only for this session (Up/Down) | Yes — saved in the browser, survives a page reload |
| How you leave | Ctrl-C | Just navigate away or close the tab |

Pick serial when the controller has no network yet, when you are chasing a
boot problem, or when you want a plain-text log you can paste anywhere. Pick
the dashboard when you are already looking at it and just want to fire a
command without reaching for a cable.

## Attach a serial terminal

**Use a raw-mode terminal, not a plain one.** The Console's line editor
draws the prompt with cursor-movement codes (the same codes vi/nano use), so
a terminal that is not in raw mode shows those codes as literal text instead
of moving the cursor — Backspace looks broken, Tab does nothing useful.
Three terminals behave correctly out of the box: the two below, plus this
repo's own `python3 tools/serial_monitor.py --interactive` (nothing extra to
install). [troubleshooting.md](troubleshooting.md#console-interactive-session)
is the authoritative list — exact flags, why each one is safe to open, and
the key that detaches each one, which is **not the same for all three**:

```bash
# PlatformIO's own monitor, already configured for raw mode on this project
pio device monitor -e artoo_esp32       # artoo-esp32, USB-serial bridge
pio device monitor -e firebeetle2       # FireBeetle 2, native USB

# picocom (any Linux box with it installed) -- flags keep it byte-for-byte raw
picocom -b 115200 -q --imap "" --omap "" /dev/ttyUSB0   # artoo-esp32
picocom -b 115200 -q --imap "" --omap "" /dev/ttyACM0   # FireBeetle 2
```

Both boards run at **115200 baud**. Do not attach with `make monitor` — that
command runs a read-only capture tool and cannot send anything you type; use
`pio device monitor -e <env>` above instead.

**Flash the right firmware image.** `firebeetle2_bringup` is an early
bring-up image that only prints a banner and answers nothing — it is not the
Console. The image that runs the Console is `firebeetle2` (and, on the
classic board, `artoo_esp32` or one of its variants).

**Once attached**, the controller prints a ready banner and a prompt:

```text
Controller Console ready. Type 'help' for commands, Ctrl-C to leave.
>
```

Type a command and press Enter. **Ctrl-C detaches** on `pio device monitor`
and `tools/serial_monitor.py --interactive` — that is the terminal program's
own default key, not something the firmware does; the firmware never closes
a terminal on its own, and there is no `quit` or `exit` command to type.
**picocom is the exception**: it does not act on Ctrl-C at all and forwards
it to the port as ordinary data instead, so it looks like nothing happened —
**Ctrl-A Ctrl-X** is what leaves picocom. See
[troubleshooting.md](troubleshooting.md#console-interactive-session) for
this and every other client-specific key to know about before you attach.

### Don't reset the board by attaching

Changing a serial port's control lines (DTR/RTS) after it is already open can
reset the board, and on at least one known combination can even leave it
silent on both serial and WiFi. Stick to a plain attach — `pio device
monitor`, `picocom`, or `tools/serial_monitor.py` — and never toggle DTR/RTS
once the port is open. The full measured results, the recovery steps if a
board does get stranded, and how to tell a real reset from a missed capture
are in [troubleshooting.md](troubleshooting.md#serial-monitor-caveat).

**Port not showing up, or acting flaky right after you plug it in?** On
Linux, ModemManager sometimes probes a freshly-enumerated serial port before
you get to it. If a `pio device monitor`/`picocom` session behaves oddly
immediately after plugging in, check whether ModemManager is holding the
port and, if so, tell it to leave this device alone (a udev rule ignoring
vendor IDs `10c4`/CP2102 and `303a`/Espressif is the standard fix) rather
than assuming the board itself is unwell.

## Use the Console from the dashboard

Open the dashboard and find the **📋 Live Logs** panel. Type into the command
box under the log and press Enter — the reply prints inline in the same log,
in the same `< id=... type=...` shape a serial terminal would show, with an
error record shown in red. Up/Down cycles through commands you've sent this
session, and that history is saved in your browser, so it survives a reload.
Tab completes the same way it does on serial (below).

An unusually long reply — [`system.status.logs`](#reading-the-log-ring) is
the one likely to produce one — can come back shorter than the full answer
here, with nothing on screen to say so; see [api.md](api.md) for exactly
when that happens. Read `/api/logs` directly if you need the whole thing.

## Typing a command

A command is one line: an operation name, then zero or more `key=value`
arguments.

```text
system.status.health
sound.action.random-humming
dome.action.marcduino-sequence value=30
```

- **Operation names** are lowercase and dotted: `<domain>.<type>.<verb-noun>`
  (for example `sound.action.random-humming`). Whatever the RC mapping page
  calls a short token for the same action (`sound_rand_humming`, `seq`,
  `cmd`, `drive_speed`, …) still works too, typed exactly as it appears
  there — it runs the identical command, never a different one. `help` and
  `operations` always show you the dotted form, never the short token, so
  use the dotted form for anything you write down.
- **Arguments** are `key=value`, separated by spaces: `value=30`. Put double
  quotes around a value that has spaces or an `=` sign in it — for example a
  network name with a space — and use a backslash to put a literal quote or
  backslash inside a quoted value. Single quotes do nothing special.
- A value inside quotes may contain non-English text where the field is
  meant for one (a network name, a display label). Everything else on the
  line — the operation name, the keys, `true`/`false`, numbers — is plain
  ASCII.

### Two things that will bite you if you don't know them

- **The serial line is short.** The default input line on serial holds only
  about 60 characters. A long command with a quoted value (a WiFi network
  name, say) can quietly run out of room — extra keystrokes stop appearing
  and the command that runs on Enter is whatever fit, not what you typed.
  There is no error for this on serial today; if a command with a long
  quoted value behaves oddly, retype it from the dashboard instead, where
  the limit is generous (255 characters) and an over-length command is
  refused outright with `invalid reason=line-too-long`.
- **A bare word is not a command by itself.** `speed=200` on its own line,
  or a value that needed quotes and didn't get them, comes back as
  `invalid reason=malformed-argument` rather than being guessed at.

## Finding what you can type

- **`help`** — a one-line reminder of how to type a command and where to go
  next. On serial it also names the detach key.
- **`help <operation>`** — everything about one operation: what it does,
  its arguments, its aliases, and whether it can run right now.
- **`operations`** — every operation the firmware knows about, with its
  type and, if it can't run right now, why. Add `type=action`, `type=status`,
  `type=config`, or `type=event` to see only one kind. Nothing is hidden
  because it happens to be unavailable on this board or not included in
  this firmware — you see the full list either way, with the reason
  attached to the ones that can't run. For example, `system.api.get-profiler`
  and its two heap-trace actions
  (`system.action.profiler-trace-start`/`-stop`) show up in `operations` on
  every image, marked `not-in-this-build` on a board not running the
  profiler build — try to run one there and you get the matching
  `unavailable reason=not-in-this-build`, not a missing-command error.
- **Tab** completes an operation name, or — once you've typed the operation
  and a space — one of its argument keys. One match fills it in; several
  share a prefix and Tab fills in that much; a second Tab lists every match
  and leaves your line as you typed it. A name that can't run right now
  still completes — the point of `operations` and Tab is to show you what
  exists, not just what happens to work this second.

#### Example: looking up an operation

```text
> help sound.action.random-humming
< id=4 type=begin operation=sound.action.random-humming
< id=4 type=field name=type value=action
< id=4 type=field name=available_on_board value=true
< id=4 type=field name=available_in_build value=true
< id=4 type=field name=requires_web_control value=false
< id=4 type=field name=executor_ready value=true
< id=4 type=field name=aliases value=sound_rand_humming
< id=4 type=field name=display_name value=Random Humming
< id=4 type=field name=description value=Play one random track from the configured Humming category range
< id=4 type=field name=executor value=audioQueuePlayTrack
< id=4 type=end status=ok outcome=completed
```

If the help text file isn't available on this board (a missing or damaged
filesystem image), the `display_name`/`description`/`executor` fields are
replaced with a single `help_file_status` field (`unavailable` or
`unreadable`) — everything above it (type, availability, aliases) still
comes from the firmware itself and is unaffected.

## Reading the answer

Every command gets one or more **records**, one per line, each starting
with `id=<n>` — the **Request ID**. It's assigned by the firmware and shared
across both serial and the dashboard, so a serial session can see gaps in
the numbering while someone else is using the dashboard at the same time;
you never type an ID yourself. Because log lines and events keep printing
in between, a multi-line answer can have other output land between its
lines — match on the Request ID, not on the lines being next to each other.

A simple action or an error is one line:

```text
> sound.action.random-humming
< id=5 type=result status=ok outcome=queued
```

A query answers with a group — `begin`, one `field`/`item` line per value,
then `end` (abridged — `system.status.health` answers with thirteen fields,
not three):

```text
> system.status.health
< id=6 type=begin operation=system.status.health
< id=6 type=field name=estop value=false
< id=6 type=field name=wifiConnected value=true
< id=6 type=field name=heapFree value=42120
< id=6 type=end status=ok outcome=completed
```

`status=ok` or `status=err` says whether it was accepted at all. `outcome=`
says what actually happened:

| Outcome | What it means |
|---|---|
| `completed` | A question was answered in full — nothing was changed, nothing is pending |
| `queued` | An action was accepted and handed to the system that runs it — not proof it has physically finished yet |
| `applied` | A setting was changed |
| `staged-until-reboot` | A setting was saved but only takes effect after the next reboot |
| `unavailable` | This operation cannot run right now — see the reason |
| `blocked` | A safety or state rule refused it — see the reason |
| `queue-full` | The part of the firmware that would run this is already full; try again shortly |
| `invalid` | The line or its arguments didn't parse or validate — see the reason |
| `internal-error` | The firmware couldn't complete it for a reason of its own |

When there's a reason, it's on the line as `reason=<token>`; a clean success
never carries one. The tokens are stable — safe to match on in a script:

| Reason | What it means |
|---|---|
| `not-in-this-build` | Not included in this firmware image (shown on the dashboard as "Not included") |
| `not-on-this-board` | This board's hardware can't do this |
| `component-disabled` | The subsystem this needs is switched off |
| `blocked-by-state` | Estop, sleep, or another safety/state rule is holding it — see [Web control](#web-control-what-actions-need) if it's an action |
| `temporarily-unavailable` | The action itself has nothing to do right now — for a sound action this usually means the category it draws from has no tracks configured, not that anything is busy |
| `queue-full` | Same meaning as the `queue-full` outcome above |
| `line-too-long` | The line (or, on the dashboard, the argument list) was too long and was discarded whole |
| `secret-not-settable` | A password field — see [Passwords](#passwords-arent-typed-here) below |
| `unknown-operation` | Not a name or alias the firmware recognises |
| `unknown-argument` | An argument key this operation doesn't take |
| `missing-argument` | A required argument key wasn't supplied |
| `out-of-range` | A supplied value is outside what this argument accepts |
| `malformed-argument` | The line didn't parse into `key=value` pairs at all — a bare word, bad quoting, or invalid text in a quoted value |
| `not-executable` | This entry is not something you run — an event, or one of the [motion commands not yet wired](#what-doesnt-work-here-yet) |
| `executor-not-ready` | Recognised, but the firmware doesn't have a way to run it yet |

## Web control: what actions need

Enable **Web control** (the **✓ Enable Web Control** button under Safety
Controls on the Drive page, `POST /api/web-control/enable`, or the command
`system.action.enable-web-control`) before running an action-type command —
this applies from serial too, not just the dashboard. Until it's on, an
action answers `blocked reason=blocked-by-state`:

```text
> sound.action.random-humming
< id=7 type=result status=err outcome=blocked reason=blocked-by-state
```

`system.action.enable-web-control` and `system.action.disable-web-control`
need no Web control of their own to run and answer `applied` right away —
so a serial terminal with no network reaches this switch by itself, without
the dashboard route above. `system.action.reboot` works the same way: no
arguments, no Web control needed, and it restarts the firmware straight
away.

`system.action.estop` always answers `blocked` this way, on purpose — this
interface never triggers an estop; use the dashboard's E-Stop control or
`POST /api/estop` for that.

## WiFi settings, as one command

`wifi.config.settings` reads and writes the whole WiFi posture at once — how
the controller gets on the network, and the network name for each way of doing
it. With no arguments it reads:

```text
> wifi.config.settings
< id=8 type=begin operation=wifi.config.settings
< id=8 type=field name=provisioned value=true
< id=8 type=field name=mode value=client
< id=8 type=field name=staSsid value="Workshop WiFi"
< id=8 type=field name=staPasswordSet value=true
< id=8 type=field name=apSsid value=artoo-setup
< id=8 type=field name=apPasswordSet value=false
< id=8 type=field name=pendingApply value=false
< id=8 type=field name=networkRecovery value=false
< id=8 type=end status=ok outcome=completed
```

With arguments it writes:

```text
> wifi.config.settings mode=client sta-ssid="Workshop WiFi"
< id=9 type=result status=ok outcome=staged-until-reboot
```

The fields are checked together, the way the Setup page checks them: ask for
`mode=client` when there is no station network name — neither saved nor on the
line — and the whole command is refused, naming the field it wanted, rather
than the mode being kept on its own. What you leave out keeps its saved value.

Put a network name in quotes if it has spaces, and use any text your network
actually uses — the limit is 32 bytes, which is fewer than 32 characters once
there are accents or emoji in it.

`staged-until-reboot` means the settings are saved but the controller is still
running on the ones it started with — restart it when you are ready to move.
`applied` means what you just saved is already what is running, so there is
nothing to restart for. `pendingApply` in the read answers the same question
before you change anything.

## Passwords aren't typed here

Every WiFi setting can be read except the password. A password argument is
refused with `invalid reason=secret-not-settable` — it never even reaches
validation, and the value you typed is never printed back and never reaches
the log:

```text
> wifi.config.settings sta-password=hunter2
< id=10 type=begin operation=wifi.config.settings
< id=10 type=field name=argument value=sta-password
< id=10 type=end status=err outcome=invalid reason=secret-not-settable
```

`help wifi.config.settings` says the same thing about the field before you try
it — the two password fields are listed as `write-excluded` where every other
field says `required` or `optional`. Set or change a WiFi password on the
[WiFi provisioning page](wifi-provisioning.md); the Console is not where
that goes, by design.

The line is not remembered either. A command that sets a password field is
kept out of the serial terminal's Up-arrow history and out of the dashboard's
saved command history, so it is not sitting there to be recalled — or read
back after a page reload, which is what the dashboard's history normally
survives. Tab completion leaves the password fields out for the same reason:
there is nothing there to complete that the Console would accept.

Two narrow cases still slip through, and both need a typo to reach: a password
field typed after a misspelled command name, or a made-up field name with
"password" in it that no command actually has. The Console refuses those lines
just the same, but it cannot match them to a known field, so the line stays in
that session's history. If that happens, restart the controller (serial) or
clear the browser's site data (dashboard).

## Setting the log level

`system.config.log-level` reads and changes how much detail the controller
writes to its log — over serial and on the dashboard's Live Logs panel
alike. Read it with no arguments; write it with either the plain number or
the word, either case — `value=4` and `value=debug` set the same thing:

| Number | Word |
|---|---|
| `1` | `error` |
| `2` | `warning` |
| `3` | `info` |
| `4` | `debug` |

```text
> system.config.log-level value=debug
< id=11 type=result status=ok outcome=applied
```

The change applies immediately and is saved, the same as the four other
settings that answer `applied` right away (see
[below](#what-doesnt-work-here-yet)) — nothing to restart for, and it holds
across a reboot too.

## Reading the log ring

`system.status.logs` prints the controller's own recent log lines back to
you — the same lines the dashboard's **📋 Live Logs** panel shows — one line
per `item` record, oldest first, no arguments needed:

```text
> system.status.logs
< id=12 type=item value=[52340][I][WebServer] client connected, heapFree=173152
< id=12 type=item value=[54012][W][WebServer] accept rejected: heap floor
< id=12 type=end status=ok outcome=completed
```

The ring only keeps a fixed number of the most recent lines — how many
depends on the board and the current log level — so a long gap between
checks pushes older lines out before you read them. For a record that needs
to survive that, or a wider window than the ring holds, pull `/api/logs`
instead ([api.md](api.md)); it reads the same ring over HTTP. On the
dashboard, a reply this long can also come back shorter than the full ring
with no on-screen sign that anything was cut — see
[Use the Console from the dashboard](#use-the-console-from-the-dashboard).

## What doesn't work here yet

- **Drive and dome-speed motion** (`drive.action.move`, `drive.action.speed`,
  `drive.action.steer`, `dome.action.set-speed`, and their aliases) don't run
  through the Console yet. `drive.action.move` answers
  `unavailable reason=executor-not-ready`; the three analog axis actions
  (`drive.action.speed`, `drive.action.steer`, `dome.action.set-speed`)
  answer `unavailable reason=not-executable`. Drive the droid from the
  dashboard or RC for now.
- **Most `config`-type commands** don't run here yet, but some now do.
  Working today: the fifteen Component Toggles (`system.config.enable_drive`,
  `system.config.enable_audio`, `system.config.enable_arm1` and the rest) —
  changing one answers `staged-until-reboot`, and it takes effect at the next
  restart, not immediately; `system.config.mood`; the grouped WiFi write
  `wifi.config.settings` ([above](#wifi-settings-as-one-command)); and five
  settings that take effect straight away and answer `applied` —
  `drive.config.speed-limit`, `aux.config.led-pin`, `aux.config.led-count`,
  `rc.config.mode`, and `system.config.log-level`
  ([above](#setting-the-log-level)). Every other `config` entry answers
  `unavailable reason=executor-not-ready` whether or not you supply a value.
  `operations type=config` lists them all, so you can see which exist; change
  the rest from the **Setup** page in the meantime.
- **Events** (`type=event` entries) are output only — logs and state
  changes you'll see printed on their own — never something you run; typing
  one answers `unavailable reason=not-executable`.

None of the above is hidden: `operations` lists every one of them, so you
can see what exists even while it isn't runnable yet.

## What's never coming to this interface

Uploading or downloading whole documents — firmware images, filesystem
images, coredumps, full configuration or sequence files — keeps its own
existing path (see [api.md](api.md)); the Console only ever carries short
`key=value` commands and their replies. There is also no factory reset here,
no raw settings-store escape hatch, and no way to mute the ordinary log
stream — the log keeps printing exactly as it does everywhere else, before,
during and after a command.

## See also

- [console-protocol.md](console-protocol.md) — the full wire-format
  reference: quoting rules in detail, every record shape, and the
  reasoning behind each decision.
- [api.md](api.md) — the `POST /api/console` endpoint the dashboard uses.
- [troubleshooting.md](troubleshooting.md) — attach/reset safety, and
  Console-specific error tokens.
- [wifi-provisioning.md](wifi-provisioning.md) — where WiFi passwords
  actually get set.
- [console-catalog-contributing.md](console-catalog-contributing.md) — for
  adding or changing the registry entries the Console reads from.

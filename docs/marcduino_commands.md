# Marcduino Commands

What protoArtoo's body answers when you send it a Marcduino line, and what it
hands on to the Dome Controller instead. If you are arriving from ShadowMD or
Padawan360 with bindings you already have, this is the page that says which of
them the body runs and which go to the dome.

The rule behind it is **Command Ownership** (`GLOSSARY.md`, ADR 0055): the body
answers a command that names something it models, and forwards the rest.

## Where a line comes from

This page covers the lines **you** send:

- `POST /api/manual-command` with `command=<line>` (`docs/api.md`)
- the Controller Console's `dome.action.send-command command=<line>`
- a `dome.action.marcduino-command` binding, fired from the RC mapping or from
  the Console

Lines the **dome** sends the body are handled by the body and never sent back
to the dome (`docs/commands.md`, "Dome RX Commands").

## The three answers

Every line gets one of three answers, and they never share a value:

- **Done here.** The body owns the line and acted on it.
- **Forwarded.** The body handed the line, unchanged, to the dome over
  protoR2link. That is all it says. The Marcduino dialect has no reply
  channel, so what the dome did with it is the dome's to report.
  `POST /api/manual-command` answers `{"ok":true,"forwarded":true}` and the
  Console answers `outcome=queued`.
- **No, and why.** The body owns the line and would not run it, or the line was
  for the dome and never left. A line the body refuses is never forwarded in
  its place.

## What the body answers

| Line | Numbers | What it does here |
|---|---|---|
| `:OPnn` | `01`-`05` | Opens a body Output: `01` ARM1, `02` ARM2, `03` AUX1, `04` AUX2, `05` AUX3 |
| `:OPnn` | `00`, `99` | Opens ARM1 and ARM2 together |
| `:CLnn` | as `:OP` | Closes the same Outputs |
| `:OFnn` | as `:OP` | Flutters the part on the same Outputs: it shakes for two seconds at its full throw and ends closed. `00`/`99` shake ARM1 and ARM2 in turn. An Output with no part on it does nothing |
| `:MVnnvvvv` | `01`-`05` | Moves one Output. `vvvv` from `0000` to `0180` is degrees across the servo's range; above `0500` it is a pulse width in microseconds. No `00`/`99` here |
| `:SE10` `:SE11` `:SE13` `:SE14` | Moods | Quiet, Full-Awake, Mid-Awake, Awake+. The body sets its sound for the Mood and, with the dome connected, sends the Mood on to it itself |
| `:SE30`-`:SE36` | body routines | Runs the body routine of that number, the Factory Sequence `DM:SE30`-`DM:SE36` |
| `:SE01`-`:SE09`, `:SE15`, `:SE16` | full-droid sequences | Runs the body's half here **and** forwards the line, so the dome fires its panels, logics and holos. See below |
| `#APSL` `#APWU` | | Puts the droid to sleep, wakes it |
| `#PAHB` | | The body's own heartbeat, echoed back. Accepted and ignored |

A full-droid sequence's body half is a sound, a body routine, or both:

| Line | Sound | Body routine |
|---|---|---|
| `:SE01` | `$S` scream | `:SE30` |
| `:SE02` | none | `:SE31` |
| `:SE03` | none | `:SE31` |
| `:SE04` | none | `:SE31` |
| `:SE05` | `$c` short Cantina | `:SE31` |
| `:SE06` | `$F` faint | `:SE30` |
| `:SE07` | `$C` long Cantina | `:SE31` |
| `:SE08` | `$L` Leia message | `:SE30` |
| `:SE09` | `$D` disco, when a disco track is set | `:SE31` |
| `:SE15` | `$S` scream | none |
| `:SE16` | none | `:SE31` |

**`:SE02`, `:SE03`, `:SE04` and `:SE16` share one body half**: body routine
`:SE31` and no sound. What tells them apart happens on the dome.

The RC action tokens `droid_seq_*` run the same halves and forward the same
line, so a typed `:SE05` and a bound one do the same thing.

## What the body refuses

A line the body owns is refused, and not forwarded, when:

- **estop is latched.** The answer is `409 estop active`, or `blocked-by-state`
  on the Console. A full-droid sequence is the exception, as it is for the RC
  tokens: its body routine waits for the estop to clear, and its sound and the
  dome's half still go.
- **the Output it names is not wired** since the droid started. The answer
  names why, in the same sentence `POST /api/servo` gives
  (`src/web/api_servo.cpp:87-124`): "Restart the droid to use X.", "X has no
  Part on it. Put one on it on Wiring.", "X carries a light, not a servo.", or,
  for a PCA9685 Output, "X is on the PCA9685. Choose it as the body servo
  controller to use it." / "X is unreachable - the PCA9685 is not answering."
- **the droid is asleep.** On the manual command and the Console, every
  prefixed line is held until you wake it (`423 sleeping`).
- **the line is not one the body can run**, such as an `:MV` with no value,
  or a panel number that is not all digits: `:OPxx` is refused, never read as
  `:OP00`.

A Mood sent through a `dome.action.marcduino-command` binding is not applied:
setting a Mood stores it, and the RC loop does not stop to write storage. Use
the Mood control (`system.action.set-mood`, or `POST /api/mood`) instead.

## Everything else on `:` and `#`

**Forwarded to the dome, verbatim, and the dome's to report.** That is every
`:SE` number not listed above (`:SE00`, `:SE12`, `:SE50`-`:SE58` and the rest),
every `:OP`, `:CL` and `:OF` number above `05` other than `99`, every `:MV`
number the body has no Output for, every other `:` command, and every `#` line
except the three above.

protoArtoo does not say what the dome will do with them, and does not keep a
list of what the dome answers. If the dome's firmware handles the line,
something moves or lights; if it does not, nothing does.

A line for the dome that never left is not answered as forwarded: with
protoR2link not connected the answer is `503 dome link not connected`
(`unavailable`, `temporarily-unavailable` on the Console), and with its queue
full it is `503 dome TX queue full` (`queue-full`).

## Line length

A Marcduino line longer than 63 characters is refused before any of it runs,
on every door: the dome TX queue carries no longer a line whole, and a line
cut short would be a different command.

## The other prefixes

- `*`, `@`, `%`, `&`, `!` are always forwarded to the dome uninterpreted
  (ADR 0045), with the same answers as any other forward.
- `$` is sound, and the body plays it:
  - `$8nn` is **bank 8, sound nn**, ShadowMD's `$Bnn`. It plays where the
    fitted sound module has a bank 8, and is refused with that reason where it
    has not. The CHIRP Audio Trigger reads banks 1 to 6
    (`docs/spec-sheets/chirp-audio-trigger-sound.md`) and the other modules
    have no banks, so on every module protoArtoo supports today `$8nn` is
    refused. It never plays raw track 8nn. `$800` names sound 00, which no bank
    has, and is refused everywhere.
  - Every other number, `$nnn`, is a raw track number: `$001`, `$126`.
  - The letters are named sounds and controls: `include/audio_dollar_parser.h`
    lists them.

## `:OP01` means an arm here

ADR 0055, verbatim: *"`:OP01` means body arm 1 here
(`include/marcduino_helpers.h:33-34`) and dome panel 1 to the fork. That
matches what ShadowMD itself means on `Serial3`, so the body's reading is the
faithful one - but a builder pasting a *dome* binding into the body surface
still gets an arm. The published boundary is what tells them so."*

The same holds for `:OP02`-`:OP05`, `:OP00`/`:OP99` and the matching `:CL`,
`:OF` and `:MV` numbers. To reach dome panel 1 itself, send `:OP01` through
`POST /api/dome/cmd`, which forwards a line to the dome without asking who owns
it.

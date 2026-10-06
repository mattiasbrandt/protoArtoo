# Factory Sequences: what each one does

What every body-owned Factory `DM:*` sequence sends today: sound, dome panels, dome
lights and what is left for cleanup. Written down once, so a test run is compared
against a row rather than re-derived on the spot.

Source of truth: `src/tasks/sequence_catalog.cpp`. Keep this in sync when a sequence
changes. The dome-side meaning of each `DV:` preset is in
[dome-visual-presets.md](dome-visual-presets.md). The earlier dated hardware log
(2026-06-18) is in this file's git history.

## Cleanup rules

The body **never sends a group close** (`:CL00`/`:CL14`/`:CL15`) and never closes a pie
on its own: a group close moves every servo in the group at once and browns the dome
out from a loaded ring.

- **End of a run, abort, preempt or estop:** closes only the ring panels the run left
  open, one at a time, about 500 ms apart. A sequence that closes its own panels
  leaves nothing to close. A `:OF` flutter ends closed and marks nothing open.
- **Lights:** a step tagged with a light effect (every `DV:` preset) gets the reset
  `@0T1`, `@0P1`, `*ST00` at the end.
- **Sound:** a Bounded Audio track is ended with a Track Stop, which keeps idle
  chatter. No Factory sequence sends `$s` (it turns idle chatter off until reboot).
- **Body Parts:** nothing. A Body Step routine closes its own Parts.

## Body-owned Factory sequences

| Sequence | Sound | Dome panels | Dome lights | Left at the end |
|---|---|---|---|---|
| `DM:VADER` | `$M` Imperial March, bounded | none | `DV:VADER` | light reset (47 s) |
| `DM:HELLO` | `$H` | P1 opens, closes 800 ms later | front logic text "Hello There", rear "General Kenobi" | nothing (4 s) |
| `DM:NOD` | `$H` | P1 opens, closes 150 ms later | front logic text "Yes" | nothing (3 s) |
| `DM:FLUTTER` | none | ring then pies open one at a time, then all close one at a time | none | nothing (10 s) |
| `DM:BLOOM` | none | pies open together, wiggle three times, close together | none | nothing (8 s) |
| `DM:LEIA` | `$L` Leia message, bounded | none | `DV:LEIA` | light reset (36 s) |
| `DM:ALARM` | random alert-category track, falls back to the scream track | none | `DV:ALARM` | light reset (10 s) |
| `DM:HEART` | random sentimental-category track, falls back to the happy track | none | `DV:HEART` (the dome draws the text) | light reset (10 s) |
| `DM:RESET` | Track Stop | ring panels close one at a time, about 450 ms apart; toggle latches cleared | `*ST00`, `@0T1`, `@0P1` | **pies untouched** (4.5 s) |
| `DM:CANTINA` | `$C` long Cantina, bounded | 130 BPM loop: two mixed groups of ring and pie panels swap open and closed each beat | `DV:CANTINA` | open ring panels closed one at a time; pies stay as the last beat left them (17 s) |
| `DM:ROCKMARCH` | `$M` Imperial March, bounded; early Track Stop at 47 s | ring wave, one panel per beat, then a staggered re-close of every ring panel | `DV:ROCKMARCH` | light reset; ring already closed (49 s) |
| `DM:SCREAM` | random scream-category track, then `$H` | all pies and ring open, then a random one-panel flutter loop | `DV:SCREAM` | open ring panels closed one at a time; **pies stay open** (15 s) |
| `DM:OVERLOAD` | random sad-category track, falls back to the faint track | four ring and two pie panels flutter (`:OF`) at random, with timing jitter | `DV:OVERLOAD` | light reset; flutters end closed (12 s) |
| `DM:PIES` (toggle) | `$H` | open: pie wave PP1 to PP6 and back, twice, pies end open. Close: `*ST00`, pies close one at a time | `*ST00` on close | open half leaves the pies open (12 s) |
| `DM:LOW` (toggle) | `$H` | open: ring wave twice, then every ring panel opens, 200 ms apart. Close: `*ST00`, ring closes one at a time about 500 ms apart | `*ST00` on close | open half leaves the ring open (15 s) |
| `DM:OPENALL` (toggle) | `$H` | open: pie sweep, ring panels 200 ms apart, P1/P2 and PP2/PP4 twinkle. Close: every panel closes in order | none | open half leaves every panel open (10 s) |
| `DM:SE30` | none | none | none | utility arms out, flick twice, close (5 s) |
| `DM:SE31` | none | none | none | every body door and arm opens and works, then folds away (14 s) |
| `DM:SE32` | none | none | none | doors, dataport and utility arms spring open, wiggle shut (5 s) |
| `DM:SE33` | none | none | none | left door, gripper arm, claw snaps three times, folds away (9 s) |
| `DM:SE34` | none | none | none | right door, interface arm, tool works three times, folds away (9 s) |
| `DM:SE35` | none | none | none | breadpan doors take turns, then both close (13 s) |
| `DM:SE36` | none | none | none | BT-1: both doors, both arms, both claws snap five times, fold away (8 s) |

Times in brackets are each sequence's suppression window. `DM:SE30`..`DM:SE36` are
written in Body Steps and end with every Part they moved closed; a Part no Output
claims is skipped with `part-not-assigned` (see
[sequence-authoring.md](sequence-authoring.md#the-numbered-body-routines)).

Sequences that move pies (`DM:FLUTTER`, `DM:BLOOM`, `DM:CANTINA`, `DM:SCREAM`,
`DM:OVERLOAD`, `DM:PIES`, `DM:OPENALL`) depend on your pie linkages: nothing closes a
pie for you, and `DM:RESET` leaves them where they are.

## Dome-native aliases

These `DM:*` names forward a `:SE##` / `$NNN` target to the dome unchanged. The dome
owns every panel and light they move; the body does not choreograph them, and an alias
that moves pies or panel groups carries the same pie and group cautions as above.

`DM:STOP :SE00`; `DM:SESCREAM :SE01`; `DM:WAVE :SE02`; `DM:SMIRKWAVE :SE03`;
`DM:OCWAVE :SE04`; `DM:BEEPCANTINA :SE05`; `DM:SHORT :SE06`; `DM:SECANTINA :SE07`;
`DM:SELEIA :SE08`; `DM:DISCO :SE09`; `DM:SCREAMNOPANEL :SE50`; `DM:SCREAMPANEL :SE51`;
`DM:WAVEPANEL :SE52`; `DM:SMIRKWAVEPANEL :SE53`; `DM:OPENWAVE :SE54`; `DM:MARCHINGANTS :SE55`;
`DM:FAINT :SE56`; `DM:RYTHMIC :SE57`; `DM:HARLEMSHAKE $815`; `DM:GIRLONFIRE $821`;
`DM:YODA $720`; `DM:TOPPANELS :SE12`; `DM:WIGGLE :SE16`; `DM:BYEBYE :SE58`

## Run on a real droid

Recorded in the 2026-06-18 log: `DM:NOD`, `DM:LOW` (ring open and close, staggered
cleanup) and the `DM:ROCKMARCH` panel wave with its re-close pass. The `DV:` light
presets and the 200 ms panel spacing came after that log.

## Checking a run

`GET /api/seq/last-run` reports how the last run ended, the commands it sent and the
cleanup it queued ([api.md](api.md#get-apiseqlast-run)). Compare it with the sequence's
row above.

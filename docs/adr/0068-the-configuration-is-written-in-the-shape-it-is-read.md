# The Configuration is written in the shape it is read

Status: accepted (2026-09-25, issue #423; amended 2026-09-26, issue #431, see below). Settled by grilling the operator
after the architecture review of the operator-experience epic's hot spots.

## Context

`GET /api/config` answers the droid's **Configuration** as nested JSON
(`rc.member`, `drive.speedLimitMax`, `components.aux1.enabled`).
`POST /api/config` takes it back as flat form names (`rcMember`,
`speedLimitMax`, `enableAux1`). The only thing that turns one into the other is
hand-written browser code in the restore path (`configToFormParams`,
`data/maintenance.js`, 32 `p.set` lines), and nothing in the firmware checks
that the two agree. One scalar field touched 8 to 10 files under three names
(`rc_member` / `rc.member` / `rcMember`). A field can be readable and never
restorable, and no test notices.

An **Output** row is worse. It is read in two places: its ends as top-level
legacy names (`arm1OpenUs`) and its type, LED count and Motion Profile under
`components[id]` on `/api/config`, and its centre, `calibrated` bit and Parts on
`/api/servo/outputs`, keyed by address. It is written through four doors: the
legacy end names, `<id>ThrowMs`-style Motion Profile names, the LED count field,
and the capture and move acts. A restore has to know all four.

POST already took the GET shape for three fields, through a `plain` JSON body.
Each of those three had its range check written twice.

## Decision

**The Configuration is written in the shape it is read, and a restore sends
back what the backup holds.**

- **Scalars:** `POST /api/config` accepts the GET shape for every scalar field,
  growing the JSON body door that already did this for three. The form names
  stay for pages and the Controller Console. Both doors reach one check per
  field, so each range is written once.
- **Rows:** an Output row is written in the shape `/api/servo/outputs` reads it,
  one row per Output keyed by address with every field in it, inside the same
  `POST /api/config` JSON body. `/api/config` stops carrying row fields, the
  legacy end names included. Pages and the Console save a row through that door.
- **One Write Window:** scalars and rows land in one config Write Window
  (ADR 0011, amended 2026-09-24), so a restore of the Configuration lands whole
  or not at all, which is what ADR 0056's "a restore replaces the part it
  writes" needs.
- **Out-of-range values:** an end outside the Output's band is clamped into it
  and named in the answer, because the band can narrow after the ends were
  recorded and refusing would throw a calibration away. Every other row field is
  refused with its field, reason and accepts (ADR 0011, amended 2026-09-25). A
  row set that puts one Part on two Outputs is refused `conflict`.
- **Older backups:** a backup made before this change is translated once, on
  restore, at the backup seam: its `/api/config` row fields are folded into rows
  before they are posted. Nowhere else knows the old shape.
- **Proof:** a native round-trip test takes a Configuration through GET and back
  through POST and requires an equal one, for scalars and rows.

## Why

- The restore is the reader that matters most, and it is the one that could not
  be tested. Sending back what was read makes GET and POST one contract the
  firmware owns and one test proves.
- A row is one thing (ADR 0041: an addressed row with a directional endpoint
  pair). Reading it in two places and writing it through four doors is why every
  new row field needed a page, a restore and a firmware change to agree by hand.

## Considered and rejected

- **A firmware name table served to the browser.** It would keep the browser
  flattener, only fed from the firmware. It still leaves two shapes and a
  translation on every restore.
- **The round-trip test only.** It catches drift and fixes none of it.
- **Pages and the Console move to the GET names too.** One name per field, but
  it touches every page save and the Console's field table for no gain the
  shared check does not already give.
- **Rows through their own route, `POST /api/servo/outputs`.** A restore
  becomes two writes, and a failure between them leaves half a Configuration.
- **Rows in the `/api/config` shape.** It would carry the legacy end names
  forward and split a row across `components[id]` and the top level.
- **Refuse or partly restore an older backup.** It is the builder's own droid;
  one translation at the seam costs less than a lost calibration.
- **Refuse an end outside the band.** A narrowed band would turn a good backup
  into a recalibration.

## Consequences

- `data/maintenance.js` loses `configToFormParams` and its row flattening, and
  gains the one-time translation for older backups.
- `data/outputs.js` saves rows through the row door. It can no longer take a row
  field's form name from `/api/config`.
- The Console's `aux.config.led-count` writes through the row door.
- The firmware's legacy fixed field sets in flash, and the rows-first save order,
  are unchanged. This is about the API shape, not the flash layout.
- The capture, reverse and move-a-Part acts stay acts.
- ADR 0011's rejected "pre-copied params struct" is not reopened: nothing here
  copies values past the Param Source.

## Amended 2026-09-26: each Setting is declared once, and its words live in the browser

Settled by grilling the operator after the architecture review of 2026-09-26
(issue #431).

### Context

This ADR made GET and POST one contract, but left each **Setting** (`CONTEXT.md`)
written out by hand at every door. Measured on `epic/operator-experience` @
`6ee43d45`:

- One scalar, the dome's idle-turn move time, sits at 7 sites in 5 files: the
  struct, the default, the NVS read and write (its key typed twice), the GET
  writer, the GET-path table and the POST check, whose range is typed twice.
- A second range table, `configValidate`, disagreed with the POST check on four
  Settings (log level 1..3 against 1..4, the move time 100..10000 against
  500..10000, and two more). Outside tests, only the mood-mask write called it.
- The round-trip test this ADR asked for lists its Settings by hand, so a
  Setting left out of both GET and the test goes unnoticed.
- One Output row field, the LED count (#413), touched about 14 sites, and the row
  names its fields in two vocabularies (`leds` and `ledCount`).
- #425 put field, reason and accepts on every refusal, but only the Outputs module
  in the browser read them. Every other page showed the firmware's sentence with
  the wire name in it, which ADR 0059 forbids, and a test pinned that as correct.
- Log level took words on the Console and only numbers over HTTP. The RC mode
  words were mapped by hand in six places.

### Decision

- **Each Setting is declared once in the firmware:** its form name, its GET path,
  its parse and check (a range or a word list, answering field, reason and
  accepts), its NVS key and its default. GET, POST, the NVS save and load, and
  the Console's single-field ops loop over the declarations. **An Output row
  declares its Settings the same way**, and the row door reads and writes by it.
  NVS key strings do not change. Rules that span Settings stay hand-written
  beside the loop.
- **A Setting that takes words takes the same words at every door;** the
  declaration decides. GET keeps its shape, so the round trip holds.
- **The builder's words for every Setting, and its unit, live in one browser
  table.** Every refusal is worded from field, reason and accepts, never from the
  sentence. **A check fails when a declared Setting has no words.**
- **The browser keeps no copy of a firmware range.** The worded refusal on save
  replaces the page-side checks.
- `docs/action-registry.yaml` loses `nvs_key:`, and its check with it; the
  declaration is the one home.

### Considered and rejected

- **The words in the firmware declaration.** One home for names and words, but
  every copy review would need a build, and the words would cost flash on a board
  that is short of it.
- **A fallback sentence and no check.** ADR 0059's own evidence is a required
  field populated 194 of 194 times by convention and failing anyway.
- **As-you-type checks fed from GET.** The reference checks a pulse width at
  every keystroke, and that is a better moment. It would put every Setting's
  accepts on `/api/config`, which is tight on the artoo-esp32, and it is one more
  reader of the range. The builder learns on save, in words.
- **Keeping the page-side range copies.** They are how `configValidate` came to
  disagree.
- **Keeping `nvs_key:`, checked.** The defect pass of 2026-09-26 added exactly
  that check (`check_nvs_keys`, after 9 of the keys were found wrong). It keeps a
  third home true instead of removing it, and no code reads the registry's key;
  the check goes with the field.
- **Droid Settings only, rows later.** It would leave the row's 14 sites and a
  second list for the drift check.

### Consequences

- `configValidate` and the `ConfigKey` enum go.
- The round-trip test and a save-and-load test are generated from the
  declarations.
- `outputs.js`'s own words for row fields become entries in the one table.
- This does not reopen the rejected "firmware name table served to the browser":
  nothing is served, and the browser holds only words.

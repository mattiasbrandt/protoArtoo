---
name: bench-verification
description: Plan and run a Bench-Mode verification session for an epic's Closing Ticket - gather the verification points from the epic's sub-issues, draft the replayable Console sheet and the headed Playwright pass, run it with the operator watching, and record the evidence. Use when an epic nears closure, when asked to plan, draft or run a bench day or bench rows, or when editing tools/bench_rows/ sheets.
---

# Bench verification

A bench day answers one question: **does what this epic shipped actually work on
the hardware, and does it look right to the person who owns it?**

Two things make that answer expensive to get wrong. The bench is **Bench-Mode** -
a bare controller on a USB cable - so half the checks people want to write cannot
be run at all. And the operator is **in the room**, watching, because a green
assertion cannot tell him he dislikes what he sees.

This skill is the method. It is iterated after every session; correct it here
rather than re-deriving it.

## What a bench session verifies - all five axes, not just the API

**The image uploading and the endpoints answering is the least of it.** A session
that only proves the firmware boots and `/api/status` returns JSON has checked
the cheapest axis and skipped the four the bench exists for.

| Axis | What it means here | How it is checked |
|---|---|---|
| **1. The image is what we think** | `firmwareVersion` matches the intended commit; the filesystem image matches `fs-version.json`; no `-dirty` | Console + HTTP, first rows |
| **2. UI and UX** | Does it look right, work right and read right - at **both widths**, served from the **staged image**. Layout, focus, pointer-events, copy, whether a control is discoverable at all | **Headed Playwright, operator watching** (section 5) |
| **3. Performance** | Page load and first paint, whether a surface feels sluggish, SSE under concurrent clients, heap free / min / largest block, per-task stack headroom, fragmentation, admission-floor refusals | `/api/status` and `/api/profiler` rows, Playwright timings, resource-error counts |
| **4. Regression** | What worked last time still works. Defects this repo has shipped stay fixed | **Replay the existing rows** - see below |
| **5. API and console behaviour** | Routes answer truthfully, the Console catalog matches its pinned counts, guards and typing hold | Console `send`s |

**The sheet is the regression suite, and that is why rows accumulate rather than
being rewritten.** Every `@row` an earlier wave left behind is a check that
passed once on this hardware; replaying it is how you learn that this wave did
not break it. So:

- **Do not delete old rows** when drafting a new wave's. Add.
- Replay a bounded subset when a full sheet is too long for the day:
  `ROWS=<names>` in the order you want, and `SKIP_MANUAL=1` for the
  agent-runnable half.
- **Compare the numbers to last time, not to a threshold.** A heap or stack
  reading is evidence only against its own history; on this repo those rows
  belong to the memory headroom register, which is one ticket, not a note per
  session.

**Performance findings are measured, never impressions.** *"Felt slow"* is a
prompt to measure, not a finding. What the bench can produce today: the
`/api/status` and `/api/profiler` figures, Playwright navigation timings, the
resource-error count from the console sweep, and the behaviour of the surfaces
under a handful of concurrent SSE clients. Anything needing an instrument this
bench does not have is not a criterion - it is a note in the file that owns that
truth.

## 1. Bench-Mode is the boundary, and it is not negotiable

`CONTEXT.md` "Bench-Mode": *"powered by the computer's USB cable with **nothing
else connected to it** - no droid hardware and no test gear"*, and *"a criterion
that assumes gear on the bench is **mis-written**"*.

**No check that needs a servo, an RC radio, a dome, a sound module or a drive
backend may become a row.** Not as a candidate, not "if we have time", not
behind a `pause`. `hardware gate` and `droid gate` are both in CONTEXT.md's
`_Avoid_` list, and a sheet full of droid-only rows is a gate in everything but
name.

Such a check is **recorded, not scheduled**: it goes in the Closing Ticket's
`full-hardware-required` exposure record, which names what is unproven and where
to look. AGENTS.md: *"These labels describe evidence. **They are not a gate**"*
and *"**Where a risk is real but unmeasurable, document it, do not schedule
it.**"* A `full-hardware-required` exposure never blocks closure.

> Measured failure this exists to prevent: on epic #175, **five separate
> sessions** routed droid-component checks onto the Closing Ticket as candidate
> rows. All seven were withdrawn in one pass on operator correction. The firmware
> side of every one of them was already proven natively.

**What Bench-Mode still covers is a full day**: both images on both controllers,
every surface served from the staged filesystem image at both widths, the shell
and the status plate surviving navigation, estop reaching every surface, config
crossing real NVS, the Console catalog, and the runtime memory readings.

## 2. Gather the verification points from the epic

Do this **before** writing a single row. The point of the pass is to find what
the epic *claimed* and what nobody has yet seen happen on hardware. Work from
the tracker and the tree, never from a wave table in a body - those go stale.

```bash
# 1. Every child, with state. The epic's own body is not the source of truth.
gh api --paginate repos/<owner>/<repo>/issues/<epic>/sub_issues \
  --jq '.[] | "\(.number)\t\(.state)\t\(.title)"' | sort -n

# 2. For each MERGED child: its acceptance criteria and what earned them.
gh issue view <n> --json body --jq '.body'          # the criteria, ticked or not
gh issue view <n> --json comments --jq '.comments[].body'   # the acceptance comment

# 3. Everything already routed onto the Closing Ticket. Read EVERY comment in
#    full - these accumulate over months and the last one often supersedes the
#    rest.
gh api --paginate repos/<owner>/<repo>/issues/<closing>/comments \
  --jq '.[] | "=== \(.id) \(.created_at) ===\n\(.body)"'
```

Step 3 is long enough to be worth a subagent whose only job is to report an
inventory: each candidate check, its source ticket, the board it needs, **the
hardware it needs in the ticket's own words**, what it proves, and whether it
already ran. Treat that inventory as a claim and spot-check the two or three
load-bearing comments yourself before building on it.

### Separate the decision tickets from the build tickets FIRST

An epic's children are not all build work. On #175, **51 of 77 closed children
were decision tickets** - titled as questions (*"How deep does the servo output
model go?"*, *"Where does each operator surface live?"*). They built nothing and
have no bench-verifiable criteria.

Harvesting them produces two specific errors, both measured:

- **A false "created by" attribution.** A gathering pass credited
  `data/setup.html`'s split into Configuration and Maintenance to the decision
  ticket that *decided* it - while the split had never been executed at all.
- **A criterion that cannot be met.** A decision being recorded is not the same
  as a decision being carried out. **Check the repo shows it before writing a
  criterion that assumes it**, or the bench day inherits a check nobody can pass.

So: list the children, split them by shape, and harvest only the tickets that
shipped something.

### Three controls that make a delegated harvest trustworthy

A gathering subagent given *"work through the closed children"* will sample, and
a sampled harvest reads exactly like a complete one. All three of these are
cheap:

1. **Pass the explicit list of ticket numbers.** Never let the agent derive its
   own set - that is how a repo-wide sweep comes back dressed as an epic sweep.
2. **Require per-ticket accounting.** Demand a heading for every ticket in the
   list, printing *"none"* where there is nothing, plus a `TICKETS READ: <n>`
   line at the end. Absence of a heading is then visible.
3. **Cross-check the count against the tracker before using a word of it.** A
   first pass on #175 reported 68 closed children where the tracker said 77;
   that single number was the tell that the rest could not be trusted.

### Class 1 is the class that gets under-harvested — judge it with a checklist

It is the most valuable class and the most judgement-heavy, so an agent asked
for it vaguely returns three or four entries for a whole epic. Give it the
property list instead. **`mini_dom` has no CSS engine, no layout, no real
pointer and no real event dispatch**, so a criterion asserting any of these is
Class 1 by construction:

- layout, position, spacing, overlap, z-order, what covers what
- a control being visible, hidden, reachable, clickable, disabled, greyed, focusable
- colour, contrast, treatment, icon, visual state
- a count or a piece of text shown in a heading or a label
- pointer, hover or drag behaviour
- re-render and repaint behaviour: *"updates in place"*, *"never drops the
  current selection"*, *"no full re-render while a control is under the pointer"*
- what a person reads, is told, is announced or is warned about
- real persistence across a real reboot, real NVS, real flash, real heap figures

One worked example, so the bar is concrete. **C1a #347** closed with eleven
criteria ticked, of which five are Class 1: *"counts in the headings"*, *"a
displacement is announced before it happens"*, *"a Part with no Output reads
`- not wired -`"*, *"no row is hidden, in any state"*, and *"per-frame updates
touch only values; no full re-render occurs while a control is under the
pointer"*. That last one is a performance property as much as a UX one, and
needs a real pointer. A harvest that returns nothing for #347 has failed.

Then harvest the four classes of point that actually matter:

| Class | Where it comes from | Why it earns attention |
|---|---|---|
| **A criterion ticked on native or web evidence alone** | the merged child's body | The suite runs against `mini_dom`, which has **no CSS engine**. Layout, pointer-events and real gzip are invisible to it |
| **A new surface, route or catalog row** | `data/*.html`, `docs/api.md`, the action registry | A new route moves Console catalog counts; a new page must enter the Playwright sweep. Both are missed by targeted runs |
| **A defect this repo has already shipped** | the Closing Ticket's own defect tables | Regression risk is evidence, not pessimism. On #175 a green suite of 2531 native + 424 web hid **three** live operator-surface defects that one bench day found |
| **Behaviour that exists only on hardware** | real NVS crossing, heap and stack readings, reset reasons, OTA | No native seam reaches these |

**And name what is NOT a row in the same pass.** Every gathered point lands in
exactly one of three places: a row, the `full-hardware-required` record, or the
**Cut on purpose** table with its reason. A point with no home is how a bench day
turns into a typed session nobody can replay.

## 3. What earns a row

A row earns its place by **one** of:

- a safety invariant the change could violate,
- a defect this repo has shipped,
- a behaviour that only exists on hardware.

Cut everything else and name it. Specifically cut: a cell a native or web seam
test already proves; a second board's copy of a guard that lives in the shared
core (that proves the adapter, not the guard); anything the Console cannot reach
on that board; and any instrument or number this bench cannot produce today.

**Never write a criterion that names a number the bench cannot measure.** Check
it exists on this bench and in this firmware *before* the criterion is written;
if it does not, the risk becomes a note in the file that owns that truth.

## 4. The sheet

One replayable sheet per board: `tools/bench_rows/<board>.txt`. Sheets are
board-specific and `make bench-rows` **refuses to guess** which one you meant.

Grammar (`tools/console_client.py`, `parse_directive_line` / `split_into_row_blocks`):

- `@row <ticket> <name>` opens a block; everything to the next `@row` belongs to it.
- Directives: `send`, `raw`, `key`, `sendlen`, `listen`, `settle`, `timeout`, `pause`.
- Anything before the first `@row` is **preamble** and always runs - `timeout` and
  `settle` setup lives there.
- Row **names** are the selector and must be unique.
- A `#` comment block above each row says what it answers and why it is shaped
  that way. What a row is *expected* to answer stays on the owning ticket, never
  as an `expect` directive - there is deliberately no such directive.

```bash
make bench-rows BENCH_ROWS=tools/bench_rows/<board>.txt          # the whole sheet
make bench-rows BENCH_ROWS=... ROWS=safe-attach,survival-path    # named order, not file order
make bench-rows BENCH_ROWS=... SKIP_MANUAL=1                     # drops every row containing a pause
```

### The console/HTTP/pause split

The Console cannot express a browser, and some evidence is HTTP-only. So a row is
**`pause` for the human half, then `send`s that capture machine-readable evidence
of the same moment**:

```
@row 339 the-controller-says-where-it-routes
send system.api.get-identity
pause Fetch GET /api/identity and keep the board_lanes object, then press Enter
send system.status.health
```

That is what makes a browser or HTTP check replayable instead of a typed session.
Use it for anything the console genuinely cannot reach - and prefer a Playwright
script over a `pause` whenever one can do the job (section 5).

## 5. The Playwright pass - **headed, and the operator watches**

> **Standing operator instruction: not headless.** The bench day is a
> collaboration. He watches the browser to catch what no assertion was written
> for - something that looks wrong, or that he simply does not like. A headless
> run answers *"did anything throw"*; a watched run also answers *"is this
> good"*, and only one of those has a script.

So: **give the run a pace a person can follow and a way to stop on a page.** A
sweep that blinks through thirteen surfaces in twenty seconds is not a session he
can take part in.

**Two required checks, every session:**

1. **Console errors - zero, on every served page.** `test/playwright/console-sweep.js`
   already does this against the live controller. It separates `pageerror`,
   `console.error` and failed resource loads, and settles on a timer rather than
   `networkidle` because SSE never closes.

   ```bash
   HEADED=1 BASE=http://<board-ip> node test/playwright/console-sweep.js
   ```

   **Before trusting it, check its `PAGES` list against `data/*.html`.** It goes
   stale every time the epic adds a surface, and a zero-error sweep that never
   loaded the new page says nothing about it. Check the viewport too: the sweep
   owes **both widths**, and a hardcoded one is half a sweep.

2. **UI and UX of what this epic implemented.** Per-surface scripts live in
   `test/playwright/<surface>/`. Any surface the epic built without one is a gap
   the Closing Ticket owns.

Prefer asserting over eyeballing where the property is objective - a STOP button
that a dialog has covered with `pointer-events: none` is a real assertion, and it
is exactly the class of defect `mini_dom` cannot see. Keep the operator's eyes for
the judgements only he can make.

**Three more things to take from the same run, because the browser is already
open on the real controller:**

- **Performance.** Navigation timing per surface, and whether anything reloads or
  re-renders in a loop. The sweep's resource-error column is a free read on
  wasted requests and 404s.
- **Regression.** Re-run the per-surface scripts the epic did not touch. A script
  that passed last wave and fails now is the cheapest regression signal available
  and nobody has to have predicted it.
- **What the operator says.** He is watching for the reason in the callout above.
  Write what he raises onto the ticket that owns those files **while the browser
  is still open on it** - that is the difference between a finding and a memory.

**Close the browser as the last step of every run** - including a run that found
nothing and a run you abandoned. Headed means every browser is a real window left
on his desktop. `.claude/skills/playwright/SKILL.md` carries the full shutdown
protocol.

## 6. Running the session

- **Run it in a Herdr pane, never through a plain shell tool.** `pause` calls
  `sys.stdin.isatty()` and fails without a controlling terminal; a `sudo`
  YubiKey cue needs the same. Tee the output to a log and parse the log - the
  pane is for the operator to watch, the log is what you verify against.
- **One build machine-wide.** `make` and the slice gate take
  `/tmp/protoartoo-pio.lock` themselves; run them plainly, never with `flock`
  in front, which is refused as a nested take.
- **Confirm the image before any acceptance run.** `firmwareVersion` must match
  the intended commit; a `-dirty` or stale image invalidates the whole run. If
  the change touched `data/`, the filesystem image must be uploaded too and
  `fs-version.json` must match.
- **Ask before every device session.** The board is a single shared resource and
  another epic may be using it.
- **Leave the board as the session left it.** Do not reflash or restore it to
  some earlier state as a courtesy; say in the report what state it is in. The
  next session reads that, and a silent restore has destroyed provisioning here
  before.

## 7. Record it

- **The transcript is the evidence comment's body**, on the Closing Ticket. One
  comment per run, dated, with the AGENTS.md verification label.
- **The verification tail of an epic is ONE ticket.** Bench rows, soak, audit and
  the closure PR together. Never a ticket per runbook, matrix, audit or
  integration-readiness step. A ticket with every box ticked closes in the same
  pass; it is never left open for one unobtainable number.
- **Route what the session finds while it is still open.** A defect goes on the
  ticket that owns those files, in the same pass, naming `file:line` and what
  that ticket has to do about it. That includes anything the operator says while
  watching - his findings are findings. A finding named in a session summary and
  nowhere else is lost.
- **Update this skill** when a session teaches something. That is the point of it
  being a skill rather than a habit.

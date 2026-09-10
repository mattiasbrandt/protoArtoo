# An output is found by moving it, and one output may drive several parts

Status: accepted (2026-09-08, issue #296). Describes the **target** model.
Nothing in it ships today.

## Context

A builder rewires constantly: a linkage gets reprinted, a servo burns out and
moves to a spare output, an expander arrives and everything shifts. The **Droid
Parts Catalog** names forty-two parts on a full MK4; two of them are `body-ledc`
today and twenty-four sit at `control: none`. After #301 an **Output** records the
**Part** it drives, so *which output moves this part* became a live question with
no surface to answer it.

**The reference's answer does not transfer.** Its `AUTO-MAP BY NAME` works because
its channels carry **builder-typed names** it matches against part roles. An
Output here is `(driver, channel)` plus the Part it drives - ADR 0041 deliberately
gave it no name, and `CONTEXT.md` is explicit that the Part is identity while the
address is only where the lead plugs in. The catalog's `aliases` are first-class
for **import** matching, which is #294's, not for a blank device. So on a droid
nobody has configured yet there is nothing at all to match on, and the ticket's own
"auto-map by name" was naming a capability that cannot exist in this model.

**The reference also contradicts itself on multiplicity.** Its dome map treats a
part claimed by two channels as `dup`, *"flagged, not forbidden"*; its part-first
table states the opposite, *"a part has exactly one channel: giving a panel a
channel that another one holds MOVES it"*. Both of its models are one-to-one on the
*channel* side, and that is the case neither of them can express: a builder who
Y-harnesses both breadpan doors to one lead can name only one of them, while the
other reads *- not wired -*, reports `part-not-assigned` when a sequence names it,
and moves anyway. That is a wrong answer, not a missing feature.

**And the committed naming policy cannot be applied as written.** The catalog's own
rule (operator, 2026-08-22) makes `cad_name` the base name. Every dome `cad_name`
is `TBD` and the file says not to guess them, because the dome CAD names are
unordered and duplicated. The body ones exist but are inconsistently cased straight
out of Fusion - `FLBreadpanDoor` beside `FRBreadpandoor`, `RLBreadpanDoor` beside
`RRBreadpandoor` - which the reference's own parts list reproduces verbatim, so the
inconsistency is authentic rather than ours to fix.

## Decision

**Parts** becomes a destination in the **Configure** **Activity Group**, carrying
both projections of one mapping - part-first for droid work, output-first for
wiring work (#318) - together with the calibration dial (#291) and the discovery
run. Guided **Setup** could not host it: #297 made that one-shot, and this is a
table a builder returns to.

**An Output is found by moving it.** **Find by Moving**: from an unwired row,
protoArtoo steps through the unclaimed Outputs, nudges each one briefly, and
records the assignment when the builder says *that one moved*. The nudge is
**bounded and symmetric about where the Output already is** - never to an endpoint,
never to a recorded end that may not exist. It is motion, but it asks for no
**Non-RC Control** consent - that flag has never reached `POST /api/servo`, and
ADR 0064 settled one rule for every browser-initiated servo move. Corrected
2026-09-09; this clause originally said it took the same consent calibration
does, which #291 reversed the following day. Any estop ends the run, and a run
nothing responds to leaves the Part *- not wired -*.

**The multiplicity is asymmetric.** A **Part** is driven by **at most one Output**,
and assigning it elsewhere *moves* it rather than sharing it. An **Output** may
drive **several Parts**, so a ganged lead tells the truth about everything it
moves.

**A row is labelled with the part's plain-English alias** - *Left body door*,
*Dome pie 1* - with the Printed Droid shorthand alongside on dome rows. `cad_name`
stays provenance in the data and reaches the bench through **Wiring**, which #293
already made the bridge between the two naming systems.

**A builder does not rename a Part.**

**The surface still never writes.** It reports a pick; the caller owns what the
pick means. Unchanged from the shipped contract, and it is what lets one renderer
serve a picker, a live-state display and a preview without any of them acquiring a
write path.

## Why

**When names cannot be the fast path, movement is.** protoArtoo drives the real
droid over HTTP where the reference needs a flashed bridge sketch, so the one thing
we can do that it cannot is make the droid answer the question itself. It is also
this project's existing calibration philosophy pointed at a different problem:
*"you turn it until the panel is where you want it and record that number; you do
not know it in advance"*. The alternative that needed no motion - proposing from
the **Board Lane** each Output sits on - fills two rows of five today and two of
twenty-one once an expander is fitted, which is to say it guesses hardest exactly
where a builder needs help most.

**The nudge is bounded because an unassigned Output is an uncalibrated one.** #286
records what the first move on an unmeasured channel is: a jump, not a ramp, hand
near the power. A discovery run that drove to a recorded end would aim that jump at
a linkage nobody has measured, on the one occasion the builder does not yet know
what is on the other end of the lead.

**The asymmetry is not a compromise; the two questions genuinely have different
answers.** *What drives this part* must have exactly one, or firmware resolves by
whichever row it scans first and the droid's behaviour depends on table order.
*What does this lead move* can honestly have two, and refusing to say so does not
make the second servo stop moving. Two Parts on one Output cannot move
independently, which is a **Rehearsal Warning** and a second cause for #287's
existing "two steps driving one Output at once" rule - advice, never a refusal.

**A rule written for the catalog's data is not a rule for the operator's eye.** The
2026-08-22 policy fixes what the *catalog* calls a part, and it stays: `cad_name`
is provenance and the wiring sheet is where a builder meets it, next to the output
it plugs into. What a row *reads* is a copy decision, and the research's own voice
rules on it - plain imperative language, no backend nouns - which `UpperUtilityArm`
fails and *Upper utility arm* passes.

**Renaming would spend the one thing the vocabulary is for.** The community's names
are what make a screenshot, a support request and a shared sequence legible to
somebody who has never seen this droid. #301 makes a rename technically safe -
sequences reference the part id - but safe is not the same as free, and the map's
destination is a builder speaking *the vocabulary they already use*, which is the
community's, not one invented per droid.

## Considered and rejected

**Auto-map by name**, the reference's own answer. Rejected on fact: there is no
name on an Output to match against, and adding one creates a second identity beside
the Part, which is the coupling `CONTEXT.md` separates on purpose.

**Proposing from the Board Lane, then correcting.** Cheap and needs no motion.
Rejected: only the two arm headers carry a convention worth proposing.

**Driving to a recorded end so the move is unmistakable.** Rejected: see above -
it is the JUMP-not-a-ramp move aimed at an unmeasured linkage.

**Handing the builder the drive control for each candidate.** The strongest safety
story, since every millimetre is a deliberate human act. Rejected: barely faster
than the by-hand table it replaces, and it puts the builder at the keyboard when
they need to be looking at the droid.

**Exclusive both ways** - the ticket's own recommendation and the reference's
part-first table verbatim. Simplest model anyone can hold, one answer in both
directions. Rejected: a ganged lead is unrepresentable, so a builder either accepts
that half their wiring is invisible to the software or gives up the gang.

**Shared and flagged both ways**, the dome map's `dup`. Rejected: two Outputs
claiming one Part means firmware resolves by scan order.

**Renaming, stored with the Droid Build.** Rejected above.

**CAD names as row labels.** Rejected: inconsistent casing on adjacent rows, absent
for every dome part, and the wrong voice.

**Wiring as the home.** Its CONTEXT entry already asks this ticket's question
word for word. Rejected: #293 decided Wiring writes nothing and that its promise is
bounded to what the running firmware reports; making it the place you reassign a
servo reopens that decision rather than extending it.

**Configuration as the home.** No new destination, and #288's two-way split of the
old Setup page would hold as decided. Rejected: it would carry Droid Identity,
components, LED routing, the **Droid Build** and a forty-row table with live motion
behind it.

**"Bench" as the name**, the reference's word and the builder's own. Rejected: this
project already spends it on **Bench-Mode** and the **Bench Runbook**.

## Consequences

- **`CONTEXT.md`'s Output changes shape**: "drives exactly one Part" becomes "one
  or more". Every consumer asking *which Output drives this Part* still gets one
  answer, so nothing downstream of the sequence engine changes.
- **A bulk "test everything" needs no special case.** It is a generated set of
  moves, so the **Cadence Floor** (#319) paces it, exactly as it paces a Gesture's
  expansion.
- **`- not wired -` finally keeps its promise.** #327 placed `part-not-assigned` in
  the **change it here** **Availability Family**, whose whole claim is that the next
  move is available on this screen. Now it is, and it is *find it*.
- **#317 inherits the label rule and no rename affordance**; its body views draw
  *Left body door*, and the identity surface it was going to own for renaming does
  not exist.
- **#318 shares this destination**, so the two projections cannot drift into two
  pages, and #291's dial opens from the same table.
- **The catalog's naming policy is amended for display only.** `cad_name` remains
  the base name in the data and the thing the wiring sheet prints.
- **Parts the destination sits beside Part the model term.** Deliberate: the plural
  is the page where a builder meets every Part of their droid, and the singular is
  what the page is full of.

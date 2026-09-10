# The shell outlives the page, and the estop rides it

Status: accepted (2026-09-08, issue #325). Describes the **target** model.
Nothing in it ships today.

## Context

The nav is ten `href`s to ten documents (`data/shell.js:25-34`), so every change
of screen is a full document teardown: the `/api/events` stream drops, the
**Common Page Bootstrap** re-runs, and the droid's state is blank until it comes
back.

Two facts make that worse than it sounds.

**The estop control exists on two surfaces of ten.** `data/index.html:42` and
`data/drive.html:27`. On Sequences, Dome, Sound, Servos, RC, Configuration, WiFi
or Firmware there is no way to stop the droid without navigating first — and
until this decision, navigating also cost the status stream. The map is already
growing that list: #288 split Setup into **Configuration** and **Maintenance**,
#293 made **Wiring** a destination, #297 may add guided **Setup**, #333 added the
**Droid Build**. Twelve or thirteen surfaces, two of which can stop the droid.

**The live-update budget is three clients** (`PA_ADMISSION_MAX_SSE_CLIENTS`,
`include/web_event_stream.h:34`) — a fixed heap-free registry, a 250 ms per-event
send deadline, and a stalled client evicted rather than buffered, deliberately,
because *"the stalled client IS the operator"*. The client is already
visibility-aware and backs off with jitter, explicitly for *"multiple open pages
and tabs losing the stream at the same moment"*.

That budget points the opposite way to the intuition it invites. Ten documents
take and release a slot per page view; a frame that survives navigation holds one
slot for a whole session and makes no reconnect at all. **Not tearing down is the
cheaper option on the device**, not the dearer one.

**The reference is not the model here.** Its four workspaces are pane
compositions — across Drive, Sequence and Configure the droid, the drive sliders
and the controller pad are on screen in all three, and only the right-hand
inspector swaps (`tasks/research-r2d2-sim-shots/02`, `03`, `06`). protoArtoo is
not building that, by operator decision on 2026-09-08: no live droid view, and
layout stays with the frontend work. The interactive SVG maps do extend from the
dome to the body, but as surfaces a builder navigates to (#317), not as furniture.

## Decision

**The shell outlives the page, and the estop rides it.**

An **Operator Shell** is the persistent frame every surface is shown inside. It
owns the nav, the identity, the status chips, the `/api/events` stream and the
**Latching Estop** control, and it survives every navigation. Content swaps
beneath it.

**This is pages, not workspaces.** One content region, no pane composition, no
droid view in the frame. The shell carries what the droid is *doing*, never a
picture of it.

**The estop is on every surface. Clearing the latch is not.** Releasing a latched
estop stays on Drive and Dashboard, where it is today, because the direction that
lets a droid move again should stay somewhere an operator went on purpose. The
asymmetry already exists in the firmware and in **Browser Request Priority**,
where a Latching Estop already bypasses queued page work.

**Leaving a surface unmounts it and stops any polling it owned**, while the
configuration it already fetched is kept, so returning paints without a refetch.
Anything live comes from the shared stream, which never stopped.

**Addresses are hash routes** — `/#sequences`. Every surface stays linkable, and
the device needs no change: the browser asks for `/`, the existing
`serveStatic("/", LittleFS, "/")->setDefaultFile("index.html")` answers, and the
fragment never reaches the ESP32. The ten `.html` files keep resolving through a
transition.

**A reload honours the address.** `/#sequences` opens Sequences at its last saved
state, warns before an unload that would lose work, and says plainly after one
that did. There is no invisible rule that relocates the operator.

**The nav is ordered by job, not by subsystem** — **Drive**, **Perform**,
**Configure**, **Maintain** — and a surface may appear in more than one
**Activity Group**, because Sound and Dome are reached for in two of them.
Dashboard sits outside as the landing; guided Setup sits outside as a takeover.

## Why

**Two of twelve is the whole argument.** Everything else here is plumbing. A
builder at an event, on the Sequences page, with a stranger's child near the
feet, currently has to navigate before they can stop the droid. Nobody ever
regretted stopping a droid, so the cost of the control being present is bounded
and the cost of it being absent is not.

**The budget argues for the shell, not against it.** The instinct that a bigger
single page is heavier on an ESP32 is wrong here: the device's costs are the SSE
slot and the HTTP requests, and a persistent shell reduces both. Browser-side
memory is the browser's.

**Recovery already works this way one level down.**
`data/page_bootstrap.js:126` — *"once resources are ready, sections are
independent: one section's failure neither blocks nor resets another."* Making
the shell the outermost always-live region is a restatement of that model, not a
new idea, and it buys something specific: a surface that cannot load never costs
the operator the estop or the status behind it.

**A link that does not open what it names is worse than no link.** The ten pages
are addressable today and a support conversation relies on it. Hash routes keep
that for nothing, so the addressability question never had to become a firmware
question.

**Grouping is finding, not owning.** The awkward cases are two — Sound and Dome —
and forcing them into one home each would mean an operator looks in the right
place for them and finds nothing. Letting a surface appear twice costs a
duplicate entry pointing at the same route.

## Considered and rejected

**Workspaces as pane compositions**, the reference's actual model. Rejected by
operator decision: protoArtoo is not building the live droid view that makes it
work, and layout belongs to the frontend UI work rather than to this map.

**Keeping ten documents and making the gap invisible** by holding the last known
status across a load. Cheapest, no architecture exposure. Rejected: it paints
state that is stale by construction, which is the confusion #298 spent a whole
decision preventing.

**Keeping ten documents and calling reach a navigation problem**, solved with
deep links and a status strip. Rejected: it answers the ticket's own capability
question with no, and leaves the estop on two surfaces.

**Both estop directions in the shell.** Symmetric, simplest to explain. Rejected:
it puts the control that lets a latched droid move again one press from every
screen, including the ones where nobody is watching the feet.

**Keeping a left surface mounted and live**, the reference's stated *visibility
only, never behaviour*. Rejected on device cost: the two surfaces that poll
hardest are RC diagnostics and the profiler, which are exactly the ones an
operator opens when something is already wrong.

**Real paths rather than hash routes.** Tidier addresses. Rejected: the static
handler serves files and has no fallback, so it would buy tidier links with a
change to the request seam ADR 0021 owns, for an outcome hash routes already
deliver.

**Never resuming an authoring surface on reload**, the reference's rule.
Rejected: the same address would then behave differently depending on how the
operator arrived at it, and they cannot see which rule is firing.

## Consequences

- **The reference's *visibility only, never behaviour* rule is broken on
  purpose.** Leaving a surface stops its polling. What changes is only what the
  browser asks for — nothing the droid is doing changes — which is the half of
  that rule worth keeping, and this ADR is where the departure is recorded.
- **The Page Recovery View renders inside the content region**, not over the
  whole screen, so the chips and the estop stay live behind a surface that
  failed to load. **Common Page Bootstrap** is restated rather than replaced: a
  page becomes a content module that mounts, and its declaration of resources and
  sections is unchanged.
- **#288 is unblocked** with the group set it was waiting on, and its pages model
  stands rather than being overturned.
- **No firmware change is required** by this decision. The hash-route form was
  chosen partly so that it is not.
- **Unverified, and worth measuring rather than assuming:** whether a fast page
  switch can transiently reach the three-client cap before the old socket is
  reaped, costing a 2-30 s backoff with the status dark. It argues for the shell
  either way, so nothing here depends on the answer.
- **Layout is not decided here** and stays with the frontend UI and UX work.
  Neither are the SVG body views (#317) or draft persistence for an in-progress
  edit (#289, #299).

# The body routes a command by what it owns, not by its prefix

Status: accepted (2026-09-08, issue #321). Describes the **target** routing.
The raw-family forwarding ADR 0045 settled is unchanged.

## Context

A builder arriving from ShadowMD or Padawan360 has a droid whose parts are
addressed by a shared ASCII vocabulary and existing button bindings pointing at
it. ShadowMD's catalogue is 89 numbered functions; the template emits **101
distinct command strings**, split across two serial ports — `Serial1` to the dome
Marcduino and `Serial3` to the body Marcduino.

**The dialect assumes two boards. protoArtoo is one.** Both boards use the same
numbers for different hardware, so when one controller answers for both, the two
number spaces collapse into each other.

Measured against the template, of those 101 tokens protoArtoo today:

| | count |
|---|---:|
| interprets itself | 57 |
| forwards, and the dome fork handles | 8 |
| forwards, and the dome fork has no handler | 7 |
| **rejects or silently swallows** | **29** |

**36 of 101 do nothing at all**, and the largest single cause is structural
rather than missing features: `src/web/api_drive.cpp:172-181` consumes every `:`
and `#` command and returns success regardless — *"always accept - body handles or
discards per routing table"*. Nothing on those two families is ever forwarded.

So **23 of the 29 rejected tokens have a working handler in the dome fork** and
are unreachable only because the body ate them: `:SE51`–`:SE57` and `:SE00`,
`:SE12`, `:SE50`, `:SE58` (`AstroPixelsPlus/MarcduinoSequence.h:287-343`), and
`:OP06`–`:OP12` / `:CL06`–`:CL10`, which the fork serves across 60 live panel
commands while the body accepts 7 `:OP` numbers and no `:OF` at all.

## Decision

**The body interprets the commands that name things it owns, and forwards the
rest.** Prefix stops deciding who answers; ownership does.

**Ownership derives from the Droid Parts Catalog.** A command is the body's when
it names a **Part** the catalog does not mark `control: dome-link`. Nothing new is
declared: #301 already made `docs/droid-parts.yaml` the single declaration that
firmware and the browser are generated from, with a report-only drift check
behind it, so routing becomes a consequence of a fact the project already
maintains.

A command naming a Part the body models but no **Output** drives stays the body's
and reports `part-not-assigned` at run (#301, #319) — it is not forwarded into
silence.

**And the promise is published, bounded at the body.** protoArtoo states which
commands it answers, and says of the rest only that they are forwarded and the
dome's to report. It commits to no new drivers: the boundary is drawn where the
model already ends.

## Why

**A builder's existing bindings are the compatibility surface that matters.** ADR
0045 kept the passthrough on that reasoning — *"a controller that refuses `*RD00`
because it would rather own the vocabulary is a controller a builder works around
on their first evening"*. Swallowing 29 tokens and reporting success is the same
failure wearing politeness: the command is accepted, logged as handled, and
nothing happens.

**Ownership is already written down.** The catalog's `control` column exists, is
generated, and is drift-checked. A static routing table or a per-command registry
field would both be a second place for a fact that already has one — the trap
#301 was created to close.

**Forwarding what we do not own costs nothing and revives 23 commands.** The dome
fork already implements them. The body was the only thing standing in the way.

## Considered and rejected

1. **A static routing table of tokens the body owns.** Smallest change, entirely
   legible. Rejected: it is a promise restated in a comment, kept in step by hand,
   and it drifts the first time a Part gains an Output and nobody remembers the
   table exists.
2. **A per-command declaration in `docs/action-registry.yaml`**, enforced by
   `make check-action-drift`. Covers direct panel commands uniformly, which the
   catalog derivation covers less directly. Rejected: for everything #319 turned
   into a Factory Sequence of **Body Steps**, the Parts are already named by the
   steps, so this becomes a second home for a fact that has one.
3. **Making the destination explicit instead** — two addresses, as the dialect has
   two boards. Not rejected on merit; it makes every answer honest but revives no
   dead command, because the failure there is not addressing but that nothing
   forwards.
4. **Declaring what the dome does with a forwarded command**, either by shipping a
   table of our fork's handlers or by having the dome report its own surface.
   Rejected here for the reasons ADR 0045 gave two days earlier: the first is a
   body-side table describing a dome-side build that goes stale the first time the
   fork adds a verb, and the second commits dome firmware to a new contract from a
   body-side decision ticket — the third time that has been declined, after #287
   for dome timing and #320 for light state.

## Consequences

- **The residual collision is real and is now stated rather than silent.** `:OP01`
  means body arm 1 here (`include/marcduino_helpers.h:33-34`) and dome panel 1 to
  the fork. That matches what ShadowMD itself means on `Serial3`, so the body's
  reading is the faithful one — but a builder pasting a *dome* binding into the
  body surface still gets an arm. The published boundary is what tells them so.
- **A forwarded command's fate is not ours to report.** The Marcduino dialect has
  no reply channel, so the availability vocabulary — `part-not-assigned` and the
  rest — reaches the Controller Console, the web and the **Rehearsal**, and never
  the sender of a `:` command.
- **`:SE01`–`:SE09` become whole.** Today the body fires their audio and a body
  servo sequence and forwards nothing, so panels, logics and holos never fire —
  roughly a third of what each promises. Under ownership routing the dome half
  reaches the dome.
- **`:OF` has to be recognised.** The body accepts none today while the fork
  serves 20; the flutter it names is a shape **Move Shape** already carries.
- **Nothing about the raw families changes.** `*`, `@`, `%`, `&` and `!` forward
  uninterpreted exactly as ADR 0045 settled.

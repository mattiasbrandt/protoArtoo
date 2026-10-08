# A light type belongs to the wire, and the dome keeps its own

Status: accepted (2026-09-20, issue #410). Settled by grilling the operator
after three rejected iterations of the Lights surface.

## Context

#410 set out to give the LED strip a page of its own. Three iterations were
built and all three were rejected, each for the same underlying reason, which
only became visible on the third:

| Iteration showed | The operator said |
|---|---|
| A picker choosing which **Output** carries the strip | *"Does not make sense that this is a mapping section"* |
| `aux_led_pin`, Output labels, "AUX" on screen | *"we are too focused on the old pages design and firmware specifics for Artoo!"*, *"AUX is a pure artoo term"* |
| An empty body-lights section beside a running strip | *"strange cause we have a active led strip... just bad"* |
| `NOT DRIVEN` on the six dome lights | *"the dome lights are wired and handled by the dome controller (astropixels plus) but we can control them in the body using the documented commands"* |
| `LED strip` as a row beside *Charge Bay Indicator* and *Data Panel* | *"That is also WRONG! LED strip is simply one of the differed light types we can assign to a droid part"* |

The model was the defect, not the layout. **protoArtoo had no word for a
lighting technology**, so the only vocabulary available for the body strip was
the firmware's own plumbing - `aux_led_pin`, an **Output**, a board label - and
every page built from it became a page about the strip.

Meanwhile the shape was already half-present and unnamed: `data/output_settings.js`
holds an Output's `type` as `mg996r`, `mg90s`, `none` **or** `rgb`. A servo model
and an LED strip already share one field, because both answer *what is on the end
of this wire*. Nothing said so, `rgb` sat inside the servo list, and it was chosen
on **Wiring** while servo models were chosen on **Servos**.

## Decision

**A Light Type is what protoArtoo puts on its own lead to light a Part, and it
belongs to the Output.** It is the light half of the answer a servo gives as its
model. A **Part** inherits the type of the wire it is on; it does not carry one.

**A dome light has no Light Type.** The dome controller owns that hardware.
protoArtoo reaches it by forwarding documented commands - `*`, `@`, `%`, `&` and
`!` to dome TX (`docs/commands.md`), and `DL:` / `DT:` / `DH:` which
`src/protocol_check.cpp` validates and `data/seq.js` already labels for a builder.

**So the two halves of Lights read differently, on purpose.** A light we drive
names its type and reads *on* / *off* / *flash* with brightness for *how far*,
the words ADR 0049 gives every Part. A dome light offers the dome controller's
own modes and colors, under the labels a **Sequence** already shows.

**A droid may have several lit body Parts, each on its own wire.** Today's
single `aux_led_pin` becomes one answer per Output. That firmware change is
staged behind the surface and carries its own bench run.

## Why

**A technology modelled as a thing produces a page about the technology.** That
is the mechanism behind all three rejections, and naming the type is what stops
it. Once *LED strip* is a type, *Data Panel* can be the thing, and the page is a
list of the droid's lights rather than a console for one product.

**The field already existed; only the name was missing.** Putting a Light Type
beside a servo model costs no new storage shape and no new surface grammar - it
describes what `type` has always held. A model that names what the code already
does is cheap to adopt and hard to get wrong.

**Honesty about the dome is not optional.** `control: none` in the catalog means
*no Output of ours drives it*, and a surface that renders that as "not driven"
tells a builder they cannot do something they can do today from Sequences. ADR
0045 already binds every surface here: a modelled light state is commanded
intent, never device truth.

**Two vocabularies beat one dishonest one.** ADR 0049 rejected *lights with
their own words* for **sequence steps**, because a Gesture must span a mixed set
and Protocol Check would gain a second grammar. Neither argument reaches a live
control on a surface: nothing spans a mixed set here and nothing is stored. A
dome light offered only *on/off/flash* would hide the modes its hardware has,
which is the dishonesty ADR 0045 forbids in the other direction.

## Considered and rejected

1. **A Light Type on the Part.** Reads naturally - *"the Data Panel is lit by an
   LED strip"* - and matches the operator's own sentence. Rejected: a servo's
   model is not on the Part either, and splitting the two would mean a Part
   claiming hardware that is really a fact about the wire. The Part inherits it,
   which says the same thing once.
2. **A light part declared in the catalog with the strip among its rows.** What
   iteration 3 built. Rejected by the operator by name: it puts a technology
   beside two things on the droid.
3. **One vocabulary everywhere - on/off/flash for the dome too.** One word means
   one thing, and it is what ADR 0049 does for steps. Rejected: it hides the
   dome controller's modes behind three words that cannot express them, on a
   surface whose stated job is telling a builder what they can do.
4. **Wiring loses the LED strip question; a light's wire comes from the Part's
   Output assignment**, as a servo's does. Tidier, one answer instead of two.
   Rejected by the operator: what is physically on a wire is a wiring fact, and
   Wiring's charter is *"I am holding a wire: where on this board does it go?"*.
5. **Build the per-wire firmware change and the surface together.** No builder
   would meet the one-strip limit. Rejected: nothing is reviewable until both
   land, and the config, NVS and API change wants its own bench run.

## Consequences

- **`rgb` is a Light Type, not a servo type.** It leaves the servo list. The
  stored token stays `rgb`; the builder already reads *LED strip*.
- **Declaring a Part opens an Output picker** (`data/parts.js`), so a body light
  Part gets one and it is meaningful. The strip was never a Part, which is why
  its picker would have recorded a mapping nothing reads.
- **`aux_led_pin` and `aux_led_count` become per-Output.** Until they do, the
  surface shows one lit body Part and says so.
- **The Lights surface carries no Output name, pin or board label.** Where a
  wire goes is Wiring's answer, and it is reached from here rather than repeated.
- **ADR 0045's deferral to #319 is spent.** #319 closed without settling body
  light outputs, so the per-wire change now hangs off this decision instead.

// =============================================================================
// data/droid_part_kind.js
//
// What a Part Kind promises, and therefore what a surface must not show (#357).
//
// The Droid Parts Catalog declares a `kind` on the Parts it can classify - a
// light today, and CONTEXT.md names servo-driven and indicator as the two the
// model foresees. This module is what reads it. A caller branches on the answer
// here, never on an id prefix or a name match: the Magic Panel is a light
// because `docs/droid-parts.yaml` says so, not because it is called one.
//
// Two rules shape everything below.
//
// A Kind is carried by TREATMENT, never by colour. #327 reserves colour for two
// meanings - red is stopped or refused, amber is you can do something about
// this - and a Part being a light is neither (ADR 0063; docs/ui-copy-voice.md
// rule 11). So the Kind earns a state class and nothing else, the way the
// reference project's unwired brick does.
//
// And a treatment REMOVES what its Kind cannot promise. A light has no travel,
// so a light row shows no travel: not a greyed-out one, not a zero, not an
// em dash where a number goes. Every one of those still reads as a promise
// about movement, and this Part cannot keep it. `Output Release` goes the same
// way - a light can fight nothing, so a light row does not carry the column
// (#318).
//
// One thing a light's Kind does NOT answer, deliberately: what a latched estop
// does to it. ADR 0043 releases servo OUTPUTS, and a light has no travel to
// release - a latched estop leaves the holos running today. That is an open
// question (#318) rather than a behaviour this module implies, so nothing here
// reports a light as stopped, held, or released.
//
// NO PAGE LOADS THIS MODULE YET (#375). It appears in no `data-scripts`
// attribute and no other data/*.js calls `window.DroidPartKind`; what reads it
// today is the web suite, and tools/check_droid_parts_drift.py, which names it
// as the consumer that keeps the catalog's `light` Kind honest. It is staged
// for C1a (#347), the first surface that draws a Part row - which is what the
// rules below are written for. Said out loud because a module nothing loads
// reads as shipped otherwise, and this project has shipped one before. Delete
// this paragraph when #347 wires the first page to it.
//
// It is ADVISORY, and it never refuses anything. Plenty of builds move
// something the reference drawing shows as a display, so a servo Output mapped
// onto a lit panel saves exactly as any other mapping does; `lightOn()` is
// there so a surface can SAY what that panel usually carries, which is a
// question, not a veto (ADR 0045).
// =============================================================================
(() => {
  // The Part Kind tokens, spelled as docs/droid-parts.yaml declares them. A
  // Part with no `kind` is one the catalog does not classify - the escape-hatch
  // slots are whatever the builder wired to them - which is a different fact
  // from being classified as something that moves.
  const LIGHT = "light";

  // What a surface may offer for a Part, by Kind. These are names of
  // affordances rather than field names: the row decides how to draw one, and
  // this decides whether it exists at all.
  //
  //   travel    the ends the Part moves between, and where it is within them
  //   throw     how long a full travel takes
  //   position  the commanded position right now
  //   release   Output Release - how long the Output holds after arrival
  //   brightness  "how far" as a light hears it (CONTEXT.md, Part Action)
  const SERVO_AFFORDANCES = Object.freeze(["travel", "throw", "position", "release"]);
  const LIGHT_AFFORDANCES = Object.freeze(["brightness"]);

  // The state class a light wears. The rules for it belong beside the first
  // surface that draws a Part row (#347), in the token layer #341 landed - this
  // module decides WHICH treatment a Part gets, never what it looks like.
  const LIGHT_CLASS = "partkind-light";

  /** The Kind the catalog declares for this Part, or null where it declares none. */
  const kindOf = (part) => {
    const kind = part && part.kind;
    return typeof kind === "string" && kind !== "" ? kind : null;
  };

  const isLight = (part) => kindOf(part) === LIGHT;

  /**
   * The affordances this Part's Kind can promise, as a frozen list.
   *
   * A Part the catalog does not classify gets the moving set, because every
   * Output this droid has is a servo Output and a row has to draw something.
   * That default is the honest one and it is deliberately not a Kind: nothing
   * here ever reports an unclassified Part AS a servo Part.
   */
  const affordances = (part) => (isLight(part) ? LIGHT_AFFORDANCES : SERVO_AFFORDANCES);

  /** Whether a surface may show one named affordance for this Part. */
  const shows = (part, affordance) => affordances(part).indexOf(affordance) !== -1;

  /** The state class for this Part's Kind - never a colour, and "" for no Kind. */
  const treatmentClass = (part) => (isLight(part) ? LIGHT_CLASS : "");

  /**
   * The light Part a given Part carries, or null.
   *
   * `sitsOn` is the declared link, so this asks the catalog rather than
   * matching on a name. It is what lets a surface about to record a servo
   * Output against P5 say "P5 carries the Magic Panel on most builds" - and
   * then save it anyway, because the Kind advises and never refuses.
   */
  const lightOn = (partId, parts) => {
    if (typeof partId !== "string" || partId === "" || !Array.isArray(parts)) {
      return null;
    }
    for (const part of parts) {
      if (part && part.sitsOn === partId && isLight(part)) {
        return part;
      }
    }
    return null;
  };

  const api = Object.freeze({
    LIGHT,
    LIGHT_CLASS,
    kindOf,
    isLight,
    affordances,
    shows,
    treatmentClass,
    lightOn,
  });

  if (typeof window !== "undefined") {
    window.DroidPartKind = api;
  }

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})();

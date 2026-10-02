// =============================================================================
// data/seq_gesture.js
//
// A Gesture in the browser (ADR 0046, #438): one authored move spread across a
// set of Parts. This is the MIRROR of include/sequence_gesture.h, which is the
// one home of the rules: the vocabulary, the order round the droid, the
// spread's timing and the dome's `$` table below say the same as the header's,
// table for table. A change there is a change here.
//
// The firmware resolves and performs a Gesture; the browser only needs to read
// one - to pick one in the editor, to draw it on the timeline, and for the
// Rehearsal to say what the dome will make of it. Nothing here sends anything.
// =============================================================================

(() => {
  "use strict";

  // The spread (SeqGestureSpread): the id is stored, and the label says the
  // timing rule in beats, so the picker needs no help text.
  const SPREADS = Object.freeze([
    { id: "together", label: "Together: all on the beat" },
    { id: "wave", label: "Wave: one per beat, each stays" },
    { id: "chase", label: "Chase: one per beat, the last goes back" },
    { id: "alternate", label: "Alternate: all on one beat, all back on the next" },
    { id: "pulse", label: "Pulse: on the beat, back on the half-beat" },
  ]);
  const SHAPES = Object.freeze(["open", "close", "flutter"]);
  const DIRECTIONS = Object.freeze([
    { id: "cw", label: "Clockwise" },
    { id: "ccw", label: "Counter-clockwise" },
  ]);
  // Where the order starts, as a bearing: 0 dead astern, 180 dead ahead,
  // clockwise from above (DROID_BEARING_DEAD_AHEAD_TENTHS), so 90 is the
  // droid's left and 270 its right.
  const STARTS = Object.freeze([
    { id: "front", label: "Front", bearing: 180 },
    { id: "right", label: "Right", bearing: 270 },
    { id: "rear", label: "Rear", bearing: 0 },
    { id: "left", label: "Left", bearing: 90 },
  ]);
  // The Motion Profile's own easing words; absent is the Output's own.
  const EASINGS = Object.freeze(["none", "soft", "overshoot"]);

  // Bounds (protocol_check.h PC_GESTURE_*). Protocol Check's mirror has them
  // (data/seq_protocol_check.js GESTURE_MS), and they are read from it rather
  // than kept a second time, so this file loads after that one: every page
  // that loads it does (seq.html's data-scripts).
  const { STEP_MS, SPEED_MS, REPEAT_MS, EXTENT_MS_MAX } = window.SeqProtocolCheck.GESTURE_MS;
  // The pace a Gesture takes with no tempo to lend it a beat
  // (SEQ_GESTURE_STEP_DEFAULT_MS).
  const STEP_DEFAULT_MS = 500;
  const MEMBERS_MAX = 24;

  const catalog = () => window.DroidParts || { parts: [], sets: [] };
  const setOf = (id) => (catalog().sets || []).find((s) => s.id === id) || null;
  const partOf = (id) => (catalog().parts || []).find((p) => p.id === id) || null;

  // The Parts a Gesture names, in the order it names them: a set's members, or
  // the listed ids. Unknown ids are kept out of the order and never dropped
  // from what is stored.
  const namedParts = (def) => {
    if (typeof def?.set === "string") return (setOf(def.set)?.members || []).slice();
    return Array.isArray(def?.parts) ? def.parts.filter((id) => partOf(id)) : [];
  };

  // Whether the dome performs it: its set's half, or the first listed Part's.
  const onDome = (def) => {
    if (typeof def?.set === "string") return setOf(def.set)?.half === "dome";
    const first = Array.isArray(def?.parts) ? partOf(def.parts[0]) : null;
    return first?.half === "dome";
  };

  // The members in the Gesture's order: clockwise (or counter) distance from
  // the start's bearing; a Part with no bearing goes last, in catalog order
  // (seqGestureMembers()). Tenths, as the firmware counts them.
  const members = (def) => {
    const start = (STARTS.find((s) => s.id === def?.start) || STARTS[0]).bearing * 10;
    const ccw = def?.direction === "ccw";
    const key = (id) => {
      const part = partOf(id);
      if (!part || typeof part.bearingDeg !== "number") return 100000 + (part ? part.index : 0);
      const cw = (Math.round(part.bearingDeg * 10) - start + 3600) % 3600;
      return ccw ? (3600 - cw) % 3600 : cw;
    };
    return namedParts(def)
      .map((id, order) => ({ id, order, k: key(id) }))
      .sort((a, b) => a.k - b.k || a.order - b.order)
      .slice(0, MEMBERS_MAX)
      .map((m) => m.id);
  };

  const spreadOf = (def) => (SPREADS.some((s) => s.id === def?.spread) ? def.spread : "together");
  const shapeOf = (def) => (SHAPES.includes(def?.shape) ? def.shape : "open");
  const undoOf = (shape) => (shape === "close" ? "open" : "close");

  // One pass, as moves in time order (seqGesturePassMove()).
  const passMoves = (spread, n, stepMs) => {
    const out = [];
    if (spread === "wave") {
      for (let k = 0; k < n; k++) out.push({ member: k, atMs: k * stepMs, undo: false });
    } else if (spread === "chase") {
      for (let j = 0; j < n; j++) {
        if (j > 0) out.push({ member: j - 1, atMs: j * stepMs, undo: true });
        out.push({ member: j, atMs: j * stepMs, undo: false });
      }
      if (n > 0) out.push({ member: n - 1, atMs: n * stepMs, undo: true });
    } else if (spread === "alternate" || spread === "pulse") {
      const back = spread === "pulse" ? Math.floor(stepMs / 2) : stepMs;
      for (let k = 0; k < n; k++) out.push({ member: k, atMs: 0, undo: false });
      for (let k = 0; k < n; k++) out.push({ member: k, atMs: back, undo: true });
    } else {
      for (let k = 0; k < n; k++) out.push({ member: k, atMs: 0, undo: false });
    }
    return out;
  };

  // Passes the Gesture makes (seqGesturePasses()).
  const passes = (def) => {
    const repeat = Number(def?.repeatMs) || 0;
    const extent = Number(def?.extentMs) || 0;
    return repeat > 0 && extent > 0 ? Math.ceil(extent / repeat) : 1;
  };

  // Every move of a BODY Gesture fired at `t`, as the Coordinator would ask for
  // it before pacing: {t, part, shape}. The Coordinator then holds them at
  // least the Cadence Floor apart, so a real move can land later than this,
  // never earlier. A dome Gesture is the dome's and has no moves here.
  const bodyMoves = (def, t) => {
    if (onDome(def)) return [];
    const ids = members(def);
    const spread = spreadOf(def);
    const shape = shapeOf(def);
    const stepMs = Number(def?.stepMs) || STEP_DEFAULT_MS;
    const repeat = Number(def?.repeatMs) || 0;
    const out = [];
    for (let p = 0; p < passes(def); p++) {
      passMoves(spread, ids.length, stepMs).forEach((m) => {
        out.push({ t: t + p * repeat + m.atMs, part: ids[m.member], shape: m.undo ? undoOf(shape) : shape });
      });
    }
    return out.sort((a, b) => a.t - b.t);
  };

  // The dome's side (seqGestureDomePrefix() and the address table). Which
  // `$` command a pair becomes, and what the dome does with it.
  const DOME_ADDRESSED = new Set([
    "panel1", "panel2", "panel3", "panel4", "panel7", "panel11", "panel13",
    "pie1", "pie2", "pie3", "pie4", "pie5", "pie6",
  ]);
  // Whether the dome has an address of its own for a Part
  // (seqGestureDomeBit()). One it has none for - ring panels 5, 6, 8, 9, 10,
  // 12 and 14 - is in no `$` command and stays where it is.
  const domeAddressed = (id) => DOME_ADDRESSED.has(id);
  const domeCommand = (shape, spread) => {
    if (spread === "together") return shape === "close" ? ":CL$" : shape === "flutter" ? ":OF$" : ":OP$";
    if (shape !== "open") return null;
    return { wave: ":OWC$", chase: ":OW$", alternate: ":OC$", pulse: ":OCR$" }[spread] || null;
  };

  // What the connected dome makes of a dome Gesture, in the Rehearsal's
  // words, or null when it performs it as written. The body never breaks a
  // dome Gesture into single panel commands (ADR 0046), so a pair with no
  // command moves nothing.
  const domeReading = (def) => {
    if (!onDome(def)) return null;
    const shape = shapeOf(def);
    const spread = spreadOf(def);
    const unaddressed = members(def).filter((id) => !domeAddressed(id));
    const notes = [];
    if (!domeCommand(shape, spread)) {
      return { performs: false, notes: [`The dome has no ${shape} ${spread}, so it does nothing.`] };
    }
    if (spread === "wave") notes.push("The dome waves them open, then closes them again.");
    if (spread === "alternate") notes.push("The dome opens them all, then closes them all.");
    if (spread === "pulse") notes.push("The dome opens and closes them quickly, on its own count.");
    if (spread === "wave" || spread === "chase") notes.push("It goes round in its own order, not the one set here.");
    if (Number(def?.howFar) > 0 && Number(def.howFar) < 100) notes.push("The panels travel all the way.");
    if (def?.speedMs !== undefined || def?.easing !== undefined) notes.push("The dome sets its own speed.");
    if (unaddressed.length) notes.push(`${unaddressed.length} of them the dome cannot move.`);
    return notes.length ? { performs: true, notes } : null;
  };

  window.SeqGesture = Object.freeze({
    SPREADS,
    SHAPES,
    DIRECTIONS,
    STARTS,
    EASINGS,
    STEP_MS,
    SPEED_MS,
    REPEAT_MS,
    EXTENT_MS_MAX,
    STEP_DEFAULT_MS,
    MEMBERS_MAX,
    setOf,
    partOf,
    namedParts,
    onDome,
    members,
    bodyMoves,
    domeAddressed,
    domeCommand,
    domeReading,
  });
})();

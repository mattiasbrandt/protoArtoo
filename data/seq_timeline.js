// =============================================================================
// data/seq_timeline.js
//
// A sequence read as time (#440, ADR 0062, ADR 0057): a lane per Part, each
// move drawn for as long as it takes, and what the routine leaves open drawn
// on to the end. A marker moves over it and the droid picture beside it shows
// that moment.
//
// IT NEVER REACHES THE DROID. There is no PAApi call in this file, and moving
// the marker only repaints the picture and the readout. The drawing follows
// the marker freely and silently; nothing follows a dragging finger (ADR
// 0062). The droid moves only on the separate press beside the marker, and
// even that is handed to the caller (`onPose`), which sends the one request
// -- a name and an instant -- and the firmware works out and paces the pose
// itself (POST /api/seq/pose, include/sequence_pose.h).
//
// IT EDITS ONLY WHEN IT IS HANDED `edit` (#441, ADR 0057). Without it the view
// is read-only, as a Factory sequence is shown. With it a block is moved by
// dragging its body and made longer or shorter by dragging an edge, on the
// sequence the caller is editing: the view reads that object through a getter
// and writes step times into it, never into a copy, and every drawing is
// rebuilt from it by build(), so the picture cannot drift from the routine.
// What it writes is a step's time and its own duration. Which steps there are
// and what order they are stored in is the caller's (`edit.commit`,
// `edit.remove`), and so is the history. Only the routine as written is
// edited: a loop's later passes and the Expanded reading are read-only.
//
// ONE READING OF THE ROUTINE. The steps are expanded by the Rehearsal's own
// expand() and a body move is resolved by its bodyMove() (data/seq_rehearsal.js),
// so the timeline and the rules that judge a routine read it the same way. A
// body move is timed by the generated planner (data/servo_motion.js), the same
// arithmetic ServoTask runs. A dome panel's travel is the dome's to know -- the
// Rehearsal's dome-timing Gap -- so a panel move is drawn as the instant it is
// sent and never as though its time were measured.
//
// NO BEAT GRID YET. A grid comes only from a tempo stored on the sequence
// (ADR 0058: the optional `tempo` block at format 1, include/seq_tempo.h),
// and it is not drawn yet (#441). What this view does read is where the droid
// runs each step: a step placed on a beat is drawn at the millisecond its beat
// resolves to (data/seq_protocol_check.js resolveBeats()), the same resolution
// the firmware makes at parse. The view never infers a tempo from step spacing
// and never holds one of its own (ADR 0062). A drag sets a millisecond, so a
// step dragged off its beat leaves it, as a time typed over a beat does.
//
// WHAT THE END DOES is the engine's, not a guess at it (src/tasks/
// sequence_engine.cpp beginFinish()): ring panels the run left open close one
// at a time, 500 ms apart, after the end step; a pie and a body Part stay
// where the last step left them; a toggle's open half closes nothing, because
// it is meant to stay open. Light modes are reset at the end. The group close
// :CL00 is never sent.
// =============================================================================
(() => {
  "use strict";

  // The engine's spacing between the ring closes it sends after the end step
  // (kRingCloseSpacingMs, src/tasks/sequence_engine.cpp).
  const RING_CLOSE_SPACING_MS = 500;
  // Air after the last thing drawn, so the end and what follows it are not
  // flush against the edge.
  const TAIL_MS = 1000;
  // Keyboard steps for the marker.
  const KEY_STEP_MS = 100;
  const KEY_BIG_STEP_MS = 1000;
  const PX_PER_SECOND = 56;
  // How close a dragged edge must come to time 0 or to another block's start
  // or end to land on it. A time, not a pixel count (ADR 0060): the same drag
  // lands the same way however long the routine on screen is.
  const SNAP_MS = 100;
  // How far in from a block's end a press takes the edge rather than the body.
  // The grip the stylesheet draws reads the same number (--tl-grip), and a
  // block too narrow for two grips and a body gives each a third.
  const EDGE_PX = 8;
  // What a drag will not go past. Protocol Check has the rules; these only
  // keep a block on the timeline and wide enough to take hold of again.
  const STEP_T_MAX_MS = 120000;
  const MIN_LENGTH_MS = 50;
  // The arrow keys move the selected blocks this far; Shift, the bigger step.
  const NUDGE_MS = 10;
  const NUDGE_BIG_MS = 100;

  // Logic and PSI targets that name more than one light Part, as the dome
  // command grammar spells them (kDlTargets, src/protocol_check.cpp). A single
  // target (FLD, RLD, FPSI, RPSI) is the alias the catalog carries on the
  // light Part itself (docs/droid-parts.yaml).
  const LIGHT_GROUPS = Object.freeze({
    LOGIC: ["FLD", "RLD"],
    PSI: ["FPSI", "RPSI"],
    ALL: ["FLD", "RLD", "FPSI", "RPSI"],
  });

  const esc = (value) =>
    String(value ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");

  const seconds = (ms) => `${(ms / 1000).toFixed(2)} s`;

  // ---------------------------------------------------------------------------
  // The catalog: every lane is a Part from data/droid_parts.js.
  // ---------------------------------------------------------------------------
  const parts = () => (window.DroidParts && Array.isArray(window.DroidParts.parts) ? window.DroidParts.parts : []);
  const partById = (id) => parts().find((part) => part.id === id) || null;
  const partByShorthand = (shorthand) => parts().find((part) => part.shorthand === shorthand) || null;

  // The Parts a panel command or a random set moves, from the body's own
  // command map (data/dome_command_map.js). A group target moves every member.
  const commandTargets = () => window.DomeCommandMap?.PANEL_COMMAND_TARGETS || { ring: {}, pie: {} };
  const setShorthands = (set) => {
    const targets = commandTargets();
    const ring = Object.keys(targets.ring || {});
    const pie = Object.keys(targets.pie || {});
    return set === "ring" ? ring : set === "pie" ? pie : [...ring, ...pie];
  };
  const idsOf = (shorthands) => shorthands.map(partByShorthand).filter(Boolean).map((part) => part.id);
  const isRingPart = (id) => {
    const part = partById(id);
    return Boolean(part && Object.hasOwn(commandTargets().ring || {}, part.shorthand));
  };

  const panelCommand = (cmd) => {
    const decoded = window.DomeCommandMap?.decodeCommandToElement?.(cmd);
    if (!decoded) return null;
    const shorthands = decoded.kind === "group" ? setShorthands(decoded.id) : [decoded.id];
    return { word: decoded.capability, ids: idsOf(shorthands) };
  };

  // DL: and DT: name a logic or PSI light by the target token.
  const lightCommand = (cmd) => {
    const match = /^D([LT]):([A-Z]+)/.exec(String(cmd || ""));
    if (!match) return null;
    const aliases = LIGHT_GROUPS[match[2]] || [match[2]];
    const ids = aliases
      .map((alias) => parts().find((part) => part.kind === "light" && (part.aliases || []).includes(alias)))
      .filter(Boolean)
      .map((part) => part.id);
    if (ids.length === 0) return null;
    // DL:target:mode[:color[:duration]] and DT:target:color:duration:speed:text
    // both carry a duration in whole seconds; 0 or absent is "until changed".
    const fields = String(cmd).split(":");
    const durationSec = Number(match[1] === "L" ? fields[4] : fields[3]) || 0;
    return { ids, durationMs: durationSec * 1000 };
  };

  // ---------------------------------------------------------------------------
  // build() -- the whole drawing, as data. Pure: the sequence and the facts the
  // droid reported go in, lanes and items come out.
  //
  // An item is {kind, t0, t1, label, ghost}:
  //   move     a body Part travelling, for the planner's time
  //   open     a Part standing open between an open and a later close
  //   left     still open when this finishes -- to the right edge
  //   flutter  a flutter; a dome one has no known length
  //   maybe    a random step that may land on this Part: the pick is not made
  //            until the droid runs it
  //   light    a light mode, until it is changed, its time runs out or the
  //            end resets it
  //   span     anything else with a length: a dome visual, a dome turn, a loop
  //   tick     one command at its instant: every step draws at least this
  // `ghost` marks an item from a loop's second pass or later, which the
  // as-written reading draws faintly.
  //
  // An item drawn from steps the builder wrote also says which, so a drag can
  // find its way back to them (#441):
  //   steps    the indices of the steps a drag of the body moves in time
  //   l, r     what dragging that edge changes, as {step, field}: field "t" is
  //            that step's time (a Part standing open ends at its close step),
  //            any other field is the step's own duration
  // An item with no `steps` is derived and is not draggable: a later pass of a
  // loop, a move a Gesture becomes, what the droid does after the end.
  //
  // A lane also carries `changes`: [{t, at}] for every moment the routine
  // commands the Part to a position (at 0 closed, 1 fully open), which is what
  // poseAt() reads. A Part the routine has not yet moved has no change before
  // that moment, so it is not in the pose.
  // ---------------------------------------------------------------------------
  const build = (seq, context = {}) => {
    const rehearsal = window.SeqRehearsal;
    const motion = window.ServoMotion;
    const run = window.SeqProtocolCheck?.resolveBeats ? window.SeqProtocolCheck.resolveBeats(seq) : seq;
    const steps = Array.isArray(run?.steps) ? run.steps : [];
    const endIndex = steps.findIndex((step) => step && step.type === "end");
    // The engine stops at the end step, so a step written after it never runs.
    const events = (rehearsal ? rehearsal.expand(steps) : []).filter(
      (event) => endIndex === -1 || event.step <= endIndex,
    );
    const endMs = endIndex !== -1 ? Number(steps[endIndex].t) || 0 : events.reduce((max, e) => Math.max(max, e.t), 0);
    // Only a toggle's open half leaves its ring panels open on purpose: the
    // engine runs the open half when the group is closed, and it is the half
    // this view draws (ADR 0062 rejects drawing the close half beside it).
    const toggleOpenHalf = Boolean(seq?.toggleGroup && seq.toggleGroup !== "none" &&
      Array.isArray(seq.closeSteps) && seq.closeSteps.length > 0);

    const lanes = new Map();
    const partLane = (id) => {
      if (!lanes.has(id)) {
        const part = partById(id);
        lanes.set(id, {
          key: id,
          part: id,
          half: part ? part.half || "body" : "body",
          index: part ? part.index : Infinity,
          name: part ? part.name : id,
          short: part && part.shorthand ? part.shorthand : "",
          items: [],
          changes: [],
          state: { open: false, since: 0, sinceGhost: false, sinceStep: null, fromUs: null },
        });
      }
      return lanes.get(id);
    };
    const rows = new Map();
    const rowLane = (key, name) => {
      if (!rows.has(key)) rows.set(key, { key, name, items: [] });
      return rows.get(key);
    };
    const add = (lane, item) => {
      lane.items.push(item);
      return item;
    };

    // The step an event was written as, or null when nobody wrote it there: a
    // loop's later pass, or a move a Gesture becomes.
    const written = (event) => (event.generated || (event.iter || 0) > 0 ? null : event.step);
    const drawnFrom = (step) => (step === null ? {} : { steps: [step] });
    const lasts = (step, field) =>
      step === null ? {} : { steps: [step], l: { step, field }, r: { step, field } };

    // A dome panel or a body Part told to open, close or flutter. `step` is
    // the written step that did it, or null.
    const openFrom = (lane, t, ghost, step) => {
      if (!lane.state.open) {
        lane.state.open = true;
        lane.state.since = t;
        lane.state.sinceGhost = ghost;
        lane.state.sinceStep = step;
      }
    };
    // A Part standing open is drawn from two steps: its start is the step that
    // opened it and its end the step that closed it. Dragging the body moves
    // both; with no written close (the droid closes it after the end) only the
    // start can be taken hold of.
    const standing = (opened, closed) => {
      if (opened === null || opened === undefined) return {};
      const l = { step: opened, field: "t" };
      return closed === null ? { steps: [opened], l } : { steps: [...new Set([opened, closed])], l, r: { step: closed, field: "t" } };
    };
    const closeAt = (lane, t, step) => {
      if (lane.state.open) {
        add(lane, { kind: "open", t0: lane.state.since, t1: t, ghost: lane.state.sinceGhost, ...standing(lane.state.sinceStep, step) });
        lane.state.open = false;
      }
    };

    // The pick a random step made, for a hold step to attach to: its set.
    let lastRandom = null;
    const light = new Map(); // light Part id -> its current light item
    const lightEnd = (id, t) => {
      const item = light.get(id);
      if (item && item.t1 === null) item.t1 = t;
      light.delete(id);
    };
    const domeVisual = { item: null };

    events.forEach((event) => {
      const def = event.def || {};
      const t = event.t;
      const ghost = (event.iter || 0) > 0;
      const label = context.describe ? context.describe(def) : def.type;
      const step = written(event);
      switch (def.type) {
        case "dome": {
          const panel = panelCommand(def.cmd);
          if (panel) {
            panel.ids.forEach((id) => {
              const lane = partLane(id);
              add(lane, { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
              // How far, where the step says it: an open that far from
              // closed, a close that far from open (sequence_dome_how_far.h).
              const part = Number(def.howFar) > 0 ? Math.min(100, Number(def.howFar)) / 100 : 1;
              if (panel.word === "open") {
                openFrom(lane, t, ghost, step);
                lane.changes.push({ t, at: part });
              } else if (panel.word === "close" && part < 1) {
                openFrom(lane, t, ghost, step);
                lane.changes.push({ t, at: 1 - part });
              } else if (panel.word === "close") {
                closeAt(lane, t, step);
                lane.changes.push({ t, at: 0 });
              } else {
                // A dome flutter has no length the body knows, and leaves
                // the panel where the dome leaves it.
                add(lane, { kind: "flutter", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
              }
            });
            return;
          }
          const lit = lightCommand(def.cmd);
          if (lit) {
            lit.ids.forEach((id) => {
              lightEnd(id, t);
              const item = add(partLane(id), {
                kind: "light", t0: t, t1: lit.durationMs > 0 ? t + lit.durationMs : null, label, ghost, ...drawnFrom(step),
              });
              light.set(id, item);
            });
            return;
          }
          // A dome visual preset holds until the next one or the end resets it.
          const row = rowLane("dome", "Dome");
          if (/^DV:/.test(String(def.cmd || ""))) {
            if (domeVisual.item && domeVisual.item.t1 === null) domeVisual.item.t1 = t;
            domeVisual.item = add(row, { kind: "span", t0: t, t1: null, label, ghost, ...drawnFrom(step) });
            return;
          }
          add(row, { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
          return;
        }
        case "random": {
          const set = def.set === "hold" ? lastRandom : def.set || "ring";
          if (def.set !== "hold") lastRandom = set;
          // A hold reuses the pick before it, so it is drawn on that step's
          // lanes and says so; a pick of its own says what it does.
          const said = def.set === "hold" ? `${label} (same pick)` : label;
          const reach = (Number(def.jitterMs) || 0) + (Number(def.moveMs) || 0);
          idsOf(setShorthands(set || "ring")).forEach((id) => {
            add(partLane(id), { kind: "maybe", t0: t, t1: t + reach, label: said, ghost, ...drawnFrom(step) });
          });
          return;
        }
        case "body": {
          if (!def.part) return;
          const lane = partLane(def.part);
          const move = rehearsal ? rehearsal.bodyMove(def, context) : { shape: def.shape || "open" };
          const shape = move.shape || "open";
          // Where the move starts is where the last one left the Part; the
          // first move of a routine starts from the end its shape points away
          // from (seq_rehearsal.js retargetBeforeArrival() reads it the same
          // way). A Part on no calibrated servo Output has no travel the
          // planner can time: on no Output it has none to draw, and an Output
          // nobody has measured jumps (servoMotionPlan() snaps).
          let travel = 0;
          if (move.timed && motion) {
            const from = lane.state.fromUs ?? (shape === "close" ? move.output.openUs : move.output.closeUs);
            travel = motion.servoMotionArrivalMs(from, move.targetUs, move.profile);
            lane.state.fromUs = move.targetUs;
          }
          if (shape === "flutter") {
            const flutterMs = Number(def.flutterMs) || 0;
            add(lane, { kind: "flutter", t0: t, t1: t + flutterMs, label, ghost, ...lasts(step, "flutterMs") });
            openFrom(lane, t + flutterMs, ghost, step);
          } else {
            // How long the Part takes to get there is the Output's, not the
            // step's (ADR 0052), so a move is dragged and never resized.
            if (travel > 0) add(lane, { kind: "move", t0: t, t1: t + travel, label, ghost, ...drawnFrom(step) });
            else add(lane, { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
            if (shape === "close") closeAt(lane, t, step);
            else openFrom(lane, t + travel, ghost, step);
          }
          const howFar = Number(def.howFar) || 100;
          lane.changes.push({ t, at: shape === "close" ? 0 : Math.min(100, Math.max(5, howFar)) / 100 });
          return;
        }
        case "audio":
        case "audioCat":
          add(rowLane("sound", "Sound"), { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
          return;
        case "domeRotate": {
          const durationMs = Number(def.durationMs) || 0;
          add(rowLane("spin", "Dome turn"), durationMs > 0
            ? { kind: "span", t0: t, t1: t + durationMs, label, ghost, ...lasts(step, "durationMs") }
            : { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
          return;
        }
        case "gesture":
          // Where the Gesture was fired, which is the one thing of it a drag
          // takes hold of. The moves it becomes are drawn on their own Parts'
          // lanes from the same expansion the Rehearsal reads
          // (seq_rehearsal.js expand()); drawing it as one block across those
          // lanes comes with Gesture authoring (#441).
          add(rowLane("gesture", "Gesture"), { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
          return;
        case "sequence":
          // A sequence inside this one, where it starts. What it does is read
          // when it runs; drawing it as one linked block comes with nesting
          // on the timeline (#441).
          add(rowLane("phrase", "Sequence"), { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
          return;
        case "end":
          return;
        default:
          // A step kind this view does not know yet still draws, as a labelled
          // mark at its time. It is never dropped.
          add(rowLane("other", "Other"), { kind: "tick", t0: t, t1: t, label: def.type || label, ghost, ...drawnFrom(step) });
      }
    });

    // The end: what the engine does after the end step, in its order.
    let ringCloses = 0;
    let cleanupEnd = endMs;
    const partLanes = [...lanes.values()].sort((a, b) =>
      (a.half === "dome" ? 0 : 1) - (b.half === "dome" ? 0 : 1) || a.index - b.index);
    // kRingPanels order is catalog order (panel1, 2, 3, 4, 7, 11, 13).
    partLanes.forEach((lane) => {
      if (!lane.state.open || toggleOpenHalf || !isRingPart(lane.part)) return;
      ringCloses += 1;
      const at = endMs + RING_CLOSE_SPACING_MS * ringCloses;
      closeAt(lane, at, null);
      add(lane, { kind: "tick", t0: at, t1: at, label: "Closed by the droid after the end", ghost: false });
      lane.changes.push({ t: at, at: 0 });
      cleanupEnd = Math.max(cleanupEnd, at);
    });
    light.forEach((_item, id) => lightEnd(id, endMs));
    lanes.forEach((lane) => lane.items.forEach((item) => {
      if (item.kind === "light" && item.t1 > endMs) item.t1 = endMs;
    }));
    if (domeVisual.item && domeVisual.item.t1 === null) domeVisual.item.t1 = endMs;

    const lastItem = [...lanes.values(), ...rows.values()]
      .reduce((max, lane) => lane.items.reduce((m, item) => Math.max(m, item.t1 ?? item.t0), max), 0);
    const windowMs = Math.max(cleanupEnd, lastItem, endMs) + TAIL_MS;

    // What is still open closes nowhere: a lighter run to the right edge.
    partLanes.forEach((lane) => {
      if (lane.state.open) {
        add(lane, { kind: "left", t0: lane.state.since, t1: windowMs, ghost: lane.state.sinceGhost, ...standing(lane.state.sinceStep, null) });
        lane.state.open = false;
      }
    });

    // Loops, as written: the header and how many passes it makes. The passes
    // are the same steps, drawn again, never stored (ADR 0057).
    const loops = [];
    steps.forEach((step, index) => {
      if (!step || step.type !== "loop" || (endIndex !== -1 && index > endIndex)) return;
      const period = Number(step.periodMs) || 0;
      const duration = Number(step.durationMs) || 0;
      const passes = period > 0 && duration > 0 ? Math.ceil(duration / period) : 0;
      if (passes === 0 || !(Number(step.body) > 0)) return;
      loops.push({ t: Number(step.t) || 0, period, passes, step: index, label: context.describe ? context.describe(step) : "loop" });
    });

    return {
      name: seq?.name || "",
      endMs,
      // The end step's index, or -1: the one step drawn as a line, not a block.
      end: endIndex,
      windowMs,
      loops,
      parts: partLanes,
      rows: ["sound", "dome", "spin", "gesture", "phrase", "other"].map((key) => rows.get(key)).filter(Boolean),
    };
  };

  // ---------------------------------------------------------------------------
  // poseAt() -- what the routine has commanded by instant t: every Part it has
  // moved, where the last step before t left it; every light mode running;
  // and the sound started last. A Part not yet moved by t is not in it.
  // ---------------------------------------------------------------------------
  const poseAt = (model, t) => {
    const at = {};
    model.parts.forEach((lane) => {
      const last = lane.changes.filter((change) => change.t <= t).pop();
      if (last) at[lane.part] = last.at;
    });
    const lights = [];
    model.parts.forEach((lane) => {
      const on = lane.items.find((item) => item.kind === "light" && item.t0 <= t && t < item.t1);
      if (on) lights.push({ part: lane.part, name: lane.name, label: on.label });
    });
    const sound = (model.rows.find((row) => row.key === "sound")?.items || [])
      .filter((item) => item.t0 <= t && t <= model.endMs)
      .pop() || null;
    return { at, lights, sound };
  };

  // ---------------------------------------------------------------------------
  // unwired() -- the Parts this routine names that nothing on this droid can
  // move: a body Part no Output claims, and a dome Part while the dome link is
  // switched off. Nothing is said about a half the droid has not reported.
  // ---------------------------------------------------------------------------
  const unwired = (model, context = {}) => {
    const outputs = Array.isArray(context.outputs) ? context.outputs : null;
    const domeOff = context.config?.components?.protoR2link?.enabled === false;
    return model.parts
      .filter((lane) => {
        if (lane.half === "dome") return domeOff;
        if (!outputs) return false;
        return !outputs.some((output) => Array.isArray(output.parts) && output.parts.includes(lane.part));
      })
      .map((lane) => lane.part);
  };

  // ---------------------------------------------------------------------------
  // The view
  // ---------------------------------------------------------------------------
  const pct = (ms, windowMs) => `${((ms / windowMs) * 100).toFixed(3)}%`;

  // `handle` is what the editing view adds to a block it can take hold of:
  // its place in the list of drawn items, and the classes that say whether it
  // moves, which edges resize it, and whether it is selected.
  const itemHtml = (item, windowMs, authored, handle = "") => {
    const t1 = item.t1 === null || item.t1 === undefined ? item.t0 : item.t1;
    const ghost = authored && item.ghost ? " is-ghost" : "";
    const title = item.label ? `${item.label}, ${seconds(item.t0)}` : seconds(item.t0);
    const width = item.kind === "tick" ? "" : `;width:${pct(Math.max(0, t1 - item.t0), windowMs)}`;
    const text = item.kind !== "tick" && item.label ? `<span class="tl-label">${esc(item.label)}</span>` : "";
    return `<span class="tl-item tl-${item.kind}${ghost}"${handle} style="left:${pct(item.t0, windowMs)}${width}" title="${esc(title)}">${text}</span>`;
  };

  const loopItems = (model, authored) => {
    const items = [];
    model.loops.forEach((loop) => {
      if (authored) {
        // The loop as the one object the builder wrote: it moves with the
        // steps it repeats, and its edges set how long it runs.
        items.push({
          kind: "span", t0: loop.t, t1: loop.t + loop.passes * loop.period, label: loop.label,
          steps: [loop.step], l: { step: loop.step, field: "durationMs" }, r: { step: loop.step, field: "durationMs" },
        });
        return;
      }
      for (let pass = 0; pass < loop.passes; pass += 1) {
        const t0 = loop.t + pass * loop.period;
        items.push({ kind: "span", t0, t1: t0 + loop.period, label: `Pass ${pass + 1}` });
      }
    });
    return items;
  };

  const rulerHtml = (windowMs) => {
    const span = windowMs / 1000;
    const step = span <= 6 ? 0.5 : span <= 20 ? 1 : span <= 60 ? 5 : 10;
    let html = "";
    for (let s = 0; s * 1000 <= windowMs; s += step) {
      html += `<span class="tl-tick-mark" style="left:${pct(s * 1000, windowMs)}">${Number(s.toFixed(1))} s</span>`;
    }
    return html;
  };

  const laneHtml = (lane, windowMs, authored, dim, handleOf = () => "") =>
    `<div class="tl-row${dim ? " is-unwired" : ""}" data-lane="${esc(lane.key)}">` +
    `<div class="tl-name">${lane.short ? `<span class="tl-short">${esc(lane.short)}</span>` : ""}` +
    `<span class="tl-part">${esc(lane.name)}</span></div>` +
    `<div class="tl-track">${lane.items.map((item) => itemHtml(item, windowMs, authored, handleOf(item))).join("")}</div>` +
    `</div>`;

  // ---------------------------------------------------------------------------
  // mount() -- draw a sequence as a timeline into `host`.
  //
  // `source` is the sequence, or a function that returns it. An editor passes
  // the function, so the view always reads the object being edited and never
  // keeps a copy that an undo or a tempo change would leave behind.
  //
  // options:
  //   context   what the droid reported, as the Rehearsal reads it: outputs,
  //             config. Absent parts of it make the view say less, never guess.
  //   describe  step -> words, the editor's own preview (data/seq.js), so a
  //             block and a step card name a step alike
  //   onPose    the builder pressed to send the droid to the marker's instant:
  //             called with it in ms, and returns a promise of {text, level}
  //             to show beside the press. Absent, there is no press.
  //   edit      the editor's half of an edit; with it the blocks can be taken
  //             hold of, and the view draws inside the editor's own card
  //             rather than as a card of its own:
  //               begin()         the copy of the routine a gesture starts from
  //               commit(before)  a gesture changed the routine: put the steps
  //                               in order, record it, and read it all again
  //               remove(indices) take these steps out of the routine
  //   cardsLabel, onCards, onClose   the read-only view's own way out: the
  //             words on the button back to the card view, and the two presses
  //
  // Returns {refresh(context), at(), dragging(), destroy()}.
  // ---------------------------------------------------------------------------
  const mount = (host, source, options = {}) => {
    const seqNow = typeof source === "function" ? source : () => source;
    const edit = options.edit || null;
    let context = { ...(options.context || {}), describe: options.describe };
    let model = build(seqNow(), context);
    let windowMs = model.windowMs;
    let authored = true;
    let t = 0;
    let ruler = null;
    let marker = null;

    // What the editing view holds between presses. `selection` is the steps
    // themselves rather than their indices, so it survives the editor putting
    // them in a new order and empties by itself when an undo replaces them.
    // `drawn` is every item on screen, in the order it was written, which is
    // how a press finds the item under it. `drag` is the gesture under way.
    const selection = new Set();
    let drawn = [];
    let drag = null;

    const bar =
      `<div class="tl-bar">` +
      `<div class="seg tl-loop-mode" role="group" aria-label="How a loop is drawn" hidden>` +
      `<button type="button" data-tl-loop="authored" aria-pressed="true">As written</button>` +
      `<button type="button" data-tl-loop="expanded" aria-pressed="false">Expanded</button></div>` +
      `<span class="tl-now" role="status" aria-live="polite"></span>` +
      `<span class="tl-acts">` +
      (typeof options.onPose === "function"
        ? `<button type="button" class="btn btn-sm accent" data-tl-act="pose">Move the droid to this moment</button>`
        : "") +
      (edit
        ? `<button type="button" class="seq-act" data-tl-act="remove" disabled>Remove</button>`
        : `<button type="button" class="seq-act" data-tl-act="cards">${esc(options.cardsLabel || "Edit steps")}</button>` +
          `<button type="button" class="seq-act" data-tl-act="close">Close</button>`) +
      `</span></div>`;
    host.innerHTML =
      (edit
        ? `<div class="tl-view">`
        : `<div class="card tl-view"><div class="sect"><h2>Timeline</h2><span class="sub">${esc(model.name)} &middot; ${esc(seconds(model.endMs))}</span></div>`) +
      bar +
      `<p class="hint tl-said" role="status" aria-live="polite" hidden></p>` +
      `<p class="note note-act tl-unwired" hidden></p>` +
      `<div class="tl-stage">` +
      `<div class="tl-scroll"><div class="tl-grid"${edit ? ` tabindex="0" role="group" aria-label="The routine's blocks"` : ""}></div></div>` +
      `<div class="tl-side"><div class="tl-picture"></div><dl class="tl-readout"></dl></div>` +
      `</div></div>`;

    const grid = host.querySelector(".tl-grid");
    const now = host.querySelector(".tl-now");
    const readout = host.querySelector(".tl-readout");
    const unwiredNote = host.querySelector(".tl-unwired");
    const pictureHost = host.querySelector(".tl-picture");
    const loopMode = host.querySelector(".tl-loop-mode");
    const removeButton = host.querySelector('[data-tl-act="remove"]');

    // The droid picture, in its "a moment of the routine" state (ADR 0063).
    // A click on it selects nothing here: this view has no acts on a Part.
    let drawing = null;
    let picture = null;
    if (window.BodyView && window.DroidParts) {
      drawing = window.BodyView.mountDrawing(pictureHost, {
        parts: window.DroidParts.parts,
        art: window.BodyArt,
        domeSvg: window.DOME_PANEL_MAP_SVG,
      });
      drawing.showFace("dome");
      picture = window.PADroidPicture ? window.PADroidPicture.caller(drawing) : null;
    } else {
      // Never swallowed: the timeline still draws, and says why the picture
      // is missing where a builder would look for it.
      console.error("[seq-timeline] /body_view.js did not load; no droid picture");
    }

    let dim = new Set();
    const paintUnwired = () => {
      const ids = unwired(model, context);
      dim = new Set(ids);
      const names = ids.map((id) => model.parts.find((lane) => lane.part === id)?.name || id);
      unwiredNote.hidden = names.length === 0;
      unwiredNote.textContent = names.length === 0 ? ""
        : `${names.length} ${names.length === 1 ? "part here is" : "parts here are"} not wired on this droid: ${names.join(", ")}.`;
    };

    // Only the routine as written is edited: the Expanded reading is the
    // read-only check of what the droid receives (ADR 0057).
    const editing = () => Boolean(edit) && authored;
    const selected = (item) => {
      const steps = seqNow().steps;
      return item.steps.every((index) => selection.has(steps[index]));
    };
    const handleOf = (item) => {
      if (!editing() || !item.steps) return "";
      drawn.push(item);
      const sized = item.kind === "tick" ? "" : `${item.l ? " can-size-l" : ""}${item.r ? " can-size-r" : ""}`;
      return ` data-item="${drawn.length - 1}" data-edit="can-move${sized}${selected(item) ? " is-selected" : ""}"`;
    };

    const paintLanes = () => {
      drawn = [];
      const hasLoop = model.loops.length > 0;
      loopMode.hidden = !hasLoop;
      const loopLane = hasLoop
        ? laneHtml({ key: "loop", name: "Loop", short: "", items: loopItems(model, authored) }, windowMs, false, false, handleOf)
        : "";
      const snap = drag && drag.snap
        ? `<div class="tl-snap" style="left:${pct(drag.snap.t, windowMs)}">` +
          (drag.snap.label ? `<span class="tl-snap-label">${esc(drag.snap.label)}</span>` : "") + `</div>`
        : "";
      // At least this many pixels a second, so a long routine scrolls rather
      // than crushing its blocks together.
      grid.setAttribute("style", `--tl-grip:${EDGE_PX}px;min-width:calc(var(--tl-lane-w) + ${Math.round((windowMs / 1000) * PX_PER_SECOND)}px)`);
      grid.innerHTML =
        `<div class="tl-row tl-ruler-row"><div class="tl-name">Time</div>` +
        `<div class="tl-track tl-ruler" role="slider" tabindex="0" aria-label="Moment" ` +
        `aria-valuemin="0" aria-valuemax="${Math.round(windowMs)}">${rulerHtml(windowMs)}</div></div>` +
        loopLane +
        model.parts.map((lane) => laneHtml(lane, windowMs, authored, dim.has(lane.part), handleOf)).join("") +
        model.rows.map((row) => laneHtml(row, windowMs, authored, false, handleOf)).join("") +
        `<div class="tl-overlay" aria-hidden="true">` +
        `<div class="tl-end"${editing() && model.end !== -1 ? ` data-edit="can-move"` : ""} style="left:${pct(model.endMs, windowMs)}"></div>` +
        snap +
        `<div class="tl-marker"></div></div>`;
      ruler = grid.querySelector(".tl-ruler");
      marker = grid.querySelector(".tl-marker");
      ruler.addEventListener("pointerdown", scrubStart);
      ruler.addEventListener("keydown", onKey);
      if (removeButton) {
        removeButton.disabled = !editing() || selection.size === 0;
        removeButton.textContent = selection.size > 1 ? `Remove ${selection.size} steps` : "Remove";
      }
    };

    const paintReadout = () => {
      const pose = poseAt(model, t);
      const open = model.parts
        .filter((lane) => (pose.at[lane.part] ?? 0) > 0)
        .map((lane) => lane.short || lane.name);
      const rowsHtml = [
        ["Open", open.length ? open.join(", ") : "none"],
        ...pose.lights.map((lit) => [lit.name, lit.label]),
        ...(pose.sound ? [["Sound", `${pose.sound.label}, from ${seconds(pose.sound.t0)}`]] : []),
      ];
      readout.innerHTML = rowsHtml.map(([term, value]) => `<dt>${esc(term)}</dt><dd>${esc(value)}</dd>`).join("");
      if (drawing) {
        const marks = {};
        drawing.markerIds().forEach((markerId) => {
          const moved = drawing.partsOf(markerId).filter((id) => id in pose.at);
          if (moved.length) marks[markerId] = { mark: window.BodyView.MARKS.OPENABLE, at: Math.max(...moved.map((id) => pose.at[id])) };
        });
        const onPicture = picture ? picture.pictureFor() : { shown: null, domePending: false, domeNote: "" };
        drawing.update({
          kind: window.BodyView.STATE_KINDS.POSE,
          said: `Showing: the routine at ${seconds(t)}`,
          marks,
          shown: onPicture.shown,
          fitted: picture ? picture.fittedNow() : null,
          domePending: onPicture.domePending,
          domeNote: onPicture.domeNote,
        });
      }
    };

    // The one setter. It moves the marker and repaints what shows the moment,
    // and it is the only thing any scrub, click or key calls. It sends nothing:
    // the droid moves only on a separate, deliberate press (ADR 0062).
    const setMarker = (ms) => {
      t = Math.max(0, Math.min(windowMs, Math.round(ms)));
      if (marker) marker.setAttribute("style", `left:${pct(t, windowMs)}`);
      if (ruler) {
        ruler.setAttribute("aria-valuenow", String(t));
        ruler.setAttribute("aria-valuetext", seconds(t));
      }
      now.textContent = seconds(t);
      paintReadout();
    };

    // The one door from the routine to the drawing: everything on screen is
    // built again from the sequence as it is now. A drag in progress keeps the
    // scale it started with, so a block does not slide under the pointer when
    // the routine it is stretching gets longer.
    const redraw = () => {
      model = build(seqNow(), context);
      windowMs = drag ? drag.windowMs : model.windowMs;
      const steps = new Set(seqNow().steps);
      [...selection].forEach((step) => {
        if (!steps.has(step)) selection.delete(step);
      });
      paintUnwired();
      paintLanes();
      setMarker(t);
    };

    const msFrom = (event) => {
      const rect = ruler.getBoundingClientRect();
      return rect.width > 0 ? ((event.clientX - rect.left) / rect.width) * windowMs : 0;
    };
    // A press on the ruler jumps; a drag scrubs. Move, up and cancel are bound
    // on the window so a fast drag that leaves the ruler is not stranded.
    const scrubMove = (event) => setMarker(msFrom(event));
    const scrubEnd = () => {
      window.removeEventListener("pointermove", scrubMove);
      window.removeEventListener("pointerup", scrubEnd);
      window.removeEventListener("pointercancel", scrubEnd);
    };
    function scrubStart(event) {
      if (typeof event.preventDefault === "function") event.preventDefault();
      ruler.focus();
      setMarker(msFrom(event));
      window.addEventListener("pointermove", scrubMove);
      window.addEventListener("pointerup", scrubEnd);
      window.addEventListener("pointercancel", scrubEnd);
    }
    function onKey(event) {
      const step = event.shiftKey ? KEY_BIG_STEP_MS : KEY_STEP_MS;
      const next = {
        ArrowRight: t + step, ArrowUp: t + step, ArrowLeft: t - step, ArrowDown: t - step,
        PageUp: t + KEY_BIG_STEP_MS, PageDown: t - KEY_BIG_STEP_MS, Home: 0, End: model.endMs,
      }[event.key];
      if (next === undefined) return;
      if (typeof event.preventDefault === "function") event.preventDefault();
      setMarker(next);
    }

    // -------------------------------------------------------------------------
    // Editing (#441). A gesture is a list of writes - one step's time or its
    // duration each - and one number, how far the pointer has gone in ms. The
    // writes are worked out once, at the press, so the gesture cannot change
    // its mind about what it is moving half way; every pointer move sets the
    // one number, writes the steps and draws the routine again.
    // -------------------------------------------------------------------------
    // Where a step's time may go: an outer step stays between the start and
    // the end step, a step a loop repeats stays inside one pass, and the end
    // step stays after everything before it.
    const timeRange = (steps, index) => {
      const end = steps.findIndex((step) => step && step.type === "end");
      const inLoop = new Map();
      for (let at = 0; at < steps.length; ) {
        const step = steps[at] || {};
        const body = step.type === "loop" && step.body > 0 ? Math.min(step.body, steps.length - at - 1) : 0;
        for (let k = 1; k <= body; k += 1) inLoop.set(at + k, Number(step.periodMs) || 0);
        at += body + 1;
      }
      if (index === end) {
        const before = steps.slice(0, end).filter((_, at) => !inLoop.has(at)).map((step) => Number(step?.t) || 0);
        return [Math.max(0, ...before), STEP_T_MAX_MS];
      }
      if (inLoop.has(index)) return [0, Math.max(0, inLoop.get(index) - 1)];
      return [0, end === -1 || index > end ? STEP_T_MAX_MS : Number(steps[end].t) || 0];
    };

    // One write: `sign` is +1 where the value follows the pointer and -1 where
    // it runs against it (the length of a block whose start is being dragged).
    const write = (steps, index, field, sign) => {
      const step = steps[index];
      const [min, max] = field === "t" ? timeRange(steps, index) : [MIN_LENGTH_MS, STEP_T_MAX_MS];
      return { step, field, sign, from: Number(step[field]) || 0, min, max, beat: step.beat, spanBeats: step.spanBeats };
    };

    // The writes for a press on `item`: its body (edge null) moves every
    // selected step, an edge moves what that edge is made of, and neither edge
    // may cross the other.
    const planFor = (item, edge) => {
      const steps = seqNow().steps;
      if (edge === null) {
        return { writes: steps.map((step, index) => (selection.has(step) ? write(steps, index, "t", 1) : null)).filter(Boolean), lo: -Infinity, hi: Infinity };
      }
      const length = (item.t1 ?? item.t0) - item.t0;
      const side = item[edge];
      if (side.field === "t") {
        const writes = [write(steps, side.step, "t", 1)];
        return edge === "l" ? { writes, lo: -Infinity, hi: length } : { writes, lo: -length, hi: Infinity };
      }
      return edge === "l"
        ? { writes: [write(steps, side.step, "t", 1), write(steps, side.step, side.field, -1)], lo: -Infinity, hi: Infinity }
        : { writes: [write(steps, side.step, side.field, 1)], lo: -Infinity, hi: Infinity };
    };

    // How far the plan can actually go: every write stays inside its range.
    const reach = (plan) => plan.writes.reduce(
      ([lo, hi], w) => (w.sign > 0
        ? [Math.max(lo, w.min - w.from), Math.min(hi, w.max - w.from)]
        : [Math.max(lo, w.from - w.max), Math.min(hi, w.from - w.min)]),
      [plan.lo, plan.hi],
    );

    // Write the plan at `by` ms. A step whose time changed has left its beat,
    // and one whose duration changed has left its span in beats, exactly as
    // typing a millisecond over either does (ADR 0058); brought back to where
    // it started, it has them again.
    const apply = (plan, by) => {
      plan.writes.forEach((w) => {
        w.step[w.field] = w.from + w.sign * by;
        const kept = w.field === "t" ? "beat" : "spanBeats";
        if (by !== 0) delete w.step[kept];
        else if (w[kept] !== undefined) w.step[kept] = w[kept];
      });
    };

    // Time 0, and the start and end of every block the gesture is not moving.
    // A block is named by its own words, or by its lane when it has none. A
    // loop's later passes are not landed on: they are the same steps again,
    // and the ones a gesture is moving would be a target that moves with it.
    const snapTargets = (plan) => {
      const steps = seqNow().steps;
      const moving = new Set(plan.writes.map((w) => w.step));
      const targets = [{ t: 0, label: "" }];
      [{ name: "Loop", items: loopItems(model, true) }, ...model.parts, ...model.rows].forEach((lane) =>
        lane.items.forEach((item) => {
          if (item.ghost || (item.steps && item.steps.some((index) => moving.has(steps[index])))) return;
          const label = item.label || lane.name;
          targets.push({ t: item.t0, label });
          if (item.kind !== "tick" && item.kind !== "left" && item.t1 !== null && item.t1 !== undefined) targets.push({ t: item.t1, label });
        }));
      return targets;
    };

    const dragMove = (event) => {
      let by = Math.round((event.clientX - drag.x0) * drag.msPerPx);
      // The nearest target within the tolerance, to whichever of the dragged
      // edges is nearest to one.
      let best = null;
      drag.edges.forEach((edgeT) => {
        drag.targets.forEach((target) => {
          const off = target.t - (edgeT + by);
          if (Math.abs(off) <= SNAP_MS && (best === null || Math.abs(off) < Math.abs(best.off))) best = { off, target };
        });
      });
      if (best) by += best.off;
      const [lo, hi] = drag.reach;
      const held = Math.max(lo, Math.min(hi, by));
      // A snap the limits would not let the block reach is not shown.
      drag.snap = best && held === by ? best.target : null;
      drag.by = held;
      apply(drag.plan, held);
      redraw();
    };

    const dragStop = () => {
      window.removeEventListener("pointermove", dragMove);
      window.removeEventListener("pointerup", dragEnd);
      window.removeEventListener("pointercancel", dragCancel);
      grid.setAttribute("data-dragging", "");
    };
    // The gesture ends where it is. One that moved nothing was a press that
    // only selected, and is no edit.
    function dragEnd() {
      if (!drag) return;
      const done = drag;
      drag = null;
      dragStop();
      // commit() comes back through refresh(), which draws the routine again.
      if (done.by !== 0) {
        edit.commit(done.before);
        return;
      }
      selection.clear();
      done.alone.forEach((step) => selection.add(step));
      redraw();
    }
    // A cancelled gesture puts every step back where the press found it. Its
    // writes went into the caller's own step objects, so they have to be taken
    // out again by whatever ends it short of a pointerup - Escape, a
    // pointercancel, or the view being taken down under it.
    const dragAbandon = () => {
      if (!drag) return false;
      const undone = drag;
      drag = null;
      dragStop();
      apply(undone.plan, 0);
      return true;
    };
    function dragCancel() {
      if (dragAbandon()) redraw();
    }

    const clearSelection = () => {
      if (selection.size === 0) return;
      selection.clear();
      paintLanes();
      setMarker(t);
    };

    function grab(event) {
      if (!editing() || drag || event.button > 0) return;
      const target = event.target && event.target.closest ? event.target : null;
      if (!target || target.closest(".tl-ruler")) return;
      const node = target.closest(".tl-item, .tl-end");
      const isEnd = Boolean(node) && node.classList.contains("tl-end");
      const steps = seqNow().steps;
      const item = isEnd
        ? (model.end === -1 ? null : { kind: "tick", t0: model.endMs, t1: model.endMs, steps: [model.end] })
        : node && node.dataset.item !== undefined ? drawn[Number(node.dataset.item)] : null;
      if (!item) {
        clearSelection();
        return;
      }
      if (typeof event.preventDefault === "function") event.preventDefault();
      if (typeof grid.focus === "function") grid.focus();

      // Shift or Ctrl adds the block to the selection, or takes it out, and
      // starts no drag. A plain press on a block outside the selection selects
      // it alone; on one inside it, the whole selection is taken hold of, and
      // if it is then let go without moving, that block is selected alone.
      const own = item.steps.map((index) => steps[index]);
      const held = own.every((step) => selection.has(step));
      if (!isEnd && (event.shiftKey || event.ctrlKey || event.metaKey)) {
        own.forEach((step) => (held ? selection.delete(step) : selection.add(step)));
        paintLanes();
        setMarker(t);
        return;
      }
      if (isEnd || !held) {
        selection.clear();
        if (!isEnd) own.forEach((step) => selection.add(step));
      }

      // Body or edge, decided once, here.
      const rect = node.getBoundingClientRect();
      const grip = Math.min(EDGE_PX, rect.width / 3);
      const edge = isEnd || item.kind === "tick" ? null
        : item.l && event.clientX - rect.left < grip ? "l"
        : item.r && rect.right - event.clientX < grip ? "r" : null;
      const track = ruler.getBoundingClientRect();
      const plan = isEnd ? { writes: [write(steps, model.end, "t", 1)], lo: -Infinity, hi: Infinity } : planFor(item, edge);
      const length = item.kind === "tick" ? 0 : (item.t1 ?? item.t0) - item.t0;
      drag = {
        before: edit.begin(),
        plan,
        reach: reach(plan),
        // The edges that can land on something: the one being dragged, or
        // both ends of a block being moved.
        edges: edge === "l" ? [item.t0] : edge === "r" ? [item.t0 + length] : length > 0 ? [item.t0, item.t0 + length] : [item.t0],
        targets: snapTargets(plan),
        x0: event.clientX,
        msPerPx: track.width > 0 ? windowMs / track.width : 0,
        windowMs,
        by: 0,
        snap: null,
        alone: isEnd ? [] : own,
      };
      grid.setAttribute("data-dragging", edge ? "edge" : "body");
      window.addEventListener("pointermove", dragMove);
      window.addEventListener("pointerup", dragEnd);
      window.addEventListener("pointercancel", dragCancel);
      paintLanes();
      setMarker(t);
    }

    const removeSelected = () => {
      const steps = seqNow().steps;
      const indices = steps.map((step, index) => (selection.has(step) ? index : -1)).filter((index) => index >= 0);
      if (indices.length === 0) return;
      selection.clear();
      edit.remove(indices);
    };

    // The keys, with the blocks in focus: the arrows move what is selected,
    // Delete removes it, Escape lets go of a drag or of the selection. The
    // ruler inside the grid keeps its own keys for the marker.
    function gridKey(event) {
      if (!editing()) return;
      if (event.target && event.target.closest && event.target.closest(".tl-ruler")) return;
      if (event.key === "Escape") {
        if (drag) dragCancel();
        else clearSelection();
        return;
      }
      if (drag || selection.size === 0) return;
      if (event.key === "Delete" || event.key === "Backspace") {
        if (typeof event.preventDefault === "function") event.preventDefault();
        removeSelected();
        return;
      }
      const way = { ArrowLeft: -1, ArrowRight: 1 }[event.key];
      if (way === undefined) return;
      if (typeof event.preventDefault === "function") event.preventDefault();
      const plan = planFor(null, null);
      const [lo, hi] = reach(plan);
      const by = Math.max(lo, Math.min(hi, way * (event.shiftKey ? NUDGE_BIG_MS : NUDGE_MS)));
      if (by === 0) return;
      const before = edit.begin();
      apply(plan, by);
      edit.commit(before);
    }

    if (edit) {
      grid.addEventListener("pointerdown", grab);
      grid.addEventListener("keydown", gridKey);
    }

    // The pose press: one press, one request, at the marker's instant as it is
    // when pressed. The answer -- under way, or why not -- is said beside it.
    const said = host.querySelector(".tl-said");
    const pose = (button) => {
      const at = t;
      button.disabled = true;
      Promise.resolve(options.onPose(at))
        .then((answer) => {
          said.hidden = !answer || !answer.text;
          said.textContent = answer && answer.text ? answer.text : "";
          said.className = `hint tl-said${answer && answer.level === "error" ? " is-refused" : ""}`;
        })
        .finally(() => {
          button.disabled = false;
        });
    };

    host.querySelector(".tl-bar").addEventListener("click", (event) => {
      const target = event.target && event.target.closest ? event.target : null;
      const loopButton = target ? target.closest("[data-tl-loop]") : null;
      if (loopButton) {
        authored = loopButton.dataset.tlLoop === "authored";
        host.querySelectorAll("[data-tl-loop]").forEach((button) =>
          button.setAttribute("aria-pressed", String((button.dataset.tlLoop === "authored") === authored)));
        paintLanes();
        setMarker(t);
        return;
      }
      const act = target ? target.closest("[data-tl-act]") : null;
      if (!act) return;
      if (act.dataset.tlAct === "pose") {
        pose(act);
        return;
      }
      if (act.dataset.tlAct === "remove") removeSelected();
      if (act.dataset.tlAct === "cards" && typeof options.onCards === "function") options.onCards();
      if (act.dataset.tlAct === "close" && typeof options.onClose === "function") options.onClose();
    });

    paintUnwired();
    paintLanes();
    setMarker(0);

    return {
      // The droid answered with its Outputs or its config after the view
      // opened, or the editor changed the routine: say again what is not
      // wired, and draw the routine as it is now.
      refresh(next) {
        context = { ...(next || {}), describe: options.describe };
        redraw();
      },
      at: () => t,
      // A drag is under way: its half-made writes are in the routine and not
      // yet in the history, so the editor must not undo or redo under it.
      dragging: () => drag !== null,
      destroy() {
        scrubEnd();
        dragAbandon();
        host.innerHTML = "";
      },
    };
  };

  window.SeqTimeline = Object.freeze({ build, poseAt, unwired, mount });
})();

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
// IT DRAWS INTO THE CALLER'S STAGE (#441, variant C). The caller owns the page
// around it and hands over three places: the bar over the routine (the moment
// and the pose press), the column the lanes are drawn in, and the side where
// the droid is shown at the marker. The same three serve the editor and a
// Factory sequence's read-only stage.
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
// Something dragged in from outside - a Part from the library - is the
// caller's to insert; this view only says where it would land (`aim`), by the
// same tolerance a dragged block lands by.
//
// ONE READING OF THE ROUTINE. The steps are expanded by the Rehearsal's own
// expand() and a body move is resolved by its bodyMove() (data/seq_rehearsal.js),
// so the timeline and the rules that judge a routine read it the same way. A
// body move is timed by the generated planner (data/servo_motion.js), the same
// arithmetic ServoTask runs. A dome panel's travel is the dome's to know -- the
// Rehearsal's dome-timing Gap -- so a panel move is drawn as the instant it is
// sent and never as though its time were measured.
//
// THE BEAT GRID comes only from a tempo stored on the sequence (ADR 0058: the
// optional `tempo` block at format 1, include/seq_tempo.h). With one, a Bars
// row under the seconds shows every beat where the droid counts it and
// numbers the bars from the builder's downbeat (data/seq_tempo.js bars(), the
// reading the beat picker beside the routine uses), and the beats are places
// a drag can land (#441, ADR 0060). A step is drawn where the droid runs it:
// one placed on a beat at the millisecond its beat resolves to
// (data/seq_protocol_check.js resolveBeats()), the same resolution the
// firmware makes at parse. The view never infers a tempo from step spacing
// and never holds one of its own (ADR 0062).
//
// A drag sets a millisecond, so a step dragged off its beat leaves it, as a
// time typed over a beat does - unless the drag lands on a beat. Then the
// step that starts there is placed on that beat, not at its millisecond, and
// follows the tempo from then on; and an edge that lands on one gives the
// step its length in beats, where the step can keep one and starts on a beat
// itself (onBeats()).
//
// WHAT THE END DOES is the engine's, not a guess at it (src/tasks/
// sequence_engine.cpp beginFinish()): ring panels the run left open close one
// at a time, 500 ms apart, after the end step; a pie and a body Part stay
// where the last step left them; a toggle's open half closes nothing, because
// it is meant to stay open. Light modes are reset at the end. The group close
// :CL00 is never sent.
//
// ONE HALF OF A TOGGLE AT A TIME (#441, ADR 0062). A sequence in an interrupt
// group has two halves, and this view draws whichever the caller hands it as
// the routine's `steps`: the opening half as the sequence itself, the close
// half as a routine of its own with no close half. It never draws one beside
// the other. What it is told about the other half is `context.open`: the
// Parts the opening half left standing open, which a close half starts with.
// leftOpen() is that reading of an opening half, for the caller to hand back.
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
  // What a drag will not go past. Protocol Check has the rules; these only
  // keep a block on the timeline and wide enough to take hold of again.
  const STEP_T_MAX_MS = 120000;
  const MIN_LENGTH_MS = 50;
  // The arrow keys move the selected blocks this far; Shift, the bigger step.
  const NUDGE_MS = 10;
  const NUDGE_BIG_MS = 100;
  // How long a body flutter may last, as [least, most]: Protocol Check's own
  // bounds, which a typed or dragged length is held to. Without them, the
  // limits any length has here.
  const flutterRange = () => window.SeqProtocolCheck?.BODY_FLUTTER_MS || [MIN_LENGTH_MS, STEP_T_MAX_MS];

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

  // The beats of a stored tempo up to `untilMs`, each with where the droid
  // counts it: {index, bar, beat, strong, t}. The bars and their numbers are
  // data/seq_tempo.js bars(), and the millisecond is Protocol Check's
  // tempoBeatMs(), which a step placed on that beat resolves to. None without
  // a tempo the droid would accept.
  const beatsOf = (tempo, untilMs) => {
    const check = window.SeqProtocolCheck;
    if (!tempo || !window.SeqTempo || !check?.tempoBeatMs) return [];
    return window.SeqTempo.bars(tempo, untilMs)
      .flatMap((bar) => bar.beats.map((beat) => ({ ...beat, t: check.tempoBeatMs(tempo, beat.index) })))
      .filter((beat) => beat.t <= untilMs);
  };

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

  // A sequence inside this one, read from its own steps at the milliseconds
  // they run at: how long it runs - the length its own end step gives it;
  // spliced into a routine, the engine runs on to that routine's end - and
  // the Parts it names, each kind of step read as build() reads it. A
  // Random Flutter names a set the droid picks from when it runs, not a Part.
  // One level only: a sequence inside the phrase is not followed.
  const phraseRuns = (steps) => {
    const end = steps.findIndex((step) => step && step.type === "end");
    return end === -1 ? steps : steps.slice(0, end);
  };
  const phraseLength = (steps) => {
    const end = steps.find((step) => step && step.type === "end");
    return end ? Number(end.t) || 0 : 0;
  };
  const phraseParts = (steps) => {
    const ids = new Set();
    phraseRuns(steps).forEach((step) => {
      if (!step) return;
      if (step.type === "body" && step.part) ids.add(step.part);
      else if (step.type === "dome") (panelCommand(step.cmd) || lightCommand(step.cmd) || { ids: [] }).ids.forEach((id) => ids.add(id));
      else if (step.type === "gesture" && window.SeqGesture) window.SeqGesture.members(step).forEach((id) => ids.add(id));
    });
    return [...ids];
  };

  // ---------------------------------------------------------------------------
  // build() -- the whole drawing, as data. Pure: the sequence and the facts the
  // droid reported go in, lanes and items come out.
  //
  // An item is {kind, t0, t1, label, ghost}:
  //   move     a body Part travelling, for the planner's time
  //   open     a Part standing open between an open and a later close
  //   left     still open when this finishes -- to the right edge
  //   flutter  a flutter, which ends closed (ADR 0049, amended 2026-10-02):
  //            a body one for its own length, or to the end step where that
  //            comes first; a dome one has no length the body knows
  //   maybe    a random step that may land on this Part: the pick is not made
  //            until the droid runs it
  //   light    a light mode, until it is changed, its time runs out or the
  //            end resets it
  //   span     anything else with a length: a dome visual, a dome turn, a loop
  //   gesture  a Gesture, on the lane of every Part it spreads across: the
  //            one step drawn once per lane, so the lanes' pieces are one
  //            block (as a light mode on a group of lights is)
  //   phrase   a sequence inside this one, on the lane of every Part it
  //            names, for as long as it runs: one linked block, as a Gesture
  //            is one, and what it does is not drawn inside it
  //   take     a take (#442, ADR 0061), on the lane of every Part it moves,
  //            for as long as its trim plays: one linked block, edited whole
  //            and never opened step by step. One whose file has not been
  //            read is a mark on the Takes row where it starts.
  //   overrun  where a later take covers the same Part, drawn over the
  //            earlier take's block: there the later one moves it
  //   tick     one command at its instant: every step draws at least this
  // `ghost` marks an item from a loop's second pass or later, which the
  // as-written reading draws faintly.
  //
  // An item drawn from steps the builder wrote also says which, so a drag can
  // find its way back to them (#441):
  //   steps    the indices of the steps a drag of the body moves in time
  //   l, r     what dragging that edge changes, as {step, field}: field "t" is
  //            that step's time (a Part standing open ends at its close step),
  //            any other field is the step's own duration - a turn's, or a
  //            body flutter's length, which is all its right edge changes
  // A Part standing open (`open`) also carries `sent`: when the command that
  // opened it was sent, which for a body Part is before it stands open.
  // A take is not a step: its block says which take it is (`take`, its place
  // in the sequence's `takes`), and its edges are its trim - the left edge
  // writes where it starts and its `from` together, so what plays stays where
  // it was in time, and the right edge writes its `to`.
  // An item with no `steps` is derived and is not draggable: a later pass of a
  // loop, a move a Gesture becomes, what the droid does after the end. One a
  // Gesture becomes says which step that Gesture is (`of`), because it moves
  // when the Gesture does.
  //
  // A lane also carries `changes`: [{t, at}] for every moment the routine
  // commands the Part to a position (at 0 closed, 1 fully open), which is what
  // poseAt() reads. A Part the routine has not yet moved has no change before
  // that moment, so it is not in the pose.
  //
  // Two things in `context` are about a toggle's halves, and neither is read
  // off the droid:
  //   open       [{part, at}]: the Parts standing open when the routine
  //              starts, and how far - what a close half starts with. Each is
  //              drawn open from the start to the step that closes it, as a
  //              block no step here wrote, and is in the pose from the start.
  //   staysOpen  read the routine as a toggle's opening half whether or not
  //              it has a close half yet (leftOpen()). Absent, it is one
  //              exactly when it has both a group and a close half.
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
    // engine runs the open half when the group is closed. A close half handed
    // over as a routine of its own has no close half, so it is not one, and
    // its end is the end of a run that closes (see the header).
    const toggleOpenHalf = context.staysOpen ?? Boolean(seq?.toggleGroup && seq.toggleGroup !== "none" &&
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
          // `before` is a Part standing open from before this routine
          // started and not opened by it since (context.open).
          state: { open: false, since: 0, sent: 0, sinceGhost: false, sinceStep: null, sinceOf: null, fromUs: null, before: false },
        });
      }
      return lanes.get(id);
    };
    const rows = new Map();
    const rowLane = (key, name) => {
      if (!rows.has(key)) rows.set(key, { key, name, items: [] });
      return rows.get(key);
    };
    // The Gesture whose move is being drawn, as its step's index, or null
    // while what is drawn was written as a step of its own. Only an item no
    // written step draws says so: one a builder wrote - a hand-written open
    // that a Gesture's move closes - stays where its own step is, and is still
    // somewhere a dragged Gesture can land.
    let source = null;
    const add = (lane, item) => {
      if (!item.steps && item.of === undefined && source !== null) item.of = source;
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
    // the written step that did it, or null. `t` is when the Part stands open
    // and `sent` when the command that opens it is sent: the same moment for a
    // dome panel, and for a body Part the start of the travel or the flutter
    // that comes first.
    const openFrom = (lane, t, ghost, step, sent = t) => {
      // Opened by this routine, whether or not it was open already: the
      // engine then counts it among the panels this run opened.
      lane.state.before = false;
      if (!lane.state.open) {
        lane.state.open = true;
        lane.state.since = t;
        lane.state.sent = sent;
        lane.state.sinceGhost = ghost;
        lane.state.sinceStep = step;
        lane.state.sinceOf = source;
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
    // The Part stops standing open at `t`. `step` is the written step that
    // closes it, which is then the block's right edge, or null where no close
    // was written there: the droid's own close after the end, or a flutter,
    // which ends the Part closed and is a block of its own.
    const closeAt = (lane, t, step) => {
      if (lane.state.open) {
        const pair = standing(lane.state.sinceStep, step);
        const of = lane.state.sinceOf === null ? {} : { of: lane.state.sinceOf };
        add(lane, { kind: "open", t0: lane.state.since, t1: t, sent: lane.state.sent, ghost: lane.state.sinceGhost, ...of, ...pair });
        lane.state.open = false;
      }
    };

    // What stood open before the routine started (context.open), in the
    // order given, which is lane order.
    (Array.isArray(context.open) ? context.open : []).forEach(({ part, at }) => {
      const lane = partLane(part);
      Object.assign(lane.state, { open: true, before: true });
      lane.changes.push({ t: 0, at });
    });

    // The pick a random step made, for a hold step to attach to: its set.
    let lastRandom = null;
    const light = new Map(); // light Part id -> its current light item
    // A light's mode ends where the next one for that light starts, or at the
    // end: one with no time of its own, and one whose time had not run out.
    const lightEnd = (id, t) => {
      const item = light.get(id);
      if (item && (item.t1 === null || item.t1 > t)) item.t1 = t;
      light.delete(id);
    };
    const domeVisual = { item: null };

    events.forEach((event) => {
      const def = event.def || {};
      const t = event.t;
      const ghost = (event.iter || 0) > 0;
      const label = context.describe ? context.describe(def) : def.type;
      const step = written(event);
      source = event.generated ? event.step : null;
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
                // A dome flutter has no length the body knows, and the dome
                // ends it closed (ADR 0008, amended 2026-10-02). So a panel
                // standing open stops standing open at its flutter, as a
                // body Part does, and a close written after it is a block of
                // its own: a close of a closed panel. The engine still
                // counts a panel an earlier open marked as open, and closes
                // it again after the end (recordRingOpenState(),
                // src/tasks/sequence_engine.cpp), which is harmless.
                closeAt(lane, t, null);
                add(lane, { kind: "flutter", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
                lane.changes.push({ t, at: 0 });
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
            // A flutter is its own block: it swings for its length, or to
            // the end step where that comes first, and ends closed
            // (include/sequence_flutter.h "HOW IT ENDS"). Its right edge is
            // that length and nothing else. A Part standing open stops
            // standing open where its flutter starts, and a close written
            // after it is a block of its own.
            const flutterMs = Number(def.flutterMs) || 0;
            const over = endIndex !== -1 && t <= endMs ? Math.min(t + flutterMs, endMs) : t + flutterMs;
            closeAt(lane, t, null);
            add(lane, { kind: "flutter", t0: t, t1: over, label, ghost, ...lasts(step, "flutterMs") });
          } else {
            // How long the Part takes to get there is the Output's, not the
            // step's (ADR 0052), so a move is dragged and never resized.
            if (travel > 0) add(lane, { kind: "move", t0: t, t1: t + travel, label, ghost, ...drawnFrom(step) });
            else add(lane, { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
            if (shape === "close") closeAt(lane, t, step);
            else openFrom(lane, t + travel, ghost, step, t);
          }
          // A flutter is posed where it leaves the Part, on its closed end
          // (seqBodyTargetUs(), include/sequence_body_step.h).
          const howFar = Number(def.howFar) || 100;
          lane.changes.push({ t, at: shape === "open" ? Math.min(100, Math.max(5, howFar)) / 100 : 0 });
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
        case "domeBearing":
          // A turn until a target faces front lasts what the dome takes, which
          // is known only when it runs: a mark on the dome's row where it starts.
          add(rowLane("spin", "Dome turn"), { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
          return;
        case "gesture": {
          // One block across the lanes of its Parts, from where it fires
          // (#441). A body Gesture runs to the end of the last move it
          // makes; a dome Gesture is the dome's one command, with no length
          // the body knows, as a dome flutter has none. It has no edges: its
          // length comes of its pace and its repeat, which no drag owns, so a
          // drag only moves when it fires. A member no Output claims gets its
          // lane all the same, dimmed as any such lane is.
          //
          // The moves it becomes are drawn inside it on the same lanes, from
          // the expansion the Rehearsal reads (seq_rehearsal.js expand()),
          // and are derived: read, never taken hold of.
          const G = window.SeqGesture;
          const ids = G ? G.members(def) : [];
          if (ids.length === 0) {
            // It names no Part this page knows: still drawn, never dropped.
            add(rowLane("other", "Other"), { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
            return;
          }
          // A member's flutter lasts past the moment it starts, to the end
          // step at most, as a Body Step's does.
          const flutterMs = rehearsal ? rehearsal.gestureFlutterMs(def) : 0;
          const overAt = (move) => (move.shape !== "flutter" ? move.t
            : endIndex !== -1 && move.t <= endMs ? Math.min(move.t + flutterMs, endMs) : move.t + flutterMs);
          const until = G.bodyMoves(def, t).reduce((last, move) => Math.max(last, overAt(move)), t);
          ids.forEach((id) => {
            add(partLane(id), { kind: "gesture", t0: t, t1: until, label, ghost, ...drawnFrom(step) });
          });
          return;
        }
        case "sequence": {
          // A sequence inside this one (ADR 0046): one linked block from
          // where it starts, for as long as it runs, across the lanes of the
          // Parts it names (#441). The droid splices its steps in when the
          // routine runs (src/seq_store.cpp), so the block is the phrase as
          // the droid holds it now, which the caller reads and hands over
          // (`context.phrase`). What it does is not drawn inside it, and its
          // Parts are not in the pose.
          //
          // It has no edges: its length is the phrase's own, or what this
          // routine's end leaves of it, so a drag only moves where it starts.
          //
          // One the caller has not read - it is not on this droid, or the
          // read failed - is a mark on the Sequence row where it starts. One
          // that names no Part is a block on that row, at its length.
          const inner = def.ref && typeof context.phrase === "function" ? context.phrase(def.ref) : null;
          if (!Array.isArray(inner)) {
            add(rowLane("phrase", "Sequence"), { kind: "tick", t0: t, t1: t, label, ghost, ...drawnFrom(step) });
            return;
          }
          const block = { kind: "phrase", t0: t, t1: t + phraseLength(inner), label, ghost };
          const ids = phraseParts(inner);
          if (ids.length === 0) add(rowLane("phrase", "Sequence"), { ...block, ...drawnFrom(step) });
          ids.forEach((id) => {
            add(partLane(id), { ...block, ...drawnFrom(step) });
          });
          return;
        }
        case "end":
          return;
        default:
          // A step kind this view does not know yet still draws, as a labelled
          // mark at its time. It is never dropped.
          add(rowLane("other", "Other"), { kind: "tick", t0: t, t1: t, label: def.type || label, ghost, ...drawnFrom(step) });
      }
    });

    // The takes, as the droid plays them (SeqRehearsal.takeSpans()): a take is
    // played on the opening half only, and a close half is handed over
    // without them (data/seq.js halfRoutine()). The pose takes each change a
    // take makes, except where a later take covers the Part; where that one
    // stops, the earlier one has the Part again, where it would by then have
    // put it (include/take_replay.h). That a step's move holds a take off its
    // Part is the droid's to time, and is not read into the pose.
    source = null;
    const spans = rehearsal?.takeSpans ? rehearsal.takeSpans(seq, context, endIndex !== -1 ? endMs : null) : [];
    const overruns = rehearsal ? rehearsal.takeOverlaps(spans) : [];
    spans.forEach((span) => {
      const label = `Take ${span.index + 1}`;
      if (!span.facts) {
        add(rowLane("take", "Takes"), { kind: "tick", t0: span.t0, t1: span.t0, label, take: span.index });
        return;
      }
      const trim = { take: span.index, l: { take: span.index, field: "from" }, r: { take: span.index, field: "to" }, lengthMs: span.lengthMs, from: span.from, to: span.to };
      span.parts.forEach((cover) => {
        const lane = partLane(cover.part);
        add(lane, { kind: "take", t0: span.t0, t1: span.t1, label, ...trim });
        // The stretches where another take, later in the list, moves this
        // Part over this one's block.
        const under = overruns.filter((over) => over.part === cover.part && over.takes.includes(span.index) && over.winner !== span.index);
        under.forEach((over) => add(lane, { kind: "overrun", t0: over.t0, t1: over.t1, label: `Take ${over.winner + 1} moves it here` }));
        const outranked = (t) => under.some((over) => over.t0 <= t && t < over.t1);
        cover.changes.filter((change) => !outranked(change.t)).forEach((change) => lane.changes.push(change));
        under.forEach((over) => {
          const held = cover.changes.filter((change) => change.t <= over.t1).pop();
          if (held && over.t1 < cover.covers[1] && !outranked(over.t1)) lane.changes.push({ t: over.t1, at: held.at });
        });
      });
    });

    // The end: what the engine does after the end step, in its order.
    let ringCloses = 0;
    let cleanupEnd = endMs;
    const partLanes = [...lanes.values()].sort((a, b) =>
      (a.half === "dome" ? 0 : 1) - (b.half === "dome" ? 0 : 1) || a.index - b.index);
    // kRingPanels order is catalog order (panel1, 2, 3, 4, 7, 11, 13). The
    // engine closes only the ring panels this run opened (st.ringOpenMask is
    // cleared at seqEngineStart()), so one that stood open before it started
    // is left as it is.
    partLanes.forEach((lane) => {
      if (!lane.state.open || lane.state.before || toggleOpenHalf || !isRingPart(lane.part)) return;
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
    // A sequence inside this one stops at this routine's end step too: the
    // droid cuts what it splices in past the end (seqStoreSplicePhrase(),
    // include/seq_store_util.h).
    if (endIndex !== -1) {
      [...lanes.values(), ...rows.values()].forEach((lane) => lane.items.forEach((item) => {
        if (item.kind === "phrase" && item.t1 > endMs) item.t1 = endMs;
      }));
    }

    // A take's changes and the droid's closes after the end were added out of
    // time order; poseAt() reads the last change at or before an instant.
    lanes.forEach((lane) => lane.changes.sort((a, b) => a.t - b.t));

    const lastItem = [...lanes.values(), ...rows.values()]
      .reduce((max, lane) => lane.items.reduce((m, item) => Math.max(m, item.t1 ?? item.t0), max), 0);
    const windowMs = Math.max(cleanupEnd, lastItem, endMs) + TAIL_MS;

    // What is still open closes nowhere: a lighter run to the right edge.
    partLanes.forEach((lane) => {
      if (lane.state.open) {
        const of = lane.state.sinceOf === null ? {} : { of: lane.state.sinceOf };
        add(lane, { kind: "left", t0: lane.state.since, t1: windowMs, ghost: lane.state.sinceGhost, ...of, ...standing(lane.state.sinceStep, null) });
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
      rows: ["take", "sound", "dome", "spin", "phrase", "other"].map((key) => rows.get(key)).filter(Boolean),
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
  // leftOpen() -- what a routine leaves standing open when it is run as a
  // toggle's opening half: the Parts whose lanes build() draws open to the
  // right edge, in lane order, each with how far open the last step left it.
  // The one reading of it: the close half a builder is started with closes
  // these (data/seq.js), and a close half on the stage starts with them open
  // (`context.open`). Two things are not in it: a random step's pick, which
  // nobody knows until the droid runs, and what a sequence inside this one
  // leaves open, which is drawn as one block and not read into.
  //
  // A KNOWN OVER-READ. Where the opening half holds a Marcduino sequence
  // (:SE##), the engine closes the ring panels that run opened even on a
  // toggle's opening half (`wantRingClose` under FX_DOME_SEQUENCE,
  // beginFinish(), src/tasks/sequence_engine.cpp), while build() lists them
  // open. A close half started from this closes them a second time, which is
  // harmless: a close of a closed panel.
  // ---------------------------------------------------------------------------
  const leftOpen = (seq, context = {}) => {
    const model = build(seq, { ...context, open: null, staysOpen: true });
    const pose = poseAt(model, model.windowMs);
    return model.parts
      .filter((lane) => lane.items.some((item) => item.kind === "left"))
      .map((lane) => ({ part: lane.part, at: pose.at[lane.part] ?? 1 }));
  };

  // ---------------------------------------------------------------------------
  // notWired() -- whether nothing on this droid can move a Part: a body Part no
  // Output claims, and a dome Part while the dome link is switched off. Nothing
  // is said about a half the droid has not reported. The one rule, for a lane
  // here and for the Parts list beside the routine (data/seq.js).
  //
  // unwired() -- the Parts this routine names that it holds for.
  // ---------------------------------------------------------------------------
  const notWired = (partId, half, context = {}) => {
    if (half === "dome") return context.config?.components?.protoR2link?.enabled === false;
    if (!Array.isArray(context.outputs)) return false;
    return !context.outputs.some((output) => Array.isArray(output.parts) && output.parts.includes(partId));
  };

  const unwired = (model, context = {}) =>
    model.parts.filter((lane) => notWired(lane.part, lane.half, context)).map((lane) => lane.part);

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
    // A Gesture's block holds the moves it becomes, each with its own words,
    // so its own are in its title and not written over theirs.
    // An overrun lies over a take's block, whose words are not written over.
    const text = item.kind !== "tick" && item.kind !== "gesture" && item.kind !== "overrun" && item.label ? `<span class="tl-label">${esc(item.label)}</span>` : "";
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

  // The Bars row under the seconds: a line at every beat, and the bar's
  // number at its first. A pickup has no number. Where the beats would stand
  // closer than a line can be told from the next - a fast tempo over a long
  // routine, at the scale the lanes are never drawn below - only the bars'
  // first beats are drawn.
  // shownBeats() is that choice, and the beats a drag can land on are the
  // same ones: a landing is always on a line the builder can see.
  const BEAT_MIN_PX = 6;
  const shownBeats = (beats) => {
    const gapMs = beats.length > 1 ? beats[1].t - beats[0].t : Infinity;
    const every = (gapMs / 1000) * PX_PER_SECOND >= BEAT_MIN_PX;
    return beats.filter((beat) => every || beat.strong);
  };
  const barsRowHtml = (beats, windowMs) => {
    if (beats.length === 0) return "";
    const marks = shownBeats(beats)
      .map((beat) =>
        `<span class="tl-beat${beat.strong ? " is-bar" : ""}" style="left:${pct(beat.t, windowMs)}">${beat.strong && beat.bar > 0 ? beat.bar : ""}</span>`)
      .join("");
    return `<div class="tl-row tl-bars-row"><div class="tl-name">Bars</div><div class="tl-track tl-bars" aria-hidden="true">${marks}</div></div>`;
  };

  // A lane's items in the order they are drawn, the last on top. A Part
  // standing open (`open`, `left`) is listed when it closes, or at the end,
  // which is after every block that starts while it stands; drawn in that
  // place it would lie over them and take their presses. So standing goes
  // under, and the blocks over it in the order they were listed. The order is
  // the whole of it: a block given a layer of its own would bury the marks
  // drawn inside it.
  const isStanding = (item) => item.kind === "open" || item.kind === "left";
  const stacked = (items) => [...items.filter(isStanding), ...items.filter((item) => !isStanding(item))];

  const laneHtml = (lane, windowMs, authored, dim, handleOf = () => "") =>
    `<div class="tl-row${dim ? " is-unwired" : ""}${lane.part ? "" : " is-kind"}" data-lane="${esc(lane.key)}">` +
    `<div class="tl-name">${lane.short ? `<span class="tl-short">${esc(lane.short)}</span>` : ""}` +
    `<span class="tl-part">${esc(lane.name)}</span></div>` +
    `<div class="tl-track">${stacked(lane.items).map((item) => itemHtml(item, windowMs, authored, handleOf(item, lane))).join("")}</div>` +
    `</div>`;

  // ---------------------------------------------------------------------------
  // mount() -- draw a sequence as a timeline into the caller's stage.
  //
  // `hosts` is the three places the caller gives it: `bar` (the moment, the
  // pose press and what the droid answered), `lanes` (the note about Parts
  // this droid cannot move, and the routine itself) and `side` (the droid at
  // the marker, and what the routine has commanded by then).
  //
  // `source` is the sequence, or a function that returns it. An editor passes
  // the function, so the view always reads the object being edited and never
  // keeps a copy that an undo or a tempo change would leave behind. It is
  // also how the caller puts the other half of a toggle on the stage: the
  // function answers the half to show, and refresh() draws it.
  //
  // options:
  //   context   what the droid reported, as the Rehearsal reads it: outputs,
  //             config. Absent parts of it make the view say less, never guess.
  //             Its `phrase(ref)` answers the steps of a sequence this one
  //             names, at the milliseconds they run at, or null for one the
  //             caller has not read: this view reads nothing off the droid.
  //             Its `open` is the Parts standing open when the routine
  //             starts, for a toggle's close half (build()).
  //   describe  step -> words, the editor's own preview (data/seq.js), so a
  //             block and the inspector name a step alike
  //   onPose    the builder pressed to send the droid to the marker's instant:
  //             called with it in ms, and returns a promise of {text, level}
  //             to show beside the press. Absent, there is no press.
  //   edit      the editor's half of an edit; with it the blocks can be taken
  //             hold of:
  //               begin()         the copy of the routine a gesture starts from
  //               commit(before)  a gesture changed the routine: put the steps
  //                               in order, record it, and read it all again
  //               remove(indices, takes)
  //                               take these steps, and these entries of
  //                               the sequence's `takes`, out of the
  //                               routine, as one edit
  //   onPicked  the blocks the builder has picked, said again whenever the
  //             routine is drawn: called with picked() and, on the press that
  //             picked one, `true`. The caller shows them; this view only
  //             knows which they are.
  //
  // Returns {refresh(context), at(), dragging(), cancel(), picked(),
  // pick(indices), movePickedTo(ms, landed), pickedRange(), removePicked(),
  // aim(point, isEnd),
  // beatAt(ms), standing(index), sizeStanding(index, ms), say(answer),
  // destroy()}.
  // ---------------------------------------------------------------------------
  const mount = (hosts, source, options = {}) => {
    const seqNow = typeof source === "function" ? source : () => source;
    const edit = options.edit || null;
    let context = { ...(options.context || {}), describe: options.describe };
    let model = build(seqNow(), context);
    let windowMs = model.windowMs;
    // The stored tempo's beats on screen, read again whenever the routine is.
    let beats = beatsOf(seqNow()?.tempo, windowMs);
    let authored = true;
    let t = 0;
    let ruler = null;
    let marker = null;

    // What the editing view holds between presses. `selection` is the steps
    // themselves rather than their indices, so it survives the editor putting
    // them in a new order and empties by itself when an undo replaces them.
    // `drawn` is every item on screen that can be taken hold of, with the lane
    // it is drawn on, in the order it was written: how a press finds the item
    // under it, and how the picked blocks are named. `drag` is the gesture
    // under way.
    const selection = new Set();
    let drawn = [];
    let drag = null;
    // Where something dragged in from outside would land, while it is over
    // the lanes: {t, label}, or null.
    let aimed = null;

    // The pose press is a quiet act: the one filled act on this surface is
    // Save, and this press moves the droid, which nothing here does by itself.
    hosts.bar.innerHTML =
      `<span class="tl-now" role="status" aria-live="polite"></span>` +
      (typeof options.onPose === "function"
        ? `<button type="button" class="seq-act icon-act act-keeps-words" data-tl-act="pose">${window.PAUi.actFace("ray-start-arrow", "Move the droid to this moment")}</button>`
        : "") +
      `<span class="hint tl-said" role="status" aria-live="polite" hidden></span>` +
      `<div class="seg seg-sm tl-loop-mode" role="group" aria-label="How a loop is drawn" hidden>` +
      `<button type="button" data-tl-loop="authored" aria-pressed="true">As written</button>` +
      `<button type="button" data-tl-loop="expanded" aria-pressed="false">Expanded</button></div>`;
    hosts.lanes.innerHTML =
      `<p class="note note-act tl-unwired" hidden></p>` +
      `<div class="tl-scroll"><div class="tl-grid"${edit ? ` tabindex="0" role="group" aria-label="The routine's blocks"` : ""}></div></div>`;
    hosts.side.innerHTML =
      `<div class="sect"><h3>The droid</h3><span class="sub tl-at"></span></div>` +
      `<div class="tl-picture"></div><dl class="tl-readout"></dl>`;

    const grid = hosts.lanes.querySelector(".tl-grid");
    const now = hosts.bar.querySelector(".tl-now");
    const atSub = hosts.side.querySelector(".tl-at");
    const readout = hosts.side.querySelector(".tl-readout");
    const unwiredNote = hosts.lanes.querySelector(".tl-unwired");
    const pictureHost = hosts.side.querySelector(".tl-picture");
    const loopMode = hosts.bar.querySelector(".tl-loop-mode");

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
    // What an item is drawn from, as the objects the selection holds: the
    // steps it was written as, or the take it is (an entry of the sequence's
    // `takes`). None for a derived item.
    const ownOf = (item) => (item.take !== undefined
      ? [(seqNow().takes || [])[item.take]]
      : (item.steps || []).map((index) => seqNow().steps[index])).filter(Boolean);
    const selected = (item) => {
      const own = ownOf(item);
      return own.length > 0 && own.every((each) => selection.has(each));
    };
    const handleOf = (item, lane) => {
      if (!editing()) return "";
      // A move a Gesture becomes, as written, says which Gesture: a press on
      // it takes hold of that Gesture's block (grab()).
      if (!item.steps && item.take === undefined) return item.of !== undefined && !item.ghost ? ` data-of="${item.of}"` : "";
      drawn.push({ item, lane });
      const sized = item.kind === "tick" ? "" : `${item.l ? " can-size-l" : ""}${item.r ? " can-size-r" : ""}`;
      return ` data-item="${drawn.length - 1}" data-edit="can-move${sized}${selected(item) ? " is-selected" : ""}"`;
    };

    // The blocks the builder has picked, as the routine wrote them. One step
    // can draw several items - a Part standing open is drawn over the mark of
    // the command that opened it - so an item whose steps are all inside
    // another picked item's is that block's own detail, not a second block.
    // Each is {steps, t0, name, words}: the indices of its steps, where it
    // starts, the lane it is on when it is on one Part's lane and nowhere
    // else, and the step's own words. A take is a block of its own, with no
    // steps: `take` is its place in the sequence's `takes`.
    const picked = () => {
      const blocks = new Map();
      drawn.forEach(({ item, lane }) => {
        if (!selected(item)) return;
        const key = item.take !== undefined ? `take:${item.take}` : item.steps.join(",");
        const block = blocks.get(key) || { steps: item.steps || [], take: item.take, t0: item.t0, lanes: new Set(), part: true, words: "" };
        block.t0 = Math.min(block.t0, item.t0);
        block.lanes.add(lane.name);
        block.part = block.part && Boolean(lane.part);
        block.words = block.words || item.label || "";
        blocks.set(key, block);
      });
      const all = [...blocks.values()];
      // The end step is drawn as a line, not an item: picked, it is a block
      // with no lane and no words of its own.
      if (model.end !== -1 && selection.has(seqNow().steps[model.end])) {
        all.push({ steps: [model.end], t0: model.endMs, lanes: new Set(), part: false, words: "" });
      }
      const within = (block, other) => block.take === undefined && other.take === undefined &&
        other !== block && other.steps.length > block.steps.length && block.steps.every((index) => other.steps.includes(index));
      // A block starts where the earliest of its details does: a body Part
      // standing open is drawn from the end of its travel, and the block
      // starts at the step that sent it there.
      return all
        .filter((block) => !all.some((other) => within(block, other)))
        .map((block) => ({
          steps: block.steps,
          ...(block.take !== undefined ? { take: block.take } : {}),
          t0: Math.min(block.t0, ...all.filter((detail) => within(detail, block)).map((detail) => detail.t0)),
          name: block.part && block.lanes.size === 1 ? [...block.lanes][0] : "",
          // A Part standing open has no words of its own: it takes those of
          // the command that opened it.
          words: block.words || all.find((detail) => within(detail, block) && detail.words)?.words || "",
        }));
    };

    const paintLanes = (pressed = false) => {
      drawn = [];
      const hasLoop = model.loops.length > 0;
      loopMode.hidden = !hasLoop;
      const loopLane = hasLoop
        ? laneHtml({ key: "loop", name: "Loop", short: "", items: loopItems(model, authored) }, windowMs, false, false, handleOf)
        : "";
      const landing = drag ? drag.snap : aimed;
      const snap = landing
        ? `<div class="tl-snap" style="left:${pct(landing.t, windowMs)}">` +
          (landing.label ? `<span class="tl-snap-label">${esc(landing.label)}</span>` : "") + `</div>`
        : "";
      const endPicked = model.end !== -1 && selection.has(seqNow().steps[model.end]);
      // At least this many pixels a second, so a long routine scrolls rather
      // than crushing its blocks together.
      grid.setAttribute("style", `min-width:calc(var(--tl-lane-w) + ${Math.round((windowMs / 1000) * PX_PER_SECOND)}px)`);
      grid.innerHTML =
        `<div class="tl-row tl-ruler-row"><div class="tl-name">Time</div>` +
        `<div class="tl-track tl-ruler" role="slider" tabindex="0" aria-label="Moment" ` +
        `aria-valuemin="0" aria-valuemax="${Math.round(windowMs)}">${rulerHtml(windowMs)}</div></div>` +
        barsRowHtml(beats, windowMs) +
        loopLane +
        model.parts.map((lane) => laneHtml(lane, windowMs, authored, dim.has(lane.part), handleOf)).join("") +
        model.rows.map((row) => laneHtml(row, windowMs, authored, false, handleOf)).join("") +
        `<div class="tl-overlay" aria-hidden="true">` +
        `<div class="tl-end"${editing() && model.end !== -1 ? ` data-edit="can-move${endPicked ? " is-selected" : ""}"` : ""} style="left:${pct(model.endMs, windowMs)}"></div>` +
        snap +
        `<div class="tl-marker"></div></div>`;
      ruler = grid.querySelector(".tl-ruler");
      marker = grid.querySelector(".tl-marker");
      ruler.addEventListener("pointerdown", scrubStart);
      ruler.addEventListener("keydown", onKey);
      if (typeof options.onPicked === "function") options.onPicked(picked(), pressed);
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
      atSub.textContent = `at ${seconds(t)}`;
      paintReadout();
    };

    // The one door from the routine to the drawing: everything on screen is
    // built again from the sequence as it is now. A drag in progress keeps the
    // scale it started with, so a block does not slide under the pointer when
    // the routine it is stretching gets longer.
    const redraw = () => {
      model = build(seqNow(), context);
      windowMs = drag ? drag.windowMs : model.windowMs;
      beats = beatsOf(seqNow().tempo, windowMs);
      const held = new Set([...seqNow().steps, ...(seqNow().takes || [])]);
      [...selection].forEach((each) => {
        if (!held.has(each)) selection.delete(each);
      });
      paintUnwired();
      paintLanes();
      setMarker(t);
    };

    const msFrom = (event) => {
      const rect = ruler.getBoundingClientRect();
      return rect.width > 0 ? ((event.clientX - rect.left) / rect.width) * windowMs : 0;
    };
    // A gesture belongs to the pointer that started it: a second finger or a
    // pen that comes down while it runs neither moves nor ends it.
    const foreign = (event, pointer) =>
      Boolean(event) && pointer !== undefined && event.pointerId !== undefined && event.pointerId !== pointer;

    // A press on the ruler jumps; a drag scrubs. Move, up and cancel are bound
    // on the window so a fast drag that leaves the ruler is not stranded.
    let scrubPointer;
    const scrubMove = (event) => {
      if (!foreign(event, scrubPointer)) setMarker(msFrom(event));
    };
    const scrubEnd = (event) => {
      if (foreign(event, scrubPointer)) return;
      window.removeEventListener("pointermove", scrubMove);
      window.removeEventListener("pointerup", scrubEnd);
      window.removeEventListener("pointercancel", scrubEnd);
    };
    function scrubStart(event) {
      if (typeof event.preventDefault === "function") event.preventDefault();
      ruler.focus();
      scrubPointer = event.pointerId;
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
      const [min, max] = field === "t" ? timeRange(steps, index)
        : field === "flutterMs" ? flutterRange() : [MIN_LENGTH_MS, STEP_T_MAX_MS];
      return { step, field, sign, from: Number(step[field]) || 0, min, max, beat: step.beat, spanBeats: step.spanBeats };
    };

    // One write to a take's entry. `whole` is the value its field has when
    // the entry does not say it - `from` 0, `to` the take's length - which is
    // stored as absence, so a take never trimmed saves as it was kept.
    const takeWrite = (entry, field, sign, [min, max], whole) => ({
      step: entry, field, sign, min, max, whole,
      from: entry[field] === undefined ? whole ?? 0 : Number(entry[field]) || 0,
    });

    // The writes for a press on `item`: its body (edge null) moves every
    // selected step and take, an edge moves what that edge is made of, and
    // neither edge may cross the other. A take's edges are its trim: the left
    // one moves where it starts and its `from` together, so what plays stays
    // where it was in time, and the right one moves its `to`. Neither goes
    // past the take's own ends, nor leaves less than a block to take hold of.
    const planFor = (item, edge) => {
      const steps = seqNow().steps;
      if (edge === null) {
        const takes = (seqNow().takes || []).map((entry) =>
          (selection.has(entry) ? takeWrite(entry, "t", 1, [0, STEP_T_MAX_MS]) : null));
        return { writes: [...steps.map((step, index) => (selection.has(step) ? write(steps, index, "t", 1) : null)), ...takes].filter(Boolean), lo: -Infinity, hi: Infinity };
      }
      if (item.take !== undefined) {
        const entry = seqNow().takes[item.take];
        const writes = edge === "l"
          ? [takeWrite(entry, "t", 1, [0, STEP_T_MAX_MS]), takeWrite(entry, "from", 1, [0, item.to - MIN_LENGTH_MS], 0)]
          : [takeWrite(entry, "to", 1, [item.from + MIN_LENGTH_MS, item.lengthMs], item.lengthMs)];
        return { writes, lo: -Infinity, hi: Infinity };
      }
      const length = (item.t1 ?? item.t0) - item.t0;
      const side = item[edge];
      if (side.field === "t") {
        const writes = [write(steps, side.step, "t", 1)];
        return edge === "l" ? { writes, lo: -Infinity, hi: length } : { writes, lo: -length, hi: Infinity };
      }
      // A block with a length of its own - a turn, a body flutter: the left
      // edge moves its start and takes the same off its length, and the right
      // edge changes the length alone.
      if (edge === "l") {
        return { writes: [write(steps, side.step, "t", 1), write(steps, side.step, side.field, -1)], lo: -Infinity, hi: Infinity };
      }
      return { writes: [write(steps, side.step, side.field, 1)], lo: -Infinity, hi: Infinity };
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
        if (w.whole !== undefined && w.step[w.field] === w.whole) delete w.step[w.field];
        const kept = w.field === "t" ? "beat" : "spanBeats";
        if (by !== 0) delete w.step[kept];
        else if (w[kept] !== undefined) w.step[kept] = w[kept];
      });
    };

    // The stored tempo, where it is one the droid would accept, or null.
    const tempoNow = () => {
      const tempo = seqNow().tempo;
      return tempo && window.SeqProtocolCheck?.validateTempo(tempo).ok ? tempo : null;
    };

    // The beat whose millisecond is exactly `ms`, as its index, or null. Any
    // beat the droid counts, drawn or not, on screen or past it: a beat
    // picked in the inspector need not be one a drag can land on. Beat k
    // resolves to phase + round(k * 60000 / bpm), so only the beats either
    // side of the nearest can.
    const beatAt = (ms) => {
      const check = window.SeqProtocolCheck;
      const tempo = tempoNow();
      if (!tempo || !Number.isFinite(ms)) return null;
      const near = Math.round((ms - (Number(tempo.phase) || 0)) / check.tempoSpanMs(tempo, 1));
      return [near - 1, near, near + 1].find((index) =>
        index >= 0 && index <= check.SPAN_BEATS[1] && check.tempoBeatMs(tempo, index) === ms) ?? null;
    };

    // What landed on a beat is placed on it (ADR 0060), for a plan already
    // written at where it landed:
    //   - a step whose time the plan moved, and which now starts exactly on
    //     a beat, is on that beat. A step a loop repeats is timed from its
    //     pass and carries no beat (Protocol Check).
    //   - a step whose length the plan changed gets that length in beats
    //     where it can keep one (Protocol Check's spansBeats()), it is on a
    //     beat itself and its end is now exactly on a later one. The length
    //     is then written as that many beats resolve - the count of beats
    //     times one beat, which can be a millisecond off the distance between
    //     the two rounded beat times - so the step is stored as the droid
    //     will read it. A step that starts off the beat keeps its
    //     milliseconds: a whole number of beats from there ends on no beat.
    // The callers decide when: a drag that landed on a beat, and a move the
    // editor says landed on one.
    const onBeats = (plan) => {
      const check = window.SeqProtocolCheck;
      if (!tempoNow() || !check?.spansBeats) return;
      const steps = seqNow().steps;
      const repeated = check.loopBodySteps(steps);
      // A take is placed in milliseconds only: the droid reads no beat on one.
      const writes = plan.writes.filter((w) => steps.includes(w.step));
      writes.forEach((w) => {
        if (w.field !== "t" || repeated.has(steps.indexOf(w.step))) return;
        const beat = beatAt(w.step.t);
        if (beat !== null) w.step.beat = beat;
      });
      writes.forEach((w) => {
        if (w.field === "t" || !check.spansBeats(w.step) || !Number.isInteger(w.step.beat)) return;
        const end = beatAt(w.step.t + w.step[w.field]);
        if (end === null || end <= w.step.beat) return;
        // Resolved, the span can be a millisecond past where the edge was
        // held: at the edge of the length's own range it stays milliseconds.
        const ms = check.tempoSpanMs(seqNow().tempo, end - w.step.beat);
        if (ms < w.min || ms > w.max) return;
        w.step.spanBeats = end - w.step.beat;
        w.step[w.field] = ms;
      });
    };

    // The beats of a stored tempo, time 0, and the start and end of every
    // block the gesture is not moving. A beat is named by its bar and its
    // place in it, a block by its own words, or by its lane when it has
    // none. The beats come first: where a beat and a block's edge are the
    // same moment, the landing is the beat. A loop's later passes are not
    // landed on: they are the same steps again, and the ones a gesture is
    // moving would be a target that moves with it. Nor are the moves a
    // dragged Gesture becomes, which go where it goes.
    const snapTargets = (plan) => {
      const steps = seqNow().steps;
      const moving = new Set(plan.writes.map((w) => w.step));
      const tempo = seqNow().tempo;
      const targets = [
        ...shownBeats(beats).map((beat) => ({ t: beat.t, label: window.SeqTempo.beatWords(tempo, beat.index), beat: beat.index })),
        { t: 0, label: "" },
      ];
      [{ name: "Loop", items: loopItems(model, true) }, ...model.parts, ...model.rows].forEach((lane) =>
        lane.items.forEach((item) => {
          // An overrun lies inside a take's block, and goes where it goes.
          if (item.ghost || item.kind === "overrun" || ownOf(item).some((each) => moving.has(each))) return;
          if (item.of !== undefined && moving.has(steps[item.of])) return;
          const label = item.label || lane.name;
          targets.push({ t: item.t0, label });
          if (item.kind !== "tick" && item.kind !== "left" && item.t1 !== null && item.t1 !== undefined) targets.push({ t: item.t1, label });
        }));
      return targets;
    };

    const dragMove = (event) => {
      if (foreign(event, drag.pointer)) return;
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
    function dragEnd(event) {
      if (!drag || foreign(event, drag.pointer)) return;
      const done = drag;
      drag = null;
      dragStop();
      // commit() comes back through refresh(), which draws the routine again.
      if (done.by !== 0) {
        if (done.snap && done.snap.beat !== undefined) onBeats(done.plan);
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
    function dragCancel(event) {
      if (drag && foreign(event, drag.pointer)) return;
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
      let node = target.closest(".tl-item, .tl-end");
      const isEnd = Boolean(node) && node.classList.contains("tl-end");
      // A press on one of the moves a Gesture becomes is a press on the
      // Gesture: they are drawn over its block and are not taken hold of
      // themselves, so the block they belong to answers for them.
      const within = () => {
        const of = node ? node.dataset.of : undefined;
        if (of === undefined) return null;
        return drawn.find((each) => each.item.kind === "gesture" && each.item.steps[0] === Number(of))?.item || null;
      };
      // A press on the mark of the step that opens a Part, or of the step
      // that closes it, is a press on the Part standing open: standing is
      // drawn under the blocks (stacked()), so those two marks lie over the
      // ends of the block they make. Taken by itself, either would move its
      // one step with nothing to stop it at the other, and past it the pair
      // changes places. Handed to the standing block, the press is that
      // block's edge or its body by where it falls, and an edge stops at the
      // other end.
      const opened = (pressed) => {
        const { item: mark, lane } = drawn[pressed];
        if (mark.kind !== "tick" || !mark.steps || mark.steps.length !== 1) return pressed;
        const ends = (side) => side !== undefined && side.field === "t" && side.step === mark.steps[0];
        const over = drawn.findIndex((each) =>
          each.lane === lane && isStanding(each.item) && (ends(each.item.l) || ends(each.item.r)));
        return over === -1 ? pressed : over;
      };
      let item = null;
      if (isEnd) {
        item = model.end === -1 ? null : { kind: "tick", t0: model.endMs, t1: model.endMs, steps: [model.end] };
      } else if (node && node.dataset.item !== undefined) {
        const taken = opened(Number(node.dataset.item));
        item = drawn[taken].item;
        // The block's own box decides edge or body below, not the mark's.
        node = grid.querySelector(`[data-item="${taken}"]`) || node;
      } else {
        item = within();
      }
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
      const own = ownOf(item);
      const held = own.every((each) => selection.has(each));
      if (event.shiftKey || event.ctrlKey || event.metaKey) {
        own.forEach((step) => (held ? selection.delete(step) : selection.add(step)));
        paintLanes(true);
        setMarker(t);
        return;
      }
      if (!held) {
        selection.clear();
        own.forEach((step) => selection.add(step));
      }

      // Body or edge, decided once, here. How far in from a block's end a
      // press takes the edge is the width the stylesheet draws the grip at,
      // --tl-grip, read from it rather than written a second time; a block
      // too narrow for two grips and a body gives each a third, as the
      // stylesheet does. With no stylesheet there is no grip, and no edge.
      const rect = node.getBoundingClientRect();
      const drawnGrip = typeof window.getComputedStyle === "function"
        ? parseFloat(window.getComputedStyle(grid).getPropertyValue("--tl-grip")) : NaN;
      const grip = Math.min(Number.isFinite(drawnGrip) ? drawnGrip : 0, rect.width / 3);
      const edge = isEnd || item.kind === "tick" ? null
        : item.l && event.clientX - rect.left < grip ? "l"
        : item.r && rect.right - event.clientX < grip ? "r" : null;
      const track = ruler.getBoundingClientRect();
      const plan = planFor(item, edge);
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
        pointer: event.pointerId,
        msPerPx: track.width > 0 ? windowMs / track.width : 0,
        windowMs,
        by: 0,
        snap: null,
        alone: own,
      };
      grid.setAttribute("data-dragging", edge ? "edge" : "body");
      window.addEventListener("pointermove", dragMove);
      window.addEventListener("pointerup", dragEnd);
      window.addEventListener("pointercancel", dragCancel);
      paintLanes(true);
      setMarker(t);
    }

    // Typed rather than dragged: the picked blocks start at `ms`, moved as a
    // drag of their bodies moves them, as far as their limits allow, and it is
    // one edit. A typed time is a millisecond (ADR 0058); `landed` says the
    // time is a beat - where something dropped on the lanes landed (aim()),
    // or the beat picked in the inspector - and then a block that starts on
    // a beat is placed on it, as a dragged one is.
    const movePickedTo = (ms, landed = false) => {
      const blocks = picked();
      if (drag || blocks.length === 0 || !Number.isFinite(ms)) return;
      const plan = planFor(null, null);
      const [lo, hi] = reach(plan);
      const by = Math.max(lo, Math.min(hi, Math.round(ms) - Math.min(...blocks.map((block) => block.t0))));
      const before = edit.begin();
      apply(plan, by);
      // At by 0 too: a block already at that millisecond is put on its beat.
      if (landed) onBeats(plan);
      edit.commit(before);
    };

    // Where the picked blocks can start, as movePickedTo() would take them:
    // {from, to} in ms, or null with nothing picked.
    const pickedRange = () => {
      const blocks = picked();
      if (blocks.length === 0) return null;
      const [lo, hi] = reach(planFor(null, null));
      const t0 = Math.min(...blocks.map((block) => block.t0));
      return { from: t0 + lo, to: t0 + hi };
    };

    // A Part standing open is one block made of two steps. By the step that
    // opens it: the step that closes it and how long the block runs, from the
    // step that opens it to the step that closes it - a body Part's travel
    // included - or null when no written step closes it. A flutter opens no
    // such block: it ends closed.
    const standingItem = (index) => {
      for (const lane of model.parts) {
        const item = lane.items.find((each) => each.kind === "open" && each.l && each.l.step === index && each.r);
        if (item) return item;
      }
      return null;
    };
    const standing = (index) => {
      const item = standingItem(index);
      return item ? { close: item.r.step, ms: item.t1 - item.sent } : null;
    };
    // Typed rather than dragged: the block the step at `index` opens runs
    // for `ms`, its close moved as far as its limits allow, and it is one
    // edit. The close comes no earlier than the step that opens the Part; one
    // that comes before a body Part has arrived is the Rehearsal's to say, not
    // a limit here (ADR 0052).
    const sizeStanding = (index, ms) => {
      const item = standingItem(index);
      if (drag || !item || !Number.isFinite(ms)) return;
      const steps = seqNow().steps;
      const span = item.t1 - item.sent;
      const plan = { writes: [write(steps, item.r.step, "t", 1)], lo: -span, hi: Infinity };
      const [lo, hi] = reach(plan);
      const by = Math.min(hi, Math.max(lo, Math.round(ms) - span));
      const before = edit.begin();
      apply(plan, by);
      edit.commit(before);
    };

    // Where something dragged in from outside the view would land: the time
    // under `point` ({clientX, clientY}), on the nearest beat or block edge
    // within the tolerance, as a dragged block lands (beatAt() says whether
    // that time is a beat, for the caller to place what it inserts on it),
    // and never past the end step -
    // unless it is the end that is being dropped (`isEnd`), which may go on
    // past where it is. Null when the pointer is not over the lanes or
    // nothing here is being edited. While it is over them the landing line
    // shows where; aim(null) takes the line away.
    const aim = (point, isEnd = false) => {
      const was = aimed;
      aimed = null;
      if (point && editing() && !drag) {
        const track = ruler.getBoundingClientRect();
        const lanes = grid.getBoundingClientRect();
        const over = track.width > 0 && point.clientX >= track.left && point.clientX <= track.right &&
          point.clientY >= lanes.top && point.clientY <= lanes.bottom;
        if (over) {
          const at = Math.round(msFrom(point));
          const near = snapTargets({ writes: [] })
            .filter((target) => Math.abs(target.t - at) <= SNAP_MS)
            .sort((a, b) => Math.abs(a.t - at) - Math.abs(b.t - at))[0];
          const last = isEnd || model.end === -1 ? STEP_T_MAX_MS : model.endMs;
          const landed = Math.max(0, Math.min(last, near ? near.t : at));
          aimed = { t: landed, label: near && near.t === landed ? near.label : "" };
        }
      }
      if ((aimed && aimed.t) !== (was && was.t) || Boolean(aimed) !== Boolean(was)) {
        paintLanes();
        setMarker(t);
      }
      return aimed ? aimed.t : null;
    };

    const removeSelected = () => {
      const steps = seqNow().steps;
      const indices = steps.map((step, index) => (selection.has(step) ? index : -1)).filter((index) => index >= 0);
      const takes = (seqNow().takes || []).filter((entry) => selection.has(entry));
      if (indices.length === 0 && takes.length === 0) return;
      selection.clear();
      edit.remove(indices, takes);
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

    // What is said beside the pose press: the droid's answer to it, or the
    // caller's word on something the builder just tried here ({text, level}).
    const said = hosts.bar.querySelector(".tl-said");
    const say = (answer) => {
      said.hidden = !answer || !answer.text;
      said.textContent = answer && answer.text ? answer.text : "";
      said.className = `hint tl-said${answer && answer.level === "error" ? " is-refused" : ""}`;
    };

    // The pose press: one press, one request, at the marker's instant as it is
    // when pressed. The answer -- under way, or why not -- is said beside it.
    const pose = (button) => {
      const at = t;
      button.disabled = true;
      Promise.resolve(options.onPose(at))
        .then(say)
        .finally(() => {
          button.disabled = false;
        });
    };

    const barClick = (event) => {
      const target = event.target && event.target.closest ? event.target : null;
      const loopButton = target ? target.closest("[data-tl-loop]") : null;
      if (loopButton) {
        authored = loopButton.dataset.tlLoop === "authored";
        hosts.bar.querySelectorAll("[data-tl-loop]").forEach((button) =>
          button.setAttribute("aria-pressed", String((button.dataset.tlLoop === "authored") === authored)));
        paintLanes();
        setMarker(t);
        return;
      }
      const act = target ? target.closest("[data-tl-act]") : null;
      if (act && act.dataset.tlAct === "pose") pose(act);
    };
    hosts.bar.addEventListener("click", barClick);

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
      // The lanes are about to go out of sight with the view still mounted:
      // a drag under way is abandoned, as a pointercancel abandons it.
      cancel: () => dragCancel(),
      picked,
      // Pick the steps at these indices. What is picked is held as the steps
      // themselves, so an edit that writes new steps in their place - a beat
      // set or cleared re-resolves the routine - leaves nothing picked; the
      // editor that made the edit says which steps they are now.
      pick(indices) {
        const steps = seqNow().steps;
        selection.clear();
        indices.forEach((index) => {
          if (steps[index]) selection.add(steps[index]);
        });
        redraw();
      },
      movePickedTo,
      pickedRange,
      removePicked: removeSelected,
      aim,
      beatAt,
      standing,
      sizeStanding,
      say,
      destroy() {
        scrubEnd();
        dragAbandon();
        hosts.bar.removeEventListener("click", barClick);
        [hosts.bar, hosts.lanes, hosts.side].forEach((host) => {
          host.innerHTML = "";
        });
      },
    };
  };

  window.SeqTimeline = Object.freeze({ build, poseAt, leftOpen, unwired, notWired, mount, NUDGE_MS, NUDGE_BIG_MS, MIN_LENGTH_MS });
})();

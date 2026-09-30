// =============================================================================
// data/seq_timeline.js
//
// A saved sequence read as time (#440, ADR 0062, ADR 0057): a lane per Part,
// each move drawn for as long as it takes, and what the routine leaves open
// drawn on to the end. A marker moves over it and the droid picture beside it
// shows that moment.
//
// READ-ONLY, AND IT NEVER WRITES. Nothing here edits a sequence, and nothing
// here reaches the droid: there is no PAApi call in this file, and moving the
// marker only repaints the picture and the readout. The drawing follows the
// marker freely and silently; nothing follows a dragging finger (ADR 0062).
// The droid moves only on the separate press beside the marker, and even that
// is handed to the caller (`onPose`), which sends the one request -- a name
// and an instant -- and the firmware works out and paces the pose itself
// (POST /api/seq/pose, include/sequence_pose.h).
//
// ONE READING OF THE ROUTINE. The steps are expanded by the Rehearsal's own
// expand() and a body move is resolved by its bodyMove() (data/seq_rehearsal.js),
// so the timeline and the rules that judge a routine read it the same way. A
// body move is timed by the generated planner (data/servo_motion.js), the same
// arithmetic ServoTask runs. A dome panel's travel is the dome's to know -- the
// Rehearsal's dome-timing Gap -- so a panel move is drawn as the instant it is
// sent and never as though its time were measured.
//
// NO BEAT GRID. A grid comes only from a tempo stored on the sequence, and the
// saved format carries none (src/seq_json.cpp, format 1), so there is no grid
// at all today. The view never infers a tempo from step spacing and never
// holds one of its own (ADR 0062).
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
  // A lane also carries `changes`: [{t, at}] for every moment the routine
  // commands the Part to a position (at 0 closed, 1 fully open), which is what
  // poseAt() reads. A Part the routine has not yet moved has no change before
  // that moment, so it is not in the pose.
  // ---------------------------------------------------------------------------
  const build = (seq, context = {}) => {
    const rehearsal = window.SeqRehearsal;
    const motion = window.ServoMotion;
    const steps = Array.isArray(seq?.steps) ? seq.steps : [];
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
          state: { open: false, since: 0, sinceGhost: false, fromUs: null },
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

    // A dome panel or a body Part told to open, close or flutter.
    const openFrom = (lane, t, ghost) => {
      if (!lane.state.open) {
        lane.state.open = true;
        lane.state.since = t;
        lane.state.sinceGhost = ghost;
      }
    };
    const closeAt = (lane, t) => {
      if (lane.state.open) {
        add(lane, { kind: "open", t0: lane.state.since, t1: t, ghost: lane.state.sinceGhost });
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
      switch (def.type) {
        case "dome": {
          const panel = panelCommand(def.cmd);
          if (panel) {
            panel.ids.forEach((id) => {
              const lane = partLane(id);
              add(lane, { kind: "tick", t0: t, t1: t, label, ghost });
              if (panel.word === "open") {
                openFrom(lane, t, ghost);
                lane.changes.push({ t, at: 1 });
              } else if (panel.word === "close") {
                closeAt(lane, t);
                lane.changes.push({ t, at: 0 });
              } else {
                // A dome flutter has no length the body knows, and leaves
                // the panel where the dome leaves it.
                add(lane, { kind: "flutter", t0: t, t1: t, label, ghost });
              }
            });
            return;
          }
          const lit = lightCommand(def.cmd);
          if (lit) {
            lit.ids.forEach((id) => {
              lightEnd(id, t);
              const item = add(partLane(id), {
                kind: "light", t0: t, t1: lit.durationMs > 0 ? t + lit.durationMs : null, label, ghost,
              });
              light.set(id, item);
            });
            return;
          }
          // A dome visual preset holds until the next one or the end resets it.
          const row = rowLane("dome", "Dome");
          if (/^DV:/.test(String(def.cmd || ""))) {
            if (domeVisual.item && domeVisual.item.t1 === null) domeVisual.item.t1 = t;
            domeVisual.item = add(row, { kind: "span", t0: t, t1: null, label, ghost });
            return;
          }
          add(row, { kind: "tick", t0: t, t1: t, label, ghost });
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
            add(partLane(id), { kind: "maybe", t0: t, t1: t + reach, label: said, ghost });
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
            add(lane, { kind: "flutter", t0: t, t1: t + flutterMs, label, ghost });
            openFrom(lane, t + flutterMs, ghost);
          } else {
            if (travel > 0) add(lane, { kind: "move", t0: t, t1: t + travel, label, ghost });
            else add(lane, { kind: "tick", t0: t, t1: t, label, ghost });
            if (shape === "close") closeAt(lane, t);
            else openFrom(lane, t + travel, ghost);
          }
          const howFar = Number(def.howFar) || 100;
          lane.changes.push({ t, at: shape === "close" ? 0 : Math.min(100, Math.max(5, howFar)) / 100 });
          return;
        }
        case "audio":
        case "audioCat":
          add(rowLane("sound", "Sound"), { kind: "tick", t0: t, t1: t, label, ghost });
          return;
        case "domeRotate": {
          const durationMs = Number(def.durationMs) || 0;
          add(rowLane("spin", "Dome turn"), { kind: durationMs > 0 ? "span" : "tick", t0: t, t1: t + durationMs, label, ghost });
          return;
        }
        case "end":
          return;
        default:
          // A step kind this view does not know yet -- a Gesture, a nested
          // sequence -- still draws, as a labelled mark at its time. It is
          // never dropped.
          add(rowLane("other", "Other"), { kind: "tick", t0: t, t1: t, label: def.type || label, ghost });
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
      closeAt(lane, at);
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
        add(lane, { kind: "left", t0: lane.state.since, t1: windowMs, ghost: lane.state.sinceGhost });
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
      loops.push({ t: Number(step.t) || 0, period, passes, label: context.describe ? context.describe(step) : "loop" });
    });

    return {
      name: seq?.name || "",
      endMs,
      windowMs,
      loops,
      parts: partLanes,
      rows: ["sound", "dome", "spin", "other"].map((key) => rows.get(key)).filter(Boolean),
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

  const itemHtml = (item, windowMs, authored) => {
    const t1 = item.t1 === null || item.t1 === undefined ? item.t0 : item.t1;
    const ghost = authored && item.ghost ? " is-ghost" : "";
    const title = item.label ? `${item.label}, ${seconds(item.t0)}` : seconds(item.t0);
    const width = item.kind === "tick" ? "" : `;width:${pct(Math.max(0, t1 - item.t0), windowMs)}`;
    const text = item.kind !== "tick" && item.label ? `<span class="tl-label">${esc(item.label)}</span>` : "";
    return `<span class="tl-item tl-${item.kind}${ghost}" style="left:${pct(item.t0, windowMs)}${width}" title="${esc(title)}">${text}</span>`;
  };

  const loopItems = (model, authored) => {
    const items = [];
    model.loops.forEach((loop) => {
      if (authored) {
        items.push({ kind: "span", t0: loop.t, t1: loop.t + loop.passes * loop.period, label: loop.label });
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

  const laneHtml = (lane, windowMs, authored, dim) =>
    `<div class="tl-row${dim ? " is-unwired" : ""}" data-lane="${esc(lane.key)}">` +
    `<div class="tl-name">${lane.short ? `<span class="tl-short">${esc(lane.short)}</span>` : ""}` +
    `<span class="tl-part">${esc(lane.name)}</span></div>` +
    `<div class="tl-track">${lane.items.map((item) => itemHtml(item, windowMs, authored)).join("")}</div>` +
    `</div>`;

  // ---------------------------------------------------------------------------
  // mount() -- draw a sequence as a timeline into `host`.
  //
  // options:
  //   context   what the droid reported, as the Rehearsal reads it: outputs,
  //             config. Absent parts of it make the view say less, never guess.
  //   describe  step -> words, the editor's own preview (data/seq.js), so a
  //             block and a step card name a step alike
  //   cardsLabel the words on the button back to the card view
  //   onCards   the builder asked for the card view of this sequence
  //   onClose   the builder closed the timeline
  //   onPose    the builder pressed to send the droid to the marker's instant:
  //             called with it in ms, and returns a promise of {text, level}
  //             to show beside the press. Absent, there is no press.
  //
  // Returns {refresh(context), at(), destroy()}.
  // ---------------------------------------------------------------------------
  const mount = (host, seq, options = {}) => {
    let context = { ...(options.context || {}), describe: options.describe };
    let model = build(seq, context);
    let windowMs = model.windowMs;
    let authored = true;
    let t = 0;
    let ruler = null;
    let marker = null;

    const hasLoop = model.loops.length > 0;
    host.innerHTML =
      `<div class="card tl-view">` +
      `<div class="sect"><h2>Timeline</h2><span class="sub">${esc(model.name)} &middot; ${esc(seconds(model.endMs))}</span></div>` +
      `<div class="tl-bar">` +
      (hasLoop
        ? `<div class="seg tl-loop-mode" role="group" aria-label="How a loop is drawn">` +
          `<button type="button" data-tl-loop="authored" aria-pressed="true">As written</button>` +
          `<button type="button" data-tl-loop="expanded" aria-pressed="false">Expanded</button></div>`
        : "") +
      `<span class="tl-now" role="status" aria-live="polite"></span>` +
      `<span class="tl-acts">` +
      (typeof options.onPose === "function"
        ? `<button type="button" class="btn btn-sm accent" data-tl-act="pose">Move the droid to this moment</button>`
        : "") +
      `<button type="button" class="btn btn-sm" data-tl-act="cards">${esc(options.cardsLabel || "Edit steps")}</button>` +
      `<button type="button" class="btn btn-sm btn-quiet" data-tl-act="close">Close</button>` +
      `</span></div>` +
      `<p class="hint tl-said" role="status" aria-live="polite" hidden></p>` +
      `<p class="note note-act tl-unwired" hidden></p>` +
      `<div class="tl-stage">` +
      `<div class="tl-scroll"><div class="tl-grid"></div></div>` +
      `<div class="tl-side"><div class="tl-picture"></div><dl class="tl-readout"></dl></div>` +
      `</div></div>`;

    const grid = host.querySelector(".tl-grid");
    const now = host.querySelector(".tl-now");
    const readout = host.querySelector(".tl-readout");
    const unwiredNote = host.querySelector(".tl-unwired");
    const pictureHost = host.querySelector(".tl-picture");

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

    const paintLanes = () => {
      const loopLane = hasLoop
        ? laneHtml({ key: "loop", name: "Loop", short: "", items: loopItems(model, authored) }, windowMs, false, false)
        : "";
      // At least this many pixels a second, so a long routine scrolls rather
      // than crushing its blocks together.
      grid.setAttribute("style", `min-width:calc(var(--tl-lane-w) + ${Math.round((windowMs / 1000) * PX_PER_SECOND)}px)`);
      grid.innerHTML =
        `<div class="tl-row tl-ruler-row"><div class="tl-name">Time</div>` +
        `<div class="tl-track tl-ruler" role="slider" tabindex="0" aria-label="Moment" ` +
        `aria-valuemin="0" aria-valuemax="${Math.round(windowMs)}">${rulerHtml(windowMs)}</div></div>` +
        loopLane +
        model.parts.map((lane) => laneHtml(lane, windowMs, authored, dim.has(lane.part))).join("") +
        model.rows.map((row) => laneHtml(row, windowMs, authored, false)).join("") +
        `<div class="tl-overlay" aria-hidden="true">` +
        `<div class="tl-end" style="left:${pct(model.endMs, windowMs)}"></div>` +
        `<div class="tl-marker"></div></div>`;
      ruler = grid.querySelector(".tl-ruler");
      marker = grid.querySelector(".tl-marker");
      ruler.addEventListener("pointerdown", scrubStart);
      ruler.addEventListener("keydown", onKey);
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
      if (act.dataset.tlAct === "cards" && typeof options.onCards === "function") options.onCards();
      if (act.dataset.tlAct === "close" && typeof options.onClose === "function") options.onClose();
    });

    paintUnwired();
    paintLanes();
    setMarker(0);

    return {
      // The droid answered with its Outputs or its config after the view
      // opened: say again what is not wired, and redraw the moves it times.
      refresh(next) {
        context = { ...(next || {}), describe: options.describe };
        model = build(seq, context);
        windowMs = model.windowMs;
        paintUnwired();
        paintLanes();
        setMarker(t);
      },
      at: () => t,
      destroy() {
        scrubEnd();
        host.innerHTML = "";
      },
    };
  };

  window.SeqTimeline = Object.freeze({ build, poseAt, unwired, mount });
})();

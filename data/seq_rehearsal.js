// =============================================================================
// data/seq_rehearsal.js
//
// The Rehearsal: reads a sequence and says what will not happen as its author
// wrote it (ADR 0044, #287, #354, #439).
//
// It is NOT Protocol Check and never sits inside data/seq_protocol_check.js.
// That file mirrors the device's gate, and a mirror is only trustworthy while it
// mirrors; advice put inside it would force the two to diverge. So this module
// can never refuse anything: it has no error level, it is never asked before a
// save or a run, and nothing it returns disables a button.
//
// A finding is fields, not a sentence: {level, code, msg, fix}, plus the step
// and the subject it is about when it is about exactly one, and `n`, how many
// times it happens. `code` is protocol and `msg`/`fix` are copy (#298), so a
// finding can be counted by kind without reading prose. finding() refuses a
// level that is not one of the two, and a finding with no fix -- a finding with
// no fix is a complaint, and does not ship. Each rule reports ONE finding per
// subject, carrying the count, so one authoring mistake inside a loop is one
// line rather than one per iteration (r2d2-astromech-simulator v1.79.0,
// lint.js:266).
//
// Every rule here is paid for by a failure this project has had (#287's
// admission standard), and each names its receipt:
//   dispatch-spacing        2026-06-18: the dome's eight-entry command queue
//                           overflowed and silently dropped a :CL01.
//   servo-burst             2026-06-17: DM:LOW drove seven ring closes inside
//                           ~0.9 s and the dome browned out, twice.
//   group-panel             2026-06-17: ROCKMARCH's blanket :CL00 moved pie
//                           panels it never touched; they stalled until the
//                           droid was power-cycled.
//   retarget-before-arrival DM:HELLO's five identical :OP01 made one open, and a
//                           close 670 ms after its open left P1 open (#287).
//   quiet-in-sequence       2026-06-17: DM:ROCKMARCH's $s muted idle chatter
//                           until reboot on every normal completion.
//   raw-light-code          2026-06-17/-18: ROCKMARCH's raw @0T11/@0P11 left the
//                           logics default blue against the dome's red MARCH.
//   switched-off            #170/#171/#172: a step aimed at hardware switched
//                           off is a silent no-op, with nothing telling you.
//   dome-unavailable        2026-08-04: five pies disabled for a linkage fault.
//   body-overlap            ADR 0049: the Cadence Floor paces only what the body
//                           generates, so hand-written overlaps are advised on.
//   part-left-open          ADR 0049: the engine undoes nothing a body step did.
//   flutter-cut             #453: the droid ends a flutter at its run's end
//                           step and commands nothing there, so a flutter
//                           still going is shorter than it was written.
//   audio-outlives-show     #16: DM:VADER's $M played on for three minutes.
//   dome-how-far            ADR 0046: how far is resolved by the dome, and
//                           our fork has no part-way move for PP3 and PP5.
//   gesture-cut             #438: the droid ends a Gesture at its run's end
//                           step, mid-pass, so terminal cleanup is the last
//                           thing the run moves.
//   gesture-dome            ADR 0046: a dome Gesture is the dome's `$` command,
//                           and a pair the dome has no command for saves.
//   tempo-confidence        ADR 0058: the analyzer read Cantina's ~200 BPM as
//                           127.8; a tempo that says how unsure it is must say
//                           so where the builder looks.
//   tempo-hash              ADR 0058: a sound is named as a role, so the track
//                           behind a tempo can change with the sequence
//                           untouched.
//
// One computation behind three appearances (#287 specific 6): the figures in
// the editor, the full list at save and at clone, and a badge beside a run.
// =============================================================================

(() => {
  const LEVELS = Object.freeze(["warning", "note"]);

  // Dome commands closer than this can pile up in the dome's command queue.
  // The figure is the catalog's own rule, written after the 2026-06-18 drop
  // (src/tasks/sequence_catalog.cpp, DM:ROCKMARCH).
  const DOME_SPACING_MS = 200;

  // A step no timing rule can judge from here, and what would change that.
  // These are Rehearsal Gaps, not findings: they owe the author the truth rather
  // than a fix (CONTEXT.md).
  const GAPS = Object.freeze({
    "dome-timing": {
      msg: "Only the dome knows how long its panels take, so a panel move cannot be timed here.",
      closes: "It could be, if the dome published its panel times.",
    },
    "body-timing": {
      msg: "Only a part on a calibrated output can be timed here.",
      closes: "Calibrate the output it is on, with the droid connected.",
    },
    "phrase": {
      msg: "A sequence inside this one is read when it runs, not here.",
      closes: "Open that sequence to see what it does.",
    },
    "random-pick": {
      msg: "A random step picks its panel at run time. Nothing fixed to check.",
      closes: "Nothing closes this; the pick is the point.",
    },
  });

  const escapeHtml = (value) =>
    String(value ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");

  const seconds = (ms) => `${Number((ms / 1000).toFixed(2))} s`;
  const plural = (n, one, many) => `${n} ${n === 1 ? one : many}`;

  // The motion model, generated from docs/servo-motion.yaml beside the
  // firmware's own planner (data/servo_motion.js, #439). Read when a rule runs,
  // so the load order of the page's modules does not matter.
  const motion = () => window.ServoMotion || null;

  // The dome's measured cadence between panel moves: 450 ms, the figure the
  // 2026-06-17 brownout gave (include/sequence_bulk_centre.h). It is what the
  // rules about dome panels judge by. It is not the body's Cadence Floor,
  // which a builder may set: a floor set for the body's Outputs does not
  // change what browned the dome out.
  const domeCadenceMs = () => motion()?.SEQ_CADENCE_FLOOR_MS ?? null;

  // The Cadence Floor: the least time the body holds between two Outputs it
  // starts itself. The droid says its own (GET /api/config,
  // `servo.cadenceFloorMs`) and whose figure it is (`servo.cadenceFloorSource`):
  // the dome's, which the body adopts as a stand-in because nobody has
  // measured the body's own, until a builder sets another (#453). Until the
  // droid has answered, the generated default, which is the dome's. As
  // {ms, dome}, or null. Any sentence that quotes it says whose it is.
  const cadenceFloor = (context) => {
    const config = context?.config;
    const servo = config && typeof config.servo === "object" && config.servo ? config.servo : {};
    const ms = Number(servo.cadenceFloorMs);
    if (Number.isFinite(ms) && ms > 0) return { ms, dome: servo.cadenceFloorSource === "dome" };
    const generated = domeCadenceMs();
    return generated === null ? null : { ms: generated, dome: true };
  };

  // What a builder calls a Part: its catalog name, or the id where the catalog
  // does not know it.
  const partName = (id) => {
    const parts = window.DroidParts && Array.isArray(window.DroidParts.parts) ? window.DroidParts.parts : [];
    const entry = parts.find((part) => part.id === id);
    return entry && entry.name ? entry.name : id;
  };

  // ---------------------------------------------------------------------------
  // finding() -- the only way a finding is made.
  // ---------------------------------------------------------------------------
  const finding = (level, code, msg, fix, subject = {}) => {
    if (!LEVELS.includes(level)) {
      throw new TypeError(`A Rehearsal finding is a warning or a note, never "${level}"`);
    }
    if (!code || !msg || !fix) {
      throw new TypeError(`Rehearsal finding "${code}" needs a code, a message and a fix`);
    }
    const out = { level, code, msg, fix };
    if (typeof subject.step === "number") out.step = subject.step;
    if (subject.part) out.part = subject.part;
    if (subject.element) out.element = subject.element;
    if (subject.output) out.output = subject.output;
    if (typeof subject.n === "number") out.n = subject.n;
    return out;
  };

  // One finding per (rule, subject): occurrences are gathered under a key and
  // the group keeps its first, so the count survives as `n` on the finding.
  const grouped = () => {
    const groups = new Map();
    return {
      add(key, occurrence) {
        const group = groups.get(key) || { n: 0, first: occurrence, all: [] };
        group.n += 1;
        group.all.push(occurrence);
        groups.set(key, group);
      },
      each: (make) => [...groups.values()].map(make),
    };
  };

  // ---------------------------------------------------------------------------
  // expand() -- every step as it fires, at its absolute time.
  //
  // A loop's body runs once per period while the iteration start is inside the
  // loop's duration, and its steps are timed from the iteration start -- the
  // engine's own arithmetic (stepFireAt(), src/tasks/sequence_engine.cpp).
  // An event from a loop body carries `iter`, which pass of the loop it is
  // (0 for the first), so the timeline can draw the loop as it was written
  // over the same expansion the rules read (data/seq_timeline.js).
  // ---------------------------------------------------------------------------
  const expand = (steps) => {
    const events = [];
    let i = 0;
    while (i < steps.length) {
      const step = steps[i] || {};
      if (step.type === "loop" && typeof step.body === "number" && step.body > 0) {
        const count = Math.min(step.body, steps.length - i - 1);
        const period = Number(step.periodMs) || 0;
        const duration = Number(step.durationMs) || 0;
        for (let start = 0, iter = 0; period > 0 && start < duration; start += period, iter += 1) {
          for (let k = 1; k <= count; k += 1) {
            const inner = steps[i + k] || {};
            events.push({ t: (Number(step.t) || 0) + start + (Number(inner.t) || 0), step: i + k, def: inner, iter });
          }
        }
        i += count + 1;
      } else {
        events.push({ t: Number(step.t) || 0, step: i, def: step });
        i += 1;
      }
    }
    // A Gesture also reads as the moves it becomes (ADR 0046), each marked
    // `generated`: a body Gesture's moves as the Coordinator asks for them
    // before pacing, and a dome Gesture's panels where the dome's `$` command
    // leaves them. The Part rules read them; the rules about an author's own
    // timing do not, because the body paces what it generates and the dome
    // performs its command as one (data/seq_gesture.js).
    const G = window.SeqGesture;
    if (G) {
      events.slice().forEach((event) => {
        if (event.def.type !== "gesture") return;
        const t = event.t;
        const meta = { step: event.step, iter: event.iter, generated: true };
        G.bodyMoves(event.def, t).forEach((move) =>
          events.push({
            ...meta,
            t: move.t,
            def: {
              type: "body", part: move.part, shape: move.shape, howFar: event.def.howFar, speedMs: event.def.speedMs, easing: event.def.easing,
              ...(move.shape === "flutter" ? { flutterMs: gestureFlutterMs(event.def) } : {}),
            },
          }),
        );
        domePanelsOf(event.def).forEach((cmd) => events.push({ ...meta, t, def: { type: "dome", cmd } }));
      });
    }
    // Stable: equal times keep authored order, which is the engine's order too.
    return events
      .map((event, order) => ({ ...event, order }))
      .sort((a, b) => a.t - b.t || a.order - b.order);
  };

  // How long each member of a flutter Gesture flutters: what the Gesture
  // states, or one step of its own pace when it states none
  // (seqGestureFlutterMs(), include/sequence_gesture.h).
  const gestureFlutterMs = (def) =>
    Number(def?.flutterMs) || Number(def?.stepMs) || window.SeqGesture?.STEP_DEFAULT_MS || 0;

  // A dome Gesture's panels as its `$` command leaves them: a together open or
  // close as written, a flutter as the dome's to know (nothing), every other
  // command the dome performs ends its panels closed, and a pair with no
  // command moves nothing (include/sequence_gesture.h).
  //
  // Only the members the dome has an address for: one it has none for is in
  // no `$` command and is not moved (domePartTarget(),
  // include/sequence_pose.h). Its number is not a panel's either - panel 14
  // would spell `:OP14`, which is the pie group's command.
  const domePanelsOf = (def) => {
    const G = window.SeqGesture;
    if (!G || !G.onDome(def)) return [];
    const shape = def.shape || "open";
    const spread = def.spread || "together";
    if (!G.domeCommand(shape, spread) || (spread === "together" && shape === "flutter")) return [];
    const word = spread === "together" && shape === "open" ? "OP" : "CL";
    return G.members(def)
      .filter((id) => G.domeAddressed(id))
      .map((id) => {
        const panel = /^panel(\d+)$/.exec(id);
        const pie = /^pie(\d)$/.exec(id);
        if (panel) return `:${word}${String(panel[1]).padStart(2, "0")}`;
        return pie ? `:${word}P${pie[1]}` : null;
      })
      .filter((cmd) => cmd && panelIntent(cmd));
  };

  // A panel intent (:OP/:CL/:OF) split into what it does and to which panel.
  const panelIntent = (cmd) => {
    const match = /^:(OP|CL|OF)([0-9A-Z]{2})$/.exec(String(cmd || ""));
    return match ? { word: match[1], target: match[2] } : null;
  };

  const panelName = (target) => (/^P\d$/.test(target) ? `PP${target.slice(1)}` : `P${Number(target)}`);
  const panelVerb = { OP: "open", CL: "close", OF: "flutter" };

  // The panels a group target moves, from the body's own command map
  // (data/dome_command_map.js): every ring panel, every pie, or both.
  const GROUP_WORDS = { all: "every panel", ring: "every ring panel", pie: "every pie panel" };
  const groupOf = (cmd) => {
    const decoded = window.DomeCommandMap?.decodeCommandToElement?.(cmd);
    return decoded && decoded.kind === "group" ? decoded.id : null;
  };
  const groupMembers = (group) => {
    const targets = window.DomeCommandMap?.PANEL_COMMAND_TARGETS || {};
    const ring = Object.keys(targets.ring || {});
    const pie = Object.keys(targets.pie || {});
    return group === "ring" ? ring : group === "pie" ? pie : [...ring, ...pie];
  };

  // ---------------------------------------------------------------------------
  // The body: which Output carries a Part, and where a move of it lands.
  //
  // `outputs` are the rows GET /api/servo/outputs answers, as data/outputs.js
  // reads them. What follows reads a row the way the firmware does, and says at
  // each step which firmware function it follows; the motion itself is the
  // generated planner's, never worked out here.
  // ---------------------------------------------------------------------------
  const outputOf = (part, context) =>
    (Array.isArray(context.outputs) ? context.outputs : []).find(
      (output) => Array.isArray(output.parts) && output.parts.includes(part),
    ) || null;

  // A light has no ends and no travel: it is never timed and never named as
  // unmeasured (#287 second pass: "inapplicable is silent").
  const isServo = (output) => !output.light;

  const numeric = (value) => typeof value === "number" && Number.isFinite(value);

  // servoMotionProfileOf() (include/servo_motion_ramp.h): the pair ordered by
  // servoOutputLowUs()/servoOutputHighUs(), and the ease that runs --
  // servoOutputEffectiveEasing() degrades an overshoot on an Output nobody has
  // measured to `none` (include/servo_output_row.h).
  const profileOf = (output) => {
    const model = motion();
    if (!model || !numeric(output.openUs) || !numeric(output.closeUs) ||
        !numeric(output.throwMs) || !numeric(output.accelMs)) {
      return null;
    }
    const ease = model.ServoEasing;
    const words = { none: ease.SERVO_EASE_NONE, soft: ease.SERVO_EASE_SOFT, overshoot: ease.SERVO_EASE_OVERSHOOT };
    let easing = words[output.ease] ?? ease.SERVO_EASE_NONE;
    if (easing === ease.SERVO_EASE_OVERSHOOT && !output.calibrated) easing = ease.SERVO_EASE_NONE;
    return {
      loUs: Math.min(output.openUs, output.closeUs),
      hiUs: Math.max(output.openUs, output.closeUs),
      throwMs: output.throwMs,
      accelMs: output.accelMs,
      easing,
      calibrated: output.calibrated === true,
    };
  };

  // A Gesture's move may state its own full-throw time and easing, which run in
  // place of the Output's for that move (servoMotionOverride(),
  // include/servo_motion_ramp.h); an overshoot still never passes an
  // unmeasured Output's ends.
  const overridden = (profile, def) => {
    if (!profile) return profile;
    const model = motion();
    const out = { ...profile };
    if (Number(def.speedMs) > 0) out.throwMs = Number(def.speedMs);
    const ease = model?.ServoEasing;
    const words = ease ? { none: ease.SERVO_EASE_NONE, soft: ease.SERVO_EASE_SOFT, overshoot: ease.SERVO_EASE_OVERSHOOT } : {};
    if (def.easing in words) {
      out.easing = def.easing === "overshoot" && !out.calibrated ? ease.SERVO_EASE_NONE : words[def.easing];
    }
    return out;
  };

  // seqBodyHowFar() (include/sequence_engine.h): absent is the whole throw, and
  // a stated value is floored at 5 and capped at 100.
  const howFarOf = (def) => {
    const stated = Number(def.howFar) || 0;
    if (stated === 0) return 100;
    return Math.min(100, Math.max(5, stated));
  };

  // seqBodyTargetUs() (include/sequence_body_step.h): how far is measured along
  // the shape's own direction of travel, rounded half away from zero on a
  // reversed pair too. A flutter resolves to the closed end, whatever its how
  // far, because a flutter ends closed (ADR 0049, amended 2026-10-02): this
  // is where it leaves the Part, not the swing on the way. Then the row's
  // component band bounds it (servoOutputClampPulse()).
  const targetOf = (output, shape, howFar) => {
    const span = output.openUs - output.closeUs;
    const bias = span >= 0 ? 50 : -50;
    const travelled = Math.trunc((span * howFar + bias) / 100);
    const target = shape === "flutter" ? output.closeUs
      : shape === "close" ? output.openUs - travelled : output.closeUs + travelled;
    const bounded = Math.min(0xffff, Math.max(0, target));
    return output.bandHiUs > 0 ? Math.min(output.bandHiUs, Math.max(output.bandLoUs, bounded)) : bounded;
  };

  // A body step, with what the droid in front of the author would do with it.
  // `timed` is the criterion #439 sets: a Part on a calibrated servo Output.
  const bodyMove = (def, context) => {
    const output = outputOf(def.part, context);
    const shape = def.shape || "open";
    const profile = output && isServo(output) ? overridden(profileOf(output), def) : null;
    const timed = Boolean(profile && profile.calibrated);
    return {
      part: def.part,
      shape,
      output,
      profile,
      timed,
      targetUs: timed ? targetOf(output, shape, howFarOf(def)) : null,
    };
  };

  // ---------------------------------------------------------------------------
  // The rules. Each reads the expanded events and returns findings.
  // ---------------------------------------------------------------------------

  // Consecutive pairs closer than `limitMs`, and the tightest of them.
  const tightPairs = (events, limitMs, same = () => false) => {
    let tight = 0;
    let worst = null;
    for (let k = 1; k < events.length; k += 1) {
      if (same(events[k - 1], events[k])) continue;
      const gap = events[k].t - events[k - 1].t;
      if (gap < limitMs) {
        tight += 1;
        if (!worst || gap < worst.gap) worst = { gap, before: events[k - 1], after: events[k] };
      }
    }
    return { tight, worst };
  };

  const pairWords = (worst, name) =>
    worst.gap === 0
      ? `${name(worst.before)} and ${name(worst.after)} both at ${seconds(worst.after.t)}`
      : `${name(worst.after)} ${worst.gap} ms after ${name(worst.before)}`;

  const dispatchSpacing = (events) => {
    const dome = events.filter((event) => event.def.type === "dome" && !event.generated);
    const { tight, worst } = tightPairs(dome, DOME_SPACING_MS);
    if (!worst) return [];
    const when =
      worst.gap === 0
        ? `${worst.before.def.cmd} and ${worst.after.def.cmd} both leave at ${seconds(worst.after.t)}`
        : `${worst.after.def.cmd} leaves ${worst.gap} ms after ${worst.before.def.cmd}`;
    return [
      finding(
        "warning",
        "dispatch-spacing",
        `${tight} dome ${tight === 1 ? "command follows" : "commands follow"} the one before by less than ${DOME_SPACING_MS} ms -- ${when}. The dome holds eight and drops the rest, which can leave a panel open.`,
        `Space dome commands at least ${DOME_SPACING_MS} ms apart.`,
        { step: worst.after.step, n: tight },
      ),
    ];
  };

  // Panel moves closer than the dome's measured cadence: what browned the dome
  // out was the burst, never a single close.
  const servoBurst = (events) => {
    const floor = domeCadenceMs();
    if (floor === null) return [];
    const panels = events.filter((event) => event.def.type === "dome" && panelIntent(event.def.cmd) && !event.generated);
    const { tight, worst } = tightPairs(panels, floor);
    if (!worst) return [];
    return [
      finding(
        "warning",
        "servo-burst",
        `${plural(tight, "panel move follows", "panel moves follow")} the one before by less than ${floor} ms -- ${pairWords(worst, (e) => e.def.cmd)}. The dome browned out driving panels this close together.`,
        `Space panel moves at least ${floor} ms apart.`,
        { step: worst.after.step, n: tight },
      ),
    ];
  };

  // A group target moves every member at once -- and the members this sequence
  // never moves on its own are the ones nobody expected to move.
  const groupPanel = (events) => {
    const touched = new Set();
    events.forEach((event) => {
      if (event.def.type !== "dome") return;
      const decoded = window.DomeCommandMap?.decodeCommandToElement?.(event.def.cmd);
      if (decoded && decoded.kind !== "group") touched.add(decoded.id);
    });
    const groups = grouped();
    events.forEach((event) => {
      if (event.def.type !== "dome") return;
      const group = groupOf(event.def.cmd);
      if (group) groups.add(event.def.cmd, { event, group });
    });
    const floor = domeCadenceMs();
    return groups.each(({ n, first }) => {
      const untouched = groupMembers(first.group).filter((id) => !touched.has(id));
      // A few are named; more than that is a count, or the sentence is a list.
      const named = untouched.length <= 3 ? untouched.join(", ") : `${untouched.length} panels`;
      const wider = untouched.length ? `, including ${named} nothing else here moves` : "";
      return finding(
        "warning",
        "group-panel",
        `${first.event.def.cmd} moves ${GROUP_WORDS[first.group]} at once${wider}${n > 1 ? ` (${n} times)` : ""}.`,
        floor === null ? "Move the panels you mean one at a time." : `Move the panels you mean one at a time, ${floor} ms apart.`,
        { step: first.event.step, element: first.group, n },
      );
    });
  };

  // The re-target: a Part or a panel told to go somewhere before it can have
  // got where it was going.
  //
  // Two halves. The identical re-issue -- the same open or close, to the same
  // subject, with nothing else sent to it in between -- moves nothing, and is
  // judged on every subject. A reversal needs a travel time: on a body Part
  // whose Output is calibrated that is the generated planner's, and the unit
  // judged is the RUN, not the step -- re-targets the same way on extend one
  // move, and only a turn back is measured, against the time the run it cuts
  // short needs (r2d2-astromech-simulator v1.79.0, lint.js:109). A dome panel's
  // travel is the dome's, so its reversal half stays the dome-timing Gap.
  const retargetBeforeArrival = (events, context) => {
    const end = events.find((event) => event.def.type === "end");
    const endT = end ? end.t : Infinity;
    const last = new Map();
    const runs = new Map();
    const groups = grouped();
    events.forEach((event) => {
      // A generated move waits for the one before to arrive: the Coordinator
      // paces it (include/sequence_gesture.h).
      if (event.generated) return;
      const def = event.def;
      let key = null;
      let command = null;
      let label = null;
      if (def.type === "dome") {
        const intent = panelIntent(def.cmd);
        if (!intent) return;
        key = `dome:${intent.target}`;
        command = intent.word === "OF" ? null : `${intent.word}`;
        label = { element: panelName(intent.target), verb: panelVerb[intent.word] };
      } else if (def.type === "body") {
        const shape = def.shape || "open";
        key = `body:${def.part}`;
        command = shape === "flutter" ? null : `${shape}:${def.howFar || 100}`;
        label = { part: def.part, verb: shape };
      } else {
        return;
      }
      if (command !== null && last.get(key) === command) {
        groups.add(key, { event, label, kind: "repeat" });
      }
      last.set(key, command);

      if (def.type !== "body") return;
      const move = bodyMove(def, context);
      if (!move.timed) {
        runs.delete(key);
        return;
      }
      const SM = motion();
      if (move.shape === "flutter") {
        // A flutter is no single move: it ends closed (ADR 0049, amended
        // 2026-10-02) - `targetUs` is the closed end, the end
        // seqBodyTargetUs() measures how far from - when its length has run,
        // or at the end step where that comes first
        // (include/sequence_flutter.h "HOW IT ENDS").
        const over = Math.max(event.t, Math.min(event.t + (Number(def.flutterMs) || 0), endT));
        runs.set(key, { from: move.targetUs, target: move.targetUs, dir: 0, start: over });
        return;
      }
      // Where the Part starts is where the move before left it; the first move
      // of a routine starts from the end its shape points away from, the end
      // seqBodyTargetUs() measures how-far from.
      const run = runs.get(key) || {
        from: move.shape === "close" ? move.output.openUs : move.output.closeUs,
        target: move.shape === "close" ? move.output.openUs : move.output.closeUs,
        dir: 0,
        start: event.t,
      };
      runs.set(key, run);
      if (move.targetUs === run.target) return;
      const dir = Math.sign(move.targetUs - run.target);
      if (run.dir === dir) {
        run.target = move.targetUs;
        return;
      }
      if (run.dir !== 0) {
        const need = SM.servoMotionArrivalMs(run.from, run.target, move.profile);
        const since = event.t - run.start;
        if (since < need) groups.add(key, { event, label, kind: "reversal", need, since });
      }
      runs.set(key, { from: run.target, target: move.targetUs, dir, start: event.t });
    });
    return groups.each(({ n, first, all }) => {
      const { label } = first;
      const who = label.element || partName(label.part);
      const reversal = all.find((occurrence) => occurrence.kind === "reversal");
      const times = n > 1 ? `, ${n} times` : "";
      const subject = label.element ? { element: label.element } : { part: label.part };
      if (reversal) {
        return finding(
          "warning",
          "retarget-before-arrival",
          `${who} turns back at ${seconds(reversal.event.t)}, ${reversal.since} ms into a move that takes ${reversal.need} ms${times}. It never gets there.`,
          `Give that move ${reversal.need} ms before the next one, or make it shorter.`,
          { step: reversal.event.step, n, ...subject },
        );
      }
      return finding(
        "warning",
        "retarget-before-arrival",
        `${who} is told to ${label.verb} again at ${seconds(first.event.t)} with nothing in between${times}. It is already on its way, so the repeat moves nothing.`,
        `Delete the repeated ${label.verb}, or put the opposite move between them if ${who} should move twice.`,
        { step: first.event.step, n, ...subject },
      );
    });
  };

  const quietInSequence = (events) => {
    const hits = events.filter((event) => event.def.type === "audio" && event.def.cmd === "$s");
    if (hits.length === 0) return [];
    return [
      finding(
        "warning",
        "quiet-in-sequence",
        "$s stops the sound, and idle chatter too, until the droid is switched off and on.",
        "Delete the $s step. A sound stops by itself when it ends, and chatter carries on.",
        { step: hits[0].step, n: hits.length },
      ),
    ];
  };

  // A raw logic, PSI or holo code the dome draws in its default colors, where a
  // Visual preset draws the dome's own. Raw text (@nM) is not one: no preset
  // carries text.
  const isRawLightCode = (cmd) => /^\*/.test(cmd) || (/^@/.test(cmd) && !/^@\d+M/.test(cmd));

  const rawLightCode = (events) => {
    const groups = grouped();
    events.forEach((event) => {
      if (event.def.type === "dome" && isRawLightCode(String(event.def.cmd || ""))) {
        groups.add(event.def.cmd, { event });
      }
    });
    return groups.each(({ n, first }) =>
      finding(
        "warning",
        "raw-light-code",
        `${first.event.def.cmd} is a raw light code, so the dome draws it in its default colors${n > 1 ? ` (${n} times)` : ""}.`,
        "Use a Visual preset step instead.",
        { step: first.event.step, n },
      ),
    );
  };

  // A step aimed at hardware this droid has switched off does nothing at all.
  // The toggles are GET /api/config's components; a body Part's is the wired
  // tick on the Output it is on (data/outputs.js). Nothing is said about a
  // toggle that was not read.
  const SWITCHES = [
    { types: ["dome", "random"], key: "protoR2link", what: "The dome link", steps: ["dome step", "dome steps"] },
    { types: ["audio", "audioCat"], key: "audio", what: "Sound", steps: ["sound step", "sound steps"] },
    { types: ["domeRotate"], key: "domeEsc", what: "The dome motor", steps: ["dome turn", "dome turns"] },
  ];

  const switchedOff = (events, context) => {
    const components = context.config?.components || null;
    const groups = grouped();
    events.forEach((event) => {
      const def = event.def;
      if (def.type === "body") {
        const output = outputOf(def.part, context);
        if (output && output.wired === false) groups.add(`body:${def.part}`, { event, output, part: def.part });
        return;
      }
      const toggle = SWITCHES.find((entry) => entry.types.includes(def.type));
      if (toggle && components?.[toggle.key]?.enabled === false) groups.add(toggle.key, { event, toggle });
    });
    return groups.each(({ n, first }) => {
      if (first.part) {
        const name = partName(first.part);
        return finding(
          "warning",
          "switched-off",
          `${name} is on ${first.output.name}, which is not wired, so ${n === 1 ? "its step does" : `its ${n} steps do`} nothing.`,
          `Wire ${first.output.name} on the Wiring page, or delete the ${n === 1 ? "step" : "steps"}.`,
          { step: first.event.step, part: first.part, output: first.output.name, n },
        );
      }
      const { toggle } = first;
      return finding(
        "warning",
        "switched-off",
        `${toggle.what} is off on this droid, so ${plural(n, toggle.steps[0], toggle.steps[1])} ${n === 1 ? "does" : "do"} nothing.`,
        `Turn ${toggle.what.toLowerCase()} on in Configuration, or delete the ${n === 1 ? "step" : "steps"}.`,
        { step: first.event.step, n },
      );
    });
  };

  // ---------------------------------------------------------------------------
  // unavailableMessage() -- what the editor says about a dome element the
  // connected dome reports it cannot move, or null when it can.
  //
  // The state clause is data/dome_layout.js's, where severity is computed and
  // where the Dashboard's dome control reads it too (#348). This adds the
  // authoring consequence. It is the ONE source of these words in the editor:
  // the inline message beside a step and the Rehearsal's finding both come from
  // here, so the two cannot drift (#287 first pass, #439).
  // ---------------------------------------------------------------------------
  const unavailableMessage = (elementId, layout) => {
    if (!elementId) return null;
    const elem = layout?.elements?.find((e) => e.id === elementId);
    if (!elem) return null;
    const clause = window.DomeLayout?.severityClause?.(elem);
    if (!clause) return null;
    // An element out of the selected layout that the dome says is active is a
    // disagreement worth naming as one, not as either state.
    if (elem.in_layout === false && elem.active === true) {
      return `${elementId} is out of the selected layout, but the dome says it is active.`;
    }
    // An element nothing maps to cannot be authored at all; every other
    // severity still writes a step, and the step still runs.
    return elem.severity === "unmapped" ? clause : `${clause} The step still runs.`;
  };

  const domeUnavailable = (events, context) => {
    const groups = grouped();
    events.forEach((event) => {
      if (event.def.type !== "dome") return;
      const decoded = window.DomeCommandMap?.decodeCommandToElement?.(event.def.cmd);
      if (!decoded || (decoded.kind !== "ring" && decoded.kind !== "pie")) return;
      const message = unavailableMessage(decoded.id, context.layout);
      if (message) groups.add(decoded.id, { event, id: decoded.id, message });
    });
    return groups.each(({ n, first }) =>
      finding(
        "warning",
        "dome-unavailable",
        n > 1 ? `${first.message} (${n} steps)` : first.message,
        `Pick another panel, or bring ${first.id} back on the dome.`,
        { step: first.event.step, element: first.id, n },
      ),
    );
  };

  // Hand-written body moves on different Parts, started closer together than
  // the Cadence Floor. The floor paces only what the body generates itself
  // (ADR 0049); an author's own timing is advised on and never rewritten, and
  // the sentence says whose number it is: the dome's, or the one set on this
  // droid.
  const bodyOverlap = (events, context) => {
    const cadence = cadenceFloor(context);
    if (cadence === null) return [];
    const floor = cadence.ms;
    const whose = cadence.dome
      ? `${floor} ms is the dome's measured spacing; the body's own is unmeasured.`
      : `${floor} ms is the spacing set on this droid.`;
    const moves = events.filter((event) => event.def.type === "body" && event.def.part && !event.generated);
    const { tight, worst } = tightPairs(moves, floor, (a, b) => a.def.part === b.def.part);
    if (!worst) return [];
    return [
      finding(
        "warning",
        "body-overlap",
        `${plural(tight, "body move starts", "body moves start")} less than ${floor} ms after the one before -- ${pairWords(worst, (e) => partName(e.def.part))}. ${whose}`,
        "Spread the moves out, one part at a time.",
        { step: worst.after.step, n: tight },
      ),
    ];
  };

  const partLeftOpen = (events) => {
    const lastShape = new Map();
    events.forEach((event) => {
      if (event.def.type === "body" && event.def.part) {
        lastShape.set(event.def.part, { shape: event.def.shape || "open", step: event.step });
      }
    });
    // Only an open leaves it open: a flutter ends closed (ADR 0049, amended
    // 2026-10-02).
    return [...lastShape.entries()]
      .filter(([, last]) => last.shape === "open")
      .map(([part, last]) =>
        finding(
          "note",
          "part-left-open",
          `${partName(part)} is still open when the sequence ends, and the body leaves it that way -- nothing closes it for you.`,
          `Add a close for ${partName(part)} near the end, unless it is meant to stay open.`,
          { step: last.step, part },
        ),
      );
  };

  // A flutter still going when the end step comes. The droid ends a flutter
  // there and commands nothing: it starts no swing that would not be back
  // first, so the Part is closed, and the flutter is shorter than it was
  // written (include/sequence_flutter.h "HOW IT ENDS"). A Note, not a
  // Warning: nothing is left open. One line for a Part a Body Step flutters,
  // and one for a body Gesture, whichever of its members are cut. A flutter
  // that starts at or after the end never runs, and a Gesture's is
  // gesture-cut's to say.
  const flutterCut = (events, steps) => {
    const end = steps.find((step) => step && step.type === "end");
    if (!end) return [];
    const endT = Number(end.t) || 0;
    const groups = grouped();
    events.forEach((event) => {
      const def = event.def;
      if (def.type !== "body" || !def.part || def.shape !== "flutter") return;
      if (!(event.t < endT && event.t + (Number(def.flutterMs) || 0) > endT)) return;
      if (event.generated) groups.add(`gesture:${event.step}`, { event });
      else groups.add(`part:${def.part}`, { event, part: def.part });
    });
    return groups.each(({ n, first }) =>
      finding(
        "note",
        "flutter-cut",
        first.part
          ? `${partName(first.part)}'s flutter is cut short by the end${n > 1 ? ` (${n} times)` : ""}.`
          : "This gesture's flutter is cut short by the end.",
        "Move the end later, or make the flutter shorter.",
        first.part ? { step: first.event.step, part: first.part, n } : { step: first.event.step },
      ),
    );
  };

  // A named track that rings out past the show. A Note, not a Warning: it plays
  // exactly as written. A sound category (audioCat) rings out on purpose
  // (ADR 0010), so it is never counted. A sound picked in this editor carries
  // no boundAudio, which the droid reads as bounded.
  const audioOutlivesShow = (events) => {
    const groups = grouped();
    events.forEach((event) => {
      if (event.def.type === "audio" && event.def.boundAudio === false && event.def.cmd !== "$s") {
        groups.add(event.def.cmd, { event });
      }
    });
    return groups.each(({ n, first }) =>
      finding(
        "note",
        "audio-outlives-show",
        `${first.event.def.cmd} keeps playing after the sequence ends${n > 1 ? ` (${n} steps)` : ""}.`,
        "Pick the sound again here: a sound picked in this editor stops when the sequence ends.",
        { step: first.event.step, n },
      ),
    );
  };

  // A dome panel move that says how far, on a panel our fork has no part-way
  // move for (PP3 and PP5, include/sequence_dome_how_far.h): it goes all the
  // way, and saying so is what the author needs.
  const domeHowFar = (events) =>
    events
      .filter((event) => event.def.type === "dome" && !event.generated && Number(event.def.howFar) > 0 && Number(event.def.howFar) < 100)
      .filter((event) => /^:(OP|CL)P[35]$/.test(String(event.def.cmd || "")) && !(event.iter > 0))
      .map((event) =>
        finding(
          "warning",
          "dome-how-far",
          `${panelName(event.def.cmd.slice(3))} has no part-way move on the dome, so it goes all the way.`,
          "Use the full move, or a panel the dome can stop part way.",
          { step: event.step, element: panelName(event.def.cmd.slice(3)) },
        ),
      );

  // A Gesture whose moves do not all fit before the end step. The droid ends a
  // Gesture there, mid-pass (sequenceGestureNext(), include/sequence_gesture.h),
  // so what falls at or after the end is never sent. A body Gesture's moves are
  // paced at least the Cadence Floor apart, which this counts; each Output's
  // own throw can push them later still, so the count is the least that is cut.
  const gestureCut = (events, steps, context) => {
    const G = window.SeqGesture;
    const end = steps.find((step) => step && step.type === "end");
    if (!G || !end) return [];
    const endT = Number(end.t) || 0;
    const floor = cadenceFloor(context)?.ms || 0;
    const out = [];
    events
      .filter((event) => event.def.type === "gesture" && !event.generated)
      .forEach((event) => {
        let cut = 0;
        if (G.onDome(event.def)) {
          const repeat = Number(event.def.repeatMs) || 0;
          const extent = Number(event.def.extentMs) || 0;
          const passes = repeat > 0 && extent > 0 ? Math.ceil(extent / repeat) : 1;
          for (let p = 0; p < passes; p++) if (event.t + p * repeat >= endT) cut += 1;
        } else {
          let last = -Infinity;
          G.bodyMoves(event.def, event.t).forEach((move) => {
            const at = Math.max(move.t, last + floor);
            last = at;
            if (at >= endT) cut += 1;
          });
        }
        if (cut > 0) {
          out.push(
            finding(
              "warning",
              "gesture-cut",
              `${plural(cut, "move of this gesture falls", "moves of this gesture fall")} at or after the end, and the droid stops it there.`,
              "Start it earlier, slow its pace, or move the end later.",
              { step: event.step, n: cut },
            ),
          );
        }
      });
    return out;
  };

  // A dome Gesture the connected dome performs only in part, or not at all
  // (ADR 0046): Coordinator Resolution maps it onto the dome's `$` family, and
  // the body never breaks it into single panel commands. It still saved; this
  // says what happens instead.
  const gestureDome = (events) => {
    const G = window.SeqGesture;
    if (!G) return [];
    return events
      .filter((event) => event.def.type === "gesture" && !event.generated && !(event.iter > 0))
      .map((event) => ({ event, reading: G.domeReading(event.def) }))
      .filter(({ reading }) => reading)
      .map(({ event, reading }) =>
        finding(
          "warning",
          "gesture-dome",
          reading.notes.join(" "),
          reading.performs
            ? "Use together, or a chase that opens, for the dome to do exactly this."
            : "Pick together, or open with another spread.",
          { step: event.step },
        ),
      );
  };

  // ---------------------------------------------------------------------------
  // The tempo (ADR 0058). Two warnings, neither a refusal: a tempo is advisory
  // and always editable, and the builder can know what no analyzer can.
  // ---------------------------------------------------------------------------

  // Below this a tempo is called a guess. Every figure ADR 0058 has came from
  // synthesised click tracks, so this is a stated stand-in awaiting a real
  // track, not a measurement: an analyzed tempo whose best lag barely beats the
  // average one reads 0.5, and so do taps whose spacing wanders by a tenth of
  // a beat.
  const TEMPO_CONFIDENCE_LOW = 0.5;

  const tempoConfidence = (seq) => {
    const tempo = seq?.tempo;
    if (!tempo || typeof tempo.confidence !== "number" || tempo.confidence >= TEMPO_CONFIDENCE_LOW) return [];
    const why =
      tempo.source === "tapped"
        ? "The taps were uneven, so this tempo is a rough guess."
        : "The track has no steady beat to lock onto, so this tempo is a guess.";
    return [finding("warning", "tempo-confidence", why, "Tap along to the track on the droid, or type the tempo.")];
  };

  // The hash can only be compared when the builder drops a copy of the track in
  // again: the browser never holds the droid's audio, and no route fetches it.
  // `context.trackHash` is that copy's, when there is one.
  const tempoHash = (seq, context) => {
    const stored = seq?.tempo?.hash;
    const dropped = context?.trackHash;
    if (typeof stored !== "string" || typeof dropped !== "string" || stored === dropped) return [];
    return [
      finding(
        "warning",
        "tempo-hash",
        "The track you dropped in is not the one this tempo was measured from.",
        "Analyze this track again, or tap along to it.",
      ),
    ];
  };

  // ---------------------------------------------------------------------------
  // The figures (#287 second pass, specific 9): each one true, no headline.
  // ---------------------------------------------------------------------------
  // The bytes POST /api/seq sends: the JSON data/web_api.js stringifies, as
  // UTF-8, which is what the droid's per-file cap counts.
  const byteLength = (seq) => {
    let bytes = 0;
    for (const ch of JSON.stringify(seq)) {
      const code = ch.codePointAt(0);
      bytes += code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
    }
    return bytes;
  };

  // The slowest full throw among the Parts this routine moves: the time an
  // Output takes end to end, by the generated planner. Parts nobody can time
  // leave it `null`, and the row says so instead of quoting a number.
  const slowestThrow = (events, context) => {
    let slowest = null;
    let untimed = false;
    events.forEach((event) => {
      const def = event.def;
      if (def.type === "dome" && panelIntent(def.cmd)) untimed = true;
      if (def.type !== "body" || !def.part) return;
      const move = bodyMove(def, context);
      if (move.output && !isServo(move.output)) return;
      if (!move.timed) {
        untimed = true;
        return;
      }
      const ms = motion().servoMotionArrivalMs(move.profile.loUs, move.profile.hiUs, move.profile);
      if (!slowest || ms > slowest.ms) slowest = { ms, part: def.part };
    });
    return { slowest, untimed };
  };

  // ---------------------------------------------------------------------------
  // rehearse() -- the one computation.
  //
  // `context` is what the droid in front of the author reports, and every part
  // of it is optional: a rule that needs something not read stays silent, and
  // the Gap lines say what could not be checked.
  //   outputs    GET /api/servo/outputs, as data/outputs.js reads each row
  //   config     GET /api/config (its `components`, and the Cadence Floor
  //              under `servo`)
  //   layout     the connected dome's layout model (data/dome_layout.js)
  //   maxBytes   the droid's per-file cap (GET /api/identity)
  //   trackHash  the fingerprint of a track the builder dropped in, if any
  //
  // The steps are read as the droid runs them, every beat at the millisecond
  // it resolves to (data/seq_protocol_check.js resolveBeats()).
  // ---------------------------------------------------------------------------
  const rehearse = (seq, context = {}) => {
    const run = window.SeqProtocolCheck?.resolveBeats ? window.SeqProtocolCheck.resolveBeats(seq) : seq;
    const steps = Array.isArray(run?.steps) ? run.steps : [];
    const events = expand(steps);
    const findings = [
      ...dispatchSpacing(events),
      ...servoBurst(events),
      ...groupPanel(events),
      ...retargetBeforeArrival(events, context),
      ...quietInSequence(events),
      ...rawLightCode(events),
      ...switchedOff(events, context),
      ...domeUnavailable(events, context),
      ...bodyOverlap(events, context),
      ...partLeftOpen(events),
      ...flutterCut(events, steps),
      ...audioOutlivesShow(events),
      ...gestureDome(events),
      ...gestureCut(events, steps, context),
      ...domeHowFar(events),
      ...tempoConfidence(seq),
      ...tempoHash(seq, context),
    ];

    // What could not be judged, by step: a panel move's timing is the dome's, a
    // body move is timed only on a calibrated Output with its Part on it, and a
    // random step has no fixed target. A step no rule applies to is not a gap
    // and stays silent.
    const gapCounts = new Map();
    steps.forEach((step) => {
      if (!step) return;
      let gap = null;
      if (step.type === "dome" && panelIntent(step.cmd)) gap = "dome-timing";
      else if (step.type === "body") {
        const move = bodyMove(step, context);
        if (!(move.output && !isServo(move.output)) && !move.timed) gap = "body-timing";
      } else if (step.type === "random") gap = "random-pick";
      else if (step.type === "sequence") gap = "phrase";
      if (gap) gapCounts.set(gap, (gapCounts.get(gap) || 0) + 1);
    });
    const gaps = [...gapCounts.entries()].map(([code, n]) => ({ code, n, ...GAPS[code] }));

    const counts = { warning: 0, note: 0 };
    findings.forEach((item) => {
      counts[item.level] += 1;
    });
    const throws = slowestThrow(events, context);
    const figures = {
      steps: steps.length,
      maxSteps: window.SeqProtocolCheck?.MAX_STEPS ?? null,
      bytes: byteLength(seq || {}),
      maxBytes: Number.isInteger(context.maxBytes) && context.maxBytes > 0 ? context.maxBytes : null,
      durationMs: window.SeqProtocolCheck?.estimateDuration?.(steps) ?? null,
      slowestThrow: throws.slowest,
      untimedMoves: throws.untimed,
    };
    return { findings, gaps, counts, figures };
  };

  // ---------------------------------------------------------------------------
  // unmeasuredOutputs() -- the Servo Outputs this routine moves that nobody has
  // calibrated. On those the planner sends the Output straight to its target
  // (servoMotionPlan() snaps an uncalibrated profile), so the first move is a
  // jump rather than a ramp. Read beside the run control, before the press, and
  // never standing in its way (#287 specific 6). A light has no ends to measure
  // and a switched-off Output does not move, so neither is named.
  // ---------------------------------------------------------------------------
  const unmeasuredOutputs = (seq, context = {}) => {
    const steps = Array.isArray(seq?.steps) ? seq.steps : [];
    const named = new Map();
    steps.forEach((step) => {
      if (!step || step.type !== "body" || !step.part) return;
      const output = outputOf(step.part, context);
      if (!output || !isServo(output) || output.wired === false || output.calibrated) return;
      const entry = named.get(output.name) || { name: output.name, parts: [] };
      const name = partName(step.part);
      if (!entry.parts.includes(name)) entry.parts.push(name);
      named.set(output.name, entry);
    });
    return [...named.values()];
  };

  // ---------------------------------------------------------------------------
  // The three appearances. Color carries the level (amber for a warning, none
  // for a note); only a refusal names a severity (docs/ui-copy-voice.md rule 11).
  //
  // A count of ZERO takes no color. Amber means "degraded, and you can do
  // something about it" (CONTEXT.md "Status Color"), and there is nothing to do
  // about no warnings - a count of nothing wrong reading as something wrong, on
  // the surface whose whole job is telling a builder what will not happen. The
  // badge below already guarded itself this way; the count did not.
  // ---------------------------------------------------------------------------
  const kb = (bytes) => `${Number((bytes / 1024).toFixed(1))} KB`;

  // What the routine weighs against the droid's limits, as one line of words:
  // the Rehearsal's subtitle (data/seq.js).
  const figuresText = (report) => {
    const f = report.figures || {};
    const cells = [];
    if (typeof f.steps === "number") {
      cells.push(f.maxSteps ? `${f.steps} of ${f.maxSteps} steps` : plural(f.steps, "step", "steps"));
    }
    if (typeof f.bytes === "number") {
      cells.push(f.maxBytes ? `${kb(f.bytes)} of ${kb(f.maxBytes)}` : kb(f.bytes));
    }
    if (typeof f.durationMs === "number") cells.push(`runs ${seconds(f.durationMs)}`);
    if (f.slowestThrow) cells.push(`slowest throw ${seconds(f.slowestThrow.ms)}`);
    else if (f.untimedMoves) cells.push("slowest throw not timed");
    return cells.join(" \u00b7 ");
  };

  const countsHtml = (report) => `
    <div class="seq-rehearsal-counts" data-rehearsal-counts>
      <span class="seq-rehearsal-count${report.counts.warning > 0 ? " seq-rehearsal-count-warning" : ""}" data-count="warning">${plural(report.counts.warning, "warning", "warnings")}</span>
      <span class="seq-rehearsal-count" data-count="note">${plural(report.counts.note, "note", "notes")}</span>
    </div>`;

  const listHtml = (report) => {
    const items = report.findings
      .map(
        (item) => `
        <li class="seq-rehearsal-finding seq-rehearsal-${item.level}" data-level="${item.level}" data-code="${escapeHtml(item.code)}">
          <span class="seq-rehearsal-msg">${escapeHtml(item.msg)}</span>
          <span class="seq-rehearsal-fix">${escapeHtml(item.fix)}</span>
          <code class="seq-rehearsal-code">${escapeHtml(item.code)}</code>
        </li>`,
      )
      .join("");
    const gaps = report.gaps
      .map(
        (gap) => `
        <li class="seq-rehearsal-gap" data-gap="${escapeHtml(gap.code)}">
          <span class="seq-rehearsal-msg">${escapeHtml(plural(gap.n, "step", "steps"))} not checked: ${escapeHtml(gap.msg)}</span>
          <span class="seq-rehearsal-fix">${escapeHtml(gap.closes)}</span>
          <code class="seq-rehearsal-code">${escapeHtml(gap.code)}</code>
        </li>`,
      )
      .join("");
    // An all-clear says what went unchecked, or it claims a clean bill it did
    // not earn (#287 specific 7): the Gap lines under it do.
    let wire;
    if (report.findings.length > 0) wire = "Nothing here stops a save or a run.";
    else if (report.gaps.length > 0) wire = "Nothing to flag in what the Rehearsal could check.";
    else wire = "Nothing to flag.";
    return `
      <div class="seq-rehearsal-report" data-rehearsal-report>
        <p class="${report.findings.length === 0 ? "seq-rehearsal-clear" : "seq-rehearsal-wire"}">${wire}</p>
        <ul class="seq-rehearsal-list">${items}${gaps}</ul>
      </div>`;
  };

  // The badge's one line as plain text, for a place that carries words rather
  // than markup: a restore's receipt says it for each Sequence it wrote
  // (data/maintenance.js, #448). One line, so the badge and the receipt cannot
  // word the same report two ways.
  const summaryText = (report) => {
    const said =
      report.findings.length === 0
        ? "nothing to flag"
        : `${plural(report.counts.warning, "warning", "warnings")}, ${plural(report.counts.note, "note", "notes")}`;
    return `Rehearsal: ${said}`;
  };

  const badgeHtml = (report) => {
    return `
      <details class="seq-rehearsal-badge${report.counts.warning > 0 ? " seq-rehearsal-badge-warning" : ""}" data-rehearsal-badge>
        <summary>${escapeHtml(summaryText(report))}</summary>
        ${listHtml(report)}
      </details>`;
  };

  // The line beside the run control: every uncalibrated Servo Output this
  // routine moves, by its board label and the Part on it. Empty when there is
  // none, so a clean routine carries no line at all.
  const unmeasuredHtml = (outputs) => {
    if (!outputs.length) return "";
    const list = outputs.map((o) => `${o.name} (${o.parts.join(", ")})`).join(", ");
    return `<b>Not calibrated:</b> ${escapeHtml(list)}. First move is a jump, not a ramp.`;
  };

  window.SeqRehearsal = Object.freeze({
    LEVELS,
    finding,
    rehearse,
    // The expansion and a body move's resolution, for the timeline to draw
    // from the same reading of a routine the rules judge it by (#440).
    expand,
    bodyMove,
    gestureFlutterMs,
    unavailableMessage,
    // The Cadence Floor as the rules read it, for the editor to space the
    // closes of a close half it starts by the same figure (#441).
    cadenceFloor,
    unmeasuredOutputs,
    countsHtml,
    figuresText,
    listHtml,
    summaryText,
    badgeHtml,
    unmeasuredHtml,
  });
})();

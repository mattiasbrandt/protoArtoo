// =============================================================================
// data/seq_rehearsal.js
//
// The Rehearsal: reads a sequence and says what will not happen as its author
// wrote it (ADR 0044, #287, #354).
//
// It is NOT Protocol Check and never sits inside data/seq_protocol_check.js.
// That file mirrors the device's gate, and a mirror is only trustworthy while it
// mirrors; advice put inside it would force the two to diverge. So this module
// can never refuse anything: it has no error level, it is never asked before a
// save or a run, and nothing it returns disables a button.
//
// A finding is fields, not a sentence: {level, code, msg, fix}, plus the step
// and the subject it is about when it is about exactly one. `code` is protocol
// and `msg`/`fix` are copy (#298), so a finding can be counted by kind without
// reading prose. finding() refuses a level that is not one of the two, and a
// finding with no fix -- a finding with no fix is a complaint, and does not ship.
//
// Every rule here is paid for by a failure this project has had (#287's
// admission standard), and each names its receipt:
//   dispatch-spacing        2026-06-18: the dome's eight-entry command queue
//                           overflowed and silently dropped a :CL01.
//   retarget-before-arrival DM:HELLO's five identical :OP01 made one open (#287).
//   quiet-in-sequence       2026-06-17: DM:ROCKMARCH's $s muted idle chatter
//                           until reboot on every normal completion.
//   part-left-open          ADR 0049: the engine undoes nothing a body step did,
//                           and "this routine leaves the dataport open" is named
//                           there as a Note, because it performs as written.
//
// One computation behind three appearances (#287 specific 6): the counts in the
// editor, the full list at save and at clone, and a badge beside a run.
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
      msg: "How long a dome panel takes to move is the dome's to know, so a panel move cannot be timed here.",
      closes: "It could be, if the dome published how long its panels take.",
    },
    "body-timing": {
      msg: "How long a body part takes to move is set on the output that drives it, and this page does not read that yet.",
      closes: "It could be, once the editor reads each output's time to full throw.",
    },
    "random-pick": {
      msg: "A random step picks its panel when the sequence runs, so there is nothing fixed here to check.",
      closes: "Nothing closes this; the pick is the point of the step.",
    },
  });

  const escapeHtml = (value) =>
    String(value ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");

  const seconds = (ms) => `${Number((ms / 1000).toFixed(2))} s`;

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
    if (typeof subject.n === "number") out.n = subject.n;
    return out;
  };

  // ---------------------------------------------------------------------------
  // expand() -- every step as it fires, at its absolute time.
  //
  // A loop's body runs once per period while the iteration start is inside the
  // loop's duration, and its steps are timed from the iteration start -- the
  // engine's own arithmetic (stepFireAt(), src/tasks/sequence_engine.cpp).
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
        for (let start = 0; period > 0 && start < duration; start += period) {
          for (let k = 1; k <= count; k += 1) {
            const inner = steps[i + k] || {};
            events.push({ t: (Number(step.t) || 0) + start + (Number(inner.t) || 0), step: i + k, def: inner });
          }
        }
        i += count + 1;
      } else {
        events.push({ t: Number(step.t) || 0, step: i, def: step });
        i += 1;
      }
    }
    // Stable: equal times keep authored order, which is the engine's order too.
    return events
      .map((event, order) => ({ ...event, order }))
      .sort((a, b) => a.t - b.t || a.order - b.order);
  };

  // A panel intent (:OP/:CL/:OF) split into what it does and to which panel.
  const panelIntent = (cmd) => {
    const match = /^:(OP|CL|OF)([0-9A-Z]{2})$/.exec(String(cmd || ""));
    return match ? { word: match[1], target: match[2] } : null;
  };

  const panelName = (target) => (/^P\d$/.test(target) ? `PP${target.slice(1)}` : `P${Number(target)}`);
  const panelVerb = { OP: "open", CL: "close" };

  // ---------------------------------------------------------------------------
  // The rules. Each reads the expanded events and returns findings.
  // ---------------------------------------------------------------------------
  const dispatchSpacing = (events) => {
    const dome = events.filter((event) => event.def.type === "dome");
    let tight = 0;
    let worst = null;
    for (let k = 1; k < dome.length; k += 1) {
      const gap = dome[k].t - dome[k - 1].t;
      if (gap < DOME_SPACING_MS) {
        tight += 1;
        if (!worst || gap < worst.gap) worst = { gap, before: dome[k - 1], after: dome[k] };
      }
    }
    if (!worst) return [];
    const when =
      worst.gap === 0
        ? `${worst.before.def.cmd} and ${worst.after.def.cmd} both leave at ${seconds(worst.after.t)}`
        : `${worst.after.def.cmd} leaves ${worst.gap} ms after ${worst.before.def.cmd}`;
    return [
      finding(
        "warning",
        "dispatch-spacing",
        `${tight} dome ${tight === 1 ? "command follows" : "commands follow"} the one before by less than ${DOME_SPACING_MS} ms -- ${when}. The dome holds eight commands at a time and quietly drops what arrives past that; on 18 June that left a panel open.`,
        `Space dome commands at least ${DOME_SPACING_MS} ms apart.`,
        { step: worst.after.step, n: tight },
      ),
    ];
  };

  // The identical re-issue: the same open or close, to the same subject, with
  // nothing else sent to that subject in between. The target is already on its
  // way there, so the repeat moves nothing. A flutter is not idempotent and is
  // never counted; a genuine reversal needs a travel time, which is a Gap.
  const retargetBeforeArrival = (events) => {
    const last = new Map();
    const groups = new Map();
    events.forEach((event) => {
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
        const group = groups.get(key) || { n: 0, first: event, label };
        group.n += 1;
        groups.set(key, group);
      }
      last.set(key, command);
    });
    return [...groups.values()].map(({ n, first, label }) => {
      const who = label.element || label.part;
      return finding(
        "warning",
        "retarget-before-arrival",
        `${who} is told to ${label.verb} again at ${seconds(first.t)} with nothing in between${n > 1 ? `, ${n} times` : ""}. It is already on its way there, so the repeat moves nothing -- DM:HELLO made one open out of five this way.`,
        `Delete the repeated ${label.verb}, or put the opposite move between them if ${who} should move twice.`,
        label.element ? { step: first.step, element: label.element, n } : { step: first.step, part: label.part, n },
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
        "$s stops the sound and also turns the droid's idle chatter off until it is switched off and on again.",
        "Delete the $s step. A sound stops by itself when it ends, and chatter carries on.",
        { step: hits[0].step, n: hits.length },
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
    return [...lastShape.entries()]
      .filter(([, last]) => last.shape !== "close")
      .map(([part, last]) =>
        finding(
          "note",
          "part-left-open",
          `${part} is still open when the sequence ends, and the body leaves it that way -- nothing closes it for you.`,
          `Add a close for ${part} near the end, unless it is meant to stay open.`,
          { step: last.step, part },
        ),
      );
  };

  // ---------------------------------------------------------------------------
  // rehearse() -- the one computation.
  // ---------------------------------------------------------------------------
  const rehearse = (seq) => {
    const steps = Array.isArray(seq?.steps) ? seq.steps : [];
    const events = expand(steps);
    const findings = [
      ...dispatchSpacing(events),
      ...retargetBeforeArrival(events),
      ...quietInSequence(events),
      ...partLeftOpen(events),
    ];

    // A step counts as checked when every rule that applies to it could be
    // judged. A panel move and a body move each carry a timing question nobody
    // here can answer, and a random step has no fixed target; those are the
    // unchecked ones, and each names its Gap. A step no rule applies to is not
    // a gap and stays silent.
    const gapCounts = new Map();
    let total = 0;
    let unchecked = 0;
    steps.forEach((step) => {
      if (!step || step.type === "end") return;
      total += 1;
      let gap = null;
      if (step.type === "dome" && panelIntent(step.cmd)) gap = "dome-timing";
      else if (step.type === "body") gap = "body-timing";
      else if (step.type === "random") gap = "random-pick";
      if (gap) {
        unchecked += 1;
        gapCounts.set(gap, (gapCounts.get(gap) || 0) + 1);
      }
    });
    const gaps = [...gapCounts.entries()].map(([code, n]) => ({ code, n, ...GAPS[code] }));

    const counts = { warning: 0, note: 0 };
    findings.forEach((item) => {
      counts[item.level] += 1;
    });
    return { findings, gaps, counts, checked: total - unchecked, total };
  };

  // ---------------------------------------------------------------------------
  // The three appearances. Colour carries the level (amber for a warning, none
  // for a note); only a refusal names a severity (docs/ui-copy-voice.md rule 11).
  // ---------------------------------------------------------------------------
  const plural = (n, one, many) => `${n} ${n === 1 ? one : many}`;

  const checkedLine = (report) => `checked ${report.checked} of ${plural(report.total, "step", "steps")}`;

  const countsHtml = (report) => `
    <div class="seq-rehearsal-counts" data-rehearsal-counts>
      <span class="seq-rehearsal-count seq-rehearsal-count-warning" data-count="warning">${plural(report.counts.warning, "warning", "warnings")}</span>
      <span class="seq-rehearsal-count" data-count="note">${plural(report.counts.note, "note", "notes")}</span>
      <span class="seq-rehearsal-checked">Rehearsal ${checkedLine(report)}</span>
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
    const lead =
      report.findings.length === 0
        ? `<p class="seq-rehearsal-clear">Nothing to flag in the ${plural(report.checked, "step", "steps")} the Rehearsal could check, out of ${report.total}.</p>`
        : `<p class="seq-rehearsal-lead">The Rehearsal ${checkedLine(report)}. Nothing here stops a save or a run.</p>`;
    return `
      <div class="seq-rehearsal-report" data-rehearsal-report>
        ${lead}
        <ul class="seq-rehearsal-list">${items}${gaps}</ul>
      </div>`;
  };

  const badgeHtml = (report) => {
    const said =
      report.findings.length === 0
        ? "nothing to flag"
        : `${plural(report.counts.warning, "warning", "warnings")}, ${plural(report.counts.note, "note", "notes")}`;
    return `
      <details class="seq-rehearsal-badge${report.counts.warning > 0 ? " seq-rehearsal-badge-warning" : ""}" data-rehearsal-badge>
        <summary>Rehearsal: ${said}</summary>
        ${listHtml(report)}
      </details>`;
  };

  window.SeqRehearsal = Object.freeze({
    LEVELS,
    finding,
    rehearse,
    countsHtml,
    listHtml,
    badgeHtml,
  });
})();

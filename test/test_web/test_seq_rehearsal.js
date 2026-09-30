// The Rehearsal (#354, ADR 0044, #287): what it catches, and the shape every
// finding has to have.
//
// The fixtures are the three defects the shipped Factory catalog carried until
// #354 -- DM:HELLO's five identical opens, DM:LOW's same-timestamp burst and
// DM:RESET's $s -- written as the JSON a clone of each would have handed the
// editor. They are the receipts the rules were admitted on, so a rule that stops
// catching its own receipt is the regression these tests exist for.
//
// Per test_web/README.md: the shipped module runs in a vm; nothing is
// pattern-matched out of its source.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

// The modules the Rehearsal reads beside it on data/seq.html: the generated
// motion model, the dome command map and Protocol Check's mirror.
const MODULES = ["servo_motion.js", "dome_command_map.js", "seq_protocol_check.js", "seq_rehearsal.js"];

const loadRehearsal = () => {
  const sandbox = { window: {} };
  vm.createContext(sandbox);
  MODULES.forEach((name) =>
    vm.runInContext(fs.readFileSync(path.resolve(__dirname, "../../data", name), "utf8"), sandbox, { filename: name }),
  );
  return sandbox.window;
};

const W = loadRehearsal();
const R = W.SeqRehearsal;
const M = W.ServoMotion;

const seq = (steps) => ({ name: "DM:TEST", suppressMs: 8000, toggleGroup: "none", steps });

// DM:HELLO as it shipped before #354.
const helloBefore = seq([
  { t: 0, type: "audio", cmd: "$H" },
  { t: 0, type: "dome", cmd: "@1MHello There" },
  { t: 0, type: "dome", cmd: "@3MGeneral Kenobi" },
  { t: 0, type: "dome", cmd: ":OP01" },
  { t: 160, type: "dome", cmd: ":OP01" },
  { t: 320, type: "dome", cmd: ":OP01" },
  { t: 480, type: "dome", cmd: ":OP01" },
  { t: 640, type: "dome", cmd: ":OP01" },
  { t: 800, type: "dome", cmd: ":CL01" },
  { t: 950, type: "end" },
]);

const byCode = (report, code) => report.findings.filter((f) => f.code === code);

test("DM:HELLO's five identical opens are one finding about P1, with the step it starts at", () => {
  const found = byCode(R.rehearse(helloBefore), "retarget-before-arrival");
  assert.equal(found.length, 1);
  assert.equal(found[0].level, "warning");
  assert.equal(found[0].element, "P1");
  assert.equal(found[0].n, 4);
  assert.equal(found[0].step, 4);
  assert.match(found[0].msg, /P1 is told to open again at 0\.16 s/);
});

test("an open, a close and an open again is a panel moving, not a repeat", () => {
  const report = R.rehearse(
    seq([
      { t: 0, type: "dome", cmd: ":OP01" },
      { t: 500, type: "dome", cmd: ":CL01" },
      { t: 1000, type: "dome", cmd: ":OP01" },
      { t: 1500, type: "end" },
    ]),
  );
  assert.equal(byCode(report, "retarget-before-arrival").length, 0);
});

test("a repeated flutter is not counted as a repeat", () => {
  const report = R.rehearse(
    seq([
      { t: 0, type: "dome", cmd: ":OF01" },
      { t: 400, type: "dome", cmd: ":OF01" },
      { t: 800, type: "dome", cmd: ":CL01" },
      { t: 1200, type: "end" },
    ]),
  );
  assert.equal(byCode(report, "retarget-before-arrival").length, 0);
});

test("the same body move sent twice is caught on the Part it names", () => {
  const found = byCode(
    R.rehearse(
      seq([
        { t: 0, type: "body", part: "doorFL" },
        { t: 300, type: "body", part: "doorFL", shape: "open" },
        { t: 900, type: "body", part: "doorFL", shape: "close" },
        { t: 1200, type: "end" },
      ]),
    ),
    "retarget-before-arrival",
  );
  assert.equal(found.length, 1);
  assert.equal(found[0].part, "doorFL");
  assert.equal(found[0].step, 1);
});

test("DM:LOW's three opens at t=4400 are a dispatch-spacing warning", () => {
  const found = byCode(
    R.rehearse(
      seq([
        { t: 4400, type: "dome", cmd: ":OP11" },
        { t: 4400, type: "dome", cmd: ":OP13" },
        { t: 4400, type: "dome", cmd: ":OP01" },
        { t: 5900, type: "end" },
      ]),
    ),
    "dispatch-spacing",
  );
  assert.equal(found.length, 1);
  assert.equal(found[0].level, "warning");
  assert.equal(found[0].n, 2);
  assert.match(found[0].msg, /both leave at 4\.4 s/);
});

test("dome commands 200 ms apart pass, and 199 ms apart do not", () => {
  const at = (gap) =>
    R.rehearse(
      seq([
        { t: 0, type: "dome", cmd: ":OP11" },
        { t: gap, type: "dome", cmd: ":OP13" },
        { t: 2000, type: "end" },
      ]),
    );
  assert.equal(byCode(at(200), "dispatch-spacing").length, 0);
  assert.equal(byCode(at(199), "dispatch-spacing").length, 1);
});

test("a loop is judged as it runs, one iteration after another", () => {
  // Each iteration is spaced correctly inside itself; it is the next
  // iteration's first command, 50 ms after this one's last, that is too close.
  const found = byCode(
    R.rehearse(
      seq([
        { t: 0, type: "loop", body: 2, periodMs: 450, durationMs: 900 },
        { t: 0, type: "dome", cmd: ":OP01" },
        { t: 400, type: "dome", cmd: ":CL01" },
        { t: 2000, type: "end" },
      ]),
    ),
    "dispatch-spacing",
  );
  assert.equal(found.length, 1);
  assert.equal(found[0].n, 1);
  assert.match(found[0].msg, /:OP01 leaves 50 ms after :CL01/);
});

test("DM:RESET's $s is a quiet-in-sequence warning, and $S is not", () => {
  const found = byCode(
    R.rehearse(
      seq([
        { t: 0, type: "audio", cmd: "$s" },
        { t: 100, type: "audio", cmd: "$S" },
        { t: 3900, type: "end" },
      ]),
    ),
    "quiet-in-sequence",
  );
  assert.equal(found.length, 1);
  assert.equal(found[0].step, 0);
  assert.equal(found[0].n, 1);
});


test("a count of zero warnings takes no state color", () => {
  // CONTEXT.md "Status Color": amber is "degraded, and you can do something
  // about it". There is nothing to do about no warnings, and a count of nothing
  // wrong reading as something wrong is the defect on the surface whose whole
  // job is telling a builder what will not happen. The badge already guarded
  // itself; the count carried .seq-rehearsal-count-warning unconditionally.
  const classOf = (warning) =>
    R.countsHtml({ counts: { warning, note: 0 }, checked: 3, total: 3 }).match(
      /<span class="([^"]*)" data-count="warning"/,
    )[1];
  assert.equal(classOf(0), "seq-rehearsal-count", "0 warnings wears the warning color");
  assert.match(classOf(1), /\bseq-rehearsal-count-warning\b/, "1 warning lost its color");
});

test("Protocol Check's mirror answers with a verdict and carries no advice channel", () => {
  // ADR 0044 decision 3 deleted the mirror's `warnings` slot "so nothing invites
  // advice back into the mirror later": advice is the Rehearsal's, and a mirror
  // with a place for it is how the two would start to diverge.
  const sandbox = { window: {} };
  vm.createContext(sandbox);
  vm.runInContext(
    fs.readFileSync(path.resolve(__dirname, "../../data/seq_protocol_check.js"), "utf8"),
    sandbox,
    { filename: "seq_protocol_check.js" },
  );
  const check = sandbox.window.SeqProtocolCheck;
  const accepted = check.validateSequence(
    seq([
      { t: 0, type: "dome", cmd: ":OP01" },
      { t: 500, type: "dome", cmd: ":CL01" },
      { t: 1000, type: "end" },
    ]),
  );
  assert.deepEqual(Object.keys(accepted), ["ok"]);
  assert.equal(accepted.ok, true);
  const refused = check.validateSequence(seq([{ t: 0, type: "dome", cmd: ":OP01" }]));
  assert.equal(refused.ok, false);
  assert.deepEqual(Object.keys(refused).filter((k) => !["ok", "field", "error"].includes(k)), []);
});

// ---------------------------------------------------------------------------
// The body, timed by the planner the firmware runs (#439).
// ---------------------------------------------------------------------------

// A Servo Output row as data/outputs.js reads GET /api/servo/outputs: an
// MG996R-sized pair, 900 ms end to end and 225 ms to speed -- the profile
// test_native/test_servo_motion_ramp pins its figures on.
const output = (parts, extra = {}) => ({
  name: "ARM1",
  wired: true,
  light: null,
  openUs: 2000,
  closeUs: 1000,
  bandLoUs: 1000,
  bandHiUs: 2000,
  throwMs: 900,
  accelMs: 225,
  ease: "none",
  calibrated: true,
  parts,
  ...extra,
});

test("the browser's planner lands on the firmware's own figures", () => {
  // test_native/test_servo_motion_ramp: a full throw takes the Output's own
  // time with its own ramp, a half throw pays both ramps (562.5 ms, not 450),
  // and an Output nobody has measured jumps.
  const profile = { loUs: 1000, hiUs: 2000, throwMs: 900, accelMs: 225, easing: 0, calibrated: true };
  const full = M.servoMotionPlan(1000, 2000, profile, 1000);
  assert.equal(full.durationMs, 900);
  assert.equal(full.rampMs, 225);
  assert.equal(M.servoMotionPlan(1000, 1500, profile, 1000).durationMs, 563);
  assert.equal(M.servoMotionArrivalMs(1000, 2000, { ...profile, calibrated: false }), 0);
  // An overshoot arrives when its settle back does, not when its aim is reached.
  const overshoot = { ...profile, easing: M.ServoEasing.SERVO_EASE_OVERSHOOT };
  const out = M.servoMotionPlan(1000, 1800, overshoot, 0);
  assert.ok(M.servoMotionSettles(out));
  const back = M.servoMotionSettleBack(out, overshoot, out.durationMs);
  assert.equal(M.servoMotionArrivalMs(1000, 1800, overshoot), out.durationMs + back.durationMs);
});

test("a Part turned back before it arrives is judged against the run it cuts short", () => {
  const context = { outputs: [output(["doorFL"])] };
  const found = byCode(
    R.rehearse(
      seq([
        { t: 0, type: "body", part: "doorFL" },
        { t: 300, type: "body", part: "doorFL", shape: "close" },
        { t: 2000, type: "end" },
      ]),
      context,
    ),
    "retarget-before-arrival",
  );
  assert.equal(found.length, 1);
  assert.equal(found[0].part, "doorFL");
  assert.equal(found[0].step, 1);
  assert.match(found[0].msg, /300 ms into a move that takes 900 ms/);
});

test("re-targets the same way on are one move, measured from where it set off", () => {
  // The reference's v1.78.0 lesson: judging every re-target buried the real
  // findings under ones about routines that arrive exactly when they say.
  // Half open, then all the way open 200 ms later, then closed once the whole
  // 900 ms run has had its time.
  const context = { outputs: [output(["doorFL"])] };
  const report = R.rehearse(
    seq([
      { t: 0, type: "body", part: "doorFL", howFar: 50 },
      { t: 200, type: "body", part: "doorFL" },
      { t: 900, type: "body", part: "doorFL", shape: "close" },
      { t: 2000, type: "end" },
    ]),
    context,
  );
  assert.equal(byCode(report, "retarget-before-arrival").length, 0);
  // Timed, so no body-timing Gap; the slowest throw is the Output's full one.
  assert.equal(report.gaps.length, 0);
  assert.equal(report.figures.slowestThrow.ms, 900);
});

test("a body move that cannot be timed keeps its Gap, and a dome-only routine keeps the dome's", () => {
  const uncalibrated = { outputs: [output(["doorFL"], { calibrated: false })] };
  const body = R.rehearse(seq([{ t: 0, type: "body", part: "doorFL" }, { t: 900, type: "end" }]), uncalibrated);
  assert.equal(body.gaps.map((gap) => gap.code).join(), "body-timing");
  const dome = R.rehearse(seq([{ t: 0, type: "dome", cmd: ":OP01" }, { t: 900, type: "end" }]), uncalibrated);
  assert.equal(dome.gaps.map((gap) => gap.code).join(), "dome-timing");
});

// The 2026-06-17 ROCKMARCH run, as far as a sequence can carry it: raw logic
// and PSI codes that left the logics default blue, a blanket :CL00 that moved
// pies the routine never touched until they stalled, and a named track that
// played on past the show.
test("ROCKMARCH as it ran on 17 June trips the receipts it paid for", () => {
  const report = R.rehearse(
    seq([
      { t: 0, type: "audio", cmd: "$M", boundAudio: false },
      { t: 0, type: "audioCat", category: "alert", fallback: "none" },
      { t: 500, type: "dome", cmd: "@0T11" },
      { t: 1000, type: "dome", cmd: "@0P11" },
      { t: 1500, type: "dome", cmd: ":OP01" },
      { t: 3000, type: "dome", cmd: ":CL00" },
      { t: 4000, type: "end" },
    ]),
  );
  assert.equal(byCode(report, "raw-light-code").length, 2);
  const group = byCode(report, "group-panel");
  assert.equal(group.length, 1);
  // Every panel but P1, which the routine opens itself.
  assert.match(group[0].msg, /including 12 panels nothing else here moves/);
  const outlives = byCode(report, "audio-outlives-show");
  // A Note, and only the named track: a category rings out on purpose (ADR 0010).
  assert.equal(outlives.length, 1);
  assert.equal(outlives[0].level, "note");
  assert.equal(outlives[0].step, 0);
});

test("DM:LOW's ring closes inside the dome's cadence are a burst, and nine body closes at once overlap", () => {
  const low = R.rehearse(
    seq([
      ...[1, 2, 3, 4, 7, 11, 13].map((panel, k) => ({ t: k * 150, type: "dome", cmd: `:CL${String(panel).padStart(2, "0")}` })),
      { t: 2000, type: "end" },
    ]),
  );
  const burst = byCode(low, "servo-burst");
  assert.equal(burst.length, 1);
  assert.equal(burst[0].n, 6);
  // The :SE routines fire nine BODY_CLOSE steps at t=0 (include/sequence_bulk_centre.h).
  const parts = ["doorFL", "doorFR", "doorBL", "doorBR", "dataPort", "chargeBay", "utilArmT", "utilArmB", "drawer"];
  const se = R.rehearse(seq([...parts.map((part) => ({ t: 0, type: "body", part, shape: "close" })), { t: 900, type: "end" }]));
  const overlap = byCode(se, "body-overlap");
  assert.equal(overlap.length, 1);
  assert.equal(overlap[0].n, 8);
});

test("a step aimed at hardware switched off on this droid is one finding per switch", () => {
  const report = R.rehearse(
    seq([
      { t: 0, type: "audio", cmd: "$H" },
      { t: 0, type: "dome", cmd: ":OP01" },
      { t: 500, type: "dome", cmd: ":CL01" },
      { t: 900, type: "body", part: "doorFL" },
      { t: 1500, type: "end" },
    ]),
    { config: { components: { protoR2link: { enabled: false }, audio: { enabled: true } } }, outputs: [output(["doorFL"], { wired: false })] },
  );
  const off = byCode(report, "switched-off");
  assert.equal(off.length, 2);
  assert.equal(off.find((item) => !item.part).n, 2);
  assert.equal(off.find((item) => item.part).part, "doorFL");
});

// A tempo is advisory and always editable (ADR 0058): a weak one, and one whose
// track has changed underneath it, are warnings the builder reads, and neither
// is ever a reason the droid refuses the save.
test("a weak or stale tempo is warned about and never refused", () => {
  const guessed = {
    ...seq([
      { t: 0, beat: 0, type: "audio", cmd: "$H" },
      { t: 1846, beat: 4, type: "end" },
    ]),
    tempo: { bpm: 130, phase: 0, barLen: 4, barPhase: 0, source: "analysed", confidence: 0.3, hash: "0a1b2c3d" },
  };
  const report = R.rehearse(guessed, { trackHash: "ffffffff" });
  assert.equal(byCode(report, "tempo-confidence").length, 1);
  assert.equal(byCode(report, "tempo-confidence")[0].level, "warning");
  assert.equal(byCode(report, "tempo-hash").length, 1);
  assert.equal(W.SeqProtocolCheck.validateSequence(guessed).ok, true, "a weak tempo was refused");

  const sure = { ...guessed, tempo: { ...guessed.tempo, confidence: 0.9 } };
  const matched = R.rehearse(sure, { trackHash: "0a1b2c3d" });
  assert.equal(byCode(matched, "tempo-confidence").length, 0);
  assert.equal(byCode(matched, "tempo-hash").length, 0);
});

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

const loadRehearsal = () => {
  const sandbox = { window: {} };
  vm.createContext(sandbox);
  vm.runInContext(
    fs.readFileSync(path.resolve(__dirname, "../../data/seq_rehearsal.js"), "utf8"),
    sandbox,
    { filename: "seq_rehearsal.js" },
  );
  return sandbox.window.SeqRehearsal;
};

const R = loadRehearsal();

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


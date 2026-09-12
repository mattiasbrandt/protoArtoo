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

// Values made inside the vm carry that realm's prototypes, so they are compared
// as plain data rather than with deepEqual against this realm's literals.
const plain = (value) => JSON.parse(JSON.stringify(value));

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

test("a Part the sequence leaves open is a note, not a warning", () => {
  const report = R.rehearse(
    seq([
      { t: 0, type: "body", part: "dataport" },
      { t: 0, type: "body", part: "doorFL" },
      { t: 1000, type: "body", part: "doorFL", shape: "close" },
      { t: 2000, type: "end" },
    ]),
  );
  const found = byCode(report, "part-left-open");
  assert.equal(found.length, 1);
  assert.equal(found[0].level, "note");
  assert.equal(found[0].part, "dataport");
  assert.equal(report.counts.note, 1);
  assert.equal(report.counts.warning, 0);
});

test("every finding carries a level of two, a token and a fix", () => {
  const report = R.rehearse(
    seq([
      ...helloBefore.steps.slice(0, -1),
      { t: 900, type: "audio", cmd: "$s" },
      { t: 900, type: "body", part: "dataport" },
      { t: 950, type: "end" },
    ]),
  );
  assert.ok(report.findings.length >= 4);
  report.findings.forEach((item) => {
    assert.ok(["warning", "note"].includes(item.level), item.level);
    assert.ok(item.code.length > 0);
    assert.ok(item.fix.length > 0, `${item.code} ships without a fix`);
  });
  assert.equal(report.counts.warning + report.counts.note, report.findings.length);
});

test("a finding cannot be made at a third level or without a fix", () => {
  assert.throws(() => R.finding("error", "x", "msg", "fix"), /a warning or a note, never "error"/);
  assert.throws(() => R.finding("warning", "x", "msg", ""), /needs a code, a message and a fix/);
  assert.equal(R.finding("note", "x", "msg", "fix").level, "note");
});

test("coverage is a count of the steps it could check, and it names why the rest were not", () => {
  const report = R.rehearse(
    seq([
      { t: 0, type: "audio", cmd: "$H" },
      { t: 0, type: "dome", cmd: "@0T6" },
      { t: 200, type: "dome", cmd: ":OP01" },
      { t: 400, type: "body", part: "doorFL" },
      { t: 900, type: "body", part: "doorFL", shape: "close" },
      { t: 1000, type: "random", set: "ring", mode: "open" },
      { t: 2000, type: "end" },
    ]),
  );
  assert.equal(report.total, 6);
  assert.equal(report.checked, 2);
  assert.deepEqual(
    plain(report.gaps.map((gap) => [gap.code, gap.n])),
    [["dome-timing", 1], ["body-timing", 2], ["random-pick", 1]],
  );
  report.gaps.forEach((gap) => assert.ok(gap.closes.length > 0));
});

test("the counts are never blank, even on a sequence with nothing in it", () => {
  const html = R.countsHtml(R.rehearse(seq([{ t: 0, type: "end" }])));
  assert.match(html, /0 warnings/);
  assert.match(html, /0 notes/);
  assert.match(html, /checked 0 of 0 steps/);
});

test("a clean sequence says so, and still says what went unchecked", () => {
  const report = R.rehearse(
    seq([
      { t: 0, type: "audio", cmd: "$H" },
      { t: 0, type: "dome", cmd: ":OP01" },
      { t: 800, type: "dome", cmd: ":CL01" },
      { t: 950, type: "end" },
    ]),
  );
  assert.equal(report.findings.length, 0);
  const html = R.listHtml(report);
  assert.match(html, /Nothing to flag in the 1 step the Rehearsal could check, out of 3/);
  assert.match(html, /data-gap="dome-timing"/);
});

test("the list puts each token beside its phrase and colours only the warnings", () => {
  const html = R.listHtml(R.rehearse(helloBefore));
  assert.match(
    html,
    /data-level="warning" data-code="retarget-before-arrival">\s*<span class="seq-rehearsal-msg">P1 is told/,
  );
  assert.match(html, /<code class="seq-rehearsal-code">retarget-before-arrival<\/code>/);
  const badge = R.badgeHtml(R.rehearse(helloBefore));
  assert.match(badge, /<details class="seq-rehearsal-badge seq-rehearsal-badge-warning"/);
  assert.match(badge, /Rehearsal: 2 warnings, 0 notes/);
});

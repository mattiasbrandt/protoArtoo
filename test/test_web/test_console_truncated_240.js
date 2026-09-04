// =============================================================================
// test/test_web/test_console_truncated_240.js
//
// Behavioural tests for the Live Logs console's handling of a BOUNDED reply
// (#240). Loads the REAL data/app.js, types a command the way an operator
// does, and asserts on the lines the page put in the log.
//
// The defect these exist for: src/web/api_console.cpp reports an answer it
// could not carry whole on the response ENVELOPE - "truncated":true beside
// "records" (docs/api.md, POST /api/console) - and the page ignored it. Every
// record in such a reply is well-formed and the group is properly closed by
// its `end` record, so the transcript reads exactly like a complete answer:
// nothing on screen says lines were left out. No source-text assertion sees
// that, because the record-rendering code is present and correct.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";

// A system.status.logs answer in the module's record shape
// (docs/console-protocol.md) - the one query whose item count can outgrow what
// the bounded adapter path holds.
const LOG_RECORDS = [
  { id: 12, type: "begin", operation: "system.status.logs" },
  { id: 12, type: "item", value: "[52340][I][WebServer] client connected" },
  { id: 12, type: "item", value: "[54012][W][WebServer] accept rejected: heap floor" },
  { id: 12, type: "end", status: "ok", outcome: "completed" },
];

// Loads app.js and returns a runner that types a command and presses Enter,
// handing back the log lines in the order the page painted them.
const consoleHarness = async (respond) => {
  const env = loadPageModule("app.js", { respond });
  await env.settle();

  const logConsole = env.element("log-console");
  const rendered = [];
  // appendLogLine writes the first line through innerHTML and every later one
  // through insertAdjacentHTML; capture both so line order is preserved.
  logConsole.insertAdjacentHTML = (_position, html) => rendered.push(html);

  const run = async (command) => {
    const input = env.element("log-command-input");
    input.value = command;
    env.emitOn("log-command-input", "keydown", {
      key: "Enter",
      preventDefault() {},
      stopPropagation() {},
    });
    await env.settle(6);
    return [...(logConsole.innerHTML ? [logConsole.innerHTML] : []), ...rendered];
  };

  return { env, run };
};

const respondWith = (payload) => (path) => {
  if (path === "/api/console") return { ok: true, status: 200, data: payload };
  return { data: {} };
};

// The one line that tells the operator the answer was cut. Matched on the
// clause that carries the meaning, not the whole sentence, so a copy edit does
// not fail these tests while a dropped notice still does.
const CUT_NOTICE = "some lines are missing";

test("a bounded reply says so - the operator is told lines are missing", async () => {
  const { run } = await consoleHarness(
    respondWith({ records: LOG_RECORDS, truncated: true })
  );

  const lines = await run("system.status.logs");
  const output = lines.join("\n");

  assert.ok(
    output.includes(CUT_NOTICE),
    "a truncated reply printed no notice - the operator sees a well-formed, " +
      "terminated group and cannot tell it is short"
  );
  // The records themselves still print: the notice is an addition to the
  // answer, never a replacement for it.
  assert.ok(output.includes("type=begin"), "begin record missing from the log");
  assert.ok(output.includes("accept rejected"), "item record missing from the log");
  assert.ok(output.includes("type=end"), "end record missing from the log");
});

test("a complete reply says nothing - no notice on an answer that fit", async () => {
  const { run } = await consoleHarness(respondWith({ records: LOG_RECORDS }));

  const output = (await run("system.status.logs")).join("\n");

  assert.ok(output.includes("type=end"), "the reply itself never reached the log");
  assert.ok(
    !output.includes(CUT_NOTICE),
    "a complete reply was labelled as cut - a notice on every answer tells " +
      "the operator nothing"
  );
});

test("truncated:false is not truncated", async () => {
  const { run } = await consoleHarness(
    respondWith({ records: LOG_RECORDS, truncated: false })
  );

  const output = (await run("system.status.logs")).join("\n");
  assert.ok(!output.includes(CUT_NOTICE), "truncated:false was read as truncated");
});

test("the notice follows the reply it belongs to, and is not styled as a failure", async () => {
  const { run } = await consoleHarness(
    respondWith({ records: LOG_RECORDS, truncated: true })
  );

  const lines = await run("system.status.logs");
  const noticeAt = lines.findIndex((line) => line.includes(CUT_NOTICE));
  const endAt = lines.findIndex((line) => line.includes("type=end"));

  assert.ok(noticeAt !== -1, "the notice never reached the log");
  assert.ok(endAt !== -1, "the closing record never reached the log");
  assert.ok(
    noticeAt > endAt,
    `the notice must come after the records it describes (notice at ${noticeAt}, ` +
      `end record at ${endAt}) - the log keeps scrolling, so a notice printed ` +
      "ahead of its own answer comes loose from it"
  );

  // The command ran and every printed line is real, so this is a warning about
  // the answer, not a failed command: it must not borrow the error styling the
  // page uses for a refused request.
  const noticeLine = lines[noticeAt];
  assert.ok(
    noticeLine.includes("log-line-command-cut"),
    `the notice is missing its own class: ${noticeLine}`
  );
  assert.ok(
    !noticeLine.includes("log-line-command-error"),
    `the notice is painted as an error: ${noticeLine}`
  );
});

test("a malformed answer is still just an error - no notice without a reply", async () => {
  // truncated:true with no records at all: there is no reply above for a
  // notice to belong to, and the page's existing malformed-answer line is the
  // honest report.
  const { run } = await consoleHarness(respondWith({ truncated: true }));

  const output = (await run("system.status.logs")).join("\n");

  assert.ok(output.includes("[ERROR]"), "a malformed console answer produced no visible line");
  assert.ok(
    !output.includes(CUT_NOTICE),
    "the page claimed lines were cut from a reply it never rendered"
  );
});

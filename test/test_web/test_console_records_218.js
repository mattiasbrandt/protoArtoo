// =============================================================================
// test/test_web/test_console_records_218.js
//
// Behavioural tests for the Live Logs console adapter in data/app.js (#218,
// ADR 0036). Loads the REAL app.js, drives the command box the way an operator
// does (Enter on #log-command-input), and asserts on the lines the page put in
// the log.
//
// The defect these exist for: the console endpoint answers through the shared
// request helper, whose result is {ok, status, data}. A page that reads the
// records off the envelope instead of off .data renders nothing at all and
// reports "invalid response format" for every command - a total failure that
// no source-text assertion notices, because the rendering code is still there
// and still correct.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";

// A status answer in the module's record shape (docs/console-protocol.md).
const HEALTH_RECORDS = [
  { id: 7, type: "begin", operation: "system.status.health" },
  { id: 7, type: "field", name: "estop", value: "false" },
  { id: 7, type: "field", name: "heapLargestBlock", value: "262144" },
  { id: 7, type: "end", status: "ok", outcome: "queued" },
];

// Loads app.js, records every line the page appends to the log, and returns a
// runner that types a command and presses Enter.
const consoleHarness = async (respond) => {
  const env = loadPageModule("app.js", { respond });
  await env.settle();

  const logConsole = env.element("log-console");
  const rendered = [];
  // appendLogLine writes the first line through innerHTML and every later one
  // through insertAdjacentHTML; capture both so line order is preserved.
  logConsole.insertAdjacentHTML = (_position, html) => rendered.push(html);
  const innerHtmlSeen = () => (logConsole.innerHTML ? [logConsole.innerHTML] : []);

  const run = async (command) => {
    const input = env.element("log-command-input");
    input.value = command;
    env.emitOn("log-command-input", "keydown", {
      key: "Enter",
      preventDefault() {},
      stopPropagation() {},
    });
    await env.settle(6);
    return [...innerHtmlSeen(), ...rendered].join("\n");
  };

  return { env, run };
};

test("console records from the endpoint are rendered as log lines", async () => {
  const { env, run } = await consoleHarness((path) => {
    if (path === "/api/console") {
      return { ok: true, status: 200, data: { records: HEALTH_RECORDS } };
    }
    return { data: {} };
  });

  const output = await run("system.status.health");

  assert.ok(
    !output.includes("invalid response format"),
    "the page rejected a well-formed record stream; it is reading the records " +
      "off the response envelope instead of off .data"
  );
  // Each record type reaches the log, with its own values intact.
  assert.ok(output.includes("type=begin"), "begin record missing from the log");
  assert.ok(output.includes("name=estop"), "field record missing from the log");
  assert.ok(output.includes("262144"), "field value missing from the log");
  assert.ok(output.includes("type=end"), "end record missing from the log");

  // The command went to the console endpoint, not the action test endpoint.
  const paths = env.pathsRequested();
  assert.ok(paths.includes("/api/console"), `expected /api/console, got ${paths.join(", ")}`);
  assert.ok(
    !paths.includes("/api/actions/test"),
    "/api/actions/test must stay untouched until #220"
  );
});

test("an error record is rendered, not swallowed", async () => {
  const { run } = await consoleHarness((path) => {
    if (path === "/api/console") {
      return {
        ok: true,
        status: 200,
        data: {
          records: [
            { id: 9, type: "result", status: "err", outcome: "invalid", reason: "unknown-operation" },
          ],
        },
      };
    }
    return { data: {} };
  });

  const output = await run("no.such.operation");
  assert.ok(output.includes("unknown-operation"), "the error reason never reached the log");
  assert.ok(output.includes("type=result"), "the result record never reached the log");
});

test("a refused request is visible, never silent", async () => {
  const { run } = await consoleHarness((path) => {
    if (path === "/api/console") {
      const error = new Error("Device unavailable");
      error.name = "ApiError";
      error.kind = "http";
      error.status = 503;
      throw error;
    }
    return { data: {} };
  });

  const output = await run("system.status.health");
  assert.ok(output.includes("[ERROR]"), "a refused console request produced no visible line");
});

test("a malformed answer is reported, not rendered as empty success", async () => {
  const { run } = await consoleHarness((path) => {
    if (path === "/api/console") {
      // Well-formed HTTP, nonsense body: no records array anywhere.
      return { ok: true, status: 200, data: { unexpected: true } };
    }
    return { data: {} };
  });

  const output = await run("system.status.health");
  assert.ok(
    output.includes("[ERROR]"),
    "a malformed console answer produced no visible line - the operator would " +
      "see the echoed command and nothing else"
  );
});

// Catalog and help text tests (#219)
test("operations command lists catalog entries", async () => {
  const { run } = await consoleHarness((path) => {
    if (path === "/api/console") {
      return {
        ok: true,
        status: 200,
        data: {
          records: [
            { id: 10, type: "begin", operation: "operations" },
            { id: 10, type: "item", value: "drive.action.move (action)" },
            { id: 10, type: "item", value: "system.status.health (status)" },
            { id: 10, type: "end", status: "ok", outcome: "completed" },
          ],
        },
      };
    }
    return { data: {} };
  });

  const output = await run("operations");
  assert.ok(
    output.includes("drive.action.move"),
    "operations list missing drive.action.move entry"
  );
  assert.ok(
    output.includes("system.status.health"),
    "operations list missing system.status.health entry"
  );
  assert.ok(
    output.includes("type=item"),
    "operations list item records missing from the log"
  );
});

test("help command renders operation details from catalog", async () => {
  const { run } = await consoleHarness((path) => {
    if (path === "/api/console") {
      return {
        ok: true,
        status: 200,
        data: {
          records: [
            { id: 11, type: "begin", operation: "drive.action.move" },
            { id: 11, type: "field", name: "type", value: "action" },
            { id: 11, type: "field", name: "display_name", value: "Move" },
            { id: 11, type: "field", name: "description", value: "Set drive speed and steering" },
            { id: 11, type: "end", status: "ok", outcome: "completed" },
          ],
        },
      };
    }
    return { data: {} };
  });

  const output = await run("help drive.action.move");
  assert.ok(
    output.includes("display_name"),
    "help output missing display_name field"
  );
  assert.ok(
    output.includes("Move"),
    "help output missing operation display name"
  );
  assert.ok(
    output.includes("description"),
    "help output missing description field"
  );
  assert.ok(
    output.includes("Set drive speed and steering"),
    "help output missing operation description"
  );
});

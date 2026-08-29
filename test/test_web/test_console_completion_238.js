// =============================================================================
// test/test_web/test_console_completion_238.js
//
// Behavioural tests for the Live Logs command box's Tab completion and
// persistent session history (#238, ADR 0034). Loads the REAL app.js and
// drives the command box the way an operator does (Tab/Enter/ArrowUp/ArrowDown
// on #log-command-input), asserting on the value the box ends up with and the
// requests the page actually made - never a reimplementation of the matching
// logic under test (test/test_web/README.md's own trap list).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";

// Shaped like POST /api/console { command: "operations" }'s real response
// (docs/console-protocol.md s.2 / src/web/api_console.cpp).
const operationsRecords = (names) => {
  const records = [{ id: 1, type: "begin", operation: "operations" }];
  for (const name of names) records.push({ id: 1, type: "item", value: `${name} (action)` });
  records.push({ id: 1, type: "end", status: "ok", outcome: "completed" });
  return records;
};

// Shaped like POST /api/console { command: "help <op>" }'s real response
// carrying a "params" field (console_module.cpp: "name:type:required|optional"
// comma-joined).
const helpRecordsWithParams = (opName, paramSpecs) => [
  { id: 2, type: "begin", operation: opName },
  { id: 2, type: "field", name: "type", value: "action" },
  { id: 2, type: "field", name: "params", value: paramSpecs.join(",") },
  { id: 2, type: "end", status: "ok", outcome: "completed" },
];

// help response for an operation the catalog knows has no arguments: no
// "params" field is emitted at all (console_module.cpp only emits it when
// entry->params != nullptr).
const helpRecordsNoParams = (opName) => [
  { id: 2, type: "begin", operation: opName },
  { id: 2, type: "field", name: "type", value: "action" },
  { id: 2, type: "end", status: "ok", outcome: "completed" },
];

// A stateful fake localStorage, so a test can simulate "the same browser
// storage surviving a page reload" by handing the same instance to a second
// loadPageModule() call.
const makeFakeStorage = () => {
  const store = new Map();
  return {
    getItem: (key) => (store.has(key) ? store.get(key) : null),
    setItem: (key, value) => store.set(key, String(value)),
    removeItem: (key) => store.delete(key),
    clear: () => store.clear(),
  };
};

// A localStorage stand-in for a browser with site data blocked: every call
// throws, the way some private-mode / storage-restricted browsers behave.
const throwingStorage = {
  getItem: () => {
    throw new Error("SecurityError: storage blocked");
  },
  setItem: () => {
    throw new Error("SecurityError: storage blocked");
  },
  removeItem: () => {
    throw new Error("SecurityError: storage blocked");
  },
  clear: () => {
    throw new Error("SecurityError: storage blocked");
  },
};

// Loads app.js, runs the app-console-catalog section (so consoleCatalogNames
// is populated the way the real page bootstrap would populate it), and
// returns the harness handles a test drives the command box through.
const consoleHarness = async ({ respond, overrides = {} } = {}) => {
  const env = loadPageModule("app.js", { respond, overrides });
  await env.settle();

  const logConsole = env.element("log-console");
  const rendered = [];
  logConsole.insertAdjacentHTML = (_position, html) => rendered.push(html);
  const innerHtmlSeen = () => (logConsole.innerHTML ? [logConsole.innerHTML] : []);
  const loggedOutput = () => [...innerHtmlSeen(), ...rendered].join("\n");

  const input = env.element("log-command-input");

  const pressTab = async () => {
    env.emitOn("log-command-input", "keydown", {
      key: "Tab",
      preventDefault() {},
    });
    await env.settle(6);
  };

  const pressEnter = async () => {
    env.emitOn("log-command-input", "keydown", {
      key: "Enter",
      preventDefault() {},
    });
    await env.settle(6);
  };

  const pressArrow = async (key) => {
    env.emitOn("log-command-input", "keydown", { key, preventDefault() {} });
    await env.settle(1);
  };

  return { env, input, pressTab, pressEnter, pressArrow, loggedOutput };
};

// -----------------------------------------------------------------------------
// Operation-name completion
// -----------------------------------------------------------------------------

test("Tab completes a unique operation name with a trailing space", async () => {
  const { env, input, pressTab } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords(["drive.action.move", "sound.action.random-humming"]) } };
      }
      return { data: {} };
    },
  });
  await env.runSection("app-console-catalog");

  input.value = "drive.action.mo";
  await pressTab();

  assert.equal(input.value, "drive.action.move ", "unique match must complete with a trailing space");
});

test("Ambiguous operation-name prefix extends to the common prefix, then lists on a second Tab", async () => {
  const { env, input, pressTab, loggedOutput } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords(["sound.action.random-happy", "sound.action.random-humming"]) } };
      }
      return { data: {} };
    },
  });
  await env.runSection("app-console-catalog");

  input.value = "sound.action.rando";
  await pressTab();
  assert.equal(
    input.value,
    "sound.action.random-h",
    "the first Tab must extend to the longest common prefix shared by both candidates"
  );
  assert.ok(!loggedOutput().includes("random-happy"), "candidates must not be listed before the common prefix is reached");

  await pressTab();
  const output = loggedOutput();
  assert.ok(output.includes("sound.action.random-happy"), "listing must include the first candidate");
  assert.ok(output.includes("sound.action.random-humming"), "listing must include the second candidate");
  assert.equal(input.value, "sound.action.random-h", "the typed line must be restored unchanged after listing");
});

// -----------------------------------------------------------------------------
// Argument-key completion
// -----------------------------------------------------------------------------

test("Tab in argument-key position fetches help for the single operation and completes without a trailing space", async () => {
  const { env, input, pressTab } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords(["drive.action.move"]) } };
      }
      if (path === "/api/console" && opts.body?.command === "help drive.action.move") {
        return { data: { records: helpRecordsWithParams("drive.action.move", ["speed:int16:required", "steer:int16:required"]) } };
      }
      return { data: {} };
    },
  });
  await env.runSection("app-console-catalog");

  input.value = "drive.action.move sp";
  await pressTab();

  assert.equal(
    input.value,
    "drive.action.move speed=",
    "an argument-key candidate must complete without a trailing space (key=value has no space around '=')"
  );
});

test("Argument-key completion resolves the operation from the FIRST token regardless of later tokens", async () => {
  const { env, input, pressTab } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords(["drive.action.move"]) } };
      }
      if (path === "/api/console" && opts.body?.command === "help drive.action.move") {
        return { data: { records: helpRecordsWithParams("drive.action.move", ["speed:int16:required", "steer:int16:required"]) } };
      }
      return { data: {} };
    },
  });
  await env.runSection("app-console-catalog");

  input.value = "drive.action.move speed=200 st";
  await pressTab();

  assert.equal(
    input.value,
    "drive.action.move speed=200 steer=",
    "the second argument key must still resolve against drive.action.move, not against 'st' as if it were an operation name"
  );
});

test("An operation with no params yields no candidates - Tab is a silent no-op", async () => {
  const { env, input, pressTab, loggedOutput } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords(["dome.seq.overload"]) } };
      }
      if (path === "/api/console" && opts.body?.command === "help dome.seq.overload") {
        return { data: { records: helpRecordsNoParams("dome.seq.overload") } };
      }
      return { data: {} };
    },
  });
  await env.runSection("app-console-catalog");

  input.value = "dome.seq.overload ";
  await pressTab();

  assert.equal(input.value, "dome.seq.overload ", "value must be unchanged when there are no candidates");
  assert.equal(loggedOutput(), "", "zero candidates must produce no log line - matches the serial adapter exactly");
});

// -----------------------------------------------------------------------------
// Failure and recovery states
// -----------------------------------------------------------------------------

test("A catalog not yet loaded makes Tab a silent no-op, not an error", async () => {
  // Deliberately does NOT run the app-console-catalog section: this is the
  // "resource absent" state - the operator Tabs before the catalog fetch
  // (still pending, or refused) has resolved.
  const { input, pressTab, loggedOutput } = await consoleHarness({
    respond: () => ({ data: {} }),
  });

  input.value = "drive";
  await pressTab();

  assert.equal(input.value, "drive", "value must be unchanged with no catalog loaded");
  assert.equal(loggedOutput(), "", "an unloaded catalog must not surface an error from a Tab press");
});

test("A failed argument-key fetch is not cached - the next Tab press retries and can succeed", async () => {
  let helpCallCount = 0;
  const { env, input, pressTab } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords(["drive.action.move"]) } };
      }
      if (path === "/api/console" && opts.body?.command === "help drive.action.move") {
        helpCallCount += 1;
        if (helpCallCount === 1) {
          const error = new Error("Device unavailable");
          error.name = "ApiError";
          error.kind = "http";
          error.status = 503;
          throw error;
        }
        return { data: { records: helpRecordsWithParams("drive.action.move", ["speed:int16:required"]) } };
      }
      return { data: {} };
    },
  });
  await env.runSection("app-console-catalog");

  input.value = "drive.action.move sp";
  await pressTab();
  assert.equal(input.value, "drive.action.move sp", "a failed fetch must leave the line unchanged, not partially completed");

  // Same value, second attempt: must retry the network call rather than
  // reusing a cached empty result from the failure above.
  await pressTab();
  assert.equal(input.value, "drive.action.move speed=", "a retried Tab press after recovery must complete normally");
  assert.equal(helpCallCount, 2, "the failed fetch must not have been cached");
});

// -----------------------------------------------------------------------------
// Enter never autocompletes (regression, mirrors #213's serial-side guarantee)
// -----------------------------------------------------------------------------

test("Enter dispatches exactly the typed text, even on an ambiguous unsubmitted prefix", async () => {
  const { env, input, pressEnter } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords(["sound.action.random-happy", "sound.action.random-humming"]) } };
      }
      if (path === "/api/console") {
        return { data: { records: [{ id: 3, type: "result", status: "err", outcome: "invalid", reason: "unknown-operation" }] } };
      }
      return { data: {} };
    },
  });
  await env.runSection("app-console-catalog");

  input.value = "sound.action.random-h";
  await pressEnter();

  const dispatchCall = env.requests.find(
    (r) => r.path === "/api/console" && r.opts?.body?.command !== "operations"
  );
  assert.ok(dispatchCall, "Enter must dispatch a command");
  assert.equal(
    dispatchCall.opts.body.command,
    "sound.action.random-h",
    "Enter must dispatch exactly what was typed, never a silently-expanded candidate"
  );
  assert.equal(input.value, "", "the command box must clear after dispatch");
});

// -----------------------------------------------------------------------------
// Persistent session history
// -----------------------------------------------------------------------------

test("Command history survives a page reload via localStorage", async () => {
  const sharedStorage = makeFakeStorage();
  const respond = (path, opts = {}) => {
    if (path === "/api/console" && opts.body?.command === "operations") {
      return { data: { records: operationsRecords([]) } };
    }
    if (path === "/api/console") {
      return { data: { records: [{ id: 4, type: "result", status: "ok", outcome: "queued" }] } };
    }
    return { data: {} };
  };

  const first = await consoleHarness({ respond, overrides: { localStorage: sharedStorage } });
  first.input.value = "sound.action.random-humming";
  await first.pressEnter();

  // A second page load ("reload") against the SAME storage instance.
  const second = await consoleHarness({ respond, overrides: { localStorage: sharedStorage } });
  await second.pressArrow("ArrowUp");

  assert.equal(
    second.input.value,
    "sound.action.random-humming",
    "history from a previous page load must be available immediately after reload"
  );
});

test("History beyond the maximum is trimmed before being persisted", async () => {
  const sharedStorage = makeFakeStorage();
  const respond = (path, opts = {}) => {
    if (path === "/api/console" && opts.body?.command === "operations") {
      return { data: { records: operationsRecords([]) } };
    }
    if (path === "/api/console") {
      return { data: { records: [{ id: 5, type: "result", status: "ok", outcome: "queued" }] } };
    }
    return { data: {} };
  };

  const { input, pressEnter } = await consoleHarness({ respond, overrides: { localStorage: sharedStorage } });
  for (let i = 0; i < 25; i += 1) {
    input.value = `system.action.set-mood mood=${i}`;
    await pressEnter();
  }

  const stored = JSON.parse(sharedStorage.getItem("pa-console-history"));
  assert.equal(stored.length, 20, "stored history must be trimmed to the maximum");
  assert.equal(
    stored[stored.length - 1],
    "system.action.set-mood mood=24",
    "the most recent command must be kept, not an arbitrary one"
  );
  assert.ok(
    !stored.includes("system.action.set-mood mood=0"),
    "the oldest command beyond the maximum must have been dropped"
  );
});

test("With no history, ArrowUp and ArrowDown do nothing", async () => {
  const { input, pressArrow } = await consoleHarness({ respond: () => ({ data: {} }) });

  input.value = "";
  await pressArrow("ArrowUp");
  assert.equal(input.value, "", "ArrowUp with empty history must not change the value");
  await pressArrow("ArrowDown");
  assert.equal(input.value, "", "ArrowDown with empty history must not change the value");
});

test("Site data blocked: the command box still works, just without persistence", async () => {
  const { env, input, pressEnter, pressArrow, loggedOutput } = await consoleHarness({
    respond: (path, opts = {}) => {
      if (path === "/api/console" && opts.body?.command === "operations") {
        return { data: { records: operationsRecords([]) } };
      }
      if (path === "/api/console") {
        return { data: { records: [{ id: 6, type: "result", status: "ok", outcome: "queued" }] } };
      }
      return { data: {} };
    },
    overrides: { localStorage: throwingStorage },
  });

  // Page load itself must not have thrown (asserted implicitly by reaching
  // here - env.settle() inside consoleHarness would have rejected).
  input.value = "system.action.estop";
  await assert.doesNotReject(async () => {
    await pressEnter();
  }, "dispatch must not throw when storage access throws");
  assert.ok(loggedOutput().includes("system.action.estop"), "the command must still be echoed and dispatched");

  input.value = "second.command";
  await assert.doesNotReject(async () => {
    await pressEnter();
  });

  // In-memory history for THIS session still works even though nothing
  // persisted.
  await pressArrow("ArrowUp");
  assert.equal(input.value, "second.command", "in-session history must still navigate even with storage blocked");
});

// =============================================================================
// test/test_web/test_console_secret_history_227.js
//
// Behavioural tests for the browser Console adapter's half of #227's
// write-exclusion rule: Tab never offers a write-excluded argument key, and a
// line that assigns one is never kept in the command history - which on this
// adapter means localStorage, so it would otherwise survive a page reload.
//
// Loads the REAL data/app.js and drives the command box the way an operator
// does (Tab / Enter / ArrowUp on #log-command-input), asserting on what the
// box ends up with and on what the page actually stored and requested - never
// on a reimplementation of the rule under test (test/test_web/README.md).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { loadPageModule } from "./helpers/page_module_env.js";

const operationsRecords = (names) => {
  const records = [{ id: 1, type: "begin", operation: "operations" }];
  for (const name of names) records.push({ id: 1, type: "item", value: `${name} (config)` });
  records.push({ id: 1, type: "end", status: "ok", outcome: "completed" });
  return records;
};

// Shaped like POST /api/console { command: "help <op>" }'s real response.
// console_module.cpp renders a write-excluded parameter's disposition as
// "write-excluded" IN PLACE OF required/optional - that third token is the
// browser's whole view of the catalog's write_excluded flag.
const helpRecordsWithParams = (opName, paramSpecs) => [
  { id: 2, type: "begin", operation: opName },
  { id: 2, type: "field", name: "type", value: "config" },
  { id: 2, type: "field", name: "params", value: paramSpecs.join(",") },
  { id: 2, type: "end", status: "ok", outcome: "completed" },
];

// The shipped wifi.config.settings params, in registry order, except that the
// excluded key is deliberately NOT last here: an excluded parameter followed
// by a settable one is the case that catches a skip which truncates the rest
// of the list, and it is the case the real catalog cannot exercise today.
const WIFI_PARAMS = [
  "mode:string:optional",
  "sta-password:string:write-excluded",
  "sta-ssid:string:optional",
  "ap-ssid:string:optional",
  "ap-password:string:write-excluded",
];

const REFUSED_LINE = 'wifi.config.settings sta-password=hunter2';
const ACCEPTED_LINE = 'wifi.config.settings mode=client sta-ssid="Workshop WiFi"';

const makeFakeStorage = () => {
  const store = new Map();
  return {
    getItem: (key) => (store.has(key) ? store.get(key) : null),
    setItem: (key, value) => store.set(key, String(value)),
    removeItem: (key) => store.delete(key),
    clear: () => store.clear(),
  };
};

const secretRefusalRecords = () => [
  { id: 3, type: "begin", operation: "wifi.config.settings" },
  { id: 3, type: "field", name: "argument", value: "sta-password" },
  { id: 3, type: "end", status: "err", outcome: "invalid", reason: "secret-not-settable" },
];

// respond() for a page where wifi.config.settings is the only operation, its
// help is answerable, and any dispatch answers the way the firmware does.
const wifiResponder = ({ helpFails = false, params = WIFI_PARAMS } = {}) => (path, opts = {}) => {
  const command = opts.body?.command;
  if (path !== "/api/console") return { data: {} };
  if (command === "operations") {
    return { data: { records: operationsRecords(["wifi.config.settings"]) } };
  }
  if (command === "help wifi.config.settings") {
    if (helpFails) {
      const error = new Error("Device unavailable");
      error.name = "ApiError";
      error.kind = "http";
      error.status = 503;
      throw error;
    }
    return { data: { records: helpRecordsWithParams("wifi.config.settings", params) } };
  }
  return { data: { records: secretRefusalRecords() } };
};

const consoleHarness = async ({ respond, overrides = {} } = {}) => {
  const env = loadPageModule("app.js", { respond, overrides });
  await env.settle();

  const logConsole = env.element("log-console");
  const rendered = [];
  logConsole.insertAdjacentHTML = (_position, html) => rendered.push(html);
  const loggedOutput = () =>
    [...(logConsole.innerHTML ? [logConsole.innerHTML] : []), ...rendered].join("\n");

  const input = env.element("log-command-input");

  const press = async (key, settles = 6) => {
    env.emitOn("log-command-input", "keydown", { key, preventDefault() {} });
    await env.settle(settles);
  };

  return {
    env,
    input,
    loggedOutput,
    pressTab: () => press("Tab"),
    pressEnter: () => press("Enter"),
    pressArrow: (key) => press(key),
  };
};

const storedHistory = (storage) => {
  const raw = storage.getItem("pa-console-history");
  return raw === null ? null : JSON.parse(raw);
};

// -----------------------------------------------------------------------------
// Tab must not offer a write-excluded key
// -----------------------------------------------------------------------------

test("Tab does not offer a write-excluded argument key", async () => {
  const { env, input, pressTab } = await consoleHarness({ respond: wifiResponder() });
  await env.runSection("app-console-catalog");

  input.value = "wifi.config.settings sta-pass";
  await pressTab();

  assert.equal(
    input.value,
    "wifi.config.settings sta-pass",
    "a write-excluded key must not complete - the Console can only refuse it"
  );
});

test("Skipping a write-excluded key does not truncate the keys after it", async () => {
  const { env, input, pressTab } = await consoleHarness({ respond: wifiResponder() });
  await env.runSection("app-console-catalog");

  // sta-ssid and ap-ssid both follow the excluded sta-password in the help
  // response; both must still be offered.
  input.value = "wifi.config.settings sta-s";
  await pressTab();
  assert.equal(input.value, "wifi.config.settings sta-ssid=", "a key after the excluded one must still complete");

  input.value = "wifi.config.settings ap-s";
  await pressTab();
  assert.equal(input.value, "wifi.config.settings ap-ssid=", "the last key must still complete");
});

test("Tab on the bare operation lists only the settable keys", async () => {
  const { env, input, pressTab, loggedOutput } = await consoleHarness({ respond: wifiResponder() });
  await env.runSection("app-console-catalog");

  // Ambiguous: several candidates share no longer prefix, so the second Tab
  // lists them - the listing is the operator-visible candidate set.
  input.value = "wifi.config.settings ";
  await pressTab();
  await pressTab();

  const listed = loggedOutput();
  assert.ok(listed.includes("mode="), "a settable key must be listed");
  assert.ok(listed.includes("sta-ssid="), "a settable key after the excluded one must be listed");
  assert.ok(listed.includes("ap-ssid="), "every settable key must be listed");
  assert.ok(!listed.includes("password"), "no write-excluded key may appear in the listing");
});

// -----------------------------------------------------------------------------
// A refused line must not reach the persisted history
// -----------------------------------------------------------------------------

test("A refused password line is never stored, in memory or in localStorage", async () => {
  const storage = makeFakeStorage();
  const { env, input, pressEnter, pressArrow } = await consoleHarness({
    respond: wifiResponder(),
    overrides: { localStorage: storage },
  });
  await env.runSection("app-console-catalog");

  input.value = ACCEPTED_LINE;
  await pressEnter();
  input.value = REFUSED_LINE;
  await pressEnter();

  const stored = storedHistory(storage) ?? [];
  assert.ok(
    !JSON.stringify(stored).includes("hunter2"),
    "the password must not be anywhere in persisted history"
  );
  assert.deepEqual(stored, [ACCEPTED_LINE], "only the accepted line may be stored");

  // In-memory history is the same ring the arrows walk: Up must reach the
  // accepted line, not the refused one.
  await pressArrow("ArrowUp");
  assert.equal(input.value, ACCEPTED_LINE, "ArrowUp must recall the last accepted line");
  await pressArrow("ArrowUp");
  assert.equal(input.value, ACCEPTED_LINE, "there must be nothing older to recall");
});

test("The refused line is still dispatched - the firmware is what refuses it", async () => {
  const { env, input, pressEnter, loggedOutput } = await consoleHarness({ respond: wifiResponder() });
  await env.runSection("app-console-catalog");

  input.value = REFUSED_LINE;
  await pressEnter();

  const dispatch = env.requests.find(
    (r) => r.path === "/api/console" && r.opts?.body?.command === REFUSED_LINE
  );
  assert.ok(dispatch, "not remembering a command must not stop it being sent");
  assert.ok(
    loggedOutput().includes("secret-not-settable"),
    "the operator must still see the refusal"
  );
});

test("A refused line is absent from history after a page reload", async () => {
  const storage = makeFakeStorage();
  const respond = wifiResponder();

  const first = await consoleHarness({ respond, overrides: { localStorage: storage } });
  await first.env.runSection("app-console-catalog");
  first.input.value = ACCEPTED_LINE;
  await first.pressEnter();
  first.input.value = REFUSED_LINE;
  await first.pressEnter();

  const second = await consoleHarness({ respond, overrides: { localStorage: storage } });
  await second.pressArrow("ArrowUp");
  assert.equal(second.input.value, ACCEPTED_LINE, "the reloaded page must recall the accepted line");
  await second.pressArrow("ArrowUp");
  assert.equal(second.input.value, ACCEPTED_LINE, "the refused line must not be behind it");
});

// -----------------------------------------------------------------------------
// The rule refuses lines, not operations
// -----------------------------------------------------------------------------

test("A settable line for the same operation is stored normally", async () => {
  const storage = makeFakeStorage();
  const { env, input, pressEnter } = await consoleHarness({
    respond: wifiResponder(),
    overrides: { localStorage: storage },
  });
  await env.runSection("app-console-catalog");

  input.value = ACCEPTED_LINE;
  await pressEnter();

  assert.deepEqual(
    storedHistory(storage),
    [ACCEPTED_LINE],
    "an operation that HAS a write-excluded parameter is still fully usable"
  );
});

test("A write-excluded key inside a quoted value is not an assignment", async () => {
  const storage = makeFakeStorage();
  const { env, input, pressEnter } = await consoleHarness({
    respond: wifiResponder(),
    overrides: { localStorage: storage },
  });
  await env.runSection("app-console-catalog");

  const line = 'wifi.config.settings sta-ssid="lab sta-password=x"';
  input.value = line;
  await pressEnter();

  assert.deepEqual(
    storedHistory(storage),
    [line],
    "the whole quoted run is one SSID - refusing it would drop a line carrying no secret"
  );
});

test("An unterminated quote does not hide the assignment after it", async () => {
  const storage = makeFakeStorage();
  const { env, input, pressEnter } = await consoleHarness({
    respond: wifiResponder(),
    overrides: { localStorage: storage },
  });
  await env.runSection("app-console-catalog");

  // The operator opened a quote and never closed it. Walking that run to the
  // end of the line would swallow the sta-password= assignment with it, and
  // the plaintext password would land in localStorage - where it survives a
  // reload. The firmware refuses this line as malformed, so nothing is lost
  // by not remembering it.
  input.value = 'wifi.config.settings sta-ssid="lab sta-password=hunter2';
  await pressEnter();

  assert.equal(
    storedHistory(storage),
    null,
    "an unterminated quote must not hide a write-excluded assignment behind it"
  );
});

test("A case variant of a write-excluded key is refused too", async () => {
  const storage = makeFakeStorage();
  const { env, input, pressEnter } = await consoleHarness({
    respond: wifiResponder(),
    overrides: { localStorage: storage },
  });
  await env.runSection("app-console-catalog");

  input.value = "wifi.config.settings sta-Password=hunter2";
  await pressEnter();

  assert.equal(
    storedHistory(storage),
    null,
    "the firmware refuses this line case-insensitively, so history must not keep it"
  );
});

// -----------------------------------------------------------------------------
// What happens when the rule cannot be established
// -----------------------------------------------------------------------------

test("A line with arguments is not stored when the operation's parameters cannot be fetched", async () => {
  const storage = makeFakeStorage();
  const { env, input, pressEnter } = await consoleHarness({
    respond: wifiResponder({ helpFails: true }),
    overrides: { localStorage: storage },
  });
  await env.runSection("app-console-catalog");

  input.value = ACCEPTED_LINE;
  await pressEnter();

  assert.equal(
    storedHistory(storage),
    null,
    "an undeterminable line must not be persisted - the failure mode of guessing wrong is a password on disk"
  );
});

test("An argument-less line is stored without asking for the operation's parameters", async () => {
  const storage = makeFakeStorage();
  const { env, input, pressEnter } = await consoleHarness({
    respond: wifiResponder({ helpFails: true }),
    overrides: { localStorage: storage },
  });
  await env.runSection("app-console-catalog");

  input.value = "wifi.config.settings";
  await pressEnter();

  assert.deepEqual(
    storedHistory(storage),
    ["wifi.config.settings"],
    "a line with no key=value pair can assign nothing, so it needs no lookup and is always storable"
  );
  assert.ok(
    !env.requests.some((r) => String(r.opts?.body?.command ?? "").startsWith("help ")),
    "a read with no arguments must not cost a help fetch"
  );
});

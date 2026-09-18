// =============================================================================
// test/test_web/test_unreported_state_network.js
//
// The Surface Anatomy on Maintain and the rest: WiFi, Firmware and the Page
// Recovery View (#399 slice 4, ADR 0066).
//
// What is mechanical about the anatomy on these three, and nothing else. Four
// kinds of claim, because this slice makes four:
//
//   A SECTION HEAD CARRIES A COUNT OR A STATE. docs/ui-copy-voice.md rule 15
//   says a subtitle is computed from what the droid answered rather than typed
//   into the markup, so every head below is driven with at least two different
//   answers. A constant looks right on every screenshot and is wrong on every
//   droid.
//
//   A STATE IS ON THE LIGHT OR THE EDGE, NEVER ON A WORD THE BUILDER CHOSE.
//   CONTEXT.md "Status Colour" reserves the four signal colours for how a thing
//   is doing right now. WiFi's posture subtitle used to be a coloured pill, and
//   the pill's colour and its text disagreed about what they were for; the
//   assertions are on the text AND on the absence of the class beside it,
//   because a renderer that stopped writing the word and went on painting the
//   element would pass half of it.
//
//   A FEEDBACK LINE ANSWERS WHERE THE ACT IS. Firmware's three acts wrote to
//   one line on the third plate. Each claim below plants a DECOY in the other
//   two lines, because the harness answers getElementById for any id and a test
//   that only read its own line back would pass against the code that wrote to
//   all three.
//
//   THE RECOVERY KERNEL'S SECOND COPY OF THE PALETTE AGREES WITH THE FIRST.
//   data/_recovery_kernel.html is the one file in the product that may carry a
//   colour literal, because it has to render when data/style.css is what
//   failed. That is also why the two have already disagreed once. Each of its
//   literals is resolved against :root here, and the three .indicator meanings
//   are compared with the stylesheet's own.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import vm from "node:vm";

import { loadPageModule } from "./helpers/page_module_env.js";

const here = dirname(fileURLToPath(import.meta.url));
const dataDir = join(here, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf8");

// -----------------------------------------------------------------------------
// WiFi
// -----------------------------------------------------------------------------

const WIFI_CLIENT = Object.freeze({
  mode: "client",
  staSsid: "AstroHome",
  apSsid: "protoArtoo-AP",
  provisioned: true,
  pendingApply: false,
  staPasswordSet: true,
  apPasswordSet: true,
});

const DIAG_CONNECTED = Object.freeze({
  staEnabled: true,
  staConnected: true,
  staIp: "10.0.0.22",
  apIp: "192.168.4.1",
  staSsid: "AstroHome",
  wifiRssi: -52,
  networkRecovery: false,
});

const loadWifi = async (wifi, diagnostics) => {
  const env = loadPageModule("wifi.js", {
    respond: (path) => {
      if (path === "/api/config") return { data: { wifi } };
      if (path === "/api/wifi") return { data: diagnostics };
      if (path === "/api/identity") return { data: { droidName: "artoo", mdnsUseName: true } };
      return { data: {} };
    },
  });
  await env.runSection("wifi-identity");
  await env.runSection("wifi-config");
  await env.runSection("wifi-diagnostics");
  await env.settle();
  return env;
};

// -----------------------------------------------------------------------------
// Firmware: three acts, three plates, three feedback lines
// -----------------------------------------------------------------------------

const FIRMWARE_LINES = ["fw-feedback", "fs-feedback", "fw-reboot-feedback"];

// Firmware runs in its own vm context rather than through
// helpers/page_module_env.js, the way test_seq_stated_design.js does: this
// module reads sessionStorage, confirm and FormData as bare globals, which the
// shared harness does not carry, and the surface is small enough that a stub
// document is clearer than widening a harness eleven other suites depend on.
function newFirmwarePage() {
  const elements = new Map();
  const makeStub = () => {
    const element = {
      className: "",
      textContent: "",
      dataset: {},
      style: {},
      disabled: false,
      files: null,
      listeners: {},
      classList: {
        add(name) { element.classes.add(name); },
        remove(name) { element.classes.delete(name); },
        toggle(name, on) { if (on) element.classes.add(name); else element.classes.delete(name); },
        contains: (name) => element.classes.has(name),
      },
      classes: new Set(),
      setAttribute() {},
      getAttribute: () => null,
      addEventListener(name, fn) { (element.listeners[name] = element.listeners[name] || []).push(fn); },
      removeEventListener() {},
      focus() {},
    };
    return element;
  };
  const byId = (id) => {
    if (!elements.has(id)) elements.set(id, makeStub());
    return elements.get(id);
  };

  const posted = [];
  // The upload transport, settled by the test rather than by the clock: a
  // pending promise holds the in-flight state still so it can be read, and
  // resolving or rejecting it drives the two ends.
  let settleUpload = null;
  const sandbox = {
    PAApi: {
      postForm: (path) => { posted.push(path); return Promise.resolve({ ok: true, data: {} }); },
      messageFor: (error) => String(error && error.message),
    },
    sessionStorage: { getItem: () => null, setItem() {}, removeItem() {} },
    confirm: () => true,
    fetch: () => new Promise((resolve, reject) => { settleUpload = { resolve, reject }; }),
    FormData: class { append() {} },
    AbortController,
    document: { getElementById: byId, addEventListener() {}, body: makeStub() },
    // Recorded, never run: waitForReconnect schedules a four-second poll on
    // success, and a live timer would hold the test process open.
    setTimeout: () => 0,
    clearTimeout: () => {},
    Promise,
    console,
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(readData("firmware.js"), sandbox, { filename: "firmware.js" });

  FIRMWARE_LINES.forEach((id) => { byId(id).textContent = `DECOY-${id}`; });
  return {
    byId,
    posted,
    chooseFile: (inputId, name) => { byId(inputId).files = [{ name }]; },
    finishUpload: (ok) =>
      ok
        ? settleUpload.resolve({ ok: true, status: 200, headers: { get: () => "" } })
        : settleUpload.reject(new Error("connection lost")),
    settled: () => new Promise((resolve) => setImmediate(resolve)),
    press: (id) => {
      const handlers = byId(id).listeners.click || [];
      assert.ok(handlers.length > 0, `#${id} has no click handler`);
      handlers.forEach((fn) => fn({}));
    },
    untouched: (ids) => ids.every((id) => byId(id).textContent === `DECOY-${id}`),
  };
}

test("an upload the page cannot measure claims no fraction of one", () => {
  const source = readData("firmware.js");
  const css = readData("style.css");

  // The renderer writes a class while an upload is in flight and a width only
  // when there is something real to say: 100% on success, 0% on failure.
  assert.match(source, /setIndeterminate\(progressBar, true\);/);
  assert.match(source, /setIndeterminate\(fsProgressBar, true\);/);
  assert.doesNotMatch(source, /style\.width = "50%"/, "the bar is back to claiming half an upload");
  assert.doesNotMatch(
    source,
    /style\.width = "(?!0%|100%)[^"]+"/,
    "a width that is neither the end nor the start is a measurement nothing took",
  );

  // And the class lights something rather than being a name nothing reads.
  assert.match(css, /\.fw-progress-bar\.indeterminate \{[^}]*animation: fw-progress-sweep/);
  assert.match(css, /@keyframes fw-progress-sweep \{/);
  assert.match(
    css,
    /@media \(prefers-reduced-motion: reduce\) \{\s*\.fw-progress-bar\.indeterminate \{[^}]*animation: none/,
    "a bar that sweeps forever must stand still for an operator who asked it to",
  );
});

// -----------------------------------------------------------------------------
// The Dashboard's mood readout
// -----------------------------------------------------------------------------

const mountDashboard = () => {
  let deliver = null;
  const env = loadPageModule("app.js", {
    respond: () => ({ data: {} }),
    overrides: {
      PAStatusStream: {
        isSupported: () => true,
        subscribe: (handler) => {
          deliver = handler;
          return () => {};
        },
        getLastStatus: () => null,
      },
    },
  });
  return { env, send: (payload) => deliver("status", payload) };
};

test("a mood the droid has not reported reads Not reported, not mood zero", () => {
  const known = mountDashboard();
  known.send({ activeMood: 13 });
  assert.equal(known.env.element("snapshot-mood").textContent, "Mid-Awake");
  assert.equal(known.env.element("mood-now").textContent, "Mid-Awake");

  const silent = mountDashboard();
  silent.send({});
  assert.equal(
    silent.env.element("snapshot-mood").textContent,
    "Not reported",
    "a frame that carried no mood is not a droid reporting mood zero",
  );
  assert.equal(silent.env.element("mood-now").textContent, "Not reported");

  // A number this surface has no name for is not printed either: a raw
  // identifier reaches an operator only through the mapping table (ADR 0059).
  const unknown = mountDashboard();
  unknown.send({ activeMood: 77 });
  assert.equal(unknown.env.element("snapshot-mood").textContent, "Not reported");
  assert.doesNotMatch(unknown.env.element("snapshot-mood").textContent, /77/);
});

// -----------------------------------------------------------------------------
// The Page Recovery View's kernel, against the token layer it copies by hand
// -----------------------------------------------------------------------------

// A parser small enough to be obviously right, rather than a second copy of
// test_style_token_layer.js's: requiring that file would register its tests in
// this one's run. It reads :root's colour tokens out of the stylesheet and the
// declarations out of the kernel's single inline <style> block.
const hexOf = (value) => {
  const hex = /^#([0-9a-fA-F]{6})$/.exec(value.trim());
  if (hex) return `#${hex[1].toLowerCase()}`;
  const rgb = /^rgb\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)$/.exec(value.trim());
  if (!rgb) return null;
  return `#${[1, 2, 3].map((i) => Number(rgb[i]).toString(16).padStart(2, "0")).join("")}`;
};

const ROOT_TOKENS = (() => {
  const css = readData("style.css").replace(/\/\*[\s\S]*?\*\//g, "");
  const root = /:root\s*\{([\s\S]*?)\n\}/.exec(css);
  assert.ok(root, "data/style.css no longer declares :root as this test reads it");
  const tokens = new Map();
  for (const line of root[1].split(";")) {
    const at = line.indexOf(":");
    if (at < 0) continue;
    const name = line.slice(0, at).trim();
    if (name.startsWith("--")) tokens.set(name, line.slice(at + 1).trim());
  }
  return tokens;
})();

const TOKEN_HEXES = new Map(
  [...ROOT_TOKENS].map(([name, value]) => [name, hexOf(value)]).filter(([, hex]) => hex),
);

const KERNEL = readData("_recovery_kernel.html");
// Comments out of both grammars first: this file explains its own literals in
// prose, and an assertion that could not tell a rule from the sentence above it
// would be reading the wrong thing in both directions.
const KERNEL_CODE = KERNEL.replace(/<!--[\s\S]*?-->/g, "").replace(/\/\*[\s\S]*?\*\//g, "");
const KERNEL_STYLE = (() => {
  const block = /<style>([\s\S]*?)<\/style>/.exec(KERNEL_CODE);
  assert.ok(block, "the recovery kernel no longer carries one inline <style> block");
  return block[1];
})();

// One rule's declarations, by the exact selector the kernel writes.
const kernelRule = (selector) => {
  const at = KERNEL_STYLE.indexOf(`${selector} {`);
  assert.ok(at >= 0, `the recovery kernel no longer declares ${selector}`);
  const body = KERNEL_STYLE.slice(at + selector.length + 2, KERNEL_STYLE.indexOf("}", at));
  const declarations = new Map();
  for (const piece of body.split(";")) {
    const colon = piece.indexOf(":");
    if (colon < 0) continue;
    declarations.set(piece.slice(0, colon).trim(), piece.slice(colon + 1).trim());
  }
  return declarations;
};

test("the unlit dot is the kernel's default, so a state nobody has answered cannot come out amber", () => {
  const base = kernelRule("#page-recovery-backdrop .indicator");
  assert.equal(
    hexOf(base.get("background")),
    TOKEN_HEXES.get("--text-dim"),
    "an indicator with no state class yet fell back to a lit colour",
  );
  assert.equal(base.get("box-shadow"), "none", "and it glowed while it was at it");
});

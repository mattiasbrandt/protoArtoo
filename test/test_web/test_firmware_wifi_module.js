// WiFi Module Update card on the Firmware page (#241).
//
// Loads shipped data/firmware.js. Green is not evidence: mutations in
// test/test_web/mutations/241/ must turn this suite red.

import { test } from "node:test";
import assert from "node:assert/strict";
import vm from "node:vm";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "../..");
const firmwareSrc = readFileSync(join(root, "data/firmware.js"), "utf8");

const makeClassList = (el) => {
  const names = new Set();
  return {
    add: (...list) => list.forEach((n) => names.add(n)),
    remove: (...list) => list.forEach((n) => names.delete(n)),
    contains: (n) => names.has(n),
    toggle: (n, force) => {
      if (force === true) names.add(n);
      else if (force === false) names.delete(n);
      else if (names.has(n)) names.delete(n);
      else names.add(n);
    },
    toString: () => [...names].join(" "),
  };
};

const makeEl = (id) => {
  const attributes = new Map();
  const el = {
    id,
    textContent: "",
    className: "",
    hidden: false,
    disabled: false,
    inert: false,
    style: {},
    dataset: {},
    files: null,
    listeners: new Map(),
    classList: null,
    setAttribute(name, value) {
      attributes.set(name, String(value));
      if (name === "aria-hidden") el.ariaHidden = String(value);
    },
    getAttribute(name) {
      return attributes.has(name) ? attributes.get(name) : null;
    },
    addEventListener(type, handler) {
      if (!el.listeners.has(type)) el.listeners.set(type, []);
      el.listeners.get(type).push(handler);
    },
    click() {
      for (const handler of el.listeners.get("click") || []) handler({ target: el });
    },
    ariaHidden: null,
  };
  el.classList = makeClassList(el);
  return el;
};

const hostedIdentity = {
  droidName: "r2",
  board: "firebeetle2",
  board_capabilities: { PA_CAP_NATIVE_WIFI: false, PA_CAP_HOSTED_WIFI: true },
  build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
};

const artooIdentity = {
  droidName: "artoo",
  board: "artoo_esp32",
  board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
  build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
};

const loadFirmware = ({ identity = null, statusBody = null, upload = null } = {}) => {
  const elements = new Map();
  const ids = [
    "fw-file", "upload-fw-button", "reboot-button", "fw-progress", "fw-bar", "fw-status",
    "fw-feedback", "fs-file", "upload-fs-button", "fs-progress", "fs-bar", "fs-status",
    "wifi-module-card", "wm-availability-status", "wm-availability-lamp",
    "wm-availability-reason", "wm-content", "wm-support-line", "wm-version-line",
    "wm-file", "wm-progress", "wm-bar", "wm-status", "wm-feedback", "upload-wm-button",
  ];
  for (const id of ids) elements.set(id, makeEl(id));

  const windowListeners = new Map();
  const fetches = [];
  const timers = [];
  const sessionStore = new Map();

  const fetchImpl = (url, opts = {}) => {
    const path = String(url);
    fetches.push({ path, opts });
    if (path === "/api/status") {
      return Promise.resolve({
        ok: true,
        json: () => Promise.resolve(statusBody || {}),
        headers: { get: () => "application/json" },
      });
    }
    if (path === "/upload/wifi-module") {
      const result = typeof upload === "function" ? upload() : upload;
      if (result && result.error) {
        return Promise.resolve({
          ok: false,
          status: result.status || 409,
          headers: { get: () => "application/json" },
          json: () => Promise.resolve({ ok: false, error: result.error }),
        });
      }
      return Promise.resolve({
        ok: true,
        status: 200,
        headers: { get: () => "application/json" },
        json: () => Promise.resolve({ ok: true, bytes: 12 }),
      });
    }
    return Promise.reject(new Error(`unexpected fetch ${path}`));
  };

  const flush = async (times = 12) => {
    for (let i = 0; i < times; i += 1) {
      await Promise.resolve();
    }
  };

  const context = {
    Object,
    Promise,
    JSON,
    Error,
    Map,
    Set,
    window: null,
    document: {
      getElementById: (id) => elements.get(id) || null,
    },
    console,
    fetch: fetchImpl,
    FormData: class {
      constructor() { this.parts = []; }
      append(name, file, filename) { this.parts.push({ name, file, filename }); }
    },
    confirm: () => true,
    sessionStorage: {
      getItem: (k) => (sessionStore.has(k) ? sessionStore.get(k) : null),
      setItem: (k, v) => sessionStore.set(k, String(v)),
      removeItem: (k) => sessionStore.delete(k),
    },
    AbortController: class {
      constructor() { this.signal = { aborted: false }; }
      abort() { this.signal.aborted = true; }
    },
    setTimeout: (fn, ms) => {
      const id = timers.length + 1;
      timers.push({ id, fn, ms });
      return id;
    },
    clearTimeout: () => {},
    CustomEvent: class {
      constructor(type, init) { this.type = type; this.detail = init && init.detail; }
    },
  };
  context.window = context;
  context.window.PAIdentity = identity;
  context.window.PAStatusStream = {
    subscribe(listener) {
      context._statusListener = listener;
      return () => {};
    },
  };
  context.window.addEventListener = (type, handler) => {
    if (!windowListeners.has(type)) windowListeners.set(type, []);
    windowListeners.get(type).push(handler);
  };
  context.window.dispatchEvent = (event) => {
    for (const handler of windowListeners.get(event.type) || []) handler(event);
    return true;
  };
  context.window.setTimeout = context.setTimeout;
  context.window.clearTimeout = context.clearTimeout;
  context.window.location = { reload() { context.reloaded = true; } };

  vm.runInNewContext(firmwareSrc, context);

  return { elements, fetches, timers, sessionStore, context, windowListeners, flush };
};

test("artoo owners still see the WiFi module name as not on this board", async () => {
  const { elements, context } = loadFirmware({ identity: artooIdentity });
  const card = elements.get("wifi-module-card");
  assert.equal(card.hidden, false);
  assert.equal(elements.get("wm-availability-status").textContent, "Not on this board");
  assert.equal(elements.get("wm-availability-reason").textContent,
               "This controller board cannot run a WiFi module.");
  assert.equal(elements.get("wm-content").getAttribute("aria-hidden"), "true");
  assert.equal(elements.get("wm-content").inert, true);
  assert.equal(elements.get("upload-wm-button").dataset.wmLocked, "1");
  assert.equal(context.reloaded, undefined);
});

test("unknown is not 0.0.0 and is not not-supported", async () => {
  const { elements, flush } = loadFirmware({
    identity: hostedIdentity,
    statusBody: { wifiModule: { updateSupport: "unknown", hostVersion: "2.12.11" } },
  });
  await flush();
  const support = elements.get("wm-support-line").textContent;
  assert.match(support, /not answering/);
  assert.doesNotMatch(support, /0\.0\.0/);
  assert.doesNotMatch(support, /cannot take an update over the air/);
  assert.equal(elements.get("wm-version-line").textContent, "");
  assert.equal(elements.get("upload-wm-button").dataset.wmLocked, "1");
});

test("not_supported is honest and keeps the send control locked", async () => {
  const { elements, flush } = loadFirmware({
    identity: hostedIdentity,
    statusBody: { wifiModule: { updateSupport: "not_supported", hostVersion: "2.12.11" } },
  });
  await flush();
  const support = elements.get("wm-support-line").textContent;
  assert.match(support, /cannot take an update over the air/);
  assert.match(support, /wired rewrite/);
  assert.equal(elements.get("upload-wm-button").dataset.wmLocked, "1");
});

test("supported shows versions and unlocks send", async () => {
  const { elements, flush } = loadFirmware({
    identity: hostedIdentity,
    statusBody: {
      wifiModule: { updateSupport: "supported", version: "2.12.11", hostVersion: "2.12.11" },
    },
  });
  await flush();
  assert.match(elements.get("wm-support-line").textContent, /can take new software/);
  assert.match(elements.get("wm-version-line").textContent, /Module software 2\.12\.11/);
  assert.match(elements.get("wm-version-line").textContent, /Target 2\.12\.11/);
  assert.equal(elements.get("upload-wm-button").dataset.wmLocked, "0");
});

test("a refused gate stays on this page and maps the token", async () => {
  const { elements, flush } = loadFirmware({
    identity: hostedIdentity,
    statusBody: {
      wifiModule: { updateSupport: "supported", version: "1.0.0", hostVersion: "2.12.11" },
    },
    upload: () => ({ error: "wifi-module-not-supported", status: 409 }),
  });
  await flush();
  elements.get("wm-file").files = [{ name: "network_adapter.bin" }];
  elements.get("upload-wm-button").click();
  await flush();
  assert.match(elements.get("wm-feedback").textContent, /wired rewrite/);
  assert.doesNotMatch(elements.get("wm-feedback").textContent, /Waiting for device to reboot/);
});

test("a successful send waits for the module, not a controller reboot", async () => {
  const { elements, sessionStore, context, fetches, flush } = loadFirmware({
    identity: hostedIdentity,
    statusBody: {
      wifiModule: { updateSupport: "supported", version: "1.0.0", hostVersion: "2.12.11" },
    },
    upload: () => ({ ok: true }),
  });
  await flush();
  elements.get("wm-file").files = [{ name: "network_adapter.bin" }];
  elements.get("upload-wm-button").click();
  await flush();
  assert.equal(sessionStore.get("ota_flash_success"), undefined);
  assert.equal(context.reloaded, undefined);
  assert.match(elements.get("wm-status").textContent, /this page stays here/);
  assert.ok(fetches.some((f) => f.path === "/upload/wifi-module"));
  assert.ok(!fetches.some((f) => f.path === "/upload/firmware"));
});

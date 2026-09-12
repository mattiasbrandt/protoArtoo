// =============================================================================
// test/test_web/test_activity_groups_361.js
//
// The Activity Groups (#361, ADR 0048): the nav ordered by the job a builder
// is doing rather than by firmware subsystem. The shipped page_bootstrap.js
// drives the shipped shell.js against the shipped data/index.html, so what is
// asserted here is the nav a browser would actually be handed.
//
// The one case no shipped build has today -- a group whose every member row
// names a surface that does not exist yet -- is reached by booting the same
// shipped shell with rows taken out of SURFACES, which is precisely what this
// build looks like to the four dormant rows.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);
const shellSrc = readData("shell.js");
const statusStreamSrc = readData("status_stream.js");

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// The shipped shell with some surfaces struck out of SURFACES. A member row
// whose surface has no SURFACES entry is the dormant case, and striking the
// last surface out of a group is the only way to see a group with nothing
// left to draw.
const shellWithout = (pages) =>
  shellSrc.replace(/^ {4}\{ page: "([a-z]+)", doc: .*$/gm, (line, page) =>
    pages.includes(page) ? "" : line,
  );

const boot = async ({ hash = "", shellSource = shellSrc } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = { document, requests: [], events: [], store: new Map() };

  const windowListeners = new Map();
  const windowMock = {
    setTimeout: (fn, ms) => {
      const timer = setTimeout(fn, ms);
      timer.unref?.();
      return timer;
    },
    clearTimeout: (id) => clearTimeout(id),
    setInterval: (fn, ms) => {
      const timer = setInterval(fn, ms);
      timer.unref?.();
      return timer;
    },
    clearInterval: (id) => clearInterval(id),
    addEventListener: (type, fn) => {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(fn);
    },
    removeEventListener: () => {},
    dispatchEvent: (event) => {
      env.events.push({ type: event.type, detail: event.detail });
      (windowListeners.get(event.type) || []).forEach((fn) => fn(event));
      return true;
    },
    location: {
      origin: "http://device",
      _hash: hash,
      get hash() {
        return this._hash;
      },
      set hash(value) {
        const text = String(value);
        const next = text === "" || text.startsWith("#") ? text : `#${text}`;
        if (next === this._hash) return;
        this._hash = next;
        (windowListeners.get("hashchange") || []).forEach((fn) => fn(new FakeEvent("hashchange")));
      },
    },
    history: {
      replaceState: (_state, _title, url) => {
        windowMock.location._hash = String(url);
      },
    },
    localStorage: {
      getItem: (key) => (env.store.has(key) ? env.store.get(key) : null),
      setItem: (key, value) => env.store.set(key, String(value)),
      removeItem: (key) => env.store.delete(key),
    },
    PAApi: {
      get: async (path) => {
        env.requests.push(path);
        if (path === "/api/identity") {
          return { data: { droidName: "artoo", board: "artoo_esp32", board_capabilities: {}, build_flags: {} } };
        }
        if (path === "/api/status") return { data: { estop: false } };
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
    },
    PAUtils: { escapeHtml: (value) => String(value) },
  };

  class FakeEvent {
    constructor(type) {
      this.type = type;
    }
  }
  class FakeCustomEvent extends FakeEvent {
    constructor(type, opts = {}) {
      super(type);
      this.detail = opts.detail;
    }
  }

  const context = {
    window: windowMock,
    document,
    console: { warn: () => {}, log: () => {}, error: () => {} },
    AbortController,
    Date,
    JSON,
    Object,
    Array,
    Set,
    Map,
    String,
    Number,
    Boolean,
    Promise,
    Error,
    Math,
    Event: FakeEvent,
    CustomEvent: FakeCustomEvent,
    DOMParser: class {
      parseFromString(html, type) {
        return new MiniDOMParser().parseFromString(html, type);
      }
    },
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
  context.globalThis = context;
  context.EventSource = class {
    constructor() {}
    addEventListener() {}
    close() {}
  };

  const REAL_SCRIPTS = { "/shell.js": shellSource, "/status_stream.js": statusStreamSrc };
  document.onAttach = (node) => {
    if (node.nodeType !== 1 || node.tagName !== "SCRIPT" || !node.src) return;
    const src = node.src;
    setTimeout(() => {
      if (REAL_SCRIPTS[src]) vm.runInNewContext(REAL_SCRIPTS[src], context, { filename: src });
      node.onload?.();
    }, 2).unref?.();
  };

  vm.runInNewContext(part1Src, context, { filename: "page_bootstrap.part1.js" });
  vm.runInNewContext(part3Src, context, { filename: "page_bootstrap.part3.js" });

  env.window = windowMock;
  env.navigate = (to) => {
    windowMock.location.hash = to;
  };
  env.mountedSurface = () => document.querySelectorAll("[data-surface]")[0]?.dataset.surface;

  await sleep(140);
  return env;
};

// The nav as it stands in the document: one row per group, in the order they
// are drawn, each with what it offers and whether it is marked as holding what
// is on screen.
const groupsIn = (document) =>
  document.querySelectorAll("[data-nav-group]").map((el) => ({
    id: el.dataset.navGroup,
    label: el.querySelector(".nav-group-label")?.textContent.trim(),
    hint: el.querySelector(".nav-group-hint")?.textContent.trim(),
    members: el.querySelectorAll("[data-surface-link]").map((link) => link.dataset.surfaceLink),
    current: el.classList.contains("is-current"),
  }));

const currentGroups = (document) =>
  groupsIn(document)
    .filter((group) => group.current)
    .map((group) => group.id);

test("the nav is four Activity Groups, in the order a builder meets them", async () => {
  const env = await boot();
  assert.deepEqual(
    groupsIn(env.document).map((group) => [group.id, group.label]),
    [
      ["drive", "Drive"],
      ["perform", "Perform"],
      ["configure", "Configure"],
      ["maintain", "Maintain"],
    ],
    "Drive, Perform, Configure, Maintain -- ADR 0048",
  );
});

test("each group offers the surfaces its rows name, in the order they are written", async () => {
  const env = await boot();
  const members = Object.fromEntries(groupsIn(env.document).map((group) => [group.id, group.members]));
  assert.deepEqual(members.drive, ["drive", "dome", "sound", "rc"]);
  assert.deepEqual(members.perform, ["seq", "sound", "dome"]);
  assert.deepEqual(
    members.configure,
    ["setup", "servo"],
    "Droid Build, Parts and Wiring are declared and dormant, so only the two that exist are drawn",
  );
  assert.deepEqual(members.maintain, ["wifi", "firmware"], "Maintenance is dormant until it exists");
});

test("a member row naming a surface this build does not have draws nothing at all", async () => {
  const env = await boot();
  const offered = env.document.querySelectorAll("[data-surface-link]").map((link) => link.dataset.surfaceLink);
  ["droidbuild", "parts", "wiring", "maintenance"].forEach((page) => {
    assert.equal(
      offered.includes(page),
      false,
      `${page} has no SURFACES row, so it must not appear in the nav`,
    );
  });
  assert.equal(
    env.document.querySelectorAll("[data-nav-group]").length,
    4,
    "and the groups holding them are still drawn, because their other rows exist",
  );
});

test("a group with nothing left to offer draws nothing, rather than an empty heading", async () => {
  // Maintain with WiFi and Firmware struck out: every one of its member rows
  // dormant, which is the state Configure would be in today were Setup and
  // Servos not in it.
  const env = await boot({ shellSource: shellWithout(["wifi", "firmware"]) });
  assert.deepEqual(
    groupsIn(env.document).map((group) => group.id),
    ["drive", "perform", "configure"],
    "Maintain is not drawn while none of its rows name a surface that exists",
  );
  assert.equal(
    env.document.querySelectorAll(".nav-group-head").length,
    3,
    "a group that draws nothing takes its heading and its hint with it",
  );
});

test("Dashboard is outside the groups, and it is the only thing out there", async () => {
  const env = await boot();
  const nav = env.document.querySelector("nav");
  assert.deepEqual(
    nav.children.filter((child) => child.tagName === "A").map((link) => link.dataset.surfaceLink),
    ["home"],
    "the landing sits beside the groups, not inside one",
  );
  const home = env.document.querySelectorAll("[data-surface-link]")[0];
  assert.equal(home.dataset.surfaceLink, "home", "and it is drawn first");
  assert.equal(home.closest("[data-nav-group]"), null, "no group claims it");
});

test("Sound and Dome are in two groups each, and both entries are the same address", async () => {
  const env = await boot();
  const entriesFor = (page) =>
    env.document.querySelectorAll(`[data-surface-link="${page}"]`);
  ["sound", "dome"].forEach((page) => {
    const entries = entriesFor(page);
    assert.equal(entries.length, 2, `${page} is reached for in two activities, so it is offered in both`);
    entries.forEach((entry) => {
      assert.equal(entry.getAttribute("href"), `#${page}`, "a second entry is a second way to find one route");
    });
  });
  assert.deepEqual(
    groupsIn(env.document)
      .filter((group) => group.members.includes("sound"))
      .map((group) => group.id),
    ["drive", "perform"],
  );
});

test("each group says what the builder is doing there", async () => {
  const env = await boot();
  const hints = groupsIn(env.document).map((group) => group.hint);
  hints.forEach((hint, at) => {
    assert.ok(hint && hint.length > 0, `group ${at} carries no hint`);
  });
  assert.equal(new Set(hints).size, 4, "four groups, four different answers to what you would be doing here");
});

test("the group holding the surface on screen is marked, and a deep link pulls it over", async () => {
  const env = await boot({ hash: "#firmware" });
  assert.equal(env.mountedSurface(), "firmware");
  assert.deepEqual(currentGroups(env.document), ["maintain"], "the address landed in Maintain and Maintain is marked");

  env.navigate("#seq");
  await sleep(140);
  assert.deepEqual(currentGroups(env.document), ["perform"], "the marker follows the route, and leaves the group behind");

  env.navigate("#servo");
  await sleep(140);
  assert.deepEqual(currentGroups(env.document), ["configure"]);
});

test("a surface in two groups marks both, because a group finds it rather than owning it", async () => {
  const env = await boot({ hash: "#sound" });
  assert.equal(env.mountedSurface(), "sound");
  assert.deepEqual(currentGroups(env.document), ["drive", "perform"]);
  assert.equal(
    env.document.querySelectorAll("[data-surface-link=\"sound\"]").filter((link) => link.classList.contains("active"))
      .length,
    2,
    "and both entries read as the one being shown",
  );
});

test("Dashboard is in no group, so nothing is marked while it is the surface on screen", async () => {
  const env = await boot();
  assert.equal(env.mountedSurface(), "home");
  assert.deepEqual(currentGroups(env.document), []);
});

test("the nav is drawn once: changing screen flips attributes on the nodes already there", async () => {
  const env = await boot();
  const nav = env.document.querySelector("nav");
  const driveGroup = env.document.querySelector("[data-nav-group=\"drive\"]");
  const soundEntry = env.document.querySelectorAll("[data-surface-link=\"sound\"]")[0];

  env.navigate("#sound");
  await sleep(140);

  assert.strictEqual(env.document.querySelector("nav"), nav, "the nav node survived the navigation");
  assert.strictEqual(env.document.querySelector("[data-nav-group=\"drive\"]"), driveGroup, "so did the group");
  assert.strictEqual(
    env.document.querySelectorAll("[data-surface-link=\"sound\"]")[0],
    soundEntry,
    "and the entry that just became active is the same node -- a rebuilt nav would take the estop's handler with it",
  );
  assert.ok(driveGroup.classList.contains("is-current"), "what changed is a class on what was already there");
});

test("the wheeled drive is named Foot Drive in the nav, and its route is still drive", async () => {
  const env = await boot();
  const entry = env.document.querySelector("[data-surface-link=\"drive\"]");
  assert.match(entry.textContent, /Foot Drive/, "named in full, because an unqualified Drive names three things (#288)");
  assert.equal(entry.getAttribute("href"), "#drive", "the data-page identifier does not move with the name");

  env.navigate("#drive");
  await sleep(140);
  assert.equal(env.mountedSurface(), "drive");
  assert.equal(env.document.body.dataset.page, "drive");
  assert.equal(env.document.title, "Foot Drive - artoo", "and the browser title reads the one place a surface is named");
});

test("the chrome that points at that screen calls it by the same name", async () => {
  // One name per concept: the estop says where the release is, and a builder
  // looking for it reads the nav (docs/ui-copy-voice.md "Naming", #288).
  const env = await boot();
  const button = env.document.getElementById("shell-estop-button");
  assert.match(
    env.document.querySelector(".shell-estop-consequence").textContent,
    /Foot Drive or Dashboard/,
    "the line under the button names the screen the nav names",
  );
  assert.match(button.getAttribute("aria-label"), /Foot Drive or Dashboard/, "and so does the accessible name");
});

test("no other surface was renamed, and every name still comes from SURFACES", async () => {
  const env = await boot();
  const names = env.document.querySelectorAll("[data-surface-link]").map((link) => link.textContent.trim());
  const withoutIcon = (text) => text.replace(/^\S+\s+/, "");
  assert.deepEqual(
    [...new Set(names.map(withoutIcon))].sort(),
    ["Dashboard", "Dome", "Firmware", "Foot Drive", "RC Control", "Sequences", "Servos", "Setup", "Sound", "WiFi"],
    "one rename, and the other nine surfaces are untouched",
  );
});

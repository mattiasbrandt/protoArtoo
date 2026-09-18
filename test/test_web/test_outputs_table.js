// =============================================================================
// test/test_web/test_outputs_table.js
//
// Parts (#362, ADR 0050): the output-first table, run for real. The shipped
// page_bootstrap.js boots the shipped shell.js, which fetches the shipped
// parts.html and runs its own chain against a fake droid that answers
// GET /api/servo/outputs - rows, Parts, band and commanded position - and
// applies a POST /api/config move the way the firmware does. A frame is the
// page's own bench feed firing; what is asserted is what a builder sees, the
// nodes it is drawn on, and what the page asked the droid for.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";

// mini_dom has no CSSStyleDeclaration, and the position marks are painted
// through element.style. A plain object per element is all a style write needs;
// it lives here rather than in the shared helper, which is not bent to the code
// under test (test/test_web/README.md).
const elementPrototype = Object.getPrototypeOf(new MiniDocument().createElement("div"));
if (!Object.getOwnPropertyDescriptor(elementPrototype, "style")) {
  Object.defineProperty(elementPrototype, "style", {
    get() {
      if (!this.styleValues) this.styleValues = {};
      return this.styleValues;
    },
  });
}

// Every innerHTML write, by the element written to - how "built once" is told
// apart from a repaint that rebuilds a row under the builder's pointer.
const innerHtmlSetter = Object.getOwnPropertyDescriptor(elementPrototype, "innerHTML").set;
const innerHtmlWrites = [];
Object.defineProperty(elementPrototype, "innerHTML", {
  set(html) {
    innerHtmlWrites.push(this);
    innerHtmlSetter.call(this, html);
  },
});

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);

const IDENTITY = {
  droidName: "artoo",
  board: "artoo_esp32",
  board_capabilities: { sbus: true },
  build_flags: { audio: true },
};

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const output = (address, name, extra = {}) => ({
  address,
  name,
  parts: [],
  bandLoUs: 1000,
  bandHiUs: 2000,
  commandedUs: null,
  targetUs: null,
  ...extra,
});

// A controller with ARM1 and ARM2 switched on and standing at neutral, and the
// three AUX outputs off.
const freshOutputs = () => [
  output("ledc:0", "ARM1", { commandedUs: 1500, targetUs: 1500 }),
  output("ledc:1", "ARM2", { commandedUs: 1500, targetUs: 1500 }),
  output("ledc:3", "AUX1"),
  output("ledc:4", "AUX2"),
  output("ledc:5", "AUX3"),
];

const withParts = (assignments, outputs = freshOutputs()) => {
  Object.entries(assignments).forEach(([address, parts]) => {
    outputs.find((each) => each.address === address).parts = parts.slice();
  });
  return outputs;
};

const bootParts = async ({ outputs = freshOutputs() } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = { document, outputs, posts: [], gets: new Map(), intervals: [], cleared: [] };

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
      env.intervals.push({ id: timer, ms, fn });
      return timer;
    },
    clearInterval: (id) => {
      env.cleared.push(id);
      clearInterval(id);
    },
    addEventListener: (type, fn) => {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(fn);
    },
    removeEventListener: () => {},
    dispatchEvent: (event) => {
      (windowListeners.get(event.type) || []).forEach((fn) => fn(event));
      return true;
    },
    location: {
      origin: "http://device",
      _hash: "",
      get hash() {
        return this._hash;
      },
      set hash(value) {
        const text = String(value);
        const next = text === "" || text.startsWith("#") ? text : `#${text}`;
        if (next === this._hash) return;
        this._hash = next;
        (windowListeners.get("hashchange") || []).forEach((fn) => fn({ type: "hashchange" }));
      },
    },
    history: {
      replaceState: (_state, _title, url) => {
        windowMock.location._hash = String(url);
      },
    },
    localStorage: { getItem: () => null, setItem: () => {}, removeItem: () => {} },
    PAApi: {
      get: async (path) => {
        env.gets.set(path, (env.gets.get(path) || 0) + 1);
        if (path === "/api/identity") return { data: IDENTITY };
        if (path === "/api/status") return { data: { estop: false } };
        if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(env.outputs) } };
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      // The firmware's move, as the fake droid applies it: off whatever Output
      // had the Part, onto the one named.
      postForm: async (path, form) => {
        env.posts.push({ path, form: { ...form } });
        env.outputs.forEach((each) => {
          each.parts = each.parts.filter((id) => id !== form.movePart);
        });
        env.outputs.find((each) => each.address === form.movePartTo)?.parts.push(form.movePart);
        return { ok: true, status: 200, data: {} };
      },
      messageFor: (error) => error.message,
      // The shipped shape (data/web_api.js): Find by Moving (#363) gates its
      // button through it on every status frame, so the fake needs one.
      gateControls: (elements, enabled) => {
        elements.forEach((el) => {
          if (!el) return;
          el.disabled = !enabled;
          el.setAttribute("aria-disabled", enabled ? "false" : "true");
        });
      },
    },
    PAUtils: {
      // mini_dom decodes no entities, so escaping here would put "&amp;" in the
      // text a test reads; the catalog carries nothing that needs it.
      escapeHtml: (value) => String(value),
      showFeedback: (el, text, level = "") => {
        if (!el) return;
        el.textContent = text;
        el.className = level ? `feedback ${level}` : "feedback";
      },
      // The shipped PAUtils exports this (data/web_api.js) and the calibration
      // dial coalesces its hold commands through it, so the mock carries it
      // too - with REAL timers, because a debounce stubbed to call straight
      // through would make "a drag sends one hold, not twenty" true by
      // construction (test/test_web/README.md).
      debounce: (fn, ms) => {
        let timer = null;
        return (...args) => {
          if (timer !== null) clearTimeout(timer);
          timer = setTimeout(() => {
            fn(...args);
            timer = null;
          }, ms);
          timer.unref?.();
        };
      },
    },
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
    EventSource: class {
      addEventListener() {}
      close() {}
    },
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
  context.globalThis = context;

  const REAL_SCRIPTS = {
    "/shell.js": readData("shell.js"),
    "/status_stream.js": readData("status_stream.js"),
    "/droid_parts.js": readData("droid_parts.js"),
    "/droid_part_kind.js": readData("droid_part_kind.js"),
    "/parts.js": readData("parts.js"),
  };
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

  env.region = () => document.getElementById("outputs-table");
  env.rows = () => env.region().querySelectorAll("[data-output]");
  env.row = (address) => env.rows().find((node) => node.dataset.output === address);
  env.cell = (address, className) => env.row(address).querySelector(`.${className}`);
  env.text = (address, className) => env.cell(address, className).textContent;
  env.tier = (id) => document.querySelectorAll("[data-tier]").find((node) => node.dataset.tier === id).textContent;
  env.byId = (id) => document.getElementById(id);
  // What a builder does with a row's part picker: choose, and the change
  // reaches the table's delegated handler.
  env.pickOnOutput = (address, partId) => {
    const select = env.row(address).querySelector("select");
    select.value = partId;
    env.region().fire("change", { target: select });
    return select;
  };
  // One tick of the page's own bench feed: the poll refreshes when the page
  // becomes visible again, which runs exactly the attempt its interval runs.
  env.frame = async () => {
    document.dispatch("visibilitychange", { type: "visibilitychange" });
    await sleep(20);
  };
  env.click = (id) => document.getElementById(id).fire("click", {});

  windowMock.location.hash = "#parts";
  const deadline = Date.now() + 3000;
  while (!(env.region() && env.rows().length > 0 && document.getElementById("parts-table")?.querySelector("select")?.disabled === false)) {
    if (Date.now() > deadline) assert.fail("the Parts surface never mounted and painted its output rows");
    await sleep(5);
  }

  // A browser's <dialog>; mini_dom has none.
  const dialog = document.getElementById("parts-move-dialog");
  dialog.open = false;
  dialog.showModal = () => {
    dialog.open = true;
  };
  dialog.close = () => {
    dialog.open = false;
  };
  env.dialog = dialog;
  return env;
};

// ---------------------------------------------------------------------------

test("taking a Part off another Output from this table is asked in the part-first table's own words", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["doorFL", "doorFR"], "ledc:3": ["utilUp"] }) });
  const expected = env.window.PAParts.announcement(
    env.window.PAParts.moveFor(structuredClone(env.outputs), "doorFL", "ledc:3"),
  );

  const picker = env.pickOnOutput("ledc:3", "doorFL");
  assert.equal(env.posts.length, 0, "nothing reaches the droid before the builder answers");
  assert.equal(env.dialog.open, true);
  assert.equal(env.byId("parts-move-title").textContent, expected.title);
  assert.equal(env.byId("parts-move-body").textContent, expected.body);
  assert.equal(
    env.byId("parts-move-body").textContent,
    "Left body door is on ARM1. Move it to AUX1 and unwire it from ARM1? " +
      "ARM1 keeps driving Right body door. Upper utility arm is on AUX1 too — they will move together.",
  );
  assert.equal(picker.value, "", "the picker goes back to its prompt");

  env.click("parts-move-confirm");
  await sleep(20);
  assert.deepEqual(env.posts, [
    { path: "/api/config", form: { movePart: "doorFL", movePartFrom: "ledc:0", movePartTo: "ledc:3" } },
  ]);
  assert.equal(env.text("ledc:3", "outputs-parts"), "Upper utility arm, Left body door");
  assert.equal(env.text("ledc:0", "outputs-parts"), "Right body door");
  assert.equal(
    env.byId("parts-table").querySelectorAll("[data-part]").find((node) => node.dataset.part === "doorFL").querySelector("select").value,
    "ledc:3",
    "the part-first table reads the same answer and says the same thing",
  );
});

test("cancelling from this table sends nothing and leaves both Outputs as they were", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["doorFL"] }) });

  env.pickOnOutput("ledc:4", "doorFL");
  assert.equal(env.dialog.open, true);
  env.click("parts-move-cancel");
  await sleep(20);
  assert.equal(env.posts.length, 0);
  assert.equal(env.dialog.open, false);
  assert.equal(env.text("ledc:0", "outputs-parts"), "Left body door");
  assert.equal(env.text("ledc:4", "outputs-parts"), "– not wired –");
});

test("one read of the droid's answer a second feeds both tables, and stops when Parts is left", async () => {
  const env = await bootParts();

  const before = env.gets.get("/api/servo/outputs");
  await env.frame();
  assert.equal(env.gets.get("/api/servo/outputs"), before + 1, "one request a frame, not one per table");

  // The shell's status plate ticks once a second too, and it is chrome that
  // never stops, so the feed is found by what its tick asks the droid for.
  let feed = null;
  for (const timer of env.intervals.filter((each) => each.ms === 1000)) {
    const asked = env.gets.get("/api/servo/outputs");
    timer.fn();
    await sleep(20);
    if (env.gets.get("/api/servo/outputs") === asked + 1) feed = timer;
  }
  assert.ok(feed, "the bench feed asks once a second while Parts is on screen");
  env.window.PASurface.showing("dashboard");
  assert.ok(env.cleared.includes(feed.id), "and it stops asking when the operator leaves Parts");
});

test("a firmware that reports no position is not shown as an Output with no pulse", async () => {
  const outputs = freshOutputs().map(({ address, name, parts }) => ({ address, name, parts }));
  outputs[2].parts = ["utilUp"];
  const env = await bootParts({ outputs });

  assert.equal(env.text("ledc:3", "outputs-us"), "Not reported by this firmware");
  assert.equal(env.tier("switched-off"), "Wired but switched off — 0 outputs");
  assert.equal(env.tier("driving"), "Driving parts — 1 output");
});

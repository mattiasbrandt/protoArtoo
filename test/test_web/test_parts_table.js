// =============================================================================
// test/test_web/test_parts_table.js
//
// Parts (#347, ADR 0050): the part-first table, run for real. The shipped
// page_bootstrap.js boots the shipped shell.js, which fetches the shipped
// parts.html and runs its own chain -- droid_parts.js, droid_part_kind.js and
// parts.js -- against a fake droid that answers GET /api/servo/outputs and
// applies a POST /api/config move the way the firmware does. What is asserted
// is what a builder sees and what the page asked the droid for.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";
import { bootParts as bootPartsSurface, sleep as wait } from "./helpers/parts_surface.js";

// mini_dom has no CSSStyleDeclaration, and since #362 this page's output-first
// table paints its position marks through element.style. A plain object per
// element is all a style write needs; it lives here rather than in the shared
// helper, which is not bent to the code under test (test/test_web/README.md).
const elementPrototype = Object.getPrototypeOf(new MiniDocument().createElement("div"));
if (!Object.getOwnPropertyDescriptor(elementPrototype, "style")) {
  Object.defineProperty(elementPrototype, "style", {
    get() {
      if (!this.styleValues) this.styleValues = {};
      return this.styleValues;
    },
  });
}

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

// The five LEDC Outputs a controller boots with, driving nothing.
const freshOutputs = () => [
  { address: "ledc:0", name: "ARM1", parts: [] },
  { address: "ledc:1", name: "ARM2", parts: [] },
  { address: "ledc:3", name: "ARM3", parts: [] },
  { address: "ledc:4", name: "ARM4", parts: [] },
  { address: "ledc:5", name: "ARM5", parts: [] },
];

const withParts = (assignments) => {
  const outputs = freshOutputs();
  Object.entries(assignments).forEach(([address, parts]) => {
    outputs.find((output) => output.address === address).parts = parts.slice();
  });
  return outputs;
};

const bootParts = async ({ outputs = freshOutputs(), catalogSource = readData("droid_parts.js") } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = { document, outputs, posts: [], refusal: null };

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
        if (path === "/api/identity") return { data: IDENTITY };
        if (path === "/api/status") return { data: { estop: false } };
        if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(env.outputs) } };
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      // The firmware's move, as the fake droid applies it: off whatever Output
      // had the Part, onto the one named. A refusal changes nothing.
      postForm: async (path, form) => {
        env.posts.push({ path, form: { ...form } });
        if (env.refusal) {
          const error = new Error(env.refusal);
          error.kind = "http";
          error.status = 409;
          throw error;
        }
        env.outputs.forEach((output) => {
          output.parts = output.parts.filter((id) => id !== form.movePart);
        });
        env.outputs.find((output) => output.address === form.movePartTo)?.parts.push(form.movePart);
        return { ok: true, status: 200, data: {} };
      },
      messageFor: (error) => error.message,
      // The shipped shape (data/web_api.js): disabled plus aria-disabled, which
      // is what the shell's ignored-input notice looks for on a press. Every
      // control on Parts that asks the droid to move something is gated through
      // it, so a host without it is not the host the page ships against.
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
    "/droid_parts.js": catalogSource,
    "/droid_part_kind.js": readData("droid_part_kind.js"),
    "/parts_mapping.js": readData("parts_mapping.js"),
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

  env.table = () => document.getElementById("parts-table");
  env.row = (id) => env.table().querySelectorAll("[data-part]").find((node) => node.dataset.part === id);
  env.select = (id) => env.row(id)?.querySelector("select");
  env.optionTexts = (id) => env.select(id).querySelectorAll("option").map((option) => option.textContent);
  env.heading = (group) =>
    env.table().querySelectorAll("tbody").find((body) => body.dataset.group === group)?.querySelector("th").textContent;
  env.text = (id) => document.getElementById(id).textContent;
  // What a builder does with a select: choose, and the change reaches the
  // table's delegated handler.
  env.pick = (id, address) => {
    const select = env.select(id);
    select.value = address;
    env.table().fire("change", { target: select });
  };
  env.click = (id) => document.getElementById(id).fire("click", {});

  windowMock.location.hash = "#parts";
  // Mounted, and painted from the droid's first answer.
  const deadline = Date.now() + 3000;
  while (!(env.table() && env.select("doorFL") && env.select("doorFL").disabled === false)) {
    if (Date.now() > deadline) assert.fail("the Parts surface never mounted and painted");
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

test("taking a Part off one Output for another is asked first, then sends where it was", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["doorFL", "doorFR"], "ledc:3": ["utilUp"] }) });

  env.pick("doorFL", "ledc:3");
  assert.equal(env.posts.length, 0, "nothing reaches the droid before the builder answers");
  assert.equal(env.dialog.open, true);
  assert.equal(env.text("parts-move-title"), "Part already wired");
  assert.equal(
    env.text("parts-move-body"),
    "Left body door is on ARM1. Move it to ARM3 and unwire it from ARM1? " +
      "ARM1 keeps driving Right body door. Upper utility arm is on ARM3 too — they will move together.",
  );
  assert.equal(env.text("parts-move-confirm"), "Move it", "the button that agrees is the verb");

  env.click("parts-move-confirm");
  await sleep(20);
  assert.deepEqual(env.posts, [
    { path: "/api/config", form: { movePart: "doorFL", movePartFrom: "ledc:0", movePartTo: "ledc:3" } },
  ]);
  assert.equal(env.dialog.open, false);
  assert.equal(env.select("doorFL").value, "ledc:3", "the table repaints from what the droid now says");
  assert.equal(env.select("doorFR").value, "ledc:0", "the Part left behind is still driven");
  assert.equal(env.text("parts-feedback"), "Left body door is on ARM3.");
});

test("cancelling the question sends nothing and puts the control back", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["doorFL"] }) });

  env.pick("doorFL", "ledc:4");
  assert.equal(
    env.text("parts-move-body"),
    "Left body door is on ARM1. Move it to ARM4 and unwire it from ARM1? ARM1 will drive nothing.",
  );
  env.click("parts-move-cancel");
  await sleep(20);
  assert.equal(env.posts.length, 0);
  assert.equal(env.dialog.open, false);
  assert.equal(env.select("doorFL").value, "ledc:0");
});

test("a Part renamed in the catalog keeps its id on the row and on the wire", async () => {
  const original = readData("droid_parts.js");
  const renamed = original.replace('"name": "Left body door"', '"name": "Front-left breadpan door"');
  assert.notEqual(renamed, original, "the rename reached the catalog source");
  const env = await bootParts({ catalogSource: renamed });

  assert.equal(env.row("doorFL").querySelector(".parts-name").textContent, "Front-left breadpan door");
  env.pick("doorFL", "ledc:3");
  await sleep(20);
  assert.equal(env.posts[0].form.movePart, "doorFL");
});

test("a move the droid refuses says the droid's reason and shows the table as it is", async () => {
  const env = await bootParts();
  env.refusal = "that Part is not on the Output movePartFrom names - read the outputs again, then move it";

  env.pick("doorRL", "ledc:3");
  await sleep(20);
  assert.equal(
    env.text("parts-feedback"),
    "Rear-left body door did not move: that Part is not on the Output movePartFrom names - read the outputs again, then move it",
  );
  assert.equal(env.select("doorRL").value, "none");
});

test("a Part the droid drives and the page does not know is named, never dropped", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["domeEye"] }) });
  assert.match(env.text("parts-summary"), /also drives domeEye/);
});

// The output-first table, back to centre, Find by moving and the calibration
// dial moved to Servos, and their code was deleted from Parts rather than
// hidden (operator, 2026-09-19 on #412). A decoy stands where the table used to
// be drawn: Parts must write nothing into it, build no table of its own, and
// never ask the droid to move anything while it sits on screen.
test("Parts carries none of the Output pieces that moved to Servos", async () => {
  const env = await bootPartsSurface({ decoys: ['<div id="outputs-table" data-decoy="yes"></div>'] });
  await env.frame();
  await wait(20);

  const tables = env.document.querySelectorAll("#outputs-table");
  assert.equal(tables.length, 1, "Parts builds no output table of its own");
  assert.equal(tables[0].dataset.decoy, "yes");
  assert.equal(tables[0].children.length, 0, "and writes nothing into the one that stands where it was");
  for (const moved of [".cal-panel", ".outputs-centre", ".parts-find", ".outputs-row"]) {
    assert.equal(env.document.querySelectorAll(moved).length, 0, `no ${moved} on Parts`);
  }
  assert.deepStrictEqual(
    env.posts.filter((post) => post.path.startsWith("/api/servo")),
    [],
    "and nothing on it asks the droid to move a servo",
  );
});

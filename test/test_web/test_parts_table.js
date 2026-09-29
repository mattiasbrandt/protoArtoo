// =============================================================================
// test/test_web/test_parts_table.js
//
// The part-first table (#347, ADR 0050), run for real. It lived on Parts and
// moved to Wiring's Outputs section with its move question (operator,
// 2026-09-28 on #411); these tests moved with it, and the file keeps its name
// so the history of the table stays in one place. The shipped
// page_bootstrap.js boots the shipped shell.js, which fetches the shipped
// wiring.html and runs its chain -- droid_parts.js, droid_part_kind.js,
// outputs.js, parts_mapping.js and wiring.js, which mounts the picker --
// against a fake droid that answers GET /api/servo/outputs and applies a POST
// /api/config move the way the firmware does. What is asserted is what a
// builder sees and what the page asked the droid for. The last test holds the
// other half of the move: Parts carries no picker of its own.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./helpers/mini_dom.js";
import { bootParts as bootPartsSurface, sleep as wait } from "./helpers/parts_surface.js";
import { freshOutputs, withParts, describe, applyRowSave, servoRow } from "./helpers/fake_droid.js";

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

// The shipped words (data/web_api.js), which is what turns a refusal into what
// the builder reads: this harness's own PAApi answers with it, so a refused
// move is shown the way a browser shows it.
const shippedWords = () => {
  const window = {};
  vm.runInNewContext(readData("web_api.js"), { window, URLSearchParams });
  return window.PAApi;
};

const bootPicker = async ({ outputs = freshOutputs(), catalogSource = readData("droid_parts.js") } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  // `refusal`: the droid's answer to the next move - its sentence and the
  // field and reason beside it - or null to let it land.
  const env = { document, outputs, posts: [], refusal: null };
  const words = shippedWords();
  words.nameOutputsWith((address) => env.outputs.find((output) => output.address === address)?.name ?? null);

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
        // Wiring's sheet reads the config with the table; nothing here
        // switches a Board Lane.
        if (path === "/api/config") return { data: { components: {} } };
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      // The firmware's move, as the fake droid applies it: off whatever Output
      // had the Part, onto the one named, and each board Output it touched
      // takes its wired tick from the Parts it holds now (src/web/api_config.cpp
      // tickFollowsMove()). A refusal changes nothing.
      postForm: async (path, form) => {
        env.posts.push({ path, form: { ...form } });
        if (env.refusal) {
          throw new words.ApiError(env.refusal.error, {
            kind: "http", status: 409, field: env.refusal.field, reason: env.refusal.reason,
          });
        }
        env.outputs.forEach((output) => {
          output.parts = output.parts.filter((id) => id !== form.movePart);
        });
        const from = env.outputs.find((output) => output.address === form.movePartFrom);
        const to = env.outputs.find((output) => output.address === form.movePartTo);
        to?.parts.push(form.movePart);
        [from, to].forEach((output) => {
          if (output?.switchable) output.wired = output.parts.length > 0;
        });
        return { ok: true, status: 200, data: {} };
      },
      // An Output row save (POST /api/config `outputs`), as the firmware takes it.
      postJson: async (path, body) => {
        env.posts.push({ path, body: structuredClone(body) });
        applyRowSave(env.outputs, body);
        return { ok: true, status: 200, data: {} };
      },
      rowTimingOf: words.rowTimingOf,
      messageFor: words.messageFor,
      // The shipped shape (data/web_api.js): disabled plus aria-disabled, which
      // is what the shell's ignored-input notice looks for on a press.
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
    "/live_reading.js": readData("live_reading.js"),
    "/droid_parts.js": catalogSource,
    "/droid_part_kind.js": readData("droid_part_kind.js"),
    "/outputs.js": readData("outputs.js"),
    "/apply_timing.js": readData("apply_timing.js"),
    "/output_settings.js": readData("output_settings.js"),
    "/dome_command_map.js": readData("dome_command_map.js"),
    "/parts_mapping.js": readData("parts_mapping.js"),
    "/wiring.js": readData("wiring.js"),
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

  env.table = () => document.getElementById("wiring-parts-table");
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

  windowMock.location.hash = "#wiring";
  // Mounted, and painted from the droid's first answer.
  const deadline = Date.now() + 3000;
  while (!(env.table() && env.select("doorFL") && env.select("doorFL").disabled === false)) {
    if (Date.now() > deadline) assert.fail("the picker on Wiring never mounted and painted");
    await sleep(5);
  }

  // A browser's <dialog>; mini_dom has none. Only show() is given: a
  // showModal() would make the shell's STOP inert (#359), so a call to it
  // throws here rather than quietly passing.
  const dialog = document.getElementById("wiring-move-dialog");
  dialog.open = false;
  dialog.show = () => {
    dialog.open = true;
  };
  dialog.showModal = () => {
    throw new Error("the move question must not be modal: it would make STOP inert (#359, ADR 0048)");
  };
  dialog.close = () => {
    dialog.open = false;
  };
  env.dialog = dialog;
  env.estop = document.getElementById("shell-estop-button");
  env.chrome = ["shell-top", "shell-nav", "shell-status"].map((id) => document.getElementById(id));
  env.surface = dialog.closest(".surface");
  return env;
};

// Every node from `node` up to the document that is marked inert. Inert is
// inherited, so any one of them takes the node out of reach.
const inertOnPath = (node) => {
  const hits = [];
  for (let at = node; at; at = at.parentElement) if (at.inert) hits.push(at.id || at.className || at.tagName);
  return hits;
};

// ---------------------------------------------------------------------------

test("taking a Part off one Output for another is asked first, then sends where it was", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["doorFL", "doorFR"], "ledc:3": ["utilUp"] }) });

  env.pick("doorFL", "ledc:3");
  assert.equal(env.posts.length, 0, "nothing reaches the droid before the builder answers");
  assert.equal(env.dialog.open, true);
  assert.equal(env.text("wiring-move-title"), "Part already wired");
  assert.equal(
    env.text("wiring-move-body"),
    "Left body door is on ARM1. Move it to ARM3 and unwire it from ARM1? " +
      "ARM1 keeps Right body door. Upper utility arm is on ARM3 too — they will move together.",
  );
  assert.equal(env.text("wiring-move-confirm"), "Move it", "the button that agrees is the verb");

  env.click("wiring-move-confirm");
  await sleep(20);
  assert.deepEqual(env.posts, [
    { path: "/api/config", form: { movePart: "doorFL", movePartFrom: "ledc:0", movePartTo: "ledc:3" } },
  ]);
  assert.equal(env.dialog.open, false);
  assert.equal(env.select("doorFL").value, "ledc:3", "the table repaints from what the droid now says");
  assert.equal(env.select("doorFR").value, "ledc:0", "the Part left behind is still on its output");
  assert.equal(env.text("wiring-parts-feedback"), "Left body door is on ARM3.");
});

test("the move question leaves STOP live and holds only the surface behind it", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["doorFL"] }) });
  assert.ok(env.estop, "the shell drew its STOP");
  assert.ok(env.surface, "Wiring is mounted in a .surface");

  env.pick("doorFL", "ledc:4");
  assert.equal(env.dialog.open, true, "the question is up");

  assert.deepEqual(inertOnPath(env.estop), [], "STOP must take a press while the question is up (#359, ADR 0048)");
  env.chrome.forEach((region) => assert.deepEqual(inertOnPath(region), [], `${region.id} stays live`));
  assert.deepEqual(inertOnPath(env.dialog), [], "the question itself can be answered");
  assert.ok(inertOnPath(env.table()).length > 0, "the table behind the question takes no press");

  env.click("wiring-move-cancel");
  await sleep(20);
  assert.deepEqual(inertOnPath(env.table()), [], "answering gives the surface back");
});

test("Escape cancels the move question", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["doorFL"] }) });

  env.pick("doorFL", "ledc:4");
  assert.equal(env.dialog.open, true);
  env.dialog.fire("keydown", { key: "Escape" });
  await sleep(20);
  assert.equal(env.dialog.open, false);
  assert.equal(env.posts.length, 0, "a cancel sends nothing");
  assert.equal(env.select("doorFL").value, "ledc:0");
  assert.deepEqual(inertOnPath(env.table()), [], "and gives the surface back");
});

test("cancelling the question sends nothing and puts the control back", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["doorFL"] }) });

  env.pick("doorFL", "ledc:4");
  assert.equal(
    env.text("wiring-move-body"),
    "Left body door is on ARM1. Move it to ARM4 and unwire it from ARM1? ARM1 will have nothing on it.",
  );
  env.click("wiring-move-cancel");
  await sleep(20);
  assert.equal(env.posts.length, 0);
  assert.equal(env.dialog.open, false);
  assert.equal(env.select("doorFL").value, "ledc:0");
});

test("a Part renamed in the catalog keeps its id on the row and on the wire", async () => {
  const original = readData("droid_parts.js");
  const renamed = original.replace('"name": "Left body door"', '"name": "Front-left breadpan door"');
  assert.notEqual(renamed, original, "the rename reached the catalog source");
  const env = await bootPicker({ catalogSource: renamed });

  assert.equal(env.row("doorFL").querySelector(".parts-name").textContent, "Front-left breadpan door");
  env.pick("doorFL", "ledc:3");
  await sleep(20);
  assert.equal(env.posts[0].form.movePart, "doorFL");
});

// The shipped defect (ADR 0059, #432): a refused move showed the droid's own
// sentence, wire name and all. The droid still sends that sentence, so it is
// the decoy here: the builder reads the refusal worded from its field and
// reason, naming the Part and the Output the move was about, and none of it.
test("a move the droid refuses is said in the builder's words and shows the table as it is", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["doorRL"] }) });
  const sentence = "that Part is not on the Output movePartFrom names - read the outputs again, then move it";
  env.refusal = { error: sentence, field: "movePartFrom", reason: "conflict" };

  env.pick("doorRL", "ledc:3");
  env.click("wiring-move-confirm");
  await sleep(20);
  const said = env.text("wiring-parts-feedback");
  assert.ok(!said.includes(sentence) && !said.includes("movePartFrom"), `the droid's sentence reached the page: ${said}`);
  assert.match(said, /^Rear-left body door did not move: ARM1 /, "it names the Part and the Output it was on");
  assert.equal(env.select("doorRL").value, "ledc:0", "the table shows where the droid still has it");
});

test("a Part on an output that the page does not know is named, never dropped", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["domeEye"] }) });
  assert.match(env.text("wiring-parts-summary"), /domeEye on an output too/);
});

// A dome Part is not this board's to wire (operator, 2026-09-29 on #411:
// "Dome wiring is all handled and managed by the dome controller"), whatever
// the catalog's `control` says: a holoprojector or a fixed side panel reads
// `control: none`, and it sits on the dome all the same. It gets no Output to
// choose - an Output select on it would offer a write that means nothing - and
// shows the command that moves it, or says it has none. Every one keeps a row,
// so no dome Part vanishes from the page. A dome Part a builder recorded on a
// body Output anyway is still named with it, so a Part on a wire is never
// invisible.
test("every dome Part gets no Output select, and shows its command or that it has none", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["panel1"] }) });
  const catalog = env.window.DroidParts.parts;
  const dome = catalog.filter((part) => part.half === "dome");
  assert.ok(dome.some((part) => part.control === "none"), "the catalog has a dome Part whose control is none");
  const domeRow = (id) => env.table().querySelectorAll("[data-dome-part]").find((node) => node.dataset.domePart === id);
  dome.forEach((part) => {
    assert.equal(env.row(part.id), undefined, `${part.id} (control ${part.control}) has an Output select`);
    assert.ok(domeRow(part.id), `${part.id} has no row`);
  });
  catalog
    .filter((part) => part.half !== "dome")
    .forEach((part) => assert.ok(env.select(part.id), `${part.id} lost its Output select`));

  assert.equal(domeRow("pie1").querySelector(".parts-command").textContent, "Open :OPP1 · Close :CLP1");
  assert.equal(domeRow("hp1Pan").querySelector(".parts-command").textContent, "No command yet");
  assert.match(domeRow("panel1").textContent, /on ARM1 too/);
  assert.equal(domeRow("pie1").querySelector("select"), null);
});

// The sheet above the picker names the Part on the end of each wire, so a Part
// moved in the picker is on its new wire the moment the droid has taken it -
// not on the next visit.
// The Output it left has no Part on it now, so it is free and draws no line
// (operator, 2026-09-29 on #411: "the drawing should only draw the actaul
// lines (wires) currently assigned/wired in").
test("a Part moved in the picker is on its new wire in the sheet at once", async () => {
  const env = await bootPicker({ outputs: withParts({ "ledc:0": ["doorFL"] }) });
  const wire = (address) => env.document.querySelectorAll(".wd-link").find((node) => node.dataset.wire === address);
  assert.match(wire("ledc:0").textContent, /Left body door/);

  env.pick("doorFL", "ledc:4");
  env.click("wiring-move-confirm");
  await sleep(40);
  assert.match(wire("ledc:4").textContent, /Left body door/, "the wire it now hangs off names it");
  assert.equal(wire("ledc:0"), undefined, "and the wire it left, free now, is not drawn");
});

// The limit and the recommendation (operator, 2026-09-29 on #411: "either we
// limit what you can define in the wiring page or give recommendations" -
// both). A light Part is never offered an Output a light cannot go on: its
// firmware would refuse the Light Type, and the builder would have wired a
// Part to a line that cannot light it. Which Outputs can, and which Part an
// Output usually takes, are the rows' own answer - here a mixed set, the
// board's LEDC Outputs and an expander's channel, so nothing assumes GPIO.
test("a light Part is offered only the Outputs a light can go on, and a board's suggestion is marked", async () => {
  const outputs = describe([...freshOutputs(), servoRow("pca:0", "")], {
    "ledc:0": { suggestedPart: "utilUp" },
    "ledc:1": { suggestedPart: "utilLo" },
    "ledc:3": { lightCapable: true },
    "ledc:4": { lightCapable: true },
    "ledc:5": { lightCapable: true },
  });
  const env = await bootPicker({ outputs });
  const offered = (id) => env.select(id).querySelectorAll("option").map((option) => option.getAttribute("value")).slice(1);

  assert.deepEqual(offered("dataPanel"), ["ledc:3", "ledc:4", "ledc:5"], "a light Part: only the lines a light can go on");
  assert.deepEqual(offered("utilUp"), ["ledc:0", "ledc:1", "ledc:3", "ledc:4", "ledc:5", "pca:0"],
    "a servo Part: every Output the droid reports, the expander's channel too");
  const suggested = (id) => env.optionTexts(id).filter((text) => /suggested$/.test(text));
  assert.deepEqual(suggested("utilUp"), ["ARM1 · free · suggested"]);
  assert.deepEqual(suggested("utilLo"), ["ARM2 · free · suggested"]);
  assert.deepEqual(suggested("doorFL"), [], "a Part no Output is suggested for sees no mark");
});

// Each wire is its own answer (#413, ADR 0067): a droid may have several lit
// Parts, so giving one light Part's wire a Light Type must not take it off
// another's, and the save carries that one Output's row and nothing else.
test("a light Part's Light Type is saved on its own wire and leaves another lit wire alone", async () => {
  const outputs = describe(withParts({ "ledc:4": ["cbi"], "ledc:5": ["dataPanel"] }), {
    "ledc:4": { lightCapable: true, type: "rgb" },
    "ledc:5": { lightCapable: true, type: "none" },
  });
  const env = await bootPicker({ outputs });
  const lightOn = (id) =>
    env.row(id).querySelector(".parts-carries").querySelectorAll("[data-value]").find((node) => node.dataset.value === "rgb");
  assert.equal(lightOn("cbi").classList.contains("active"), true);
  assert.equal(lightOn("dataPanel").classList.contains("active"), false);
  assert.equal(env.row("doorFL").querySelector(".parts-carries").children.length, 0, "a Part on no Output has nothing to pick");

  lightOn("dataPanel").fire("click", {});
  await sleep(40);
  assert.deepEqual(env.posts.filter((post) => post.body).map((post) => post.body),
    [{ outputs: [{ address: "ledc:5", component: "rgb" }] }]);
  assert.equal(env.outputs.find((output) => output.address === "ledc:4").component, "rgb", "the other lit wire still is");
  assert.equal(lightOn("dataPanel").classList.contains("active"), true, "drawn from the droid's answer");
});

// An Output with a Part on it is wired, and the droid reads that at its next
// start (#370, ADR 0027). A Part put on a free Output therefore waits, and the
// page says so beside the act; taken off again, nothing waits.
test("a Part put on a free Output waits for the next start and says so, until it is taken off", async () => {
  const outputs = describe(freshOutputs(), { "ledc:3": { wired: false, activeWired: false } });
  const env = await bootPicker({ outputs });
  const line = env.document.getElementById("wiring-parts-timing");
  assert.equal(line.classList.contains("hidden"), true, "nothing waits on a fresh read");

  env.pick("doorFL", "ledc:3");
  await sleep(40);
  assert.equal(line.dataset.pending, "true", "the droid still runs the wires it started with");
  assert.equal(line.classList.contains("hidden"), false);

  env.pick("doorFL", "none");
  await sleep(40);
  assert.equal(line.classList.contains("hidden"), true, "taken off again, nothing waits");
});

// The #355 bench (#364): ARM1 ticked live, then the page (re)loaded before a
// restart. Its first read already carried the saved tick, so a wait measured
// against that read showed nothing waiting while the droid drove nothing on
// the wire. The droid reports what it started with, and that is what the line
// waits on.
test("a Part put on before the page opened still waits for the next start", async () => {
  const env = await bootPicker({
    outputs: describe(withParts({ "ledc:3": ["doorFL"] }), { "ledc:3": { wired: true, activeWired: false } }),
  });
  assert.equal(env.document.getElementById("wiring-parts-timing").dataset.pending, "true");
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

// The part-first picker and its question moved to Wiring, and were deleted
// from Parts rather than hidden (operator, 2026-09-28 on #411): one picker,
// one question, one request. With Parts on screen - and Wiring never visited -
// nothing in the document picks an Output for a Part, and no move is sent.
test("Parts carries no picker and no move question of its own", async () => {
  const env = await bootPartsSurface();
  await env.frame();
  await wait(20);

  assert.equal(env.window.location.hash, "#parts");
  assert.equal(env.document.querySelectorAll("select").length, 0, "no Output picker on Parts");
  assert.equal(env.document.querySelectorAll("dialog").length, 0, "and no move question");
  assert.deepStrictEqual(env.moves().filter((post) => "movePart" in post.form), [], "and no move leaves it");
});

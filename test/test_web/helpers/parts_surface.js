// =============================================================================
// test/test_web/helpers/parts_surface.js
//
// Boots the shipped Operator Shell with the shipped Parts or Servos surface
// against a fake droid - Parts carries the part-first table and the droid
// picture, Servos the output-first table, Find by Moving, the calibration dial
// and back to centre (CONTEXT.md "Parts", "Servos"; #412) - and adds what a
// Find by Moving run needs the droid to answer: a nudgesDone count
// on every Output, POST /api/servo, a status stream the test can push an estop
// onto, and a PAApi.gateControls the shipped one's shape. The droid picture
// adds POST /api/dome/cmd. Kept beside the
// mini_dom rather than inside it: nothing here bends the DOM to the code under
// test, it only stands in for the droid (test/test_web/README.md).
// =============================================================================

import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

import { MiniDocument, MiniDOMParser } from "./mini_dom.js";
import { servoRow, freshOutputs, withParts, configOutputs, applyOutputSave, statusFrame } from "./fake_droid.js";

// The droid's Outputs are described once, in helpers/fake_droid.js (#415).
export { freshOutputs, withParts, statusFrame };
export const output = servoRow;

// mini_dom has no CSSStyleDeclaration, and the position marks are painted
// through element.style. A plain object per element is all a style write needs.
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
const dataDir = join(__dirname, "../../../data");
export const readData = (name) => readFileSync(join(dataDir, name), "utf-8");

const bootstrapFile = readData("page_bootstrap.js");
const part2Marker = bootstrapFile.indexOf("// =========================== PART 2");
const part3Marker = bootstrapFile.indexOf("// ============================ PART 3");
const part1Src = bootstrapFile.substring(bootstrapFile.indexOf("(() => {"), part2Marker);
const part3Src = bootstrapFile.substring(part3Marker);

// What a fresh controller comes up with fitted: the pre-selected design's
// complement, read from the shipped catalog rather than typed out here, so a
// catalog change cannot leave this fake droid carrying parts no design has.
const shippedCatalog = (() => {
  const context = { window: {} };
  vm.runInNewContext(readData("droid_parts.js"), context, { filename: "droid_parts.js" });
  return context.window.DroidParts;
})();
const MK4_COMPLEX = shippedCatalog.designs
  .find((design) => design.id === "mk4")
  .variants.find((variant) => variant.id === "complex").seeds;

const IDENTITY = {
  droidName: "artoo",
  board: "artoo_esp32",
  board_capabilities: { sbus: true },
  build_flags: { audio: true },
};

export const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// `components` replaces the GET /api/config Output entries configOutputs()
// derives from the rows (every Output wired, carrying an MG996R), for a test
// that needs an Output unwired or carrying the strip.
// `decoys` is markup placed in the document before the surface mounts: a test
// that asserts something is gone writes a decoy where it used to be and
// checks nothing reads or writes it (test/test_web/README.md).
// `frame` replaces the whole status the droid answers with, for a test about a
// frame that is missing something; otherwise the droid answers a whole frame
// with `estop` set.
const bootSurface = async (surface, { outputs = freshOutputs(), estop = false, frame = null, components = null, decoys = [] } = {}) => {
  const document = new MiniDocument();
  const indexHtml = readData("index.html");
  const parsedIndex = new MiniDOMParser().parseFromString(indexHtml);
  parsedIndex.body.children.forEach((child) => document.body.appendChild(document.importNode(child, true)));
  const chain = /data-scripts="([^"]*)"/.exec(indexHtml)[1];
  document.documentElement.setAttribute("data-scripts", chain);
  document.body.setAttribute("data-page", "home");
  document.currentScript = { dataset: { scripts: chain } };

  const env = {
    document,
    outputs,
    // What the config says about each Output, held so a save lands on it.
    components: components || configOutputs(outputs),
    status: frame || statusFrame({ estop }),
    posts: [],       // every POST: { path, form }
    gets: new Map(), // GET path -> count
    intervals: [],
    cleared: [],
    streams: [], // every EventSource the page opened, newest last
    nudgeFails: null, // set to an Error to make the next POST /api/servo fail
    configFails: null, // the same for the next POST /api/config
    centreFails: null, // the same for the next POST /api/servo/centre
    // What the device holds as the Droid Build. A fresh controller comes up on
    // the pre-selected design with that design's complement fitted, which is
    // what data/droid_build.js adopts at boot.
    droidBuild: {
      domeDesign: "mk4",
      domeVariant: "complex",
      bodyDesign: "mk4",
      bodyVariant: "complex",
      fitted: MK4_COMPLEX.slice(),
    },
  };

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
        if (path === "/api/status") return { data: { ...env.status } };
        if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(env.outputs) } };
        // The Droid Build the device holds (ADR 0047). data/droid_build.js
        // reads it once per page and the body view's "add it to the build" act
        // writes it back through POST /api/config.
        if (path === "/api/config") {
          return {
            ok: true,
            data: { droidBuild: structuredClone(env.droidBuild), components: structuredClone(env.components) },
          };
        }
        if (path.endsWith(".html")) return { data: readData(path.slice(1)) };
        throw new Error(`unexpected request ${path}`);
      },
      postForm: async (path, form) => {
        env.posts.push({ path, form: { ...form } });
        if (path === "/api/servo") {
          if (env.nudgeFails) {
            const error = env.nudgeFails;
            env.nudgeFails = null;
            throw error;
          }
          // The firmware finds the Output by its board's label, case and
          // spaces set aside (include/board_outputs.h boardOutputWordMatches()).
          const word = (text) => String(text).replace(/ /g, "").toUpperCase();
          const row = env.outputs.find((each) => each.name !== "" && word(each.name) === word(form.arm));
          // A hold drives the output there and keeps the pulse on it, with
          // both firmware bounds armed. Nothing here expires it: the bounds
          // are the controller's, and a test that wants one fires it with
          // env.wentLimp().
          if (row && form.action === "hold") {
            row.commandedUs = Number(form.positionUs);
            row.targetUs = row.commandedUs;
            row.held = true;
            row.limp = "off";
          }
          // A release takes the pulse off. The output goes limp where it is:
          // nothing is commanded, so the widths stop being a position at all.
          if (row && form.action === "release") {
            row.commandedUs = null;
            row.targetUs = null;
            row.held = false;
            row.limp = "pulses-off";
          }
          // The firmware queues the nudge and answers at once; the nudge
          // itself ends later, when the test bumps nudgesDone.
          return { ok: true, status: 200, data: { ok: true } };
        }
        if (path === "/api/servo/centre") {
          // The controller takes the press and answers at once. The sweep
          // itself is the Sequence Coordinator's - one Output per turn, no
          // closer together than the Cadence Floor - and reaches this page only
          // as commanded positions moving on a later read, which is what
          // env.centred() stands in for.
          if (env.centreFails) {
            const error = env.centreFails;
            env.centreFails = null;
            throw error;
          }
          return { ok: true, status: 200, data: { ok: true } };
        }
        if (path === "/api/config") {
          if (env.configFails) {
            const error = env.configFails;
            env.configFails = null;
            throw error;
          }
          // A capture: the named position is recorded, the row becomes
          // measured, and a captured END that has swallowed the centre drags
          // the centre inside the travel - the firmware's rule
          // (servoOutputCapture(), include/servo_output_row.h), stood in for
          // here so the page can be watched reading the result back rather
          // than re-deriving it.
          if (form.captureOutput) {
            const row = env.outputs.find((each) => each.address === form.captureOutput);
            const us = Number(form.captureUs);
            if (form.captureEnd === "open") row.openUs = us;
            else if (form.captureEnd === "close") row.closeUs = us;
            else row.centreUs = us;
            row.calibrated = true;
            if (form.captureEnd !== "centre") {
              const lo = Math.min(row.openUs, row.closeUs);
              const hi = Math.max(row.openUs, row.closeUs);
              if (row.centreUs < lo) row.centreUs = lo;
              else if (row.centreUs > hi) row.centreUs = hi;
            }
            return { ok: true, status: 200, data: {} };
          }
          // A reverse: the two ends trade places, on the row, from what the row
          // holds. No width travels with it.
          if (form.reverseOutput) {
            const row = env.outputs.find((each) => each.address === form.reverseOutput);
            const wasOpen = row.openUs;
            row.openUs = row.closeUs;
            row.closeUs = wasOpen;
            return { ok: true, status: 200, data: {} };
          }
          // An Output's own settings, under the fields its entry named; the
          // answer is the config the droid now holds.
          if (applyOutputSave(env.components, form)) {
            return { ok: true, status: 200, data: { droidBuild: structuredClone(env.droidBuild), components: structuredClone(env.components) } };
          }
          // The Fitted Parts go whole, because they are a set and there is no
          // partial form of one (data/droid_build.js).
          if (typeof form.fittedParts === "string") {
            env.droidBuild.fitted = form.fittedParts === "" ? [] : form.fittedParts.split(",");
            return { ok: true, status: 200, data: {} };
          }
          // The firmware's move: off whatever Output had the Part, onto the
          // one named.
          env.outputs.forEach((each) => {
            each.parts = each.parts.filter((id) => id !== form.movePart);
          });
          env.outputs.find((each) => each.address === form.movePartTo)?.parts.push(form.movePart);
          return { ok: true, status: 200, data: {} };
        }
        // A dome panel press: the droid relays it over the dome link and
        // answers at once. Nothing comes back about where the panel is.
        if (path === "/api/dome/cmd") return { ok: true, status: 200, data: { ok: true } };
        throw new Error(`unexpected POST ${path}`);
      },
      estopPostForm: async (path) => {
        env.posts.push({ path, form: {} });
        return { data: { ok: true } };
      },
      messageFor: (error) => error.message,
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
    // Recorded, so a test can drop the stream the way the browser reports a
    // lost connection: through the handler status_stream.js installed.
    EventSource: class {
      constructor(url) {
        this.url = url;
        env.streams.push(this);
      }
      addEventListener() {}
      close() {}
    },
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
  };
  context.globalThis = context;

  // Every script data/parts.html and data/servo.html declare, because that is
  // what a browser loads: a map that left one out would run these suites
  // against a page the device never serves. #352 added the Droid Build and the
  // body view to the chain; #412 moved the output-first table to Servos.
  const REAL_SCRIPTS = {
    "/shell.js": readData("shell.js"),
    "/status_stream.js": readData("status_stream.js"),
    "/live_reading.js": readData("live_reading.js"),
    "/droid_parts.js": readData("droid_parts.js"),
    "/droid_part_kind.js": readData("droid_part_kind.js"),
    "/droid_build.js": readData("droid_build.js"),
    "/dome_panel_model.js": readData("dome_panel_model.js"),
    "/body_art.js": readData("body_art.js"),
    "/body_view.js": readData("body_view.js"),
    "/outputs.js": readData("outputs.js"),
    "/parts_mapping.js": readData("parts_mapping.js"),
    "/parts.js": readData("parts.js"),
    "/apply_timing.js": readData("apply_timing.js"),
    "/output_settings.js": readData("output_settings.js"),
    "/servo.js": readData("servo.js"),
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

  env.partsRegion = () => document.getElementById("parts-table");
  env.partRow = (id) => env.partsRegion().querySelectorAll("[data-part]").find((node) => node.dataset.part === id);
  // Find by moving sits over Servos' rows: one picker of unwired Parts and one
  // button, so the button a Part is found with is that one.
  env.findPick = () => document.getElementById("outputs-find-part");
  env.findButton = () => document.getElementById("outputs-find").querySelector(".parts-find");
  env.runPanel = () => document.querySelector(".parts-find-run");
  env.runText = () => env.runPanel()?.querySelector(".parts-find-text")?.textContent ?? null;
  env.feedback = () => document.getElementById(surface === "servo" ? "outputs-feedback" : "parts-feedback").textContent;
  env.region = () => document.getElementById("outputs-table");
  env.rows = () => env.region().querySelectorAll("[data-output]");
  env.row = (address) => env.rows().find((node) => node.dataset.output === address);
  env.cell = (address, className) => env.row(address).querySelector(`.${className}`);
  env.byId = (id) => document.getElementById(id);
  env.tier = (id) => document.querySelectorAll("[data-tier]").find((node) => node.dataset.tier === id).textContent;
  // What a builder does with an Output row's part picker: choose, and the
  // change reaches the table's delegated handler.
  env.pickOnOutput = (address, partId) => {
    const select = env.row(address).querySelector(".outputs-add");
    select.value = partId;
    env.region().fire("change", { target: select });
    return select;
  };
  // The move question's two answers, by what they are rather than by an id.
  env.answerMove = (confirmed) => env.dialog.querySelector(confirmed ? ".move-confirm" : ".move-cancel").fire("click", {});
  env.text = (address, className) => env.cell(address, className).textContent;
  // Nudges only. POST /api/servo carries four actions since #364, so filtering
  // on the path alone would count a hold or a release as a nudge.
  env.nudges = () =>
    env.posts.filter((post) => post.path === "/api/servo" && post.form.action === "nudge");
  env.moves = () => env.posts.filter((post) => post.path === "/api/config");
  // The builder picks the Part and presses Find by moving.
  env.pressFind = (id) => {
    env.findPick().value = id;
    env.findButton().fire("click", {});
  };
  env.pressThatOne = () => env.runPanel().querySelector(".parts-find-that").fire("click", {});
  env.pressStop = () => env.runPanel().querySelector(".parts-find-stop").fire("click", {});
  // One tick of the page's own bench feed: the poll refreshes when the page
  // becomes visible again, which runs exactly the attempt its interval runs.
  env.frame = async () => {
    document.dispatch("visibilitychange", { type: "visibilitychange" });
    await sleep(20);
  };
  // The droid says a nudge on this Output has ended, however it ended.
  env.endNudge = (address) => {
    env.outputs.find((each) => each.address === address).nudgesDone += 1;
  };
  // One of the controller's own bounds fired, or anything else took the pulse
  // off: the Output is limp and says why. Ending a nudge counts as ended, so
  // the count goes up with it, exactly as the firmware does it.
  env.wentLimp = (address, why) => {
    const row = env.outputs.find((each) => each.address === address);
    row.commandedUs = null;
    row.targetUs = null;
    row.held = false;
    row.limp = why;
    row.nudgesDone += 1;
  };
  env.holds = () => env.posts.filter((post) => post.path === "/api/servo" && post.form.action === "hold");
  env.releases = () => env.posts.filter((post) => post.path === "/api/servo" && post.form.action === "release");
  env.captures = () => env.posts.filter((post) => post.path === "/api/config" && post.form.captureOutput);
  env.reverses = () => env.posts.filter((post) => post.path === "/api/config" && post.form.reverseOutput);
  // Back to centre (#365): the act above the rows, its one line of answer, and
  // the requests it made. One request per press and no more - the pace is the
  // controller's, so a page that sent a second one would be pacing.
  env.centreButton = () => document.querySelector(".outputs-centre");
  env.centreSaid = () => document.querySelector(".outputs-bulk").querySelector(".feedback").textContent;
  env.centreSaidLevel = () => document.querySelector(".outputs-bulk").querySelector(".feedback").className;
  env.pressCentre = () => env.centreButton().fire("click", {});
  env.centreRequests = () => env.posts.filter((post) => post.path === "/api/servo/centre");
  // The controller has driven one Output to its recorded centre, the way the
  // sweep does: the next read of the outputs shows it there.
  env.centred = (address) => {
    const row = env.outputs.find((each) => each.address === address);
    row.commandedUs = row.centreUs;
    row.targetUs = row.centreUs;
    row.limp = "off";
  };
  env.dial = () => document.querySelector(".cal-panel");
  env.dialOpen = () => env.dial().hidden === false;
  env.dialNote = () => env.dial().querySelector(".cal-note").textContent;
  env.dialText = (className) => env.dial().querySelector(`.${className}`).textContent;
  env.dialButton = (className) => env.dial().querySelector(`.${className}`);
  env.slider = () => env.dial().querySelector(".cal-slider");
  // The acts on an Output row reach the table's delegated handler the way a
  // real click does, the same shape env.pressFind uses for the other table.
  env.pressCalibrate = (address) =>
    env.region().fire("click", { target: env.row(address).querySelector(".outputs-calibrate") });
  env.pressPulsesOff = (address) =>
    env.region().fire("click", { target: env.row(address).querySelector(".outputs-off") });
  // The builder drags the dial. mini_dom has no value semantics for a range
  // input, so the value is set the way a browser would before the event.
  env.drive = (us) => {
    env.slider().value = String(us);
    env.slider().fire("input", { target: env.slider() });
  };
  env.pressDial = (className) => {
    const button = env.dialButton(className);
    env.dial().fire("click", { target: button });
  };
  // A status frame on the shared stream, the way /api/events delivers one: a
  // whole frame with `changes` on top.
  env.pushStatus = (changes) => windowMock.PAStatusStream.seed(statusFrame(changes));
  // The stream drops, the way a browser reports it: the open EventSource's
  // own error handler, which status_stream.js installed.
  env.loseStream = () => {
    const stream = env.streams[env.streams.length - 1];
    assert.ok(stream?.onerror, "the page opened a status stream");
    stream.onerror();
  };
  env.navigate = (to) => {
    windowMock.location.hash = to;
  };

  decoys.forEach((html) => {
    const holder = document.createElement("div");
    holder.innerHTML = html;
    holder.children.slice().forEach((child) => document.body.appendChild(child));
  });

  windowMock.location.hash = `#${surface}`;
  const painted = () =>
    surface === "servo"
      ? env.region() && env.rows().length > 0
      : env.partsRegion()?.querySelector("select")?.disabled === false;
  const deadline = Date.now() + 3000;
  while (!painted()) {
    if (Date.now() > deadline) assert.fail(`the ${surface} surface never mounted and painted its rows`);
    await sleep(5);
  }
  // The shell's own status read has landed and been handed to the stream by
  // now, so the acts have been gated once.
  await sleep(30);

  // A browser's <dialog>; mini_dom has none.
  const dialog = document.getElementById(surface === "servo" ? "outputs-move-dialog" : "parts-move-dialog");
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

export const bootParts = (options) => bootSurface("parts", options);
export const bootServos = (options) => bootSurface("servo", options);

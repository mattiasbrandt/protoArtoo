// =============================================================================
// test/test_web/test_guided_setup_351.js
//
// Guided Setup: the first-run takeover (#351).
//
// These run the shipped data/setup.js against the shipped data/setup.html, with
// the legacy asset set's product-art partial expanded exactly as the artoo_esp32
// build serves it. The markup is the point rather than a fixture: which card is
// on screen at each step is decided by a data-setup-step attribute in that file,
// so a step whose host is renamed or lost has to turn this suite red.
//
// The defect behind the whole slice: every component toggle defaults false, so a
// fresh flash boots inert and a category nobody was ever asked about is
// indistinguishable, in the config alone, from one the builder looked at and
// declined. The run records what it actually showed, and a question it has not
// shown renders hollow rather than ticked.
//
// The counting tests are the other half. The planning for this run said nine
// steps in one place and ten in another; the rule that settles it is that no
// total is ever typed, so the board - shown rather than asked - must not consume
// a number and the question after it must still read as the second question.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { MiniDOMParser } from "./helpers/mini_dom.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataDir = join(__dirname, "../../data");
const setupSrc = readFileSync(join(dataDir, "setup.js"), "utf8");

const INCLUDE_RE = /<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+)\s*-->/g;

// setup.html as the artoo_esp32 build serves it: the legacy set's sprite
// inlined. The recovery kernel is not what this suite is about and stays an
// unexpanded comment, the way test_board_panel_identity_retry.js leaves it.
const setupDocument = () => {
  const page = readFileSync(join(dataDir, "setup.html"), "utf8").replace(
    INCLUDE_RE,
    (directive, target) =>
      target === "_product_art.html"
        ? readFileSync(join(dataDir, "asset-sets", "legacy", target), "utf8")
        : directive,
  );
  return new MiniDOMParser().parseFromString(page);
};

// A fresh flash: provisioned, inert, and guided Setup has never been drawn on
// it. Deliberately every toggle false, because that is the state in which "not
// fitted" is a claim nobody made.
const freshConfig = () => ({
  components: {
    arm1: { enabled: false, type: "mg996r" },
    arm2: { enabled: false, type: "mg996r" },
    aux1: { enabled: false, type: "none" },
    aux2: { enabled: false, type: "none" },
    aux3: { enabled: false, type: "none" },
    domeEsc: { enabled: false },
    rcCh1: { enabled: false },
    rcCh2: { enabled: false },
    rcCh3: { enabled: false },
    rcCh4: { enabled: false },
    rcCh5: { enabled: false },
    rcCh6: { enabled: false },
    drive: { enabled: false },
    audio: { enabled: false },
    protoR2link: { enabled: false },
  },
  system: { logLevel: 3 },
  aux_led_pin: 0,
  aux_led_count: 1,
  wifi: {
    provisioned: true,
    mode: "client",
    staSsid: "bench-ap",
    staPasswordSet: true,
    apSsid: "artoo",
    apPasswordSet: false,
  },
  guidedSetup: { run: "not-run", recorded: false, visited: [] },
});

const IDENTITY = {
  droidName: "artoo",
  mdnsUseName: true,
  board: "artoo_esp32",
  board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
  build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
};

// Everything the module reaches for that is not this suite's subject.
const makeStub = () => ({
  id: "",
  dataset: {},
  style: {},
  className: "",
  classList: { add() {}, remove() {}, contains: () => false },
  textContent: "",
  innerHTML: "",
  value: "",
  checked: false,
  disabled: false,
  hidden: false,
  type: "checkbox",
  addEventListener() {},
  setAttribute() {},
  removeAttribute() {},
  querySelectorAll: () => [],
  querySelector: () => null,
  closest: () => null,
  appendChild() {},
  click() {},
});

const boot = ({ config = freshConfig() } = {}) => {
  const parsed = setupDocument();
  const posts = [];
  const sections = new Map();
  const timers = [];
  let nextTimer = 1;

  // The real document for the ids and the two selectors this module actually
  // asks for. Everything else gets a stub, because the rest of setup.js wires up
  // dozens of controls at load and none of that is what these tests are about -
  // and because mini_dom's selector grammar does not carry the compound
  // selector the segmented type controls use.
  const documentMock = {
    getElementById: (id) => parsed.getElementById(id) || makeStub(),
    querySelectorAll: (selector) =>
      selector === ".card" || selector === "[data-setup-step]"
        ? parsed.querySelectorAll(selector)
        : [],
    querySelector: () => makeStub(),
    createElement: (tag) => parsed.createElement(tag),
    createTextNode: () => makeStub(),
    addEventListener() {},
    removeEventListener() {},
    body: parsed.body,
  };

  const windowListeners = new Map();
  const windowMock = {
    document: documentMock,
    PAAssetsReady: true,
    PAIdentity: IDENTITY,
    PAApi: {
      messageFor: (error) => String(error?.message || error),
      get: async (path) => {
        if (path === "/api/config") return { data: config };
        throw new Error(`unexpected GET ${path}`);
      },
      // The body arrives as a plain object from the run and as URLSearchParams
      // from the restore path, and both are kept as they came: reading a
      // URLSearchParams through a spread gives {} and would make every
      // assertion below true by construction.
      postForm: async (path, body) => {
        const read = (key) => (body instanceof URLSearchParams ? body.get(key) : body[key]);
        posts.push({ path, body, get: read });
        const run = read("guidedSetupRun");
        const seen = read("guidedSetupVisited");
        if (run !== undefined && run !== null) config.guidedSetup.run = run;
        if (seen !== undefined && seen !== null) {
          config.guidedSetup.recorded = true;
          config.guidedSetup.visited = seen === "-" ? [] : seen.split(",");
        }
        return { data: config };
      },
    },
    PABootstrap: {
      registerSection: (name, load) => sections.set(name, load),
      setResourceLabels() {},
      retryNow() {},
    },
    PageBootstrap: { createBackgroundPoll: () => ({ start() {}, stop() {} }) },
    PASurface: { poll: () => ({ start() {}, stop() {}, cancelRetry() {} }) },
    addEventListener(type, handler) {
      if (!windowListeners.has(type)) windowListeners.set(type, []);
      windowListeners.get(type).push(handler);
    },
    removeEventListener() {},
    // Recorded rather than run, then fired by hand: the visited record is
    // debounced, and a test that waited on a real clock would be a test that can
    // pass by timing out.
    setTimeout: (fn) => {
      timers.push({ id: nextTimer, fn });
      return nextTimer++;
    },
    clearTimeout: (id) => {
      const at = timers.findIndex((t) => t.id === id);
      if (at >= 0) timers.splice(at, 1);
    },
    setInterval: () => 0,
    clearInterval() {},
    location: { origin: "http://device", href: "http://device/setup.html" },
    localStorage: { getItem: () => null, setItem() {} },
    requestAnimationFrame: () => 1,
    confirm: () => true,
    CustomEvent: class {
      constructor(type, init = {}) {
        this.type = type;
        this.detail = init.detail;
      }
    },
    Event: class {},
  };

  // The restore path reads the chosen file through one of these. Synchronous on
  // purpose: a test that waited on a real reader would be a test that can pass
  // by timing out.
  class FileReaderStub {
    readAsText(file) {
      this.onload?.({ target: { result: file.text } });
    }
  }

  const context = {
    window: windowMock,
    document: documentMock,
    console: { log() {}, warn() {}, error() {}, info() {} },
    setTimeout: windowMock.setTimeout,
    clearTimeout: windowMock.clearTimeout,
    setInterval: windowMock.setInterval,
    clearInterval: windowMock.clearInterval,
    fetch: async () => ({ json: async () => ({}) }),
    confirm: () => true,
    CustomEvent: windowMock.CustomEvent,
    Event: windowMock.Event,
    FileReader: FileReaderStub,
    URLSearchParams,
    AbortController,
    JSON,
    Math,
    Date,
    Number,
    String,
    Boolean,
    Array,
    Object,
    Set,
    Map,
    Promise,
    Error,
    RegExp,
  };
  context.globalThis = context;
  for (const key of ["PABootstrap", "PageBootstrap"]) context[key] = windowMock[key];

  vm.runInNewContext(setupSrc, context, { filename: "setup.js" });

  const id = (name) => parsed.getElementById(name);
  const shown = (element) => Boolean(element) && !element.classList.contains("hidden");
  const host = (key) =>
    parsed.querySelectorAll("[data-setup-step]").find((node) => node.getAttribute("data-setup-step") === key);
  const cardHolding = (element) => {
    let node = element;
    while (node && !node.classList?.contains("card")) node = node.parentElement;
    return node;
  };

  return {
    parsed,
    posts,
    config,
    id,
    shown,
    host,
    cardHolding,
    text: (name) => id(name)?.textContent ?? null,
    runSection: () => sections.get("setup-guided-run")(),
    click: (name) => id(name).fire("click", {}),
    // Fires every timer the module has pending, which is how the debounced
    // visited write is made to happen without a clock.
    flushTimers: () => {
      const pending = timers.splice(0, timers.length);
      pending.forEach((t) => t.fn());
    },
    chips: () =>
      id("wizard-rail").children.map((chip) => ({
        title: chip.querySelector(".wizard-chip-title")?.textContent,
        tick: chip.querySelector(".wizard-tick")?.textContent ?? null,
        answer: chip.querySelector(".wizard-chip-answer")?.textContent,
        current: chip.classList.contains("is-current"),
      })),
    railClick: (index) => id("wizard-rail").children[index].fire("click", {}),
  };
};

test("a droid nobody has been asked about opens the run, not the configuration page", async () => {
  const env = boot();
  assert.equal(env.shown(env.id("wizard-checking")), true, "the surface says it is reading before it knows");
  assert.equal(env.shown(env.id("wizard-head")), false);

  await env.runSection();

  assert.equal(env.shown(env.id("wizard-head")), true, "the run's chrome is on screen");
  assert.equal(env.shown(env.id("wizard-foot")), true);
  assert.equal(env.shown(env.id("wizard-checking")), false);
  // The step the run opens on, and nothing else from the page.
  assert.equal(env.shown(env.host("wifi")), true);
  assert.equal(env.shown(env.host("drive")), false);
  assert.equal(env.shown(env.cardHolding(env.id("backup-download-btn"))), false, "Backup is not part of a first run");
});

test("the step shown rather than asked does not consume a number", async () => {
  const env = boot();
  await env.runSection();
  assert.equal(env.text("wizard-position"), "Question 1 of 8 · you can stop at any step, and stopping ends the run.");

  env.click("wizard-next");
  assert.equal(env.text("wizard-step-name"), "Body Controller");
  assert.match(env.text("wizard-position"), /^Body Controller — shown, not asked\. Question 2 of 8 is next/);

  // The question AFTER the board is still the second question. A run that
  // counted the board would say three of nine here, which is exactly the
  // nine-versus-ten confusion this rule exists to stop.
  env.click("wizard-next");
  assert.equal(env.text("wizard-step-name"), "Foot Drive");
  assert.equal(env.text("wizard-position"), "Question 2 of 8 · you can stop at any step, and stopping ends the run.");
});

test("the footer states position and escape, and promises nothing about when an answer applies", async () => {
  const env = boot();
  await env.runSection();
  for (let step = 0; step < 9; step += 1) {
    const line = env.text("wizard-position");
    assert.match(line, /you can stop at any step, and stopping ends the run/, "the escape is stated at every step");
    assert.doesNotMatch(line, /appl|straight away|locked in|restart|reboot/i, `step ${step} footer promises a timing: ${line}`);
    if (step < 8) env.click("wizard-next");
  }
});

test("a question that has not been on screen renders hollow, and the one that has renders ticked", async () => {
  const env = boot();
  await env.runSection();

  const opening = env.chips();
  assert.equal(opening.length, 9, "the rail draws every step in the run");
  assert.equal(opening[0].tick, "✓", "the question on screen has been asked");
  assert.equal(opening[1].tick, null, "the board is not a question, so it carries no mark at all");
  assert.deepEqual(
    opening.slice(2).map((chip) => chip.tick),
    new Array(7).fill("○"),
    "every question still to come is hollow",
  );
  // The answer shows either way, because the default is a real value that has
  // simply not been looked at.
  assert.equal(opening[2].answer, "Not fitted");
  assert.equal(env.shown(env.id("wizard-legend")), true, "the hollow mark is explained in visible text");

  env.click("wizard-next");
  env.click("wizard-next");
  assert.equal(env.chips()[2].tick, "✓", "a question that has now been on screen is ticked");
  assert.equal(env.chips()[3].tick, "○", "the one after it has not");
});

test("the run tells the controller which steps it actually showed", async () => {
  const env = boot();
  await env.runSection();
  env.flushTimers();
  assert.deepEqual(
    env.posts.map((post) => post.get("guidedSetupVisited")),
    ["wifi"],
    "the step the run opened on is recorded",
  );

  // Straight to the last question: the six it jumped over were never on screen
  // and must not be claimed.
  env.railClick(8);
  env.flushTimers();
  assert.equal(env.posts.at(-1).get("guidedSetupVisited"), "wifi,name");
  assert.equal(env.posts.at(-1).path, "/api/config");
});

test("skipping and finishing both end the run, and stay different facts about the droid", async () => {
  const skipped = boot();
  await skipped.runSection();
  skipped.click("wizard-stop");
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(skipped.posts.at(-1).get("guidedSetupRun"), "skipped");

  const finished = boot();
  await finished.runSection();
  for (let step = 0; step < 8; step += 1) finished.click("wizard-next");
  assert.equal(finished.text("wizard-next"), "Finish", "the last step offers the end of the run, not another step");
  finished.click("wizard-next");
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(finished.posts.at(-1).get("guidedSetupRun"), "completed");
});

test("the run leaves for good, and the page it leaves behind is the whole page", async () => {
  const env = boot();
  await env.runSection();
  env.click("wizard-stop");
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(env.shown(env.id("wizard-head")), false, "the run's chrome is gone");
  assert.equal(env.shown(env.id("wizard-foot")), false);
  assert.equal(env.shown(env.id("wizard-wifi-card")), false, "a card that only belonged to the run goes with it");
  assert.equal(env.shown(env.cardHolding(env.id("backup-download-btn"))), true);
  assert.equal(env.shown(env.host("drive")), true, "every question's controls are back where they always were");
  assert.equal(env.shown(env.host("rc")), true);

  // The same controller, read again: Setup is a takeover and never re-opens.
  const again = boot({ config: env.config });
  await again.runSection();
  assert.equal(again.shown(again.id("wizard-head")), false);
  assert.equal(again.shown(again.cardHolding(again.id("backup-download-btn"))), true);
});

test("a droid configured before the record existed is not walked through a first run", async () => {
  // No record at all, and components switched on: somebody set this droid up
  // before guided Setup could say so. Reporting its categories as never asked -
  // or marching its owner through a first run - is the untruth the record exists
  // to prevent, arriving from the other side.
  const config = freshConfig();
  config.components.drive.enabled = true;
  config.guidedSetup = { run: "not-run", recorded: false, visited: [] };

  const env = boot({ config });
  await env.runSection();
  env.flushTimers();

  assert.equal(env.shown(env.id("wizard-head")), false, "no run opens");
  assert.equal(env.shown(env.cardHolding(env.id("backup-download-btn"))), true);
  assert.deepEqual(env.posts, [], "and reading the page writes nothing to the controller");
});

test("an inert droid with no record is a first run, not a grandfathered one", async () => {
  // The other side of the rule above: nothing is switched on, so there is
  // nothing to grandfather and this really is a fresh flash.
  const config = freshConfig();
  config.guidedSetup = { run: "not-run", recorded: false, visited: [] };
  const env = boot({ config });
  await env.runSection();
  assert.equal(env.shown(env.id("wizard-head")), true);
});

test("Backup and Restore carries the run's record like any other config key", async () => {
  // The restore path flattens a backup's GET /api/config body into POST form
  // params by hand, so a key it does not name is silently dropped on the way
  // back in - and a configured backup restored without this one would read as
  // never asked for every category (operator, 2026-09-17 on #371).
  //
  // Driven the way a builder drives it: choose a file, tick Core config, press
  // Restore. What the controller was asked for is the observable.
  const restoreParamsFor = async (guidedSetup) => {
    const env = boot();
    await env.runSection();
    const backup = { schema: 1, config: { ...freshConfig(), guidedSetup } };

    const fileInput = env.id("backup-file-input");
    fileInput.files = [{ text: JSON.stringify(backup) }];
    fileInput.fire("change", {});

    env.id("restore-chk-config").checked = true;
    env.id("restore-chk-rc-map").checked = false;
    env.id("restore-chk-audio-tracks").checked = false;
    env.id("restore-chk-mood-map").checked = false;

    env.click("backup-restore-btn");
    await new Promise((resolve) => setImmediate(resolve));
    const restore = env.posts.filter((post) => post.body instanceof URLSearchParams).at(-1);
    assert.ok(restore, "the restore must have reached the controller at all");
    return restore.body;
  };

  const configured = await restoreParamsFor({ run: "completed", recorded: true, visited: ["wifi", "drive"] });
  assert.equal(configured.get("guidedSetupRun"), "completed");
  assert.equal(configured.get("guidedSetupVisited"), "wifi,drive");

  const showedNothing = await restoreParamsFor({ run: "skipped", recorded: true, visited: [] });
  assert.equal(
    showedNothing.get("guidedSetupVisited"),
    "-",
    "a run that showed nothing is an answer, and an empty form value would not survive as one",
  );
});

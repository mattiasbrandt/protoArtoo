// =============================================================================
// test/test_web/test_guided_setup.js
//
// Guided Setup: the first-run takeover (#351), drawn over Configuration (#404).
//
// These run the shipped data/setup.js on the Configuration surface it takes
// over - data/configuration.html with the surface's own script chain - with the
// legacy asset set's product-art partial expanded exactly as the artoo_esp32
// build serves it. The markup is the point rather than a fixture: which card is
// on screen at each step is decided by a data-setup-step attribute in that file,
// so a step whose host is renamed or lost has to turn this suite red. The
// run's record travelling with a backup is Maintenance's half, and runs on that
// surface's own markup and chain.
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

// Each surface as the shell mounts it: its markup, and the scripts its
// <html data-scripts> loads after the shell's own.
const SURFACES = {
  configuration: {
    html: "configuration.html",
    scripts: [
      "feature_availability.js",
      "droid_parts.js",
      "droid_build.js",
      "dome_layout.js",
      "product_art.js",
      "apply_timing.js",
      "droid_build_picker.js",
      "component_picker.js",
      "configuration.js",
      "setup.js",
    ],
  },
  maintenance: {
    html: "maintenance.html",
    scripts: ["feature_availability.js", "outputs.js", "maintenance.js"],
  },
};

const INCLUDE_RE = /<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+)\s*-->/g;

// A surface as the artoo_esp32 build serves it: the legacy set's sprite
// inlined. The recovery kernel is not what this suite is about and stays an
// unexpanded comment, the way test_board_panel_identity_retry.js leaves it.
const surfaceDocument = (html) => {
  const page = readFileSync(join(dataDir, html), "utf8").replace(
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

const boot = ({ config = freshConfig(), surface = "configuration", domeLayout = null } = {}) => {
  const parsed = surfaceDocument(SURFACES[surface].html);
  const posts = [];
  const gets = [];
  const sections = new Map();
  const timers = [];
  let nextTimer = 1;

  // The real document for the ids and the two selectors the run actually asks
  // for. Everything else gets a stub, because the rest of the surface wires up
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
    createElementNS: (_ns, tag) => parsed.createElement(tag),
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
        gets.push(path);
        if (path === "/api/config") return { ok: true, data: config };
        // A dome on the WiFi link answers with its layout; with none given,
        // the controller's own answer for a dome it cannot reach (docs/api.md).
        if (path === "/api/dome/layout") {
          if (domeLayout) return { ok: true, data: domeLayout };
          throw new Error("dome layout unavailable");
        }
        throw new Error(`unexpected GET ${path}`);
      },
      // The body arrives as a plain object from the run and as URLSearchParams
      // from the restore path, and both are kept as they came: reading a
      // URLSearchParams through a spread gives {} and would make every
      // assertion below true by construction.
      postForm: async (path, body) => {
        const read = (key) => (body instanceof URLSearchParams ? body.get(key) : body[key]);
        posts.push({ path, body, get: read });
        if (read("domeDesign") !== undefined || read("bodyDesign") !== undefined) {
          const build = config.droidBuild || (config.droidBuild = {});
          ["domeDesign", "domeVariant", "bodyDesign", "bodyVariant"].forEach((key) => {
            if (read(key) !== undefined && read(key) !== null) build[key] = read(key);
          });
          if (read("fittedParts") !== undefined) build.fitted = read("fittedParts").split(",");
          return { ok: true, data: config };
        }
        const run = read("guidedSetupRun");
        const seen = read("guidedSetupVisited");
        const done = read("guidedSetupSummaryDone");
        if (run !== undefined && run !== null) config.guidedSetup.run = run;
        if (done !== undefined && done !== null) config.guidedSetup.summaryDone = done === "true";
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
      // Re-runs a section the way the bootstrap does on an explicit refresh.
      refreshSections: (names) => names.forEach((name) => sections.get(name)?.()),
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
    location: { origin: "http://device", href: `http://device/${SURFACES[surface].html}` },
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

  // Every document loads the status stream and the Live Reading ahead of a
  // surface, and the Operator Shell starts the reading before any surface runs.
  for (const file of ["status_stream.js", "live_reading.js", ...SURFACES[surface].scripts]) {
    vm.runInNewContext(readFileSync(join(dataDir, file), "utf8"), context, { filename: file });
    if (file === "live_reading.js") windowMock.PALiveReading.start();
  }

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
    gets,
    config,
    window: windowMock,
    location: windowMock.location,
    // Delivers a window event to what the surface registered for it.
    emitWindow: (type, event = {}) => (windowListeners.get(type) || []).forEach((handler) => handler(event)),
    settle: async () => {
      for (let turn = 0; turn < 4; turn += 1) await new Promise((resolve) => setImmediate(resolve));
    },
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

test("the run tells the controller which steps it actually showed", async () => {
  const env = boot();
  await env.runSection();
  env.flushTimers();
  assert.deepEqual(
    env.posts.map((post) => post.get("guidedSetupVisited")),
    ["wifi"],
    "the step the run opened on is recorded",
  );

  // Straight to the last question: every step it jumped over was never on
  // screen and must not be claimed. Found by position rather than typed, so a
  // step inserted into the run does not move what this asks about.
  env.railClick(env.chips().length - 1);
  env.flushTimers();
  assert.equal(env.posts.at(-1).get("guidedSetupVisited"), "wifi,name");
  assert.equal(env.posts.at(-1).path, "/api/config");
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
  assert.equal(env.shown(env.cardHolding(env.id("enable-drive"))), true, "Configuration is its own cards");
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

// Choose a backup file, tick Core config, press Restore - and hand back the form
// the controller was actually asked for. Shared by the two tests below, because
// a key dropped on the way back in is the same defect whichever key it is.
const restoreParamsFor = async (patch) => {
  const env = boot({ surface: "maintenance" });
  const backup = { schema: 1, config: { ...freshConfig(), ...patch } };

  const fileInput = env.id("backup-file-input");
  fileInput.files = [{ text: JSON.stringify(backup) }];
  fileInput.fire("change", {});

  env.id("restore-chk-config").checked = true;
  env.id("restore-chk-rc-map").checked = false;
  env.id("restore-chk-audio-tracks").checked = false;
  env.id("restore-chk-mood-map").checked = false;

  env.click("backup-restore-btn");
  // The restore reads the Outputs before it saves (data/outputs.js, #415).
  for (let turn = 0; turn < 4; turn += 1) await new Promise((resolve) => setImmediate(resolve));
  const restore = env.posts.filter((post) => post.body instanceof URLSearchParams).at(-1);
  assert.ok(restore, "the restore must have reached the controller at all");
  return restore.body;
};

test("Backup and Restore carries the run's record like any other config key", async () => {
  // The restore path flattens a backup's GET /api/config body into POST form
  // params by hand, so a key it does not name is silently dropped on the way
  // back in - and a configured backup restored without this one would read as
  // never asked for every category (operator, 2026-09-17 on #371).
  const configured = await restoreParamsFor({
    guidedSetup: { run: "completed", recorded: true, visited: ["wifi", "drive"] },
  });
  assert.equal(configured.get("guidedSetupRun"), "completed");
  assert.equal(configured.get("guidedSetupVisited"), "wifi,drive");

  // Done on the ended run's summary is the same record, and travels the same
  // way: a restored droid whose builder dismissed it does not show it again.
  const dismissed = await restoreParamsFor({
    guidedSetup: { run: "completed", recorded: true, visited: ["wifi"], summaryDone: true },
  });
  assert.equal(dismissed.get("guidedSetupSummaryDone"), "true");

  const showedNothing = await restoreParamsFor({
    guidedSetup: { run: "skipped", recorded: true, visited: [] },
  });
  assert.equal(
    showedNothing.get("guidedSetupVisited"),
    "-",
    "a run that showed nothing is an answer, and an empty form value would not survive as one",
  );
});

// Riding along: the same flattener was already dropping two answers a builder
// gave, and the page promises "Core config: restored" over the top of it. Found
// while wiring the run's record through it.
test("a restore puts back the sound module and the droid build, which it used to drop", async () => {
  const restored = await restoreParamsFor({
    components: { ...freshConfig().components, audio: { enabled: true, member: "mp3_trigger", activeMember: "dy_sv5w" } },
    droidBuild: {
      domeDesign: "mk4", domeVariant: "complex",
      bodyDesign: "mk3", bodyVariant: "simple",
      fitted: ["domePie1", "bodyDoorL"],
    },
  });

  // The SAVED choice, never the one the droid happens to have booted with.
  assert.equal(restored.get("soundMember"), "mp3_trigger");

  // Each half as a pair: the controller refuses a design without its variant.
  assert.equal(restored.get("domeDesign"), "mk4");
  assert.equal(restored.get("domeVariant"), "complex");
  assert.equal(restored.get("bodyDesign"), "mk3");
  assert.equal(restored.get("bodyVariant"), "simple");
  assert.equal(restored.get("fittedParts"), "domePie1,bodyDoorL");
});

// The radio a builder holds is the Radio Controller's Component Member (#369),
// saved beside the receiver type. A restore that dropped it would put the droid
// back on the default radio while the builder's receiver setting came back.
test("a restore puts back which RC Radio the droid is driven with", async () => {
  const restored = await restoreParamsFor({ rc: { member: "rc_radio", inputMode: "standard_pwm" } });
  assert.equal(restored.get("rcMember"), "rc_radio");
  assert.equal(restored.get("rcInputMode"), "standard_pwm");
});

// Which picture a design card shows is the catalog's to say (docs/droid-parts.yaml
// `picture:`, #369): every MrBaddeley design shares his, and a design that
// names none - "my own build" - is words alone. On the legacy set that picture
// is the page's own drawing.
test("a design card shows the picture its catalog row names, and my own build shows none", async () => {
  const env = boot();
  await env.runSection();
  await env.settle();
  const plates = env.id("droid-build-body").querySelectorAll("[data-option]");
  const picture = (design) =>
    plates.find((plate) => plate.getAttribute("data-option") === design)?.querySelector("use")?.getAttribute("href");
  for (const design of ["mk4", "mk41", "mk3"]) {
    assert.equal(picture(design), "#art-mrbaddeley", `${design} shows MrBaddeley's picture`);
  }
  const own = plates.find((plate) => plate.getAttribute("data-option") === "own");
  assert.ok(own, "my own build is on the step");
  assert.equal(own.querySelector(".component-card-art"), null, "and it carries no picture at all");
});

// =============================================================================
// The one way back in (#297, CONTEXT.md "Maintenance")
//
// The run never re-opens by itself. Maintenance carries the single deliberate
// way back, and it may not do it by wiping what the builder answered: the
// record of which questions were shown is the thing that keeps "never asked"
// honest, and a reopened run nobody can move through is not open.
// =============================================================================

test("Maintenance's way back in writes the run and nothing the builder answered", async () => {
  const finished = freshConfig();
  finished.components.drive.enabled = true;
  finished.guidedSetup = { run: "completed", recorded: true, visited: ["wifi", "drive", "name"] };
  const env = boot({ config: finished, surface: "maintenance" });
  env.click("setup-again-button");
  await env.settle();

  assert.equal(env.posts.length, 1, "one write");
  assert.deepEqual(Object.keys(env.posts[0].body), ["guidedSetupRun"], "the run, and nothing else");
  assert.equal(env.posts[0].get("guidedSetupRun"), "not-run");
  assert.deepEqual(finished.guidedSetup.visited, ["wifi", "drive", "name"], "the questions that were asked stay asked");
  assert.equal(env.location.hash, "configuration", "and the builder is taken to where the run is drawn");

  // Set up before the record existed: with no record, not-run reads as set up
  // all the same, so the empty record goes with it or nothing reopens.
  const older = freshConfig();
  older.components.drive.enabled = true;
  older.guidedSetup = { run: "not-run", recorded: false, visited: [] };
  const olderEnv = boot({ config: older, surface: "maintenance" });
  olderEnv.click("setup-again-button");
  await olderEnv.settle();
  assert.equal(olderEnv.posts[0].get("guidedSetupRun"), "not-run");
  assert.equal(olderEnv.posts[0].get("guidedSetupVisited"), "-");
});

test("a run reopened from Maintenance opens at its first question and can be moved through", async () => {
  const config = freshConfig();
  const env = boot({ config });
  await env.runSection();
  env.railClick(2);
  env.click("wizard-stop");
  await env.settle();
  assert.equal(env.shown(env.id("wizard-head")), false, "stopping ended the run");

  // What Maintenance's press leaves on the controller, then the word it sends
  // once Configuration is back on screen.
  config.guidedSetup.run = "not-run";
  env.emitWindow("pa:guided-setup-reopened");
  await env.settle();

  assert.equal(env.shown(env.id("wizard-head")), true, "the run is open again");
  assert.equal(env.chips().findIndex((chip) => chip.current), 0, "at its first question");
  assert.equal(env.id("wizard-next").disabled, false, "Next can be pressed");
  assert.equal(env.id("wizard-stop").disabled, false, "and so can Stop");
  env.click("wizard-next");
  assert.equal(env.chips().findIndex((chip) => chip.current), 1, "and pressing it moves the run on");
});

// =============================================================================
// What the ended run leaves behind (#371)
//
// The run's end, through the shipped markup and chain, by both of the ways a
// run ends. Four things here would each make the summary lie with every row
// still looking right: a question nobody was shown reported as answered, a
// step the summary leaves out, a wait with no way to end it, and a summary
// that reopens after the builder has dismissed it.
// =============================================================================

// The summary as a builder reads it: each row's step, its mark (null on a row
// that is not about a question) and whether it carries a link out.
const summaryRows = (env, heading) => {
  // By position, not by an h3 selector, which mini_dom's grammar does not
  // carry: a section is its .sect head, whose first child is the heading.
  const section = env.id("setup-summary-body").children.find(
    (child) => child.children[0]?.children[0]?.textContent === heading,
  );
  if (!section) return [];
  return section.querySelector(".setup-summary-list").children.map((row) => ({
    step: row.dataset.step,
    mark: row.querySelector(".wizard-tick")?.textContent ?? null,
    said: row.querySelector(".setup-summary-said")?.textContent ?? "",
    href: row.querySelector("a")?.getAttribute("href") ?? null,
  }));
};

// Every question in the run, read off the rail rather than typed: a chip with
// a mark is a question, one without is shown rather than asked.
const questionTitles = (env) => env.chips().filter((chip) => chip.tick !== null).map((chip) => chip.title);

test("a finished run's summary names every question once, and only the ones shown as answered", async () => {
  const env = boot();
  await env.runSection();
  const questions = questionTitles(env);

  // Straight to the last question and Finish: everything jumped over was
  // never on screen, and its "Not fitted" is a default nobody chose.
  env.railClick(env.chips().length - 1);
  env.click("wizard-next");
  await env.settle();

  assert.equal(env.config.guidedSetup.run, "completed");
  assert.equal(env.shown(env.id("setup-summary")), true, "the run ends on its summary");
  assert.equal(env.shown(env.id("wizard-head")), false, "and the run's chrome is gone");

  const set = summaryRows(env, "What you set");
  const notAsked = summaryRows(env, "Not asked");
  assert.deepEqual(set.map((row) => row.step), ["wifi", "name"], "only the questions that were on screen");
  assert.ok(set.every((row) => row.mark === "●"));
  assert.ok(notAsked.length > 0 && notAsked.every((row) => row.mark === "○"), "the rest hollow, never ticked");
  assert.ok(notAsked.every((row) => row.said.endsWith(", the default")), "and said to be the default");
  assert.equal(
    set.length + notAsked.length,
    questions.length,
    "every question in the run is in the summary exactly once",
  );
});

test("stopping ends the run for good, on a summary that says what was never asked", async () => {
  const config = freshConfig();
  let env = boot({ config });
  await env.runSection();
  env.click("wizard-stop");
  await env.settle();

  const end = env.posts.find((post) => post.get("guidedSetupRun") !== undefined);
  assert.equal(end.get("guidedSetupRun"), "skipped");
  assert.equal(end.get("guidedSetupSummaryDone"), "false", "a run that ends leaves a summary");
  assert.deepEqual(summaryRows(env, "What you set").map((row) => row.step), ["wifi"]);

  // The next visit - or the same page after the droid restarted - reads the
  // same record: the run stays over and the summary is drawn again from it.
  env = boot({ config });
  await env.runSection();
  await env.settle();
  assert.equal(env.shown(env.id("wizard-head")), false, "the run does not reopen on its own");
  assert.equal(env.shown(env.id("setup-summary")), true);
  assert.deepEqual(summaryRows(env, "What you set").map((row) => row.step), ["wifi"]);
  assert.ok(summaryRows(env, "Not asked").every((row) => row.mark === "○"));
});

test("every wait in the summary carries the way to the restart", async () => {
  // Saved with the feet fitted; the droid reports it started without them.
  const config = freshConfig();
  config.components.drive.enabled = true;
  config.activeToggles = [];
  config.guidedSetup = { run: "completed", recorded: true, visited: ["wifi", "drive"], summaryDone: false };
  const env = boot({ config });
  await env.runSection();
  await env.settle();

  const waiting = summaryRows(env, "Waiting for a restart");
  assert.deepEqual(waiting.map((row) => row.step), ["drive"]);
  assert.ok(waiting.every((row) => row.href === "#maintenance"), "no wait without its route to Maintenance's restart");
});

test("Done is the droid's answer: the summary goes once it is saved, and stays gone", async () => {
  const config = freshConfig();
  config.guidedSetup = { run: "completed", recorded: true, visited: ["wifi"], summaryDone: false };
  let env = boot({ config });
  await env.runSection();
  env.click("setup-summary-done");
  await env.settle();

  assert.equal(env.posts.at(-1).get("guidedSetupSummaryDone"), "true");
  assert.equal(env.shown(env.id("setup-summary")), false);

  env = boot({ config });
  await env.runSection();
  await env.settle();
  assert.equal(env.shown(env.id("setup-summary")), false, "a later visit does not bring it back");
});

// =============================================================================
// The Droid Build step (#368, ADR 0047)
//
// Drawn on Configuration and shown as a step of the run. Three things here are
// ways the decision behind it could be undone with every card still looking
// right: a pick that replaces the Fitted Parts with a design's list, a roadmap
// card that can be chosen, and a connected dome that quietly rewrites what the
// builder said their dome is.
// =============================================================================

const withBuild = (build) => {
  const config = freshConfig();
  config.droidBuild = build;
  return config;
};

// The design cards of one half, as the builder meets them.
const designCards = (env, half) =>
  env.id("droid-build-body").querySelectorAll("[data-half]")
    .find((section) => section.getAttribute("data-half") === half)
    .querySelectorAll("[data-design]");

const cardFor = (env, half, design) =>
  designCards(env, half).find((card) => card.getAttribute("data-design") === design);

const variantFor = (env, half, variant) =>
  env.id("droid-build-body").querySelectorAll("[data-half]")
    .find((section) => section.getAttribute("data-half") === half)
    .querySelectorAll("[data-variant]")
    .find((button) => button.getAttribute("data-variant") === variant);

const droidBuildPosts = (env) => env.posts.filter((post) => post.get("domeDesign") !== undefined ||
  post.get("bodyDesign") !== undefined || post.get("fittedParts") !== undefined);

test("choosing designs on the step keeps every part the builder already fitted", async () => {
  // The gripper arm belongs to no design. A pick that wrote the chosen
  // design's list as the Fitted Parts would drop it, which is the fence
  // ADR 0047 refused.
  const config = withBuild({
    domeDesign: "mk4", domeVariant: "basic",
    bodyDesign: "own", bodyVariant: "",
    fitted: ["gripArm"],
  });
  const env = boot({ config });
  await env.runSection();
  await env.settle();
  env.railClick(env.chips().findIndex((chip) => chip.title === "Droid Build"));

  // An MK4.1 dome ...
  cardFor(env, "dome", "mk41").fire("click", {});
  await env.settle();
  // ... on an MK4 Basic body: the design first, then its variant.
  cardFor(env, "body", "mk4").fire("click", {});
  await env.settle();
  variantFor(env, "body", "basic").fire("click", {});
  await env.settle();

  const writes = droidBuildPosts(env);
  assert.ok(writes.length >= 2, "each pick reached the droid");
  writes.forEach((post) => {
    const fitted = post.get("fittedParts");
    if (fitted !== undefined) assert.ok(fitted.split(",").includes("gripArm"), "a pick dropped a fitted part");
  });
  assert.equal(config.droidBuild.domeDesign, "mk41");
  assert.equal(config.droidBuild.domeVariant, "");
  assert.equal(config.droidBuild.bodyDesign, "mk4");
  assert.equal(config.droidBuild.bodyVariant, "basic");
  assert.ok(config.droidBuild.fitted.includes("gripArm"));
  assert.ok(config.droidBuild.fitted.includes("pie1"), "the MK4.1 dome seeded its parts");

  // The rail reads the same answer the cards do.
  const chip = env.chips().find((c) => c.title === "Droid Build");
  assert.equal(chip.answer, "MK4.1 · MK4");
});

// The variants of one design option, as the builder meets them under its card.
const optionVariants = (env, half, design) =>
  env.id("droid-build-body").querySelectorAll("[data-half]")
    .find((section) => section.getAttribute("data-half") === half)
    .querySelectorAll("[data-option]")
    .find((option) => option.getAttribute("data-option") === design)
    .querySelectorAll("[data-variant]")
    .map((button) => ({ id: button.getAttribute("data-variant"), checked: button.getAttribute("aria-checked") }));

test("a design's variants are on screen under it, from the first paint", async () => {
  // Shipped once without them: the row was drawn only after the droid's
  // answer had arrived, and apart from the design it belonged to (operator
  // finding, 2026-09-18 on #368). A variant is not cosmetic - it decides which
  // complement is fitted - so it is never implied.
  const reading = boot({ config: freshConfig() });
  await reading.settle();
  for (const half of ["dome", "body"]) {
    assert.deepEqual(optionVariants(reading, half, "mk4"),
      [{ id: "basic", checked: "false" }, { id: "complex", checked: "true" }],
      `${half}: the default's variants while the droid's answer is still unread`);
  }

  // A fresh flash records MK4 Complex; a stored build is its own answer.
  const stored = boot({
    config: withBuild({
      domeDesign: "mk41", domeVariant: "",
      bodyDesign: "mk4", bodyVariant: "basic",
      fitted: [],
    }),
  });
  await stored.settle();
  assert.deepEqual(optionVariants(stored, "body", "mk4"),
    [{ id: "basic", checked: "true" }, { id: "complex", checked: "false" }]);
  // MK4.1 declares no variants, so the chosen dome carries no sub-selection.
  ["mk4", "mk41", "own", "mk3"].forEach((design) =>
    assert.deepEqual(optionVariants(stored, "dome", design), [], `${design} on the dome`));
});

test("a roadmap card cannot be chosen, and nothing on the step writes before the droid has answered", async () => {
  const env = boot({
    config: withBuild({
      domeDesign: "mk4", domeVariant: "complex",
      bodyDesign: "mk4", bodyVariant: "complex",
      fitted: ["doorFL"],
    }),
  });
  await env.settle();

  const roadmap = designCards(env, "body").filter((card) =>
    card.querySelectorAll(".status-pill").some((p) => p.textContent === "Roadmap"));
  assert.deepEqual(roadmap.map((card) => card.getAttribute("data-design")), ["mk3"]);
  // Static content rather than a control: no button, nothing listening.
  roadmap.forEach((card) => {
    assert.equal(card.tagName, "DIV");
    assert.equal(card.listeners.length, 0);
    card.fire("click", {});
  });
  await env.settle();
  assert.deepEqual(droidBuildPosts(env), [], "a roadmap card wrote to the droid");

  // An older controller that reports no Droid Build: a pick here would be
  // applied over an empty build and write back a design's list alone.
  const older = boot({ config: freshConfig() });
  await older.settle();
  cardFor(older, "dome", "mk41").fire("click", {});
  await older.settle();
  assert.deepEqual(droidBuildPosts(older), [], "the step wrote before the droid had answered");
});

test("a connected dome that disagrees is shown, and the stated Dome Design stands", async () => {
  // The live MK4 dome, with one pie it does not report.
  const layout = JSON.parse(readFileSync(join(__dirname, "../../tests/fixtures/dome_layout_mk4.json"), "utf8"));
  layout.elements.find((elem) => elem.id === "PP3").in_layout = false;

  const config = withBuild({
    domeDesign: "mk4", domeVariant: "complex",
    bodyDesign: "mk4", bodyVariant: "basic",
    fitted: ["pie1", "pie3"],
  });
  const env = boot({ config, domeLayout: layout });
  await env.settle();
  const configReads = env.gets.filter((path) => path === "/api/config").length;

  // What the status stream does when the dome link comes up.
  await env.window.DomeLayout.load();
  await env.settle();

  const note = env.id("droid-build-body").querySelectorAll(".droid-build-dome-differs");
  assert.equal(note.length, 1, "the difference is shown");
  assert.match(note[0].textContent, /lacks PP3/);

  assert.deepEqual(droidBuildPosts(env), [], "the dome's layout was written over the builder's answer");
  const build = env.window.DroidBuild.current();
  assert.equal(build.dome.design, "mk4");
  assert.equal(build.dome.variant, "complex");
  assert.ok(build.fitted.includes("pie3"), "the dome took a fitted part off");
  // The page already held the droid's answer: resolving the dome spends no
  // second read of it on a controller that sheds connections under load.
  assert.equal(env.gets.filter((path) => path === "/api/config").length, configReads);
});

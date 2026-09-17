// =============================================================================
// test/test_web/test_surface_anatomy_399s2.js
//
// The Surface Anatomy on the Configure group: Parts, Setup and Servos
// (#399 slice 2, ADR 0066).
//
// What is mechanical about the anatomy on these three surfaces, and nothing
// else. Two kinds of claim are covered, because the sweep makes two kinds:
//
//   A SECTION HEAD CARRIES A COUNT. docs/ui-copy-voice.md rule 8 says a heading
//   never appears bare, and every subtitle this slice writes is computed from
//   what the droid answered rather than typed into the markup. A count that is
//   a constant would look right on every screenshot and be wrong on every
//   droid, so each one is driven with two different answers.
//
//   A STATE IS ON THE LIGHT, NOT ON THE TEXT. CONTEXT.md "Health Signal" says a
//   health signal reads as a droid LED and the colour IS the reading; "Status
//   Colour" says a chosen posture takes no colour at all. Setup painted eleven
//   states onto text with element.style.color. The assertions below are on the
//   light's class AND on the absence of an inline colour beside it, because a
//   renderer that lit the lamp and went on colouring the number would pass the
//   first half alone.
//
// Servos and Setup run through helpers/page_module_env.js - the shipped module,
// a permissive DOM stub, and a responder standing in for the droid. Parts runs
// through helpers/parts_surface.js, which boots the shipped Operator Shell with
// the shipped Parts surface in mini_dom, because the claims about it are about
// cells in a real table rather than about a string a renderer built.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";
import { bootParts, withParts, freshOutputs, output } from "./helpers/parts_surface.js";

// -----------------------------------------------------------------------------
// Servos: the two section heads count what the droid answered
// -----------------------------------------------------------------------------

// The shipped module subscribes to the status stream when one is supported, so
// the test holds the handler and delivers frames the way /api/events does.
const loadServo = (config = {}) => {
  let deliver = null;
  const env = loadPageModule("servo.js", {
    respond: () => ({ data: config }),
    overrides: {
      PAStatusStream: {
        isSupported: () => true,
        subscribe: (handler) => { deliver = handler; return () => {}; },
        getLastStatus: () => null,
      },
    },
  });
  env.status = (payload) => deliver("status", payload);
  return env;
};

const AUX_CONFIG = {
  components: {
    aux1: { enabled: true, type: "rgb" },
    aux2: { enabled: true, type: "mg996r" },
    aux3: { enabled: false, type: "none" },
  },
};

test("the arms head counts the arms the droid answered for, and says so when there are none", async () => {
  const env = loadServo();

  env.status({});
  assert.equal(
    env.element("arm-controls-summary").textContent,
    "none switched on",
    "an empty state says what is empty rather than leaving the head bare",
  );

  env.status({ arm1: { detail: "open" } });
  assert.equal(env.element("arm-controls-summary").textContent, "1 of 2 switched on");

  env.status({ arm1: { detail: "open" }, arm2: { detail: "closed" } });
  assert.equal(
    env.element("arm-controls-summary").textContent,
    "2 of 2 switched on",
    "the count follows the answer, so a subtitle cannot disagree with the rows under it",
  );
});

test("the AUX head counts what is switched on and what this page can actually drive, apart", async () => {
  const env = loadServo(AUX_CONFIG);
  await env.runSection("servo-calibration");
  await env.settle();

  // Two AUX lines are on; only one of them carries a servo. An LED strip has no
  // position to drive, so the two counts are deliberately different - a single
  // count would promise controls the page does not offer.
  assert.equal(
    env.element("aux-controls-summary").textContent,
    "2 of 3 switched on · 1 drives a servo",
  );

  const html = env.element("aux-controls-container").innerHTML;
  assert.match(html, /LED strip &middot; configure in Setup/, "the strip row names where it is set");
  assert.equal(
    (html.match(/data-arm="aux2" data-action="/g) || []).length,
    3,
    "the servo row carries open, close and stop",
  );
  assert.doesNotMatch(html, /data-arm="aux1" data-action/, "the strip row carries no act at all");
  assert.match(html, /<span class="arm-acts">/, "and the acts sit together at the end of their row");
});

test("an AUX line with nothing recorded on it says so instead of drawing an empty row", async () => {
  const env = loadServo({
    components: { aux1: { enabled: true, type: "none" }, aux2: { enabled: false }, aux3: { enabled: false } },
  });
  await env.runSection("servo-calibration");
  await env.settle();

  assert.equal(env.element("aux-controls-summary").textContent, "1 of 3 switched on · 0 drive a servo");
  assert.match(env.element("aux-controls-container").innerHTML, /Nothing here to drive/);
});

// -----------------------------------------------------------------------------
// Parts: the head carries the counts, the acts have their own column
// -----------------------------------------------------------------------------

test("the output-first table's section head IS the three tier counts", async () => {
  const env = await bootParts({ outputs: withParts({ "ledc:0": ["utilUp"] }) });

  const head = env.document.querySelector(".outputs-section").querySelector(".sect");
  assert.ok(head, "the projection has a section head rather than a bare heading");
  // mini_dom's selector grammar has no tag with a digit in it, so the heading
  // is read off the head's own first child rather than by querySelector("h2").
  assert.equal(head.children[0].tagName, "H2");
  assert.equal(head.children[0].textContent, "Outputs");

  const sub = head.querySelector(".sub");
  assert.ok(sub, "and the head carries a subtitle");
  const tiers = sub.querySelectorAll("[data-tier]").map((node) => node.textContent);
  assert.deepEqual(tiers, [
    "Driving parts — 1 output",
    "Wired but switched off — 0 outputs",
    "Output with no part — 4 outputs",
  ], "the counts are the subtitle, so nothing above them restates what they add up to");
});

test("every act on a part's row is in the row's own acts column", async () => {
  const env = await bootParts();

  const row = env.partRow("doorFL");
  const cells = row.querySelectorAll("td");
  assert.equal(cells.length, 2, "the picker and the act are two cells, not two controls in one");

  const acts = row.querySelector(".parts-acts");
  assert.ok(acts, "the row has an acts cell");
  assert.ok(acts.querySelector(".parts-find"), "and Find by moving is in it");
  assert.equal(
    row.querySelector("select").closest("td").querySelector(".parts-find"),
    null,
    "the picker's cell carries no act of its own",
  );

  const head = env.partsRegion().querySelector("thead").querySelectorAll("th").map((th) => th.textContent);
  assert.deepEqual(head, ["Part", "Driven by", "On this part"], "and the column is named");
});

test("a row act is the anatomy's small control, so the row stays one line tall", async () => {
  const env = await bootParts();

  // A .btn inside a --row-tall line is .btn-sm: the shared layer's own small
  // act. Before this the three of them each carried a private copy of the 36 px
  // floor, which is what made a part's row 93 px in a browser.
  assert.match(env.findButton("doorFL").className, /\bbtn-sm\b/);
  assert.match(env.row("ledc:0").querySelector(".outputs-calibrate").className, /\bbtn-sm\b/);
  assert.match(env.row("ledc:0").querySelector(".outputs-off").className, /\bbtn-sm\b/);
});

// -----------------------------------------------------------------------------
// Setup: the light carries the state, the value stays ink
// -----------------------------------------------------------------------------

// Every component carries a Component Registry label, the way /api/config
// answers on a real droid. The permissive DOM stub answers for an id the page
// does not have, so the "this component has no label" branch - which in a
// browser only ever runs against a span that is really there - walks a
// parentNode the stub cannot give it. That is the harness's limit, not the
// page's, and a full answer is the honest fixture either way.
const COMPONENTS = {
  drive: "Hoverboard", audio: "DY-SV5W", protoR2link: "AstroPixels Plus", domeEsc: "ISDT ESC70",
  arm1: "MG996R", arm2: "MG90S", aux1: "LED strip", aux2: "MG996R", aux3: "none",
  rcCh1: "SBUS", rcCh2: "SBUS", rcCh3: "SBUS", rcCh4: "SBUS", rcCh5: "SBUS", rcCh6: "SBUS",
};

const components = (on = {}) =>
  Object.fromEntries(
    Object.entries(COMPONENTS).map(([key, label]) => [key, { label, enabled: false, ...(on[key] || {}) }]),
  );

const loadSetup = (config = {}, respond = null) => {
  let deliver = null;
  const env = loadPageModule("setup.js", {
    respond: respond || (() => ({ data: config })),
    overrides: {
      PAStatusStream: {
        isSupported: () => true,
        subscribe: (handler) => { deliver = handler; return () => {}; },
        getLastStatus: () => null,
      },
    },
  });
  env.status = (payload) => deliver("status", payload);
  return env;
};

const HEAP_GOOD = { heapFree: 177152, heapMin: 150000, heapLargestBlock: 61440 };

test("a serial lane reports on its light, and the reading beside it takes no colour", () => {
  const env = loadSetup();
  // A decoy: the permissive DOM stub answers for any id, so a renderer that
  // stopped writing the light altogether would leave a class that looks
  // plausible. Writing something that could never be produced is what tells
  // "the renderer wrote ok" from "nobody wrote anything".
  env.element("serial-s3-light").className = "DECOY";
  env.element("serial-s3-state").style.color = "DECOY";

  env.status({ ...HEAP_GOOD, drive: { state: "idle" }, audio: { state: "playing" },
    dome_link: { state: "connected", transport: "uart", hb_rx: 8, hb_tx: 8 } });

  assert.equal(env.element("serial-s3-light").className, "indicator ok");
  assert.match(env.element("serial-s3-state").textContent, /^Connected \(UART/);
  assert.equal(
    env.element("serial-s3-state").style.color,
    "DECOY",
    "the colour is the light's job; a renderer that also painted the text would have overwritten this",
  );
});

test("a lane nobody asked about reads grey, never green", () => {
  const env = loadSetup();
  env.status({ ...HEAP_GOOD });

  assert.equal(env.element("serial-s1-light").className, "indicator off", "no drive in the frame at all");
  assert.equal(env.element("serial-s1-state").textContent, "Disabled");
  assert.equal(env.element("serial-s2-light").className, "indicator off");
  assert.equal(env.element("serial-s3-light").className, "indicator off");
});

test("the dome link's four states are four different lights", () => {
  const frames = [
    [{ state: "connected", transport: "uart", hb_rx: 1, hb_tx: 1 }, "indicator ok"],
    [{ state: "waiting", transport: "uart" }, "indicator warn"],
    [{ state: "lost", transport: "uart", last_rx_ms: 900 }, "indicator fail"],
    [{ state: "disabled" }, "indicator off"],
  ];
  for (const [dome_link, expected] of frames) {
    const env = loadSetup();
    env.status({ ...HEAP_GOOD, dome_link });
    assert.equal(env.element("serial-s3-light").className, expected, JSON.stringify(dome_link));
  }
});

test("each memory readout lights on its own threshold, and prints its number as ink", () => {
  const env = loadSetup();
  env.element("diag-heap-free").style.color = "DECOY";

  // Free heap comfortable, the low-water mark under the critical floor, and no
  // largest block reported at all: three different answers on one frame, so a
  // renderer that read one of them for all three cannot pass.
  env.status({ heapFree: 177152, heapMin: 20000 });

  assert.equal(env.element("diag-heap-free-light").className, "indicator ok");
  assert.match(env.element("diag-heap-free").textContent, /^173 KB Good$/);
  assert.equal(env.element("diag-heap-free").style.color, "DECOY", "the number is ink");

  assert.equal(env.element("diag-heap-min-light").className, "indicator fail");
  assert.match(env.element("diag-heap-min").textContent, /Critical$/);

  assert.equal(
    env.element("diag-heap-largest-light").className,
    "indicator off",
    "a firmware that reports no largest block has not been asked, so it is grey",
  );
  assert.equal(env.element("diag-heap-largest").textContent, "Not reported by this firmware");
});

test("uptime is telemetry and carries no light and no colour", () => {
  const env = loadSetup();
  env.element("diag-uptime").style.color = "DECOY";
  env.status({ ...HEAP_GOOD, uptimeMs: 2538000 });

  assert.equal(env.element("diag-uptime").textContent, "0h 42m 18s");
  assert.equal(env.element("diag-uptime").style.color, "DECOY", "a number that was never a state takes no colour");
});

test("the components head counts what this image can actually offer", async () => {
  const env = loadSetup({
    components: components({
      drive: { enabled: true },
      audio: { enabled: true },
      arm1: { enabled: true, type: "mg996r" },
      rcCh1: { enabled: true },
    }),
  });
  // The decoy is the class: the renderer used to swap this element between
  // status-pill pill-ok and status-pill pill-info by the count, which coloured
  // an answer the builder gave.
  env.element("setup-enabled-summary").className = "sub";
  await env.settle();

  assert.equal(env.element("setup-enabled-summary").textContent, "4 of 15 switched on");
  assert.equal(
    env.element("setup-enabled-summary").className,
    "sub",
    "a count of what the builder ticked is a chosen posture: it takes no pill and no colour",
  );
});

test("where the LED strip is routed is said without a colour", async () => {
  const env = loadSetup({
    components: components({ aux1: { enabled: true, type: "rgb" } }),
    aux_led_pin: 1,
  });
  // The pill's class is the decoy: the renderer used to swap it to pill-ok when
  // a strip was routed, which coloured an answer the builder gave.
  env.element("aux-led-route-badge").className = "status-pill status-pill-compact";
  await env.settle();

  assert.equal(env.element("aux-led-route-status").textContent, "Routed via AUX1 LED");
  assert.equal(env.element("aux-led-route-badge").textContent, "AUX1");
  assert.equal(
    env.element("aux-led-route-badge").className,
    "status-pill status-pill-compact",
    "no pill-ok: routing a strip is a setting, not a symptom",
  );
});

test("the profiler's task rows carry a light that follows the headroom", async () => {
  const PROFILE = {
    fragRatio: 0.62, heapFree: 1024, heapMin: 1024, heapLargest: 1024,
    allocBlocks: 1, freeBlocks: 1, failedAllocs: 0, taskHeap: [], snapshots: [],
    taskStacks: [
      { name: "DriveTask", hwmBytes: 3120, status: "ok" },
      { name: "SBUSInputTask", hwmBytes: 1880, status: "watch" },
      { name: "SequenceDispatcherTask", hwmBytes: 760, status: "critical" },
    ],
  };
  const env = loadSetup({}, (path) => ({ data: path === "/api/profiler" ? PROFILE : {} }));

  // The panel declares its requirement in the markup; the stub carries no
  // attributes, so the test states the one this card actually ships with.
  env.element("profiler-card").dataset.buildFlag = "PA_HEAP_PROFILE";
  env.element("prof-hwm-tbody").innerHTML = "DECOY";
  env.element("prof-frag-bar").className = "DECOY";

  // The manifest says this image has the profiler, which is what starts its
  // poll: the module owns that lifecycle and a test that called the renderer
  // directly would prove nothing about it.
  env.window.PAFeatureAvailability.setIdentity({ build_flags: { PA_HEAP_PROFILE: true }, board_capabilities: {} });
  await env.settle(6);

  const html = env.element("prof-hwm-tbody").innerHTML;
  assert.match(html, /<span class="indicator ok"><\/span>OK/);
  assert.match(html, /<span class="indicator warn"><\/span>WATCH/);
  assert.match(html, /<span class="indicator fail"><\/span>CRITICAL/);
  assert.doesNotMatch(html, /#4caf50|#ff9800|#f44336|style="/, "no colour literal and no inline style reaches a cell");

  assert.equal(
    env.element("prof-frag-bar").className,
    "prof-bar is-fail",
    "and the fragmentation bar takes a state class rather than a hard-coded hex",
  );
});

test("the save state says what happened without a pictograph", async () => {
  const env = loadSetup();
  await env.settle();
  assert.equal(env.element("setup-save-summary").textContent, "Auto-save ready");
  assert.doesNotMatch(
    env.element("setup-save-summary").textContent,
    /\p{Extended_Pictographic}/u,
    "an operator surface carries no emoji (ADR 0066)",
  );
});

// -----------------------------------------------------------------------------
// The output-first table still answers what it answered before the sweep
// -----------------------------------------------------------------------------

test("a light row keeps its treatment through the redrawn table", async () => {
  const env = await bootParts({
    outputs: withParts({ "ledc:0": ["magicPanel"] }, [
      output("ledc:0", "ARM1", { commandedUs: 1500, targetUs: 1500 }),
      ...freshOutputs().slice(1),
    ]),
  });

  const row = env.row("ledc:0");
  assert.equal(row.classList.contains("partkind-light"), true, "a light row is told apart by treatment");
  assert.equal(env.text("ledc:0", "outputs-us"), "A light has no position");
});

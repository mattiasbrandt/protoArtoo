// =============================================================================
// test/test_web/test_sound_catalog_truth_397.js
//
// What the Sound page says about the CHIRP catalog (#397 work items 4, 10, 11,
// 12), against the shipped data/sound.js.
//
// Three of these were reproduced on the shipped module before they were fixed:
//
//   - Bank tabs were labelled B2A and B2B and filtered on the bank NUMBER, so
//     clicking B2B listed page A's sounds as well.
//   - "Catalog refreshed" was reported whenever any catalog was ready, which an
//     older one already was -- so a refresh the controller never ran looked
//     like one that had succeeded.
//   - A listing missing whole banks, or cut off at the controller's entry
//     limit, looked exactly like a complete one, and category suggestions were
//     built over it.
//
// The fourth is new behaviour: a card whose sound list has changed under saved
// assignments now says so beside those assignments.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// The CHIRP row's capability word (include/component_registry.inc part id 20).
const CAPS_CHIRP = 0x3f;

const SOUND_LIST_CHANGED_WARNING =
  "The sound list has changed since these assignments were saved. Check the assigned sounds before using them.";
const SOUND_LIST_UNCHECKED_NOTE =
  "The sound list could not be checked against these assignments.";

const audioStatus = () => ({
  driver: "CHIRP Audio Trigger",
  link_ok: true,
  device: "Flash+SD",
  play_state: "stop",
  total_tracks: 24,
  current_track: 0,
  missing_track: 0,
  rx_status: "available",
  capabilities: CAPS_CHIRP,
});

const catalogBody = (overrides = {}) => ({
  ready: true,
  busy: false,
  complete: true,
  limits: { manifest_incomplete: false, missing_names: 0, entry_cap_reached: false },
  refresh: { request: 0, active: 0, settled: 0, state: "none" },
  bindings: { sound_list_changed: false, sound_list_checked: true },
  banks: [],
  entries: [],
  ...overrides,
});

// Mounts the shipped module and records every element it creates, which is how
// rendered rows and bank tabs become observable through the permissive DOM.
const mount = ({ catalog = catalogBody(), tracks = {}, refreshReply = { ok: true, request: 1 } } = {}) => {
  const state = { catalog, created: [], fired: new Set() };
  const env = loadPageModule("sound.js", {
    respond: (path) => {
      // The page reads sound's enabled state off /api/status, and every control
      // on the page is disabled when it is missing.
      if (path === "/api/status") return { data: { audio: { link_ok: true, play_state: "stop" } } };
      if (path === "/api/audio") return { data: audioStatus() };
      if (path === "/api/audio/tracks") return { data: tracks };
      if (path.startsWith("/api/audio/catalog/refresh")) return { data: refreshReply };
      if (path.startsWith("/api/audio/catalog")) return { data: state.catalog };
      return { data: { ok: true } };
    },
  });
  const createElement = env.document.createElement;
  env.document.createElement = (...args) => {
    const el = createElement(...args);
    state.created.push({ tag: args[0], el });
    return el;
  };
  state.env = env;
  return state;
};

const load = async (state) => {
  await state.env.runSection("audio-status", {});
  await state.env.settle();
  await state.env.runSection("audio-catalog", {});
  await state.env.settle();
};

const click = (element) => {
  const listener = element.__listeners.find((l) => l.type === "click");
  assert.ok(listener, "expected a click handler on this control");
  return listener.handler();
};

const findByText = (state, tag, text) =>
  state.created.find((c) => c.tag === tag && c.el.textContent === text)?.el;

const cellTexts = (state) =>
  state.created.filter((c) => c.tag === "td").map((c) => c.el.textContent);

// The refresh loop waits a second between polls. Firing exactly that timer is
// what advances it, one controller answer at a time.
const firePollTick = async (state) => {
  const next = state.env.timeouts.find((t) => t.ms === 1000 && !state.fired.has(t.id));
  assert.ok(next, "expected the page to be waiting on a refresh poll tick");
  state.fired.add(next.id);
  next.fn();
  await state.env.settle();
};

// -----------------------------------------------------------------------------
// Item 11 - a bank/page tab selects that page
// -----------------------------------------------------------------------------

const twoPageCatalog = () =>
  catalogBody({
    banks: [
      { bank: 2, page: "A", dir: "2A_music", count: 1 },
      { bank: 2, page: "B", dir: "2B_alert", count: 1 },
    ],
    entries: [
      { bank: 2, page: "A", index: 1, name: "cantina.mp3" },
      { bank: 2, page: "B", index: 1, name: "alarm.mp3" },
    ],
  });

test("the B2B tab lists only the B2B entries", async () => {
  const state = mount({ catalog: twoPageCatalog() });
  await load(state);

  const tab = findByText(state, "button", "B2B");
  assert.ok(tab, "the catalog offers one tab per bank AND page");

  state.created.length = 0;
  click(tab);

  const texts = cellTexts(state);
  assert.ok(
    texts.includes("alarm.mp3"),
    "B2B's own sound has to be listed under B2B"
  );
  assert.ok(
    !texts.includes("cantina.mp3"),
    "2A's sound is a different page of the same bank; filtering on the bank number alone listed it here too"
  );
});

test("All banks still shows every page", async () => {
  const state = mount({ catalog: twoPageCatalog() });
  await load(state);

  const tab = findByText(state, "button", "All banks");
  assert.ok(tab, "the All banks tab has to survive the page-aware filter");

  state.created.length = 0;
  click(tab);

  const texts = cellTexts(state);
  assert.ok(texts.includes("alarm.mp3") && texts.includes("cantina.mp3"));
});

test("only the selected page's tab is marked selected", async () => {
  const state = mount({ catalog: twoPageCatalog() });
  await load(state);

  const tab = findByText(state, "button", "B2B");
  state.created.length = 0;
  click(tab);

  const selectedLabels = state.created
    .filter((c) => c.tag === "button" && String(c.el.className).includes("accent"))
    .map((c) => c.el.textContent);
  assert.deepStrictEqual(
    selectedLabels,
    ["B2B"],
    "a filter keyed on the bank number marks both of that bank's tabs selected"
  );
});

// -----------------------------------------------------------------------------
// Item 10 - refresh completion belongs to the requested refresh
// -----------------------------------------------------------------------------

test("an older completed refresh does not satisfy the one just asked for", async () => {
  // The controller accepted request 5. Request 4 -- an earlier refresh, whose
  // catalog is still ready and on screen -- is the one that has settled.
  const catalog = catalogBody({
    refresh: { request: 5, active: 5, settled: 4, state: "completed" },
    entries: [{ bank: 1, page: "A", index: 1, name: "beep.wav" }],
    banks: [{ bank: 1, page: "A", dir: "1A_general", count: 1 }],
  });
  const state = mount({ catalog, refreshReply: { ok: true, request: 5 } });
  await load(state);

  const running = click(state.env.element("btn-catalog-refresh"));
  await state.env.settle();

  for (let i = 0; i < 3; i += 1) {
    await firePollTick(state);
    assert.notStrictEqual(
      state.env.element("catalog-feedback").textContent,
      "Catalog refreshed",
      "a ready catalog from an earlier refresh is not this refresh finishing"
    );
  }
  assert.strictEqual(
    state.env.element("btn-catalog-refresh").disabled,
    true,
    "the page must stay locked while its own refresh is still running, or a second one is enqueued on top of it"
  );

  // Now the controller settles request 5.
  catalog.refresh = { request: 5, active: 0, settled: 5, state: "completed" };
  await firePollTick(state);
  await running;

  assert.strictEqual(state.env.element("catalog-feedback").textContent, "Catalog refreshed");
});

test("a refresh that never ran is reported as not having run", async () => {
  const catalog = catalogBody({
    refresh: { request: 1, active: 1, settled: 0, state: "none" },
    entries: [{ bank: 1, page: "A", index: 1, name: "beep.wav" }],
    banks: [{ bank: 1, page: "A", dir: "1A_general", count: 1 }],
  });
  const state = mount({ catalog, refreshReply: { ok: true, request: 1 } });
  await load(state);

  const running = click(state.env.element("btn-catalog-refresh"));
  await state.env.settle();

  catalog.refresh = { request: 1, active: 0, settled: 1, state: "blocked" };
  await firePollTick(state);
  await running;

  const feedback = state.env.element("catalog-feedback").textContent;
  assert.notStrictEqual(feedback, "Catalog refreshed");
  assert.match(feedback, /did not run/);
  assert.match(
    state.env.element("catalog-limits").textContent,
    /earlier refresh/,
    "the listing may stay -- it is often the only one there is -- but it is the earlier one"
  );
});

// -----------------------------------------------------------------------------
// Item 12 - an incomplete listing says so, and is not a source range
// -----------------------------------------------------------------------------

// A bank whose directory name maps onto a category, so the suggestion machinery
// has something to offer and the gate below is the only thing withholding it.
const suggestibleCatalog = (limits) =>
  catalogBody({
    complete: !limits.manifest_incomplete && !limits.entry_cap_reached && limits.missing_names === 0,
    limits,
    banks: [{ bank: 2, page: "A", dir: "2A_alert", count: 2 }],
    entries: [
      { bank: 2, page: "A", index: 1, name: "alarm.mp3" },
      { bank: 2, page: "A", index: 2, name: "siren.mp3" },
    ],
  });

test("a whole listing offers its suggestions", async () => {
  const state = mount({
    catalog: suggestibleCatalog({
      manifest_incomplete: false,
      missing_names: 0,
      entry_cap_reached: false,
    }),
  });
  await load(state);

  assert.strictEqual(
    state.env.element("btn-catalog-apply-suggestions").disabled,
    false,
    "this fixture has a suggestion to make, which is what makes the partial case below meaningful"
  );
  assert.strictEqual(state.env.element("catalog-limits").textContent, "");
});

test("a listing missing whole banks says so and withholds its suggestions", async () => {
  const state = mount({
    catalog: suggestibleCatalog({
      manifest_incomplete: true,
      missing_names: 0,
      entry_cap_reached: false,
    }),
  });
  await load(state);

  assert.match(
    state.env.element("catalog-limits").textContent,
    /not the whole card/,
    "a usable catalog and a whole one looked the same on screen"
  );
  assert.strictEqual(
    state.env.element("btn-catalog-apply-suggestions").disabled,
    true,
    "a suggested range spans lo..hi, so building one over a listing with holes claims sounds nobody listed"
  );
});

test("a listing cut off at the entry limit says so", async () => {
  const state = mount({
    catalog: suggestibleCatalog({
      manifest_incomplete: false,
      missing_names: 0,
      entry_cap_reached: true,
    }),
  });
  await load(state);

  assert.match(state.env.element("catalog-limits").textContent, /entry limit/);
  assert.strictEqual(state.env.element("btn-catalog-apply-suggestions").disabled, true);
});

test("sounds listed by index are counted on screen, not only in a log", async () => {
  const state = mount({
    catalog: suggestibleCatalog({
      manifest_incomplete: false,
      missing_names: 2,
      entry_cap_reached: false,
    }),
  });
  await load(state);

  assert.match(state.env.element("catalog-limits").textContent, /2 sounds came back without a name/);
  assert.strictEqual(
    state.env.element("btn-catalog-apply-suggestions").disabled,
    false,
    "an unnamed entry still carries its index, so it is not a hole in a range"
  );
});

// A read the controller refuses while a refresh holds the catalog has learned
// nothing about the catalog, so it must not be spent blanking a listing that is
// still perfectly good (item 9's page-side half).
test("a catalog read the controller refused leaves the listing alone", async () => {
  const state = mount({ catalog: twoPageCatalog() });
  await load(state);
  const allBanks = findByText(state, "button", "All banks");

  state.catalog = catalogBody({ ready: false, busy: true, banks: [], entries: [] });
  await state.env.runSection("audio-catalog", {});
  await state.env.settle();

  state.created.length = 0;
  click(allBanks);

  const texts = cellTexts(state);
  assert.ok(
    texts.includes("cantina.mp3") && texts.includes("alarm.mp3"),
    "the sounds were still there; nothing was read, so nothing may be thrown away"
  );
});

// -----------------------------------------------------------------------------
// Item 4 - a changed sound list is visible beside the assignments
// -----------------------------------------------------------------------------

const withBindings = { chirp_bindings: { scream: { bank: 2, page: "A", index: 3 } } };

test("a changed sound list warns beside the saved assignments", async () => {
  const state = mount({
    tracks: withBindings,
    catalog: catalogBody({ bindings: { sound_list_changed: true, sound_list_checked: true } }),
  });
  await load(state);

  assert.strictEqual(
    state.env.element("sound-list-changed-warning").textContent,
    SOUND_LIST_CHANGED_WARNING
  );
});

test("an unchanged sound list says nothing", async () => {
  const state = mount({
    tracks: withBindings,
    catalog: catalogBody({ bindings: { sound_list_changed: false, sound_list_checked: true } }),
  });
  await load(state);

  assert.strictEqual(state.env.element("sound-list-changed-warning").textContent, "");
});

test("a check that could not be made is its own sentence", async () => {
  const state = mount({
    tracks: withBindings,
    catalog: catalogBody({ bindings: { sound_list_changed: false, sound_list_checked: false } }),
  });
  await load(state);

  assert.strictEqual(
    state.env.element("sound-list-changed-warning").textContent,
    SOUND_LIST_UNCHECKED_NOTE
  );
});

test("nothing saved yet is nothing to warn about", async () => {
  const state = mount({
    tracks: {},
    catalog: catalogBody({ bindings: { sound_list_changed: true, sound_list_checked: false } }),
  });
  await load(state);

  assert.strictEqual(
    state.env.element("sound-list-changed-warning").textContent,
    "",
    "the builder has not assigned anything against this card yet"
  );
});

test("saving an assignment is still available while the warning is up", async () => {
  const state = mount({
    tracks: withBindings,
    catalog: catalogBody({
      bindings: { sound_list_changed: true, sound_list_checked: true },
      banks: [{ bank: 1, page: "A", dir: "1A_general", count: 1 }],
      entries: [{ bank: 1, page: "A", index: 1, name: "beep.wav" }],
    }),
  });
  await load(state);

  assert.strictEqual(
    state.env.element("sound-list-changed-warning").textContent,
    SOUND_LIST_CHANGED_WARNING
  );

  const select = state.created.find((c) => c.tag === "select" && c.el.className.includes("catalog-map-select"));
  assert.ok(select, "each catalog row offers a mapping target");
  select.el.value = "slot:scream";

  const mapButton = findByText(state, "button", "Map");
  assert.ok(mapButton, "each catalog row offers a Map action");
  await click(mapButton);
  await state.env.settle();

  const posted = state.env.requests.filter(
    (r) => r.method === "POST" && r.path === "/api/audio/tracks"
  );
  assert.strictEqual(
    posted.length,
    1,
    "the warning is a warning; the builder is the one who decides what the new numbers point at"
  );
});

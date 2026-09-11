// =============================================================================
// test/test_web/test_sound_capability_consumers_340.js
//
// Two defects of the same shape (#340): the browser deciding what a sound
// module can answer without asking the module, and naming a module the droid
// does not have.
//
// AUDIO_CAP_TRACK_COUNT (0x04) was declared by all three drivers and consulted
// by nothing - data/sound.js did not even mirror the constant, and rendered
// `total_tracks` unconditionally. ADR 0042 records why that is the defect and
// not merely an omission: a declared capability nobody consults makes the page
// report a field the fitted module cannot actually produce.
//
// The second is the sound module's own name. data/app.js said "CHIRP RX
// unavailable while protoR2link owns UART2" on any droid whose sound RX was
// blocked, which has been wrong on a DY-SV5W or MP3 Trigger build all along and
// is wrong on a different boot of the same image now that the module is a
// runtime choice.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule } from "./helpers/page_module_env.js";

// AUDIO_CAP_* bits, from include/audio_driver.h.
const CAP_STATUS_QUERY = 0x01;
const CAP_DEVICE_TYPE = 0x02;
const CAP_TRACK_COUNT = 0x04;
const CAP_CURRENT_TRACK = 0x08;

const audioStatus = (capabilities, extra = {}) => ({
  driver: "TestModule",
  link_ok: true,
  device: "SD",
  play_state: "stop",
  total_tracks: 42,
  current_track: 0,
  capabilities,
  ...extra,
});

// Replaces an element's classList with one that records what was toggled, so a
// test can see a visibility decision the permissive DOM stub otherwise
// swallows. Returns the record.
const recordVisibility = (element) => {
  const toggles = [];
  element.classList = {
    add() {},
    remove() {},
    contains: () => false,
    toggle: (cls, on) => toggles.push({ cls, on }),
  };
  return toggles;
};

const lastHiddenToggle = (toggles) => {
  for (let i = toggles.length - 1; i >= 0; i -= 1) {
    if (toggles[i].cls === "hidden") return toggles[i].on;
  }
  return undefined;
};

const runAudioStatus = async (capabilities) => {
  const env = loadPageModule("sound.js", {
    respond: () => ({ data: audioStatus(capabilities) }),
  });
  const totalTracks = recordVisibility(env.element("mod-total-tracks-row"));
  const currentTrack = recordVisibility(env.element("mod-current-track-row"));
  await env.runSection("audio-status", {});
  await env.settle();
  return { totalTracks, currentTrack };
};

test("a module that cannot count tracks does not get a Total tracks row", async () => {
  // Status query and current track, but no AUDIO_CAP_TRACK_COUNT.
  const { totalTracks, currentTrack } = await runAudioStatus(
    CAP_STATUS_QUERY | CAP_DEVICE_TYPE | CAP_CURRENT_TRACK
  );

  assert.strictEqual(
    lastHiddenToggle(totalTracks),
    true,
    "Total tracks must be hidden when the module does not declare AUDIO_CAP_TRACK_COUNT"
  );
  // The neighbouring row proves the page was actually rendering capability UI
  // on this run, rather than hiding everything or nothing.
  assert.strictEqual(
    lastHiddenToggle(currentTrack),
    false,
    "Current track must stay visible when the module declares AUDIO_CAP_CURRENT_TRACK"
  );
});

test("a module that can count tracks gets the row", async () => {
  const { totalTracks } = await runAudioStatus(
    CAP_STATUS_QUERY | CAP_DEVICE_TYPE | CAP_TRACK_COUNT | CAP_CURRENT_TRACK
  );

  assert.strictEqual(
    lastHiddenToggle(totalTracks),
    false,
    "Total tracks must be visible when the module declares AUDIO_CAP_TRACK_COUNT"
  );
});

test("a module with no status query at all loses the Total tracks row too", async () => {
  // TRACK_COUNT set but STATUS_QUERY clear is not a real module's word; it is
  // the pair that proves the row is gated on both, the way its neighbours are.
  const { totalTracks } = await runAudioStatus(CAP_TRACK_COUNT);

  assert.strictEqual(
    lastHiddenToggle(totalTracks),
    true,
    "Total tracks must be hidden when the module answers no status queries"
  );
});

// -----------------------------------------------------------------------------
// The dashboard's blocked-RX line names the fitted module
// -----------------------------------------------------------------------------

const statusPayload = (audio) => ({
  audio,
  dome_link: { state: "disconnected" },
  estop: false,
  sleepMode: false,
});

const renderDashboard = async (audio) => {
  const env = loadPageModule("app.js", {
    respond: () => ({ data: statusPayload(audio) }),
  });
  const grid = env.element("component-status-grid");
  await env.runSection("app-initial-status", {});
  await env.settle();
  return String(grid.innerHTML);
};

test("the blocked-RX line names the sound module the droid actually has", async () => {
  const html = await renderDashboard({
    state: "idle",
    detail: "Ready",
    driver: "DY-SV5W",
    rx_status: "blocked_by_dome_uart",
  });

  assert.ok(
    html.includes("DY-SV5W RX unavailable while protoR2link owns UART2"),
    `expected the fitted module's name in the transport line, got: ${html}`
  );
  assert.ok(
    !html.includes("CHIRP"),
    "the dashboard must not name a module this droid does not have"
  );
});

test("a controller that reports no driver name still gets a readable line", async () => {
  const html = await renderDashboard({
    state: "idle",
    detail: "Ready",
    rx_status: "blocked_by_dome_uart",
  });

  assert.ok(
    html.includes("Sound module RX unavailable while protoR2link owns UART2"),
    `expected the generic fallback wording, got: ${html}`
  );
});

test("an unblocked sound module gets no transport line at all", async () => {
  const html = await renderDashboard({
    state: "idle",
    detail: "Ready",
    driver: "CHIRP",
    rx_status: "available",
  });

  assert.ok(
    !html.includes("RX unavailable"),
    "the transport line belongs to a blocked RX only"
  );
});

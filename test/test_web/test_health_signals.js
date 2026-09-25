// =============================================================================
// test/test_web/test_health_signals.js
//
// The state every health evaluator returns, pinned one row at a time.
//
// #402: a signal that means "we have not heard" reads grey. Amber is reserved
// for a droid that is genuinely degraded and that the builder can do something
// about (CONTEXT.md "Status Color"), so stale, unknown, never-asked and
// not-fitted all resolve to the unlit state. Staleness is not a state at all:
// a stale row keeps what the controller last reported, and the Status Plate
// carries the one freshness statement for the surface (CONTEXT.md "Health
// Signal", _Avoid_).
// =============================================================================
const test = require("node:test");
const assert = require("node:assert/strict");

const vm = require("node:vm");
const { readFileSync } = require("node:fs");
const path = require("node:path");

const { deriveHealthSignals: deriveWith } = require("../../data/health_signals.js");

// The word for a field the frame does not carry is the Live Reading's, read
// from the shipped module the way the Dashboard hands it over (data/app.js).
const UNKNOWN = (() => {
  const context = { window: {}, console };
  vm.runInNewContext(readFileSync(path.join(__dirname, "../../data/live_reading.js"), "utf8"), context);
  return context.window.PALiveReading.UNKNOWN;
})();

const deriveHealthSignals = (payload, options = {}) => deriveWith(payload, { unknown: UNKNOWN, ...options });

const toSignalMap = (payload, options) => {
  const entries = deriveHealthSignals(payload, options).map((item) => [item.id, item]);
  return Object.fromEntries(entries);
};

// A droid answering on every subsystem, so a test can take one thing away and
// keep everything else nominal.
const HEALTHY_PAYLOAD = Object.freeze({
  rcCh1: { state: "active" },
  sbusSignalLost: false,
  sbusHwFailsafe: false,
  wifiConnected: true,
  wifiRssi: -52,
  littleFsReady: true,
  heapLargest8bit: 90000,
  heapFree: 150000,
  dome_link: { state: "connected", transport: "uart" },
  audio: { state: "idle", driver: "CHIRP Audio Trigger", output: "on", link_ok: true, rx_status: "available" },
  domeEnabled: true,
  domeEsc: { state: "idle" },
});

// -----------------------------------------------------------------------------
// RC receiver
// -----------------------------------------------------------------------------

test("RC receiver reads OFF with no channels, OK on frames, FAIL on signal loss", () => {
  const absent = toSignalMap({});
  assert.equal(absent["h-sbus"].state, "off");
  assert.equal(absent["h-sbus"].reason, "No RC input");

  const framing = toSignalMap({ rcCh1: 1500, sbusSignalLost: false, sbusHwFailsafe: false });
  assert.equal(framing["h-sbus"].state, "ok");
  assert.equal(framing["h-sbus"].reason, "Frames ok");

  const lost = toSignalMap({ rcCh1: 1500, sbusSignalLost: true });
  assert.equal(lost["h-sbus"].state, "fail");
  assert.equal(lost["h-sbus"].reason, "Signal lost");

  const failsafe = toSignalMap({ rcCh1: 1500, sbusHwFailsafe: true });
  assert.equal(failsafe["h-sbus"].state, "fail");
  assert.equal(failsafe["h-sbus"].reason, "HW failsafe");
});

// -----------------------------------------------------------------------------
// WiFi - the acceptance criterion that an AP-only droid is a normal droid
// -----------------------------------------------------------------------------

test("WiFi reads OK when the radio is serving and never amber when it is not", () => {
  const serving = toSignalMap({ wifiConnected: true, wifiRssi: -47 });
  assert.equal(serving["h-wifi"].state, "ok");
  assert.equal(serving["h-wifi"].reason, "Connected");

  // deriveWiFiConnectivityFields (src/web/api_status_serializers.cpp) reports
  // wifiClientConnected when a station has associated with this droid's AP.
  const apWithClient = toSignalMap({ wifiConnected: false, wifiClientConnected: true });
  assert.equal(apWithClient["h-wifi"].state, "ok");

  const notJoined = toSignalMap({ wifiConnected: false, wifiClientConnected: false, wifiRssi: 0 });
  assert.equal(notJoined["h-wifi"].state, "off");
  assert.equal(notJoined["h-wifi"].reason, "Not joined");

  const neverAsked = toSignalMap({});
  assert.equal(neverAsked["h-wifi"].state, "off");
  assert.equal(neverAsked["h-wifi"].reason, UNKNOWN);
});

// -----------------------------------------------------------------------------
// File system
// -----------------------------------------------------------------------------

test("file system reads OK when mounted, FAIL when it said so, OFF when it did not say", () => {
  const mounted = toSignalMap({ littleFsReady: true });
  assert.equal(mounted["h-fs"].state, "ok");
  assert.equal(mounted["h-fs"].reason, "Mounted");

  const notReady = toSignalMap({ littleFsReady: false });
  assert.equal(notReady["h-fs"].state, "fail");
  assert.equal(notReady["h-fs"].reason, "Not ready");

  // A key the payload never carried is not a mount failure. Red means stopped
  // or refused, and nothing here has refused anything.
  const silent = toSignalMap({});
  assert.equal(silent["h-fs"].state, "off");
  assert.equal(silent["h-fs"].reason, UNKNOWN);
});

// -----------------------------------------------------------------------------
// Memory - where amber still belongs
// -----------------------------------------------------------------------------

test("memory keeps amber for a reported low number and reads OFF for no number", () => {
  const normal = toSignalMap({ heapLargest8bit: 90000 });
  assert.equal(normal["h-heap"].state, "ok");
  assert.equal(normal["h-heap"].reason, "Normal");

  // Between the warn and fail floors: reported, degraded, and the builder can
  // act on it. This is what amber is kept for.
  const low = toSignalMap({ heapLargest8bit: 13000 });
  assert.equal(low["h-heap"].state, "warn");
  assert.equal(low["h-heap"].reason, "Low");

  const critical = toSignalMap({ heapLargest8bit: 9000 });
  assert.equal(critical["h-heap"].state, "fail");
  assert.equal(critical["h-heap"].reason, "Critical");

  const noNumber = toSignalMap({});
  assert.equal(noNumber["h-heap"].state, "off");
  assert.equal(noNumber["h-heap"].reason, UNKNOWN);
});

// -----------------------------------------------------------------------------
// protoR2link and the sound link: one word table (#422)
//
// Every page that shows either link reads these two answers, so the table is
// the contract: the droid's state goes in, one word and one light come out.
// -----------------------------------------------------------------------------

const FINDING_OUT = (() => {
  const context = { window: {}, console };
  vm.runInNewContext(readFileSync(path.join(__dirname, "../../data/live_reading.js"), "utf8"), context);
  return context.window.PALiveReading.FINDING_OUT;
})();
const WORDS = { unknown: UNKNOWN, findingOut: FINDING_OUT };
const { readProtoR2link, readSoundLink } = require("../../data/health_signals.js");

test("protoR2link: each state the droid reports has one word and one light", () => {
  const rows = [
    [{ dome_link: { state: "connected", transport: "uart" } }, "ok", "UART (slip ring)", "UART"],
    [{ dome_link: { state: "connected", transport: "wifi" } }, "ok", "WiFi (fallback)", "WIFI"],
    [{ dome_link: { state: "disabled" } }, "off", "Off", "Off"],
    [{ dome_link: { state: "not_seen" } }, "off", "Not seen", "Not seen"],
    [{ dome_link: { state: "lost" } }, "fail", "Lost", "Lost"],
    [null, "off", FINDING_OUT, FINDING_OUT],
    [{}, "off", UNKNOWN, UNKNOWN],
  ];
  rows.forEach(([status, state, word, short]) => {
    assert.deepEqual(readProtoR2link(status, WORDS), { state, word, short }, JSON.stringify(status));
  });
  // The Dashboard's Health row is the same answer.
  assert.equal(toSignalMap({ dome_link: { state: "lost" } })["h-dome-link"].state, "fail");
});

test("the sound link: each state the droid reports has one word and one light", () => {
  const rows = [
    [{ audio: { driver: "MP3 Trigger", output: "on", link_ok: true, rx_status: "available" } }, "ok", "MP3 Trigger"],
    [{}, "off", "Off"],
    [{ audio: { driver: "DY-SV5W", output: "off", link_ok: false } }, "off", "Off"],
    [{ audio: { driver: "DY-SV5W", output: "on", link_ok: false, rx_status: "no_response" } }, "fail", "No answer"],
    [{ audio: { output: "on", link_ok: false, rx_status: "blocked_by_dome_uart" } }, "off", "Held by protoR2link"],
    [null, "off", FINDING_OUT],
    [{ audio: { state: "idle" } }, "off", UNKNOWN],
  ];
  rows.forEach(([status, state, word]) => {
    assert.deepEqual(readSoundLink(status, WORDS), { state, word, short: word }, JSON.stringify(status));
  });
  assert.equal(toSignalMap({ audio: { link_ok: false, rx_status: "no_response" } })["h-sound"].state, "fail");
});

test("who holds the shared line never changes a link's answer; the droid's state does", () => {
  // protoR2link is never held by sound: a lost with sound holding the line is
  // a real WiFi loss and reads the same red as any other lost.
  ["dome", "audio", "none"].forEach((owner) => {
    const lost = { dome_link: { state: "lost", uart_owner: owner, uart_owned_by_dome: owner === "dome" } };
    assert.equal(readProtoR2link(lost, WORDS).state, "fail", `uart_owner=${owner}`);
    assert.equal(toSignalMap(lost)["h-dome-link"].state, "fail", `uart_owner=${owner}`);
  });
});

test("a link reading refuses to guess the Live Reading's words", () => {
  assert.throws(() => readProtoR2link({}, {}), TypeError);
  assert.throws(() => readSoundLink(null, { unknown: UNKNOWN }), TypeError);
});

// -----------------------------------------------------------------------------
// Dome ESC
// -----------------------------------------------------------------------------

test("dome esc reports OFF when disabled or missing", () => {
  const missing = toSignalMap({ domeEsc: { state: "idle" } });
  assert.equal(missing["h-dome-esc"].state, "off");
  assert.equal(missing["h-dome-esc"].reason, "Disabled");

  const disabled = toSignalMap({ domeEnabled: false, domeEsc: { state: "spinning" } });
  assert.equal(disabled["h-dome-esc"].state, "off");
  assert.equal(disabled["h-dome-esc"].reason, "Disabled");
});

test("dome esc reports OK for idle and spinning states", () => {
  const idle = toSignalMap({ domeEnabled: true, domeEsc: { state: "idle" } });
  assert.equal(idle["h-dome-esc"].state, "ok");
  assert.equal(idle["h-dome-esc"].reason, "Idle");

  const spinning = toSignalMap({ domeEnabled: true, domeEsc: { state: "spinning" } });
  assert.equal(spinning["h-dome-esc"].state, "ok");
  assert.equal(spinning["h-dome-esc"].reason, "Spinning");
});

test("dome esc reads OFF for a state it does not recognise, keeping the state in the word", () => {
  const unknown = toSignalMap({ domeEnabled: true, domeEsc: { state: "paused" } });
  assert.equal(unknown["h-dome-esc"].state, "off");
  assert.equal(unknown["h-dome-esc"].reason, `${UNKNOWN} (paused)`);

  const missingState = toSignalMap({ domeEnabled: true });
  assert.equal(missingState["h-dome-esc"].state, "off");
  assert.equal(missingState["h-dome-esc"].reason, UNKNOWN);
});

// -----------------------------------------------------------------------------
// The stream being down is not a health state
// -----------------------------------------------------------------------------

test("with the stream down a row keeps the state the controller last reported", () => {
  const live = toSignalMap(HEALTHY_PAYLOAD);
  // The caller has nothing left to say about freshness: an option object is
  // not read, so the same frame derives the same rows whether or not the
  // stream behind it is still running.
  const streamDown = toSignalMap(HEALTHY_PAYLOAD, { stale: true });

  assert.deepEqual(streamDown, live);
  ["h-sbus", "h-wifi", "h-fs", "h-heap", "h-dome-link", "h-sound", "h-dome-esc"].forEach((id) => {
    assert.equal(streamDown[id].state, "ok", `${id} must keep the state it reported`);
  });
});

test("no row says anything about staleness", () => {
  const streamDown = deriveHealthSignals(HEALTHY_PAYLOAD, { stale: true });
  streamDown.forEach(({ id, reason }) => {
    assert.doesNotMatch(reason, /stale|interrupted|last known/i, `${id} must not mention staleness`);
  });
});

test("nothing that means 'we have not heard' reads amber or green", () => {
  const silent = deriveHealthSignals({});
  silent.forEach(({ id, state }) => {
    assert.equal(state, "off", `${id} must be unlit when the payload said nothing`);
  });

  // Every subsystem answering with something this build has no branch for.
  const unrecognised = deriveHealthSignals({
    rcCh1: 1500,
    sbusSignalLost: false,
    sbusHwFailsafe: false,
    wifiConnected: false,
    wifiClientConnected: false,
    dome_link: { state: "handshaking" },
    audio: { state: "booting" },
    domeEnabled: true,
    domeEsc: { state: "paused" },
  });
  unrecognised.forEach(({ id, state }) => {
    assert.notEqual(state, "warn", `${id} must not claim degradation for a state we cannot read`);
  });
});

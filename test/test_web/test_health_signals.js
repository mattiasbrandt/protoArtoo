// =============================================================================
// test/test_web/test_health_signals.js
//
// The state every health evaluator returns, pinned one row at a time.
//
// #402: a signal that means "we have not heard" reads grey. Amber is reserved
// for a droid that is genuinely degraded and that the builder can do something
// about (CONTEXT.md "Status Colour"), so stale, unknown, never-asked and
// not-fitted all resolve to the unlit state. Staleness is not a state at all:
// a stale row keeps what the controller last reported, and the Status Plate
// carries the one freshness statement for the surface (CONTEXT.md "Health
// Signal", _Avoid_).
// =============================================================================
const test = require("node:test");
const assert = require("node:assert/strict");

const { deriveHealthSignals } = require("../../data/health_signals.js");

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
  audio: { state: "idle" },
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
  assert.match(serving["h-wifi"].detail, /wifiRssi=-47 dBm/);

  // deriveWiFiConnectivityFields (src/web/api_status_serializers.cpp) reports
  // wifiClientConnected when a station has associated with this droid's AP.
  const apWithClient = toSignalMap({ wifiConnected: false, wifiClientConnected: true });
  assert.equal(apWithClient["h-wifi"].state, "ok");

  const notJoined = toSignalMap({ wifiConnected: false, wifiClientConnected: false, wifiRssi: 0 });
  assert.equal(notJoined["h-wifi"].state, "off");
  assert.equal(notJoined["h-wifi"].reason, "Not joined");

  const neverAsked = toSignalMap({});
  assert.equal(neverAsked["h-wifi"].state, "off");
  assert.equal(neverAsked["h-wifi"].reason, "Not reporting");
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
  assert.equal(silent["h-fs"].reason, "Not reporting");
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
  assert.equal(noNumber["h-heap"].reason, "No data");
});

// -----------------------------------------------------------------------------
// protoR2link
// -----------------------------------------------------------------------------

test("protoR2link reads OFF when disabled and keeps FAIL for a heartbeat it lost", () => {
  const disabled = toSignalMap({ dome_link: { state: "disabled" } });
  assert.equal(disabled["h-dome-link"].state, "off");
  assert.equal(disabled["h-dome-link"].reason, "Disabled");
  assert.match(disabled["h-dome-link"].detail, /state=disabled/);

  // "lost" is a heartbeat that WAS heard and then stopped - a real regression
  // from a known-good state, and the one dome-link branch that stays red.
  const lost = toSignalMap({ dome_link: { state: "lost", detail: "no heartbeat 12s" } });
  assert.equal(lost["h-dome-link"].state, "fail");
  assert.equal(lost["h-dome-link"].reason, "Heartbeat lost");
});

test("protoR2link connected includes transport label in reason", () => {
  const uart = toSignalMap({
    dome_link: { state: "connected", transport: "uart", uart_owned_by_dome: true },
  });
  assert.equal(uart["h-dome-link"].state, "ok");
  assert.equal(uart["h-dome-link"].reason, "Connected - UART (slip ring)");
  assert.match(uart["h-dome-link"].detail, /UART2 owned by protoR2link/);

  const wifi = toSignalMap({ dome_link: { state: "connected", transport: "wifi" } });
  assert.equal(wifi["h-dome-link"].state, "ok");
  assert.equal(wifi["h-dome-link"].reason, "Connected - WiFi (fallback)");

  const noTransport = toSignalMap({ dome_link: { state: "connected" } });
  assert.equal(noTransport["h-dome-link"].state, "ok");
  assert.equal(noTransport["h-dome-link"].reason, "Connected");
});

test("protoR2link never seen, unrecognised or silent all read OFF", () => {
  // Never seen: enabled, and the dome has never answered. A droid with no dome
  // board fitted reads exactly this, and it is not worth getting up for.
  const neverSeen = toSignalMap({ dome_link: { state: "not_seen", detail: "no heartbeat yet" } });
  assert.equal(neverSeen["h-dome-link"].state, "off");
  assert.equal(neverSeen["h-dome-link"].reason, "Not seen");

  const unknown = toSignalMap({ dome_link: { state: "handshaking" } });
  assert.equal(unknown["h-dome-link"].state, "off");
  assert.equal(unknown["h-dome-link"].reason, "Unknown (handshaking)");
  assert.match(unknown["h-dome-link"].detail, /state=handshaking/);

  const noState = toSignalMap({ dome_link: { detail: "n/a" } });
  assert.equal(noState["h-dome-link"].state, "off");
  assert.equal(noState["h-dome-link"].reason, "No status");
});

// -----------------------------------------------------------------------------
// Sound
// -----------------------------------------------------------------------------

test("sound reads OK while playing or idle and OFF when the block is absent", () => {
  const playing = toSignalMap({ audio: { state: "playing", detail: "track 3" } });
  assert.equal(playing["h-sound"].state, "ok");
  assert.equal(playing["h-sound"].reason, "Playing");

  const idle = toSignalMap({ audio: { state: "idle" } });
  assert.equal(idle["h-sound"].state, "ok");
  assert.equal(idle["h-sound"].reason, "Idle");

  const absent = toSignalMap({});
  assert.equal(absent["h-sound"].state, "off");
  assert.equal(absent["h-sound"].reason, "Disabled");
});

test("sound no response remains a failure", () => {
  const signals = toSignalMap({
    audio: {
      state: "idle",
      link_ok: false,
      rx_status: "no_response",
    },
  });

  assert.equal(signals["h-sound"].state, "fail");
  assert.equal(signals["h-sound"].reason, "No module response");
});

test("a sound status that cannot be asked for reads OFF, and is still not a failure", () => {
  // DomeLink owns UART2, so the module is not being asked. That is not
  // reporting - and it must stay distinct from the no-response fault above.
  const blocked = toSignalMap({
    audio: {
      state: "idle",
      link_ok: false,
      rx_status: "blocked_by_dome_uart",
      rx_detail: "Status unavailable: DomeLink is using UART",
    },
  });
  assert.equal(blocked["h-sound"].state, "off");
  assert.equal(blocked["h-sound"].reason, "Status unavailable");
  assert.equal(blocked["h-sound"].detail, "Status unavailable: DomeLink is using UART");

  const unknown = toSignalMap({ audio: { state: "booting" } });
  assert.equal(unknown["h-sound"].state, "off");
  assert.equal(unknown["h-sound"].reason, "Unknown (booting)");

  const noState = toSignalMap({ audio: { detail: "n/a" } });
  assert.equal(noState["h-sound"].state, "off");
  assert.equal(noState["h-sound"].reason, "No state");

  const malformed = toSignalMap({ audio: "idle" });
  assert.equal(malformed["h-sound"].state, "off");
  assert.equal(malformed["h-sound"].reason, "Invalid payload");
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
  assert.match(idle["h-dome-esc"].detail, /domeEnabled=true, state=idle/);

  const spinning = toSignalMap({ domeEnabled: true, domeEsc: { state: "spinning" } });
  assert.equal(spinning["h-dome-esc"].state, "ok");
  assert.equal(spinning["h-dome-esc"].reason, "Spinning");
  assert.match(spinning["h-dome-esc"].detail, /domeEnabled=true, state=spinning/);
});

test("dome esc reads OFF for a state it does not recognise, keeping the state in the detail", () => {
  const unknown = toSignalMap({ domeEnabled: true, domeEsc: { state: "paused" } });
  assert.equal(unknown["h-dome-esc"].state, "off");
  assert.equal(unknown["h-dome-esc"].reason, "Unknown (paused)");
  assert.match(unknown["h-dome-esc"].detail, /state=paused/);

  const missingState = toSignalMap({ domeEnabled: true });
  assert.equal(missingState["h-dome-esc"].state, "off");
  assert.equal(missingState["h-dome-esc"].reason, "No status");
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
  streamDown.forEach(({ id, reason, detail }) => {
    assert.doesNotMatch(reason, /stale/i, `${id} reason must not mention staleness`);
    assert.doesNotMatch(detail, /stale|interrupted|last known/i, `${id} detail must not mention staleness`);
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

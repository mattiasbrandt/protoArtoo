const test = require("node:test");
const assert = require("node:assert/strict");

const { deriveHealthSignals } = require("../../data/health_signals.js");

const toSignalMap = (payload, options) => {
  const entries = deriveHealthSignals(payload, options).map((item) => [item.id, item]);
  return Object.fromEntries(entries);
};

test("dome esc reports OFF when disabled or missing", () => {
  const missing = toSignalMap({ dome: { state: "idle" } });
  assert.equal(missing["h-dome-esc"].state, "off");
  assert.equal(missing["h-dome-esc"].reason, "Disabled");

  const disabled = toSignalMap({ domeEnabled: false, dome: { state: "spinning" } });
  assert.equal(disabled["h-dome-esc"].state, "off");
  assert.equal(disabled["h-dome-esc"].reason, "Disabled");
});

test("dome esc reports OK for idle and spinning states", () => {
  const idle = toSignalMap({ domeEnabled: true, dome: { state: "idle" } });
  assert.equal(idle["h-dome-esc"].state, "ok");
  assert.equal(idle["h-dome-esc"].reason, "Idle");

  const spinning = toSignalMap({ domeEnabled: true, dome: { state: "spinning" } });
  assert.equal(spinning["h-dome-esc"].state, "ok");
  assert.equal(spinning["h-dome-esc"].reason, "Spinning");
});

test("dome esc falls back to WARN for unknown or missing state", () => {
  const unknown = toSignalMap({ domeEnabled: true, dome: { state: "paused" } });
  assert.equal(unknown["h-dome-esc"].state, "warn");
  assert.equal(unknown["h-dome-esc"].reason, "Unknown (paused)");

  const missingState = toSignalMap({ domeEnabled: true });
  assert.equal(missingState["h-dome-esc"].state, "warn");
  assert.equal(missingState["h-dome-esc"].reason, "No status");
});

test("heap unknown data is WARN instead of OFF", () => {
  const missing = toSignalMap({});
  assert.equal(missing["h-heap"].state, "warn");
  assert.equal(missing["h-heap"].reason, "No data");
});

test("stale mode downgrades non-off indicators to WARN", () => {
  const stale = toSignalMap(
    {
      rcCh1: { state: "active" },
      sbusSignalLost: false,
      sbusHwFailsafe: false,
      wifiConnected: true,
      littleFsReady: true,
      heapFree: 150000,
      dome_link: { state: "connected" },
      s2Sound: { state: "idle" },
      domeEnabled: true,
      dome: { state: "idle" },
    },
    { stale: true },
  );

  assert.equal(stale["h-sbus"].state, "warn");
  assert.equal(stale["h-wifi"].state, "warn");
  assert.equal(stale["h-fs"].state, "warn");
  assert.equal(stale["h-heap"].state, "warn");
  assert.equal(stale["h-dome-link"].state, "warn");
  assert.equal(stale["h-sound"].state, "warn");
  assert.equal(stale["h-dome-esc"].state, "warn");
  assert.equal(stale["h-dome-esc"].reason, "Stale data");
});

test("stale mode preserves OFF indicators as OFF", () => {
  const stale = toSignalMap({}, { stale: true });
  assert.equal(stale["h-sbus"].state, "off");
  assert.equal(stale["h-dome-link"].state, "off");
  assert.equal(stale["h-sound"].state, "off");
  assert.equal(stale["h-dome-esc"].state, "off");
});

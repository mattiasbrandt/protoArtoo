const test = require("node:test");
const assert = require("node:assert/strict");

const { deriveHealthSignals } = require("../../data/health_signals.js");

const toSignalMap = (payload, options) => {
  const entries = deriveHealthSignals(payload, options).map((item) => [item.id, item]);
  return Object.fromEntries(entries);
};

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

test("dome esc falls back to WARN for unknown or missing state", () => {
  const unknown = toSignalMap({ domeEnabled: true, domeEsc: { state: "paused" } });
  assert.equal(unknown["h-dome-esc"].state, "warn");
  assert.equal(unknown["h-dome-esc"].reason, "Unknown (paused)");

  const missingState = toSignalMap({ domeEnabled: true });
  assert.equal(missingState["h-dome-esc"].state, "warn");
  assert.equal(missingState["h-dome-esc"].reason, "No status");
});

test("dome link reports OFF when backend marks link disabled", () => {
  const disabled = toSignalMap({ dome_link: { state: "disabled" } });
  assert.equal(disabled["h-dome-link"].state, "off");
  assert.equal(disabled["h-dome-link"].reason, "Disabled");
  assert.match(disabled["h-dome-link"].detail, /state=disabled/);
});

test("dome link connected includes transport label in reason", () => {
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
      audio: { state: "idle" },
      domeEnabled: true,
      domeEsc: { state: "idle" },
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
  assert.equal(
    stale["h-dome-esc"].detail,
    "Status stream interrupted; showing last known values",
  );
});

test("stale mode preserves OFF indicators as OFF", () => {
  const stale = toSignalMap({}, { stale: true });
  assert.equal(stale["h-sbus"].state, "off");
  assert.equal(stale["h-dome-link"].state, "off");
  assert.equal(stale["h-sound"].state, "off");
  assert.equal(stale["h-dome-esc"].state, "off");
});

test("sound RX blocked by DomeLink is warning, not module failure", () => {
  const signals = toSignalMap({
    audio: {
      state: "idle",
      link_ok: false,
      rx_status: "blocked_by_dome_uart",
      rx_detail: "Status unavailable: DomeLink is using UART",
    },
  });

  assert.equal(signals["h-sound"].state, "warn");
  assert.equal(signals["h-sound"].reason, "Status unavailable");
  assert.equal(signals["h-sound"].detail, "Status unavailable: DomeLink is using UART");
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

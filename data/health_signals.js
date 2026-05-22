// =============================================================================
// data/health_signals.js
//
// Shared health indicator derivation for the dashboard traffic-light grid.
// - Explicit state semantics: off=disabled, warn=degraded/unknown, fail=hard fault
// - Exposes concise operator summary plus richer backend tooltip detail
// - Supports stale-data override without mutating transport payloads
// =============================================================================
(() => {
  const INDICATOR_STATE_LABELS = Object.freeze({
    ok: "OK",
    warn: "WARN",
    fail: "FAIL",
    off: "OFF",
  });

  const RC_CHANNEL_KEYS = Object.freeze([
    "rcCh1",
    "rcCh2",
    "rcCh3",
    "rcCh4",
    "rcCh5",
    "rcCh6",
  ]);

  const hasOwnKey = (obj, key) => Object.prototype.hasOwnProperty.call(obj, key);
  const boolText = (value) => (value === true ? "true" : value === false ? "false" : "unknown");
  const healthSignal = (state, reason = "", detail = reason) => ({ state, reason, detail });

  const applyStaleHealth = (signal, stale) => {
    if (!stale || signal.state === "off") return signal;
    return healthSignal(
      "warn",
      "Stale data",
      "Status stream interrupted; showing last known values"
    );
  };

  const evaluateSbus = (payload) => {
    const anyRcEnabled = RC_CHANNEL_KEYS.some((key) => hasOwnKey(payload, key));
    if (!anyRcEnabled) {
      return healthSignal(
        "off",
        "No RC input",
        "No rcCh1-rcCh6 keys in payload; RC receiver likely disabled"
      );
    }
    if (payload.sbusHwFailsafe === true) {
      return healthSignal(
        "fail",
        "HW failsafe",
        `sbusHwFailsafe=true, sbusSignalLost=${boolText(payload.sbusSignalLost)}`
      );
    }
    if (payload.sbusSignalLost === true) {
      return healthSignal(
        "fail",
        "Signal lost",
        `sbusSignalLost=true, sbusHwFailsafe=${boolText(payload.sbusHwFailsafe)}`
      );
    }
    return healthSignal(
      "ok",
      "Frames ok",
      `sbusSignalLost=${boolText(payload.sbusSignalLost)}, sbusHwFailsafe=${boolText(payload.sbusHwFailsafe)}`
    );
  };

  const evaluateWifi = (payload) => {
    const connected = payload.wifiConnected === true || payload.wifiClientConnected === true;
    const wifiRssi = Number(payload.wifiRssi);
    const rssiText = Number.isFinite(wifiRssi) ? `${wifiRssi} dBm` : "unknown";
    const detail = `wifiConnected=${boolText(payload.wifiConnected)}, wifiClientConnected=${boolText(payload.wifiClientConnected)}, wifiRssi=${rssiText}`;
    return connected ? healthSignal("ok", "Connected", detail) : healthSignal("warn", "Disconnected", detail);
  };

  const evaluateFilesystem = (payload) => {
    const ready = payload.littleFsReady === true;
    return ready
      ? healthSignal("ok", "Mounted", "littleFsReady=true")
      : healthSignal("fail", "Not ready", `littleFsReady=${boolText(payload.littleFsReady)}`);
  };

  const evaluateHeap = (payload) => {
    const heapBytes = Number(payload.heapFree);
    if (!Number.isFinite(heapBytes) || heapBytes < 0) {
      return healthSignal(
        "warn",
        "No data",
        `heapFree=${String(payload.heapFree ?? "missing")} (expected non-negative bytes)`
      );
    }

    const t = (typeof window !== "undefined" && window.PA_HEAP) || {};
    const warnAt = t.freeWarn || 65000;
    const failAt = t.freeCritical || 40000;
    const detail = `heapFree=${heapBytes} B (warn <=${warnAt} B, fail <=${failAt} B)`;
    if (heapBytes > warnAt) return healthSignal("ok", "Normal", detail);
    if (heapBytes > failAt) return healthSignal("warn", "Low", detail);
    return healthSignal("fail", "Critical", detail);
  };

  const evaluateDomeLink = (payload) => {
    if (!payload.dome_link || typeof payload.dome_link !== "object") {
      return healthSignal("off", "Disabled", "dome_link block absent from payload");
    }

    const linkState = payload.dome_link.state;
    const linkDetail = typeof payload.dome_link.detail === "string" && payload.dome_link.detail.length > 0
      ? payload.dome_link.detail
      : "n/a";

    if (linkState === "disabled") {
      return healthSignal(
        "off",
        "Disabled",
        "state=disabled (protoR2link disabled in config)"
      );
    }
    if (linkState === "connected") {
      const transport = payload.dome_link.transport;
      const transportLabel = transport === "uart" ? " - UART (slip ring)"
        : transport === "wifi" ? " - WiFi (fallback)"
        : "";
      const ownerDetail = payload.dome_link.uart_owned_by_dome === true
        ? ", UART2 owned by DomeLink"
        : "";
      return healthSignal(
        "ok",
        `Connected${transportLabel}`,
        `state=connected, detail=${linkDetail}${ownerDetail}`
      );
    }
    if (linkState === "lost") {
      return healthSignal("fail", "Heartbeat lost", `state=lost, detail=${linkDetail}`);
    }
    if (linkState === "not_seen") {
      return healthSignal("warn", "Not seen", `state=not_seen, detail=${linkDetail}`);
    }
    if (typeof linkState === "string" && linkState.length > 0) {
      return healthSignal(
        "warn",
        `Unknown (${linkState})`,
        `state=${linkState}, detail=${linkDetail}`
      );
    }
    return healthSignal("warn", "No status", `state=missing, detail=${linkDetail}`);
  };

  const evaluateSound = (payload) => {
    if (!hasOwnKey(payload, "s2Sound")) {
      return healthSignal("off", "Disabled", "s2Sound block absent from payload");
    }
    if (!payload.s2Sound || typeof payload.s2Sound !== "object") {
      return healthSignal(
        "warn",
        "Invalid payload",
        `s2Sound type=${typeof payload.s2Sound} (expected object)`
      );
    }

    const soundState = payload.s2Sound.state;
    const soundDetail = typeof payload.s2Sound.detail === "string" && payload.s2Sound.detail.length > 0
      ? payload.s2Sound.detail
      : "n/a";
    const soundRxStatus = payload.s2Sound.rx_status;
    const soundRxDetail = typeof payload.s2Sound.rx_detail === "string" && payload.s2Sound.rx_detail.length > 0
      ? payload.s2Sound.rx_detail
      : soundDetail;

    if (soundRxStatus === "blocked_by_dome_uart") {
      return healthSignal("warn", "Status unavailable", soundRxDetail);
    }

    if (payload.s2Sound.link_ok === false) {
      return healthSignal(
        "fail",
        "No module response",
        `link_ok=false, state=${soundState}, rx_status=${soundRxStatus ?? "unknown"}`
      );
    }

    if (soundState === "playing") {
      return healthSignal("ok", "Playing", `state=playing, detail=${soundDetail}`);
    }
    if (soundState === "idle") {
      return healthSignal("ok", "Idle", `state=idle, detail=${soundDetail}`);
    }
    if (typeof soundState === "string" && soundState.length > 0) {
      return healthSignal(
        "warn",
        `Unknown (${soundState})`,
        `state=${soundState}, detail=${soundDetail}`
      );
    }
    return healthSignal("warn", "No state", `state=missing, detail=${soundDetail}`);
  };

  const evaluateDomeEsc = (payload) => {
    if (payload.domeEnabled !== true) {
      return healthSignal(
        "off",
        "Disabled",
        `domeEnabled=${boolText(payload.domeEnabled)}`
      );
    }

    const domeData = payload.dome && typeof payload.dome === "object" ? payload.dome : null;
    const domeState = domeData ? domeData.state : null;
    const domeDetail = domeData && typeof domeData.detail === "string" && domeData.detail.length > 0
      ? domeData.detail
      : "n/a";

    if (domeState === "spinning") {
      return healthSignal("ok", "Spinning", `domeEnabled=true, state=spinning, detail=${domeDetail}`);
    }
    if (domeState === "idle") {
      return healthSignal("ok", "Idle", `domeEnabled=true, state=idle, detail=${domeDetail}`);
    }
    if (typeof domeState === "string" && domeState.length > 0) {
      return healthSignal(
        "warn",
        `Unknown (${domeState})`,
        `domeEnabled=true, state=${domeState}, detail=${domeDetail}`
      );
    }
    return healthSignal(
      "warn",
      "No status",
      "domeEnabled=true, dome block missing state"
    );
  };

  const HEALTH_EVALUATORS = Object.freeze({
    "h-sbus": evaluateSbus,
    "h-wifi": evaluateWifi,
    "h-fs": evaluateFilesystem,
    "h-heap": evaluateHeap,
    "h-dome-link": evaluateDomeLink,
    "h-sound": evaluateSound,
    "h-dome-esc": evaluateDomeEsc,
  });

  const deriveHealthSignals = (payload, options = {}) => {
    const stale = options && options.stale === true;
    const safePayload = payload && typeof payload === "object" ? payload : {};

    return Object.entries(HEALTH_EVALUATORS).map(([id, evaluate]) => {
      const signal = evaluate(safePayload);
      const normalized = signal && typeof signal === "object"
        ? signal
        : healthSignal("warn", "Invalid state", "Health evaluator returned invalid shape");
      const resolved = applyStaleHealth(normalized, stale);
      return {
        id,
        state: resolved.state,
        reason: resolved.reason || "",
        detail: resolved.detail || "",
      };
    });
  };

  const api = Object.freeze({
    INDICATOR_STATE_LABELS,
    deriveHealthSignals,
  });

  if (typeof window !== "undefined") {
    window.PAHealthSignals = api;
  }

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})();

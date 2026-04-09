// =============================================================================
// data/health_signals.js
//
// Shared health indicator derivation for the dashboard traffic-light grid.
// - Explicit state semantics: off=disabled, warn=degraded/unknown, fail=hard fault
// - Provides concise reason text for operator visibility
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
  const healthSignal = (state, reason = "") => ({ state, reason });

  const applyStaleHealth = (signal, stale) => {
    if (!stale || signal.state === "off") return signal;
    return healthSignal("warn", "Stale data");
  };

  const evaluateSbus = (payload) => {
    const anyRcEnabled = RC_CHANNEL_KEYS.some((key) => hasOwnKey(payload, key));
    if (!anyRcEnabled) return healthSignal("off", "No RC input");
    if (payload.sbusHwFailsafe === true) return healthSignal("fail", "HW failsafe");
    if (payload.sbusSignalLost === true) return healthSignal("fail", "Signal lost");
    return healthSignal("ok", "Frames ok");
  };

  const evaluateWifi = (payload) => {
    const connected = payload.wifiConnected === true || payload.wifiClientConnected === true;
    return connected ? healthSignal("ok", "Connected") : healthSignal("warn", "Disconnected");
  };

  const evaluateFilesystem = (payload) => {
    return payload.littleFsReady === true
      ? healthSignal("ok", "Mounted")
      : healthSignal("fail", "Not ready");
  };

  const evaluateHeap = (payload) => {
    const heapBytes = Number(payload.heapFree);
    if (!Number.isFinite(heapBytes) || heapBytes < 0) return healthSignal("warn", "No data");
    if (heapBytes > 120000) return healthSignal("ok", "Normal");
    if (heapBytes > 80000) return healthSignal("warn", "Low");
    return healthSignal("fail", "Critical");
  };

  const evaluateDomeLink = (payload) => {
    if (!payload.dome_link || typeof payload.dome_link !== "object") {
      return healthSignal("off", "Disabled");
    }

    const linkState = payload.dome_link.state;
    if (linkState === "connected") return healthSignal("ok", "Connected");
    if (linkState === "lost") return healthSignal("fail", "Heartbeat lost");
    if (linkState === "not_seen") return healthSignal("warn", "Not seen");
    if (typeof linkState === "string" && linkState.length > 0) {
      return healthSignal("warn", `Unknown (${linkState})`);
    }
    return healthSignal("warn", "No status");
  };

  const evaluateSound = (payload) => {
    if (!hasOwnKey(payload, "s2Sound")) return healthSignal("off", "Disabled");
    if (!payload.s2Sound || typeof payload.s2Sound !== "object") {
      return healthSignal("warn", "Invalid payload");
    }

    const soundState = payload.s2Sound.state;
    if (soundState === "playing") return healthSignal("ok", "Playing");
    if (soundState === "idle") return healthSignal("ok", "Idle");
    if (typeof soundState === "string" && soundState.length > 0) {
      return healthSignal("warn", `Unknown (${soundState})`);
    }
    return healthSignal("warn", "No state");
  };

  const evaluateDomeEsc = (payload) => {
    if (payload.domeEnabled !== true) return healthSignal("off", "Disabled");

    const domeState = payload.dome && typeof payload.dome === "object" ? payload.dome.state : null;
    if (domeState === "spinning") return healthSignal("ok", "Spinning");
    if (domeState === "idle") return healthSignal("ok", "Idle");
    if (typeof domeState === "string" && domeState.length > 0) {
      return healthSignal("warn", `Unknown (${domeState})`);
    }
    return healthSignal("warn", "No status");
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
        : healthSignal("warn", "Invalid state");
      const resolved = applyStaleHealth(normalized, stale);
      return {
        id,
        state: resolved.state,
        reason: resolved.reason || "",
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

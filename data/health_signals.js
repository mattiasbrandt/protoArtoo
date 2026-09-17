// =============================================================================
// data/health_signals.js
//
// Shared health indicator derivation for the dashboard traffic-light grid.
// - Explicit state semantics (CONTEXT.md "Status Colour"): ok=nominal,
//   warn=degraded and the builder can do something about it, fail=hard fault,
//   off=not reporting, never asked, not fitted
// - A reading we do not have is off, never warn: amber promises a next move,
//   and "we have not heard" offers none (#402)
// - Staleness is not a health state. A stale row keeps the state the
//   controller last reported; the Status Plate carries the one freshness
//   statement for the whole surface (CONTEXT.md "Health Signal", _Avoid_)
// - Exposes concise operator summary plus richer backend tooltip detail
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

  // An AP-only droid is a normal droid, so "not joined" is not "degraded" -
  // and a payload that never carried these keys is one we have not heard from.
  // Both read off. There is no warn branch here on purpose: the payload
  // carries no measure of a join that exists and is unhealthy (wifiRssi is 0
  // whenever the station is not connected - deriveWiFiConnectivityFields,
  // src/web/api_status_serializers.cpp), and no threshold is defined for it.
  const evaluateWifi = (payload) => {
    const reported = hasOwnKey(payload, "wifiConnected") || hasOwnKey(payload, "wifiClientConnected");
    const connected = payload.wifiConnected === true || payload.wifiClientConnected === true;
    const wifiRssi = Number(payload.wifiRssi);
    const rssiText = Number.isFinite(wifiRssi) ? `${wifiRssi} dBm` : "unknown";
    const detail = `wifiConnected=${boolText(payload.wifiConnected)}, wifiClientConnected=${boolText(payload.wifiClientConnected)}, wifiRssi=${rssiText}`;
    if (connected) return healthSignal("ok", "Connected", detail);
    if (reported) return healthSignal("off", "Not joined", detail);
    return healthSignal("off", "Not reporting", detail);
  };

  // A payload that never carried littleFsReady has not told us the mount
  // failed; it has told us nothing. Red is "stopped or refused", and claiming
  // it for a key we were never sent is the same defect as claiming amber.
  const evaluateFilesystem = (payload) => {
    if (!hasOwnKey(payload, "littleFsReady")) {
      return healthSignal("off", "Not reporting", "littleFsReady absent from payload");
    }
    return payload.littleFsReady === true
      ? healthSignal("ok", "Mounted", "littleFsReady=true")
      : healthSignal("fail", "Not ready", `littleFsReady=${boolText(payload.littleFsReady)}`);
  };

  const evaluateHeap = (payload) => {
    const heapBytes = Number(payload.heapFree);
    const t = (typeof window !== "undefined" && window.PA_HEAP) || {};

    // Judge memory health by the largest allocatable DRAM block — the value
    // the device's admission control keys on (requests are shed below its
    // floors: 14000 for new work, 12000 at accept). heapLargestBlock is NOT
    // used here: it reads a capability mask dominated by leftover IRAM that
    // malloc can never allocate, so it sits frozen regardless of pressure.
    const largest = Number(payload.heapLargest8bit);
    if (Number.isFinite(largest) && largest >= 0) {
      const warnAt = t.largestWarn ?? 16000;
      const failAt = t.largestCritical ?? 12000;
      const free = Number.isFinite(heapBytes) ? `, heapFree=${heapBytes} B` : "";
      const detail = `heapLargest8bit=${largest} B (warn <=${warnAt} B, fail <=${failAt} B${free})`;
      if (largest > warnAt) return healthSignal("ok", "Normal", detail);
      if (largest > failAt) return healthSignal("warn", "Low", detail);
      return healthSignal("fail", "Critical", detail);
    }

    // Older firmware without heapLargest8bit: fall back to total free heap.
    // Neither number present is a reading we do not have, not a low one.
    if (!Number.isFinite(heapBytes) || heapBytes < 0) {
      return healthSignal(
        "off",
        "No data",
        `heapFree=${String(payload.heapFree ?? "missing")} (expected non-negative bytes)`
      );
    }

    const warnAt = t.freeWarn ?? 65000;
    const failAt = t.freeCritical ?? 40000;
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
        ? ", UART2 owned by protoR2link"
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
    // Never seen: the link is enabled and the dome has never answered, which
    // is not reporting rather than degraded - a droid with no dome board
    // fitted reads exactly this, and it is not worth getting up for. "lost"
    // above stays a hard fault: that one WAS heard and then stopped.
    if (linkState === "not_seen") {
      return healthSignal("off", "Not seen", `state=not_seen, detail=${linkDetail}`);
    }
    if (typeof linkState === "string" && linkState.length > 0) {
      return healthSignal(
        "off",
        `Unknown (${linkState})`,
        `state=${linkState}, detail=${linkDetail}`
      );
    }
    return healthSignal("off", "No status", `state=missing, detail=${linkDetail}`);
  };

  const evaluateSound = (payload) => {
    if (!hasOwnKey(payload, "audio")) {
      return healthSignal("off", "Disabled", "audio block absent from payload");
    }
    if (!payload.audio || typeof payload.audio !== "object") {
      return healthSignal(
        "off",
        "Invalid payload",
        `audio type=${typeof payload.audio} (expected object)`
      );
    }

    const soundState = payload.audio.state;
    const soundDetail = typeof payload.audio.detail === "string" && payload.audio.detail.length > 0
      ? payload.audio.detail
      : "n/a";
    const soundRxStatus = payload.audio.rx_status;
    const soundRxDetail = typeof payload.audio.rx_detail === "string" && payload.audio.rx_detail.length > 0
      ? payload.audio.rx_detail
      : soundDetail;

    // DomeLink owns the UART, so the module cannot be asked. That is the
    // definition of not reporting; it is still not a module failure, which is
    // why this branch sits above the link_ok=false hard fault below.
    if (soundRxStatus === "blocked_by_dome_uart") {
      return healthSignal("off", "Status unavailable", soundRxDetail);
    }

    if (payload.audio.link_ok === false) {
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
        "off",
        `Unknown (${soundState})`,
        `state=${soundState}, detail=${soundDetail}`
      );
    }
    return healthSignal("off", "No state", `state=missing, detail=${soundDetail}`);
  };

  const evaluateDomeEsc = (payload) => {
    if (payload.domeEnabled !== true) {
      return healthSignal(
        "off",
        "Disabled",
        `domeEnabled=${boolText(payload.domeEnabled)}`
      );
    }

    const domeData = payload.domeEsc && typeof payload.domeEsc === "object" ? payload.domeEsc : null;
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
    // A state we have no branch for is one we do not understand, which is not
    // reporting rather than degraded. The state string stays in the detail so
    // the tooltip still says what arrived.
    if (typeof domeState === "string" && domeState.length > 0) {
      return healthSignal(
        "off",
        `Unknown (${domeState})`,
        `domeEnabled=true, state=${domeState}, detail=${domeDetail}`
      );
    }
    return healthSignal(
      "off",
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

  const deriveHealthSignals = (payload) => {
    const safePayload = payload && typeof payload === "object" ? payload : {};

    return Object.entries(HEALTH_EVALUATORS).map(([id, evaluate]) => {
      const signal = evaluate(safePayload);
      const resolved = signal && typeof signal === "object"
        ? signal
        : healthSignal("off", "Invalid state", "Health evaluator returned invalid shape");
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

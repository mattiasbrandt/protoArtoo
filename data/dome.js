// =============================================================================
// dome.js
//
// Dome page controller.
// - Live RC dome target status (read-only)
// - Dome motor configuration load/save
// - Shared API helper error handling
// =============================================================================
(() => {
  const domeFeedback = document.getElementById("dome-feedback");
  const domeDisabledCard = document.getElementById("dome-disabled-card");
  const domeHardwarePill = document.getElementById("dome-hardware-pill");
  const domeWebPill = document.getElementById("dome-web-pill");
  const domeSpeedDisplay = document.getElementById("dome-speed-display");
  const domeRotationState = document.getElementById("dome-rotation-state");
  const domeLiveFill = document.getElementById("dome-live-fill");

  const domeNeutral = document.getElementById("dome-neutral");
  const domeMinPulse = document.getElementById("dome-min-pulse");
  const domeMaxPulse = document.getElementById("dome-max-pulse");
  const domeSpeedLimit = document.getElementById("dome-speed-limit");
  const reloadEscButton = document.getElementById("reload-esc-button");
  const escFeedback = document.getElementById("esc-feedback");

  let saveTimeout = null;
  let domeHardwareEnabled = true;
  let webControlEnabled = false;
  let webControlStatusKnown = false;

  const FEEDBACK_BASE_CLASS = "feedback mt-12";

  const showFeedback = (el, text, level = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = level ? `${FEEDBACK_BASE_CLASS} ${level}` : FEEDBACK_BASE_CLASS;
  };

  const setPillState = (el, text, state = "info", compact = true) => {
    if (!el) return;
    const classMap = {
      ok: "pill-ok",
      warn: "pill-warn",
      error: "pill-error",
      info: "pill-info",
    };
    const sizeClass = compact ? "status-pill status-pill-compact" : "status-pill";
    el.textContent = text;
    el.className = `${sizeClass} ${classMap[state] || classMap.info}`;
  };

  const debounce = (fn, ms) => (...args) => {
    window.clearTimeout(saveTimeout);
    saveTimeout = window.setTimeout(() => fn(...args), ms);
  };

  const clampSpeed = (value) => {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return 0;
    return Math.max(-1, Math.min(1, parsed));
  };

  const extractSpeedFromDetail = (detail) => {
    const text = String(detail || "");
    const match = text.match(/(-?\d+(?:\.\d+)?)%/);
    if (!match) return 0;
    const percent = Number(match[1]);
    if (!Number.isFinite(percent)) return 0;
    return clampSpeed(percent / 100);
  };

  const renderDomeTargetSpeed = (speed) => {
    const normalized = clampSpeed(speed);
    const percent = Math.round(normalized * 100);
    const widthPct = Math.abs(percent) / 2;

    if (domeSpeedDisplay) domeSpeedDisplay.textContent = `${percent}%`;

    if (domeLiveFill) {
      domeLiveFill.style.width = `${widthPct}%`;
      if (widthPct < 0.5) {
        domeLiveFill.style.opacity = "0";
        domeLiveFill.style.left = "50%";
      } else if (percent >= 0) {
        domeLiveFill.style.opacity = "1";
        domeLiveFill.style.left = "50%";
        domeLiveFill.style.background = "color-mix(in srgb, var(--success) 80%, var(--accent-bright))";
      } else {
        domeLiveFill.style.opacity = "1";
        domeLiveFill.style.left = `calc(50% - ${widthPct}%)`;
        domeLiveFill.style.background = "color-mix(in srgb, var(--warning) 85%, var(--accent-bright))";
      }
    }

    if (Math.abs(percent) < 2) {
      setPillState(domeRotationState, "⏸️ Idle", "info", false);
    } else if (percent > 0) {
      setPillState(domeRotationState, `↻ Forward ${percent}%`, "ok", false);
    } else {
      setPillState(domeRotationState, `↺ Reverse ${Math.abs(percent)}%`, "warn", false);
    }
  };

  const updateDomeControlsEnabled = () => {
    const configEnabled = domeHardwareEnabled;
    window.PAApi.gateControls([domeNeutral, domeMinPulse, domeMaxPulse, domeSpeedLimit, reloadEscButton],
                              configEnabled);

    domeDisabledCard?.classList.toggle("hidden", domeHardwareEnabled);

    setPillState(
      domeHardwarePill,
      domeHardwareEnabled ? "🧩 DOME enabled" : "🧩 DOME disabled in Setup",
      domeHardwareEnabled ? "ok" : "warn",
      true,
    );

    if (!webControlStatusKnown) {
      setPillState(domeWebPill, "🕹️ Web control status pending", "info", true);
    } else {
      setPillState(
        domeWebPill,
        webControlEnabled ? "🕹️ Web control enabled" : "🕹️ Web control disabled",
        webControlEnabled ? "ok" : "warn",
        true,
      );
    }

    if (!domeHardwareEnabled) {
      showFeedback(domeFeedback, "Dome controls unavailable: enable DOME in Setup.", "warning");
    } else if (!webControlStatusKnown) {
      showFeedback(domeFeedback, "Waiting for live web-control status frame...");
    } else if (!webControlEnabled) {
      showFeedback(domeFeedback, "Web control is disabled — enable it on the Drive page.", "warning");
    } else {
      showFeedback(domeFeedback, "Dome ready.");
    }
  };

  const setDomeHardwareEnabled = (enabled) => {
    domeHardwareEnabled = enabled;
    if (!enabled) {
      renderDomeTargetSpeed(0);
    }
    updateDomeControlsEnabled();
  };

  const resolveDomeEnabledFromStatus = (payload) => {
    if (typeof payload?.domeEnabled === "boolean") {
      return payload.domeEnabled;
    }
    return payload ? Object.prototype.hasOwnProperty.call(payload, "dome") : false;
  };

  const resolveDomeTargetSpeed = (payload) => {
    const direct = Number(payload?.domeTargetSpeed);
    if (Number.isFinite(direct)) return direct;
    return extractSpeedFromDetail(payload?.dome?.detail);
  };

  const renderStatusFrame = (payload) => {
    webControlStatusKnown = true;
    webControlEnabled = !!payload?.webControlEnabled;
    domeHardwareEnabled = resolveDomeEnabledFromStatus(payload);
    renderDomeTargetSpeed(resolveDomeTargetSpeed(payload));
    updateDomeControlsEnabled();
  };

  const renderEscConfigSnapshot = (data) => {
    const dome = data?.dome || {};
    const components = data?.components || {};

    if (domeNeutral) domeNeutral.value = dome.neutralUs;
    if (domeMinPulse) domeMinPulse.value = dome.minPulseUs;
    if (domeMaxPulse) domeMaxPulse.value = dome.maxPulseUs;
    if (domeSpeedLimit) domeSpeedLimit.value = dome.speedLimitPct;

    setDomeHardwareEnabled(Boolean(components.dome?.enabled));
  };


  const loadEscConfig = async () => {
    if (!window.PAApi) return;
    showFeedback(escFeedback, "Loading motor settings...");

    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 3000 });
      renderEscConfigSnapshot(result.data);
      showFeedback(escFeedback, `Motor settings loaded at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(escFeedback, `Failed to load motor settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const saveEscConfig = async () => {
    if (!window.PAApi) return;
    if (!domeHardwareEnabled) {
      showFeedback(escFeedback, "Dome settings unavailable: enable DOME in Setup.", "warning");
      return;
    }

    showFeedback(escFeedback, "Saving...");

    try {
      await window.PAApi.postForm(
        "/api/config",
        {
          domeNeutralUs: domeNeutral?.value || "1500",
          domeMinPulseUs: domeMinPulse?.value || "1000",
          domeMaxPulseUs: domeMaxPulse?.value || "2000",
          domeSpeedLimitPct: domeSpeedLimit?.value || "100",
        },
        { timeoutMs: 3000 },
      );

      showFeedback(escFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(escFeedback, `Failed to save motor settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const refreshStatus = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
      renderStatusFrame(result.data);
    } catch {
      // Keep pending/last-known UI state when status is temporarily unavailable.
    }
  };

  const debouncedSave = debounce(saveEscConfig, 500);

  domeNeutral?.addEventListener("input", debouncedSave);
  domeMinPulse?.addEventListener("input", debouncedSave);
  domeMaxPulse?.addEventListener("input", debouncedSave);
  domeSpeedLimit?.addEventListener("input", debouncedSave);
  reloadEscButton?.addEventListener("click", loadEscConfig);

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType !== "status") return;
      renderStatusFrame(payload);
    });
    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatus().catch(() => {});
    }
  } else {
    refreshStatus().catch(() => {});
    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshStatus().catch(() => {});
    }, 5000);
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshStatus().catch(() => {});
      }
    });
  }

  renderDomeTargetSpeed(0);
  updateDomeControlsEnabled();
  loadEscConfig();
})();

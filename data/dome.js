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
    if (typeof payload?.components?.dome?.enabled === "boolean") {
      return payload.components.dome.enabled;
    }
    return null;
  };

  const resolveDomeTargetSpeed = (payload) => {
    const direct = Number(payload?.domeTargetSpeed);
    if (!Number.isFinite(direct)) return 0;
    return clampSpeed(direct);
  };

  const renderStatusFrame = (payload) => {
    webControlStatusKnown = typeof payload?.webControlEnabled === "boolean";
    if (webControlStatusKnown) {
      webControlEnabled = payload.webControlEnabled;
    }

    const statusDomeEnabled = resolveDomeEnabledFromStatus(payload);
    if (typeof statusDomeEnabled === "boolean") {
      domeHardwareEnabled = statusDomeEnabled;
    }

    renderDomeTargetSpeed(resolveDomeTargetSpeed(payload));
    updateDomeControlsEnabled();
  };

  const renderEscConfigSnapshot = (data) => {
    const dome = data?.dome || {};
    const components = data?.components || {};

    if (domeNeutral && dome.neutralUs !== undefined) domeNeutral.value = dome.neutralUs;
    if (domeMinPulse && dome.minPulseUs !== undefined) domeMinPulse.value = dome.minPulseUs;
    if (domeMaxPulse && dome.maxPulseUs !== undefined) domeMaxPulse.value = dome.maxPulseUs;
    if (domeSpeedLimit && dome.speedLimitPct !== undefined) domeSpeedLimit.value = dome.speedLimitPct;

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

  const clampInt = (value, min, max) => Math.max(min, Math.min(max, value));

  const parseEscField = (input, min, max, label) => {
    const rawText = String(input?.value ?? "").trim();
    if (!rawText) {
      return { error: `${label} is required.` };
    }
    const parsed = Number.parseInt(rawText, 10);
    if (!Number.isFinite(parsed)) {
      return { error: `${label} must be a whole number.` };
    }
    const clamped = clampInt(parsed, min, max);
    return { value: clamped };
  };

  const validateEscConfig = () => {
    const neutral = parseEscField(domeNeutral, 1000, 2000, "Neutral pulse");
    if (neutral.error) return { ok: false, error: neutral.error };

    const minPulse = parseEscField(domeMinPulse, 1000, 2000, "Minimum pulse");
    if (minPulse.error) return { ok: false, error: minPulse.error };

    const maxPulse = parseEscField(domeMaxPulse, 1000, 2000, "Maximum pulse");
    if (maxPulse.error) return { ok: false, error: maxPulse.error };

    const speedLimit = parseEscField(domeSpeedLimit, 0, 100, "Speed limit");
    if (speedLimit.error) return { ok: false, error: speedLimit.error };

    if (minPulse.value > maxPulse.value) {
      return { ok: false, error: "Minimum pulse must be less than or equal to maximum pulse." };
    }
    if (neutral.value < minPulse.value || neutral.value > maxPulse.value) {
      return { ok: false, error: "Neutral pulse must be within the minimum and maximum pulse range." };
    }

    return {
      ok: true,
      payload: {
        domeNeutralUs: String(neutral.value),
        domeMinPulseUs: String(minPulse.value),
        domeMaxPulseUs: String(maxPulse.value),
        domeSpeedLimitPct: String(speedLimit.value),
      },
    };
  };

  const saveEscConfig = async () => {
    if (!window.PAApi) return;
    if (!domeHardwareEnabled) {
      showFeedback(escFeedback, "Dome settings unavailable: enable DOME in Setup.", "warning");
      return;
    }

    const validation = validateEscConfig();
    if (!validation.ok) {
      showFeedback(escFeedback, validation.error, "warning");
      return;
    }

    showFeedback(escFeedback, "Saving...");

    try {
      await window.PAApi.postForm(
        "/api/config",
        validation.payload,
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

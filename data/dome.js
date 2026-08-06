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

  const domeRndEnable = document.getElementById("dome-rnd-enable");
  const domeRndSpeed = document.getElementById("dome-rnd-speed");
  const domeRndPauseMin = document.getElementById("dome-rnd-pause-min");
  const domeRndPauseMax = document.getElementById("dome-rnd-pause-max");
  const domeRndMoveMs = document.getElementById("dome-rnd-move-ms");
  const reloadRndButton = document.getElementById("reload-rnd-button");
  const rndFeedback = document.getElementById("rnd-feedback");

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
    window.PAApi.gateControls(
      [domeNeutral, domeMinPulse, domeMaxPulse, domeSpeedLimit, reloadEscButton,
       domeRndEnable, domeRndSpeed, domeRndPauseMin, domeRndPauseMax, domeRndMoveMs, reloadRndButton],
      configEnabled,
    );

    domeDisabledCard?.classList.toggle("hidden", domeHardwareEnabled);

    setPillState(
      domeHardwarePill,
      domeHardwareEnabled ? "🧩 DOME enabled" : "🧩 DOME disabled in Setup",
      domeHardwareEnabled ? "ok" : "warn",
      true,
    );

    if (!webControlStatusKnown) {
      setPillState(domeWebPill, "🕹️ Drive web lock status pending", "info", true);
    } else {
      setPillState(
        domeWebPill,
        webControlEnabled ? "🕹️ Drive web lock ON (drive only)" : "🕹️ Drive web lock OFF (drive only)",
        "info",
        true,
      );
    }

    if (!domeHardwareEnabled) {
      showFeedback(domeFeedback, "Dome controls unavailable: enable DOME in Setup.", "warning");
    } else if (!webControlStatusKnown) {
      showFeedback(domeFeedback, "Waiting for live status frame...");
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

    if (domeRndEnable && dome.rndEnable !== undefined) domeRndEnable.checked = dome.rndEnable;
    if (domeRndSpeed && dome.rndSpeedPct !== undefined) domeRndSpeed.value = dome.rndSpeedPct;
    if (domeRndPauseMin && dome.rndPauseMin !== undefined) domeRndPauseMin.value = dome.rndPauseMin;
    if (domeRndPauseMax && dome.rndPauseMax !== undefined) domeRndPauseMax.value = dome.rndPauseMax;
    if (domeRndMoveMs && dome.rndMoveMs !== undefined) domeRndMoveMs.value = dome.rndMoveMs;

    setDomeHardwareEnabled(Boolean(components.dome?.enabled));
  };


  const loadEscConfig = async () => {
    if (!window.PAApi) return;
    showFeedback(escFeedback, "Loading motor settings...");

    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 3000 });
      renderEscConfigSnapshot(result.data);
      const ts = new Date().toLocaleTimeString();
      showFeedback(escFeedback, `Motor settings loaded at ${ts}`, "success");
      showFeedback(rndFeedback, `Loaded at ${ts}`, "success");
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

  const validateRndDomeConfig = () => {
    const speedVal = parseEscField(domeRndSpeed, 5, 100, "Speed");
    if (speedVal.error) return { ok: false, error: speedVal.error };

    const pauseMinVal = parseEscField(domeRndPauseMin, 1, 120, "Min pause");
    if (pauseMinVal.error) return { ok: false, error: pauseMinVal.error };

    const pauseMaxVal = parseEscField(domeRndPauseMax, 1, 120, "Max pause");
    if (pauseMaxVal.error) return { ok: false, error: pauseMaxVal.error };

    const moveVal = parseEscField(domeRndMoveMs, 500, 10000, "Move duration");
    if (moveVal.error) return { ok: false, error: moveVal.error };

    if (pauseMinVal.value > pauseMaxVal.value) {
      return { ok: false, error: "Min pause must be less than or equal to max pause." };
    }

    return {
      ok: true,
      payload: {
        domeRndEnable: domeRndEnable?.checked ? "true" : "false",
        domeRndSpeedPct: String(speedVal.value),
        domeRndPauseMin: String(pauseMinVal.value),
        domeRndPauseMax: String(pauseMaxVal.value),
        domeRndMoveMs: String(moveVal.value),
      },
    };
  };

  const saveRndDomeConfig = async () => {
    if (!window.PAApi) return;
    if (!domeHardwareEnabled) {
      showFeedback(rndFeedback, "Random dome controls unavailable: enable DOME in Setup.", "warning");
      return;
    }

    const validation = validateRndDomeConfig();
    if (!validation.ok) {
      showFeedback(rndFeedback, validation.error, "warning");
      return;
    }

    showFeedback(rndFeedback, "Saving...");

    try {
      await window.PAApi.postForm(
        "/api/config",
        validation.payload,
        { timeoutMs: 3000 },
      );

      showFeedback(rndFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(rndFeedback, `Failed to save random movement settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const debouncedSave = window.PAUtils.debounce(saveEscConfig, 500);
  const debouncedRndSave = window.PAUtils.debounce(saveRndDomeConfig, 500);

  domeNeutral?.addEventListener("input", debouncedSave);
  domeMinPulse?.addEventListener("input", debouncedSave);
  domeMaxPulse?.addEventListener("input", debouncedSave);
  domeSpeedLimit?.addEventListener("input", debouncedSave);
  reloadEscButton?.addEventListener("click", loadEscConfig);

  domeRndEnable?.addEventListener("input", debouncedRndSave);
  domeRndSpeed?.addEventListener("input", debouncedRndSave);
  domeRndPauseMin?.addEventListener("input", debouncedRndSave);
  domeRndPauseMax?.addEventListener("input", debouncedRndSave);
  domeRndMoveMs?.addEventListener("input", debouncedRndSave);
  reloadRndButton?.addEventListener("click", loadEscConfig);

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

  // -------------------------------------------------------------------------
  // Boot — load config then start status subscription
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API load as a section so the bootstrap
  // can show recovery state if the config fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const SECTIONS = [
    ["dome-configuration", loadEscConfig, "dome configuration"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadEscConfig().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/dome.js": "dome control",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  renderDomeTargetSpeed(0);
  updateDomeControlsEnabled();
  startPageLoad();
})();

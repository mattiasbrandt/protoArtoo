// =============================================================================
// drive.js
//
// Drive page controller.
// - Safety commands and hold-to-drive controls
// - Live status via shared SSE stream (poll fallback)
// - Config load/save via shared API helper
// =============================================================================
(() => {
  const estopButton = document.getElementById("estop-button");
  const clearEstopButton = document.getElementById("clear-estop-button");
  if (clearEstopButton) clearEstopButton.disabled = true;
  const enableWebControlButton = document.getElementById("enable-web-control-button");
  const disableWebControlButton = document.getElementById("disable-web-control-button");
  const controlFeedback = document.getElementById("control-feedback");

  const statusFailsafeLabel = document.getElementById("status-failsafe-label");
  const steerFill = document.getElementById("steer-fill");
  const steerThumb = document.getElementById("steer-thumb");
  const steerValue = document.getElementById("steer-value");
  const throttleFill = document.getElementById("throttle-fill");
  const throttleThumb = document.getElementById("throttle-thumb");
  const throttleValue = document.getElementById("throttle-value");
  const hbNoData    = document.getElementById("hb-no-data");
  const hbDataGrid  = document.getElementById("hb-data-grid");
  const hbBattery   = document.getElementById("hb-battery");
  const hbBoardTemp = document.getElementById("hb-board-temp");
  const hbSpeed     = document.getElementById("hb-speed");
  const hbCurrentRow = document.getElementById("hb-current-row");
  const hbCurrent   = document.getElementById("hb-current");


  const speedLimitMax = document.getElementById("speed-limit-max");
  const speedLimitMaxReadout = document.getElementById("speed-limit-max-readout");
  const speedPresetSlow = document.getElementById("speed-preset-slow");
  const speedPresetSlowReadout = document.getElementById("speed-preset-slow-readout");
  const speedPresetNormal = document.getElementById("speed-preset-normal");
  const speedPresetNormalReadout = document.getElementById("speed-preset-normal-readout");
  const speedPresetTurbo = document.getElementById("speed-preset-turbo");
  const speedPresetTurboReadout = document.getElementById("speed-preset-turbo-readout");
  const presetValueSlow = document.getElementById("preset-value-slow");
  const presetValueNormal = document.getElementById("preset-value-normal");
  const presetValueTurbo = document.getElementById("preset-value-turbo");
  const webDriveTimeout = document.getElementById("web-drive-timeout");
  const configFeedback = document.getElementById("config-feedback");
  const presetFeedback = document.getElementById("preset-feedback");
  const presetDistinctHint = document.getElementById("preset-distinct-hint");
  const driveDisabledCard = document.getElementById("drive-disabled-card");

  const driveButtons = document.querySelectorAll("[data-drive-speed]");
  const presetButtons = document.querySelectorAll("[data-speed-preset]");
  let holdTimer = null;
  let driveHardwareEnabled = true;
  let webControlEnabled = false;
  let estopLatched = false;
  let saveInFlight = false;
  let saveQueued = false;
  let currentSpeedLimitMax = null;
  let currentSpeedPreset = null;
  const setupActionText = window.PAUi?.setupActionText || ((action) => `${action} in Setup`);
  const s1EnableInSetup = setupActionText("Enable S1 — Hoverboard");

  const FAILSAFE_SOURCE_LABELS = {
    0: "None",
    1: "SBUS timeout",
    2: "SBUS hardware",
    3: "SBUS2 timeout",
    4: "Web timeout",
    5: "Estop command",
    6: "Watchdog reset",
  };

  const PRESET_LABELS = {
    slow: "🐌 Slow",
    normal: "Normal",
    turbo: "⚡ Turbo",
  };

  const formatFailsafeSource = (source) => {
    const parsed = Number(source);
    if (Number.isFinite(parsed)) {
      const label = FAILSAFE_SOURCE_LABELS[parsed] || "Unknown";
      return `${label} (${parsed})`;
    }
    if (source === undefined || source === null || source === "") return "--";
    return String(source);
  };

  const parsePresetNumber = (value) => {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? Math.round(parsed) : null;
  };

  const parsePresetId = (value) => {
    if (value === "slow" || value === "normal" || value === "turbo") return value;
    return null;
  };

  const updatePresetHighlight = () => {
    if (!presetButtons.length) return;
    const slow = parsePresetNumber(speedPresetSlow?.value);
    const normal = parsePresetNumber(speedPresetNormal?.value);
    const turbo = parsePresetNumber(speedPresetTurbo?.value);

    let activePreset = currentSpeedPreset;
    if (!activePreset && currentSpeedLimitMax !== null) {
      if (currentSpeedLimitMax === slow) activePreset = "slow";
      else if (currentSpeedLimitMax === normal) activePreset = "normal";
      else if (currentSpeedLimitMax === turbo) activePreset = "turbo";
    }

    presetButtons.forEach((button) => {
      const isActive = button.dataset.speedPreset === activePreset;
      button.classList.toggle("preset-active", isActive);
      button.classList.toggle("selected", isActive);
      button.setAttribute("aria-pressed", isActive ? "true" : "false");
    });
  };

  const presetsAreDistinct = () => {
    const slow = parsePresetNumber(speedPresetSlow?.value);
    const normal = parsePresetNumber(speedPresetNormal?.value);
    const turbo = parsePresetNumber(speedPresetTurbo?.value);
    if (slow === null || normal === null || turbo === null) return true;
    return slow !== normal && slow !== turbo && normal !== turbo;
  };

  const updatePresetDistinctHint = () => {
    const distinct = presetsAreDistinct();
    const duplicate = !distinct;
    [speedPresetSlow, speedPresetNormal, speedPresetTurbo].forEach((input) => {
      if (!input) return;
      if (duplicate) input.setAttribute("aria-invalid", "true");
      else input.removeAttribute("aria-invalid");
    });
    presetDistinctHint?.classList.toggle("hidden", distinct);
    return distinct;
  };



  const setDriveHardwareEnabled = (enabled) => {
    driveHardwareEnabled = enabled;
    updateDriveControlsEnabled();
  };

  const updateDriveControlsEnabled = () => {
    const driveEnabled = driveHardwareEnabled && webControlEnabled && !estopLatched;
    window.PAApi.gateControls(Array.from(driveButtons), driveEnabled);
    window.PAApi.gateControls(Array.from(presetButtons), driveEnabled);

    const controlsEnabled = driveHardwareEnabled;
    const gatedControls = [
      enableWebControlButton,
      disableWebControlButton,
    ];
    window.PAApi.gateControls(gatedControls, controlsEnabled);

    driveDisabledCard?.classList.toggle("hidden", driveHardwareEnabled);

    if (!driveEnabled) {
      stopHoldLoop();
      driveButtons.forEach((btn) => btn.classList.remove("active"));
    }
  };

  const postCommand = async (path, label) => {
    if (!window.PAApi) return;
    if (!driveHardwareEnabled && path.startsWith("/api/web-control")) {
      window.PAUtils.showFeedback(controlFeedback, `Web control unavailable: ${s1EnableInSetup}.`, "warning");
      return;
    }
    window.PAUtils.showFeedback(controlFeedback, `${label}...`);
    try {
      // Estop requests skip the slot and are never retried
      const isEstop = path === "/api/estop" || path === "/api/estop/clear";
      const apiMethod = isEstop ? window.PAApi.estopPostForm : window.PAApi.postForm;
      await apiMethod(path, {}, { timeoutMs: 3000 });
      window.PAUtils.showFeedback(controlFeedback, `${label} sent at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      window.PAUtils.showFeedback(controlFeedback, `${label} failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const postDriveCommand = async (speed, steer) => {
    if (!window.PAApi) return;
    if (!webControlEnabled) {
      window.PAUtils.showFeedback(controlFeedback, "Drive unavailable: web control is disabled.", "warning");
      return;
    }
    if (!driveHardwareEnabled) {
      window.PAUtils.showFeedback(controlFeedback, `Drive controls unavailable: ${s1EnableInSetup}.`, "warning");
      return;
    }
    try {
      await window.PAApi.postForm("/api/drive", { speed: String(speed), steer: String(steer) }, { timeoutMs: 2500 });
    } catch (error) {
      window.PAUtils.showFeedback(controlFeedback, `Drive command failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const postSpeedPreset = async (preset) => {
    if (!window.PAApi) return;
    if (!webControlEnabled) {
      window.PAUtils.showFeedback(presetFeedback, "Preset switch unavailable: web control is disabled.", "warning");
      return;
    }
    if (!driveHardwareEnabled) {
      window.PAUtils.showFeedback(presetFeedback, `Preset switch unavailable: ${s1EnableInSetup}.`, "warning");
      return;
    }
    if (estopLatched) {
      window.PAUtils.showFeedback(presetFeedback, "Preset switch blocked while estop is latched.", "warning");
      return;
    }

    const presetLabel = PRESET_LABELS[preset] || preset;
    window.PAUtils.showFeedback(presetFeedback, `Applying ${presetLabel} preset...`);
    try {
      const result = await window.PAApi.postForm("/api/drive/speed-preset", { preset }, { timeoutMs: 3000 });
      const applied = parsePresetNumber(result?.data?.speedLimitMax);
      const appliedPreset = parsePresetId(result?.data?.preset);
      if (applied !== null) {
        currentSpeedLimitMax = applied;
        if (speedLimitMax) speedLimitMax.value = String(applied);
        if (speedLimitMaxReadout) speedLimitMaxReadout.textContent = String(applied);
      }
      currentSpeedPreset = appliedPreset;
      updatePresetHighlight();
      window.PAUtils.showFeedback(presetFeedback, `${presetLabel} preset applied${applied !== null ? ` (${applied})` : ""}.`, "success");
    } catch (error) {
      window.PAUtils.showFeedback(presetFeedback, `Preset switch failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const stopHoldLoop = () => {
    if (holdTimer !== null) {
      window.clearInterval(holdTimer);
      holdTimer = null;
    }
  };

  const startHoldLoop = (speed, steer) => {
    stopHoldLoop();
    postDriveCommand(speed, steer);
    holdTimer = window.setInterval(() => {
      postDriveCommand(speed, steer);
    }, 50);
  };

  driveButtons.forEach((button) => {
    const speed = button.dataset.driveSpeed;
    const steer = button.dataset.driveSteer;

    const release = () => {
      button.classList.remove("active");
      stopHoldLoop();
      postDriveCommand(0, 0);
    };

    button.addEventListener("pointerdown", () => {
      button.classList.add("active");
      startHoldLoop(speed, steer);
    });
    button.addEventListener("pointerup", release);
    button.addEventListener("pointerleave", release);
    button.addEventListener("pointercancel", release);
  });


  const VSLIDER_TRACK_H = 160;
  const VSLIDER_THUMB_H = 24;

  const updateVSlider = (thumbEl, fillEl, valueEl, val, max) => {
    const safeMax = max > 0 ? max : 600;
    const clamped = Math.max(-safeMax, Math.min(safeMax, Number.isFinite(val) ? val : 0));
    // pct: 0% = top (+max), 50% = center (0), 100% = bottom (-max)
    const pct = 50 - (clamped / safeMax) * 50;
    const thumbTop = Math.max(0, Math.min(VSLIDER_TRACK_H - VSLIDER_THUMB_H,
      (pct / 100) * VSLIDER_TRACK_H - VSLIDER_THUMB_H / 2));
    if (thumbEl) thumbEl.style.top = `${thumbTop}px`;
    const thumbCenterPx = thumbTop + VSLIDER_THUMB_H / 2;
    const centerPx = VSLIDER_TRACK_H / 2;
    if (fillEl) {
      const top = Math.min(centerPx, thumbCenterPx);
      const height = Math.abs(thumbCenterPx - centerPx);
      fillEl.style.top = `${top}px`;
      fillEl.style.height = `${height}px`;
    }
    if (valueEl) valueEl.textContent = String(Math.round(clamped));
  };

  const updateDriveSliders = (steer, speed) => {
    const max = currentSpeedLimitMax !== null ? currentSpeedLimitMax : 600;
    updateVSlider(steerThumb, steerFill, steerValue, steer, max);
    updateVSlider(throttleThumb, throttleFill, throttleValue, speed, max);
  };

  const renderHoverboard = (hb) => {
    const batteryV = Number(hb?.batteryV);
    const boardTempC = Number(hb?.boardTempC);
    const speedR = Number(hb?.speedR);
    const speedL = Number(hb?.speedL);
    const currentL = Number(hb?.currentL);
    const currentR = Number(hb?.currentR);

    const hasTelemetry = Number.isFinite(batteryV) && Number.isFinite(boardTempC) &&
      Number.isFinite(speedR) && Number.isFinite(speedL);
    if (!hasTelemetry) {
      if (hbNoData) {
        hbNoData.textContent = driveHardwareEnabled
          ? "Waiting for complete hoverboard telemetry…"
          : `Hoverboard not enabled — ${s1EnableInSetup}.`;
        hbNoData.style.display = "";
      }
      if (hbDataGrid) hbDataGrid.style.display = "none";
      return;
    }

    if (hbNoData) hbNoData.style.display = "none";
    if (hbDataGrid) hbDataGrid.style.display = "";
    if (hbBattery) hbBattery.textContent = `${batteryV.toFixed(1)} V`;
    if (hbBoardTemp) hbBoardTemp.textContent = `${boardTempC.toFixed(1)} °C`;
    if (hbSpeed) hbSpeed.textContent = `R ${Math.round(speedR)} / L ${Math.round(speedL)} RPM`;

    const safeCurrentL = Number.isFinite(currentL) ? currentL : 0;
    const safeCurrentR = Number.isFinite(currentR) ? currentR : 0;
    const hasCurrent = Math.abs(safeCurrentL) > 0.01 || Math.abs(safeCurrentR) > 0.01;
    if (hbCurrentRow) hbCurrentRow.style.display = hasCurrent ? "" : "none";
    if (hbCurrent) hbCurrent.textContent = `L ${safeCurrentL.toFixed(1)} A / R ${safeCurrentR.toFixed(1)} A`;
  };

  const renderStatus = (payload) => {
    estopLatched = !!payload.estop;
    if (clearEstopButton) clearEstopButton.disabled = !payload.estop;
    webControlEnabled = !!payload.webControlEnabled;
    updateDriveControlsEnabled();
    if (statusFailsafeLabel) statusFailsafeLabel.textContent = formatFailsafeSource(payload.failsafeSource);
    const driveSpeed = Number(payload.driveSpeed);
    const driveSteer = Number(payload.driveSteer);
    updateDriveSliders(driveSteer, driveSpeed);
    renderHoverboard(payload.hoverboard);
    updatePresetHighlight();
  };
  const renderConfig = (payload) => {
    const drive = payload?.drive || {};
    const components = payload?.components || {};
    if (speedLimitMax) speedLimitMax.value = drive.speedLimitMax;
    if (speedLimitMaxReadout) speedLimitMaxReadout.textContent = drive.speedLimitMax ?? "—";
    if (speedPresetSlow) speedPresetSlow.value = drive.speedPresetSlow;
    if (speedPresetSlowReadout) speedPresetSlowReadout.textContent = drive.speedPresetSlow ?? "—";
    if (speedPresetNormal) speedPresetNormal.value = drive.speedPresetNormal;
    if (speedPresetNormalReadout) speedPresetNormalReadout.textContent = drive.speedPresetNormal ?? "—";
    if (speedPresetTurbo) speedPresetTurbo.value = drive.speedPresetTurbo;
    if (speedPresetTurboReadout) speedPresetTurboReadout.textContent = drive.speedPresetTurbo ?? "—";
    if (webDriveTimeout) webDriveTimeout.value = drive.webDriveTimeoutMs;
    if (presetValueSlow) presetValueSlow.textContent = drive.speedPresetSlow != null ? String(drive.speedPresetSlow) : "";
    if (presetValueNormal) presetValueNormal.textContent = drive.speedPresetNormal != null ? String(drive.speedPresetNormal) : "";
    if (presetValueTurbo) presetValueTurbo.textContent = drive.speedPresetTurbo != null ? String(drive.speedPresetTurbo) : "";

    currentSpeedLimitMax = parsePresetNumber(drive.speedLimitMax);
    currentSpeedPreset = parsePresetId(drive.speedPreset);
    updatePresetHighlight();
    updatePresetDistinctHint();
    setDriveHardwareEnabled(Boolean(components.s1Hoverboard?.enabled));

    window.PAUtils.showFeedback(configFeedback, `Settings loaded at ${new Date().toLocaleTimeString()}`, "success");
  };

  const loadConfig = async () => {
    if (!window.PAApi) throw new Error("API helper unavailable");
    window.PAUtils.showFeedback(configFeedback, "Loading settings...");
    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 3000 });
      renderConfig(result.data);
    } catch (error) {
      window.PAUtils.showFeedback(configFeedback, `Failed to load settings: ${window.PAApi.messageFor(error)}`, "error");
      throw error;
    }
  };

  const saveConfig = async () => {
    if (!window.PAApi) return;
    if (saveInFlight) {
      saveQueued = true;
      return;
    }

    saveInFlight = true;
    if (!updatePresetDistinctHint()) {
      window.PAUtils.showFeedback(configFeedback, "Speed presets must be distinct values.", "warning");
      saveInFlight = false;
      return;
    }
    window.PAUtils.showFeedback(configFeedback, "Saving...");
    try {
      const result = await window.PAApi.postForm("/api/config", {
        speedLimitMax: speedLimitMax?.value ?? "600",
        speedPresetSlow: speedPresetSlow?.value ?? "200",
        speedPresetNormal: speedPresetNormal?.value ?? "350",
        speedPresetTurbo: speedPresetTurbo?.value ?? "600",
        webDriveTimeoutMs: webDriveTimeout?.value ?? "500",
      }, { timeoutMs: 3000 });

      renderConfig(result.data);
      window.PAUtils.showFeedback(configFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      window.PAUtils.showFeedback(configFeedback, `Failed to save: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      saveInFlight = false;
      if (saveQueued) {
        saveQueued = false;
        saveConfig();
      }
    }
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
    // /api/status omits the "s1Hoverboard" key entirely when the peripheral is
    // disabled. Key presence = enabled; absence = disabled. This differs from
    // renderConfig() which reads components.s1Hoverboard.enabled explicitly.
    setDriveHardwareEnabled(Boolean(result.data.s1Hoverboard));
  };

  estopButton?.addEventListener("click", () => postCommand("/api/estop", "Estop latch"));
  clearEstopButton?.addEventListener("click", () => postCommand("/api/estop/clear", "Estop clear"));
  enableWebControlButton?.addEventListener("click", () => postCommand("/api/web-control/enable", "Web control enable"));
  disableWebControlButton?.addEventListener("click", () => postCommand("/api/web-control/disable", "Web control disable"));
  presetButtons.forEach((button) => {
    button.addEventListener("click", () => {
      const preset = button.dataset.speedPreset;
      if (!preset) return;
      postSpeedPreset(preset);
    });
  });

  const debouncedSave = window.PAUtils.debounce(saveConfig, 300);
  speedLimitMax?.addEventListener("input", () => {
    currentSpeedLimitMax = parsePresetNumber(speedLimitMax?.value);
    if (speedLimitMaxReadout) speedLimitMaxReadout.textContent = speedLimitMax.value;
    updatePresetHighlight();
    debouncedSave();
  });
  const presetInputHandler = () => {
    updatePresetDistinctHint();
    updatePresetHighlight();
    if (speedPresetSlowReadout) speedPresetSlowReadout.textContent = speedPresetSlow?.value ?? "—";
    if (speedPresetNormalReadout) speedPresetNormalReadout.textContent = speedPresetNormal?.value ?? "—";
    if (speedPresetTurboReadout) speedPresetTurboReadout.textContent = speedPresetTurbo?.value ?? "—";
    if (presetValueSlow) presetValueSlow.textContent = speedPresetSlow?.value ?? "";
    if (presetValueNormal) presetValueNormal.textContent = speedPresetNormal?.value ?? "";
    if (presetValueTurbo) presetValueTurbo.textContent = speedPresetTurbo?.value ?? "";
    debouncedSave();
  };
  speedPresetSlow?.addEventListener("input", presetInputHandler);
  speedPresetNormal?.addEventListener("input", presetInputHandler);
  speedPresetTurbo?.addEventListener("input", presetInputHandler);
  webDriveTimeout?.addEventListener("input", debouncedSave);
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderStatus(payload);
    });

    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatusOnce().catch((error) => {
        window.PAUtils.showFeedback(controlFeedback, `Status load failed: ${window.PAApi?.messageFor(error) || "request failed"}`, "error");
      });
    }
  } else {
    const refreshFromFallback = () => {
      refreshStatusOnce().catch(() => {
        // Retry next cycle.
      });
    };

    refreshFromFallback();

    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshFromFallback();
    }, 2000);

    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshFromFallback();
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
    ["drive-configuration", loadConfig, "drive configuration"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadConfig().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/drive.js": "drive control",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  startPageLoad();
  renderHoverboard(null);
  updateDriveSliders(0, 0);
})();

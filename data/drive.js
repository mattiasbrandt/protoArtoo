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

  const estopState = document.getElementById("estop-state");
  const webControlState = document.getElementById("web-control-state");
  const failsafeSource = document.getElementById("failsafe-source");
  const driveOutput = document.getElementById("drive-output");
  const speedLimitDisplay = document.getElementById("speed-limit");
  const hbNoData    = document.getElementById("hb-no-data");
  const hbDataGrid  = document.getElementById("hb-data-grid");
  const hbBattery   = document.getElementById("hb-battery");
  const hbBoardTemp = document.getElementById("hb-board-temp");
  const hbSpeed     = document.getElementById("hb-speed");
  const hbCurrentRow = document.getElementById("hb-current-row");
  const hbCurrent   = document.getElementById("hb-current");


  const speedLimitMax = document.getElementById("speed-limit-max");
  const webDriveTimeout = document.getElementById("web-drive-timeout");
  const ch8ModeLock = document.getElementById("ch8-mode-lock");
  const configFeedback = document.getElementById("config-feedback");
  const driveDisabledCard = document.getElementById("drive-disabled-card");

  const driveButtons = document.querySelectorAll("[data-drive-speed]");

  let holdTimer = null;
  let saveTimeout = null;
  let driveHardwareEnabled = true;
  let webControlEnabled = false;
  let saveInFlight = false;
  let saveQueued = false;

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

  const formatFailsafeSource = (source) => {
    const parsed = Number(source);
    if (Number.isFinite(parsed)) {
      const label = FAILSAFE_SOURCE_LABELS[parsed] || "Unknown";
      return `${label} (${parsed})`;
    }
    if (source === undefined || source === null || source === "") return "--";
    return String(source);
  };
  const showFeedback = (el, text, level = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = level ? `feedback ${level}` : "feedback";
  };

  const debounce = (fn, ms) => (...args) => {
    window.clearTimeout(saveTimeout);
    saveTimeout = window.setTimeout(() => fn(...args), ms);
  };

  const setDriveHardwareEnabled = (enabled) => {
    driveHardwareEnabled = enabled;
    updateDriveControlsEnabled();
  };

  const updateDriveControlsEnabled = () => {
    const driveEnabled = driveHardwareEnabled && webControlEnabled;
    window.PAApi.gateControls(Array.from(driveButtons), driveEnabled);

    const controlsEnabled = driveHardwareEnabled;
    const gatedControls = [
      enableWebControlButton,
      disableWebControlButton,
    ];
    window.PAApi.gateControls(gatedControls, controlsEnabled);

    driveDisabledCard?.classList.toggle('hidden', driveHardwareEnabled);

    if (!driveEnabled) {
      stopHoldLoop();
      driveButtons.forEach((btn) => btn.classList.remove('active'));
    }
  };

  const postCommand = async (path, label) => {
    if (!window.PAApi) return;
    if (!driveHardwareEnabled && path.startsWith("/api/web-control")) {
      showFeedback(controlFeedback, `Web control unavailable: ${s1EnableInSetup}.`, "warning");
      return;
    }
    showFeedback(controlFeedback, `${label}...`);
    try {
      await window.PAApi.postForm(path, {}, { timeoutMs: 3000 });
      showFeedback(controlFeedback, `${label} sent at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(controlFeedback, `${label} failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const postDriveCommand = async (speed, steer) => {
    if (!window.PAApi) return;
    if (!webControlEnabled) {
      showFeedback(controlFeedback, "Drive unavailable: web control is disabled.", "warning");
      return;
    }
    if (!driveHardwareEnabled) {
      showFeedback(controlFeedback, `Drive controls unavailable: ${s1EnableInSetup}.`, "warning");
      return;
    }
    try {
      await window.PAApi.postForm("/api/drive", { speed: String(speed), steer: String(steer) }, { timeoutMs: 2500 });
    } catch (error) {
      showFeedback(controlFeedback, `Drive command failed: ${window.PAApi.messageFor(error)}`, "error");
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
    if (estopState) estopState.textContent = payload.estop ? "❌ Latched" : "✅ Clear";
    if (clearEstopButton) clearEstopButton.disabled = !payload.estop;
    if (webControlState) webControlState.textContent = payload.webControlEnabled ? "✅ Enabled" : "⏸️ Disabled";
    webControlEnabled = !!payload.webControlEnabled;
    updateDriveControlsEnabled();
    if (failsafeSource) failsafeSource.textContent = formatFailsafeSource(payload.failsafeSource);
    const driveSpeed = Number(payload.driveSpeed);
    const driveSteer = Number(payload.driveSteer);
    if (driveOutput) {
      const speedText = Number.isFinite(driveSpeed) ? Math.round(driveSpeed) : "--";
      const steerText = Number.isFinite(driveSteer) ? Math.round(driveSteer) : "--";
      driveOutput.textContent = `SPD ${speedText} · STR ${steerText}`;
    }
    const speedLimitScale = Number(payload.speedLimitScale);
    if (speedLimitDisplay) {
      speedLimitDisplay.textContent = Number.isFinite(speedLimitScale)
        ? `${Math.round(speedLimitScale * 100)}% (${speedLimitScale.toFixed(3)})`
        : "--";
    }
    renderHoverboard(payload.hoverboard);
  };

  const renderConfig = (payload) => {
    const drive = payload?.drive || {};
    const components = payload?.components || {};
    if (speedLimitMax) speedLimitMax.value = drive.speedLimitMax;
    if (webDriveTimeout) webDriveTimeout.value = drive.webDriveTimeoutMs;
    if (ch8ModeLock) ch8ModeLock.checked = Boolean(drive.ch8ModeLock);

    setDriveHardwareEnabled(Boolean(components.s1Hoverboard?.enabled));

    showFeedback(configFeedback, `Settings loaded at ${new Date().toLocaleTimeString()}`, "success");
  };

  const loadConfig = async () => {
    if (!window.PAApi) return;
    showFeedback(configFeedback, "Loading settings...");
    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 3000 });
      renderConfig(result.data);
    } catch (error) {
      showFeedback(configFeedback, `Failed to load settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const saveConfig = async () => {
    if (!window.PAApi) return;
    if (saveInFlight) {
      saveQueued = true;
      return;
    }

    saveInFlight = true;
    showFeedback(configFeedback, "Saving...");

    try {
      const result = await window.PAApi.postForm("/api/config", {
        speedLimitMax: speedLimitMax?.value ?? "600",
        webDriveTimeoutMs: webDriveTimeout?.value ?? "500",
        ch8ModeLock: ch8ModeLock?.checked ? "true" : "false",
      }, { timeoutMs: 3000 });

      renderConfig(result.data);
      showFeedback(configFeedback, `Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(configFeedback, `Failed to save: ${window.PAApi.messageFor(error)}`, "error");
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

  const debouncedSave = debounce(saveConfig, 300);
  speedLimitMax?.addEventListener("input", debouncedSave);
  webDriveTimeout?.addEventListener("input", debouncedSave);
  ch8ModeLock?.addEventListener("change", debouncedSave);

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderStatus(payload);
    });

    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatusOnce().catch((error) => {
        showFeedback(controlFeedback, `Status load failed: ${window.PAApi?.messageFor(error) || "request failed"}`, "error");
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

  loadConfig();
  renderHoverboard(null);
})();

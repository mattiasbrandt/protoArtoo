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

  const armControlsCard = document.getElementById("arm-controls-card");
  const armControlsContainer = document.getElementById("arm-controls-container");

  const speedLimitMax = document.getElementById("speed-limit-max");
  const webDriveTimeout = document.getElementById("web-drive-timeout");
  const ch8ModeLock = document.getElementById("ch8-mode-lock");
  const reloadConfigButton = document.getElementById("reload-config-button");
  const configFeedback = document.getElementById("config-feedback");
  const driveDisabledCard = document.getElementById("drive-disabled-card");

  const driveButtons = document.querySelectorAll("[data-drive-speed]");

  let holdTimer = null;
  let saveTimeout = null;
  let driveHardwareEnabled = true;
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
    driveDisabledCard?.classList.toggle("hidden", enabled);

    const gatedControls = [
      ...Array.from(driveButtons),
      enableWebControlButton,
      disableWebControlButton,
    ];

    gatedControls.forEach((control) => {
      if (!control) return;
      control.disabled = !enabled;
      control.setAttribute("aria-disabled", enabled ? "false" : "true");
    });

    if (!enabled) {
      stopHoldLoop();
      driveButtons.forEach((button) => button.classList.remove("active"));
    }
  };

  const postCommand = async (path, label) => {
    if (!window.PAApi) return;
    if (!driveHardwareEnabled && path.startsWith("/api/web-control")) {
      showFeedback(controlFeedback, "Web control unavailable: enable S1 — Hoverboard in Setup.", "warning");
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
    if (!driveHardwareEnabled) {
      showFeedback(controlFeedback, "Drive controls unavailable: enable S1 — Hoverboard in Setup.", "warning");
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

  const postServoCommand = async (arm, action) => {
    if (!window.PAApi) return;
    try {
      await window.PAApi.postForm("/api/servo", { arm, action }, { timeoutMs: 3000 });
      showFeedback(controlFeedback, `Arm ${arm} ${action} at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      showFeedback(controlFeedback, `Arm command failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const renderArmControls = (payload) => {
    if (!armControlsContainer || !armControlsCard) return;

    const arms = [
      { id: "arm1", name: "Left Arm", present: "arm1" in payload },
      { id: "arm2", name: "Right Arm", present: "arm2" in payload },
      { id: "aux1", name: "Aux 1", present: "aux1" in payload },
      { id: "aux2", name: "Aux 2", present: "aux2" in payload },
      { id: "aux3", name: "Aux 3", present: "aux3" in payload },
    ];
    const enabledArms = arms.filter((a) => a.present);

    if (enabledArms.length === 0) {
      armControlsCard.classList.add("hidden");
      armControlsContainer.innerHTML = "";
      return;
    }

    armControlsCard.classList.remove("hidden");
    armControlsContainer.innerHTML = enabledArms.map((arm) => `
      <div class="arm-control-row">
        <span class="arm-name">${arm.name}</span>
        <button class="btn" data-arm="${arm.id}" data-action="open" type="button">📂 Open</button>
        <button class="btn" data-arm="${arm.id}" data-action="close" type="button">📁 Close</button>
        <button class="btn accent" data-arm="${arm.id}" data-action="stop" type="button">⏹️ Stop</button>
      </div>
    `).join("");

    armControlsContainer.querySelectorAll("[data-arm]").forEach((btn) => {
      btn.addEventListener("click", () => postServoCommand(btn.dataset.arm, btn.dataset.action));
    });
  };

  const renderHoverboard = (hb) => {
    if (!hb) {
      if (hbNoData)   hbNoData.style.display   = "";
      if (hbDataGrid) hbDataGrid.style.display = "none";
      return;
    }
    if (hbNoData)   hbNoData.style.display   = "none";
    if (hbDataGrid) hbDataGrid.style.display = "";
    if (hbBattery)   hbBattery.textContent   = `${hb.batteryV.toFixed(1)} V`;
    if (hbBoardTemp) hbBoardTemp.textContent = `${hb.boardTempC.toFixed(1)} \u00b0C`;
    if (hbSpeed)     hbSpeed.textContent     = `R\u00a0${hb.speedR}\u00a0/\u00a0L\u00a0${hb.speedL} RPM`;
    const hasCurrent = Math.abs(hb.currentL) > 0.01 || Math.abs(hb.currentR) > 0.01;
    if (hbCurrentRow) hbCurrentRow.style.display = hasCurrent ? "" : "none";
    if (hbCurrent)    hbCurrent.textContent = `L\u00a0${hb.currentL.toFixed(1)}\u00a0A\u00a0/\u00a0R\u00a0${hb.currentR.toFixed(1)}\u00a0A`;
  };

  const renderStatus = (payload) => {
    if (estopState) estopState.textContent = payload.estop ? "❌ Latched" : "✅ Clear";
    if (webControlState) webControlState.textContent = payload.webControlEnabled ? "✅ Enabled" : "⏸️ Disabled";
    if (failsafeSource) failsafeSource.textContent = String(payload.failsafeSource);
    if (driveOutput) driveOutput.textContent = `${payload.driveSpeed} / ${payload.driveSteer}`;
    if (speedLimitDisplay) speedLimitDisplay.textContent = Number(payload.speedLimitScale).toFixed(3);
    renderArmControls(payload);
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
    }
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
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
  reloadConfigButton?.addEventListener("click", loadConfig);

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

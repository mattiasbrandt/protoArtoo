// =============================================================================
// drive.js
//
// Drive page controller — Safety controls, movement status, D-pad hold-to-drive,
// conditional arm servo controls, and drive settings form with auto-save.
// =============================================================================
(() => {
  // Safety control buttons
  const estopButton = document.getElementById("estop-button");
  const clearEstopButton = document.getElementById("clear-estop-button");
  const enableWebControlButton = document.getElementById("enable-web-control-button");
  const disableWebControlButton = document.getElementById("disable-web-control-button");
  const controlFeedback = document.getElementById("control-feedback");

  // Movement status elements
  const estopState = document.getElementById("estop-state");
  const webControlState = document.getElementById("web-control-state");
  const failsafeSource = document.getElementById("failsafe-source");
  const driveOutput = document.getElementById("drive-output");
  const speedLimitDisplay = document.getElementById("speed-limit");

  // Arm controls (conditional)
  const armControlsCard = document.getElementById("arm-controls-card");
  const armControlsContainer = document.getElementById("arm-controls-container");

  // Drive settings form
  const speedLimitMax = document.getElementById("speed-limit-max");
  const webDriveTimeout = document.getElementById("web-drive-timeout");
  const ch8ModeLock = document.getElementById("ch8-mode-lock");
  const reloadConfigButton = document.getElementById("reload-config-button");
  const configFeedback = document.getElementById("config-feedback");

  const driveButtons = document.querySelectorAll("[data-drive-speed]");
  let holdTimer = null;
  let pollTimer = null;

  // Debounce utility for auto-save
  let saveTimeout = null;
  const debounce = (fn, ms) => {
    return (...args) => {
      clearTimeout(saveTimeout);
      saveTimeout = setTimeout(() => fn(...args), ms);
    };
  };

  // -------------------------------------------------------------------------
  // Safety commands (estop / web control)
  // -------------------------------------------------------------------------
  const postCommand = async (path, label) => {
    if (!controlFeedback) return;
    controlFeedback.textContent = `⏳ ${label}...`;
    controlFeedback.className = "feedback";
    try {
      const response = await fetch(path, { method: "POST" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      controlFeedback.textContent = `✅ ${label} sent at ${new Date().toLocaleTimeString()}`;
      controlFeedback.className = "feedback success";
    } catch (_error) {
      controlFeedback.textContent = `❌ ${label} failed`;
      controlFeedback.className = "feedback error";
    }
  };

  if (estopButton) {
    estopButton.addEventListener("click", () => postCommand("/api/estop", "Estop latch"));
  }
  if (clearEstopButton) {
    clearEstopButton.addEventListener("click", () => postCommand("/api/estop/clear", "Estop clear"));
  }
  if (enableWebControlButton) {
    enableWebControlButton.addEventListener("click", () =>
      postCommand("/api/web-control/enable", "Web control enable"));
  }
  if (disableWebControlButton) {
    disableWebControlButton.addEventListener("click", () =>
      postCommand("/api/web-control/disable", "Web control disable"));
  }

  // -------------------------------------------------------------------------
  // D-pad hold-to-drive
  // -------------------------------------------------------------------------
  const postDriveCommand = async (speed, steer) => {
    try {
      const body = new URLSearchParams({ speed: String(speed), steer: String(steer) });
      const response = await fetch("/api/drive", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
    } catch (_error) {
      if (controlFeedback) {
        controlFeedback.textContent = "❌ Drive command failed";
        controlFeedback.className = "feedback error";
      }
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
    // Send repeated commands at 150 ms so the 500 ms web timeout can't expire mid-hold
    holdTimer = window.setInterval(() => postDriveCommand(speed, steer), 150);
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

  // -------------------------------------------------------------------------
  // Arm servo controls (rendered conditionally from /api/status payload)
  // -------------------------------------------------------------------------
  const postServoCommand = async (arm, action) => {
    try {
      const body = new URLSearchParams({ arm, action });
      const response = await fetch("/api/servo", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      if (controlFeedback) {
        controlFeedback.textContent = `✅ Arm ${arm} ${action} at ${new Date().toLocaleTimeString()}`;
        controlFeedback.className = "feedback success";
      }
    } catch (_error) {
      if (controlFeedback) {
        controlFeedback.textContent = "❌ Arm command failed";
        controlFeedback.className = "feedback error";
      }
    }
  };

  const renderArmControls = (payload) => {
    if (!armControlsContainer || !armControlsCard) return;

    const arms = [
      { id: "arm1", name: "Left Arm",   present: "arm1" in payload },
      { id: "arm2", name: "Right Arm",  present: "arm2" in payload },
      { id: "aux1", name: "Aux 1",      present: "aux1" in payload },
      { id: "aux2", name: "Aux 2",      present: "aux2" in payload },
      { id: "aux3", name: "Aux 3",      present: "aux3" in payload },
    ];
    const enabledArms = arms.filter((a) => a.present);

    if (enabledArms.length === 0) {
      armControlsCard.classList.add("hidden");
      return;
    }

    armControlsCard.classList.remove("hidden");
    armControlsContainer.innerHTML = enabledArms.map((arm) => `
      <div class="arm-control-row">
        <span class="arm-name">${arm.name}</span>
        <button class="btn" data-arm="${arm.id}" data-action="open"  type="button">📂 Open</button>
        <button class="btn" data-arm="${arm.id}" data-action="close" type="button">📁 Close</button>
        <button class="btn accent" data-arm="${arm.id}" data-action="stop" type="button">⏹️ Stop</button>
      </div>
    `).join("");
    armControlsContainer.querySelectorAll("[data-arm]").forEach((btn) => {
      btn.addEventListener("click", () =>
        postServoCommand(btn.dataset.arm, btn.dataset.action));
    });
  };

  // -------------------------------------------------------------------------
  // Movement status polling
  // -------------------------------------------------------------------------
  const renderStatus = (payload) => {
    if (estopState)       estopState.textContent       = payload.estop ? "❌ Latched" : "✅ Clear";
    if (webControlState)  webControlState.textContent  = payload.webControlEnabled ? "✅ Enabled" : "⏸️ Disabled";
    if (failsafeSource)   failsafeSource.textContent   = String(payload.failsafeSource);
    if (driveOutput)      driveOutput.textContent      = `${payload.driveSpeed} / ${payload.driveSteer}`;
    if (speedLimitDisplay) speedLimitDisplay.textContent = Number(payload.speedLimitScale).toFixed(3);
    renderArmControls(payload);
  };

  const poll = async () => {
    try {
      const response = await fetch("/api/status", { cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      renderStatus(await response.json());
    } catch (_error) {}
  };

  // -------------------------------------------------------------------------
  // Drive settings with auto-save
  // -------------------------------------------------------------------------
  const driveDisabledCard = document.getElementById("drive-disabled-card");

  const renderConfig = (payload) => {
    if (speedLimitMax)   speedLimitMax.value      = payload.speedLimitMax;
    if (webDriveTimeout) webDriveTimeout.value    = payload.webDriveTimeoutMs;
    if (ch8ModeLock)     ch8ModeLock.checked      = Boolean(payload.ch8ModeLock);
    if (configFeedback) {
      configFeedback.textContent = `Settings loaded at ${new Date().toLocaleTimeString()}`;
      configFeedback.className = "feedback success";
    }
    if (driveDisabledCard) {
      driveDisabledCard.classList.toggle("hidden", Boolean(payload.enableS1Hoverboard));
    }
  };

  const loadConfig = async () => {
    if (configFeedback) {
      configFeedback.textContent = "Loading settings...";
      configFeedback.className = "feedback";
    }
    try {
      const response = await fetch("/api/config", { cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      renderConfig(await response.json());
    } catch (_error) {
      if (configFeedback) {
        configFeedback.textContent = "Failed to load settings";
        configFeedback.className = "feedback error";
      }
    }
  };

  // Auto-save function
  const saveConfig = async () => {
    if (configFeedback) {
      configFeedback.textContent = "Saving...";
      configFeedback.className = "feedback";
    }
    try {
      const body = new URLSearchParams({
        speedLimitMax:     speedLimitMax?.value ?? "600",
        webDriveTimeoutMs: webDriveTimeout?.value ?? "500",
        ch8ModeLock:       ch8ModeLock?.checked ? "true" : "false",
      });
      const response = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      if (!response.ok) {
        const errorBody = await response.json().catch(() => null);
        throw new Error(errorBody?.error || `HTTP ${response.status}`);
      }
      renderConfig(await response.json());
      if (configFeedback) {
        configFeedback.textContent = `✓ Saved at ${new Date().toLocaleTimeString()}`;
        configFeedback.className = "feedback success";
      }
    } catch (error) {
      if (configFeedback) {
        configFeedback.textContent = error instanceof Error ? `❌ ${error.message}` : "❌ Failed to save";
        configFeedback.className = "feedback error";
      }
    }
  };

  const debouncedSave = debounce(saveConfig, 300);

  // Attach auto-save listeners
  if (speedLimitMax) {
    speedLimitMax.addEventListener("input", debouncedSave);
  }
  if (webDriveTimeout) {
    webDriveTimeout.addEventListener("input", debouncedSave);
  }
  if (ch8ModeLock) {
    ch8ModeLock.addEventListener("change", debouncedSave);
  }

  if (reloadConfigButton) {
    reloadConfigButton.addEventListener("click", loadConfig);
  }

  // -------------------------------------------------------------------------
  // Init
  // -------------------------------------------------------------------------
  poll();
  pollTimer = window.setInterval(poll, 1000);
  loadConfig();
})();

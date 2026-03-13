(() => {
  const estop = document.getElementById("estop-state");
  const webControl = document.getElementById("web-control-state");
  const failsafe = document.getElementById("failsafe-source");
  const drive = document.getElementById("drive-output");
  const speedLimit = document.getElementById("speed-limit");
  const pollState = document.getElementById("poll-state");
  const pollTime = document.getElementById("poll-time");
  const estopButton = document.getElementById("estop-button");
  const clearEstopButton = document.getElementById("clear-estop-button");
  const enableWebControlButton = document.getElementById("enable-web-control-button");
  const disableWebControlButton = document.getElementById("disable-web-control-button");
  const controlFeedback = document.getElementById("control-feedback");
  const healthSummary = document.getElementById("health-summary");
  const logConsole = document.getElementById("log-console");
  const manualInput = document.getElementById("manual-cmd");
  const manualSend = document.getElementById("manual-send");
  const manualFeedback = document.getElementById("manual-feedback");
  const driveButtons = document.querySelectorAll("[data-drive-speed]");
  let holdTimer = null;
  let pollTimer = null;

  if (
    !estop ||
    !webControl ||
    !failsafe ||
    !drive ||
    !speedLimit ||
    !estopButton ||
    !clearEstopButton ||
    !enableWebControlButton ||
    !disableWebControlButton ||
    !controlFeedback ||
    !healthSummary ||
    !logConsole ||
    !manualInput ||
    !manualSend ||
    !manualFeedback
  ) {
    return;
  }

  const setIndicator = (id, state) => {
    const el = document.getElementById(id);
    if (!el) {
      return;
    }
    el.className = `indicator ${state}`;
  };

  const renderHealth = async () => {
    try {
      const response = await fetch("/api/status", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const payload = await response.json();
      setIndicator("h-estop", payload.estop ? "warn" : "ok");
      setIndicator("h-sbus", payload.sbusSignalLost || payload.sbusHwFailsafe ? "fail" : "ok");
      setIndicator("h-web", payload.webControlEnabled ? "ok" : "warn");
      setIndicator("h-wifi", payload.wifiConnected ? "ok" : "warn");
      setIndicator("h-fs", payload.littleFsReady ? "ok" : "fail");
      setIndicator("h-heap", payload.heapFree > 120000 ? "ok" : payload.heapFree > 80000 ? "warn" : "fail");

      const heapFreeKb = Math.round(payload.heapFree / 1024);
      const heapMinKb = Math.round(payload.heapMin / 1024);

      let heapLabel = "Healthy";
      let heapColor = "var(--success)";
      if (heapFreeKb < 80) {
        heapLabel = "Critical";
        heapColor = "var(--danger)";
      } else if (heapFreeKb < 120) {
        heapLabel = "Low";
        heapColor = "var(--warning)";
      }

      let heapMinLabel = "Good";
      let heapMinColor = "var(--success)";
      if (heapMinKb < 64) {
        heapMinLabel = "Critical";
        heapMinColor = "var(--danger)";
      } else if (heapMinKb < 96) {
        heapMinLabel = "Watch";
        heapMinColor = "var(--warning)";
      }

      let wifiQuality = "Unknown";
      let wifiColor = "var(--danger)";
      if (payload.wifiClientConnected && payload.wifiRssi !== 0) {
        if (payload.wifiRssi >= -67) {
          wifiQuality = `Excellent (${payload.wifiRssi} dBm)`;
          wifiColor = "var(--success)";
        } else if (payload.wifiRssi >= -75) {
          wifiQuality = `Good (${payload.wifiRssi} dBm)`;
          wifiColor = "var(--success)";
        } else if (payload.wifiRssi >= -85) {
          wifiQuality = `Fair (${payload.wifiRssi} dBm)`;
          wifiColor = "var(--warning)";
        } else {
          wifiQuality = `Poor (${payload.wifiRssi} dBm)`;
          wifiColor = "var(--danger)";
        }
      }

      healthSummary.innerHTML = `Heap: <span style="color:${heapColor};font-weight:700">${heapFreeKb} KB (${heapLabel})</span><br>Heap min: <span style="color:${heapMinColor};font-weight:700">${heapMinKb} KB (${heapMinLabel})</span><br>WiFi quality: <span style="color:${wifiColor};font-weight:700">${wifiQuality}</span>`;
    } catch (_error) {
      healthSummary.textContent = "Failed to load health";
    }
  };

  const loadLogs = async () => {
    try {
      const response = await fetch("/api/logs", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      logConsole.textContent = await response.text();
      logConsole.scrollTop = logConsole.scrollHeight;
    } catch (_error) {
      logConsole.textContent = "Failed to load logs";
    }
  };

  const sendManualCommand = async () => {
    const command = manualInput.value.trim();
    if (!command) {
      manualFeedback.textContent = "Enter a command first.";
      return;
    }

    manualFeedback.textContent = `Sending ${command}...`;
    try {
      const body = new URLSearchParams({ command });
      const response = await fetch("/api/manual-command", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
        },
        body,
      });

      if (!response.ok) {
        const errorBody = await response.json().catch(() => null);
        throw new Error(errorBody?.error || `HTTP ${response.status}`);
      }

      manualFeedback.textContent = `Sent ${command} at ${new Date().toLocaleTimeString()}`;
      manualInput.value = "";
      refresh();
      renderHealth();
      loadLogs();
    } catch (error) {
      manualFeedback.textContent = error instanceof Error ? error.message : "Command failed";
    }
  };

  const renderStatus = (payload) => {
    estop.textContent = payload.estop ? "Latched" : "Clear";
    webControl.textContent = payload.webControlEnabled ? "Enabled" : "Disabled";
    failsafe.textContent = String(payload.failsafeSource);
    drive.textContent = `${payload.driveSpeed} / ${payload.driveSteer}`;
    speedLimit.textContent = Number(payload.speedLimitScale).toFixed(3);
    if (pollState) {
      pollState.innerHTML = "Polling <code>GET /api/status</code>";
    }
    if (pollTime) {
      pollTime.textContent = `Last update ${new Date().toLocaleTimeString()}`;
    }
  };

  const renderError = () => {
    if (pollState) {
      pollState.innerHTML = "Falling back to <code>GET /api/status</code>";
    }
  };

  const postCommand = async (path, label) => {
    controlFeedback.textContent = `${label}...`;

    try {
      const response = await fetch(path, { method: "POST" });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      controlFeedback.textContent = `${label} sent at ${new Date().toLocaleTimeString()}`;
    } catch (_error) {
      controlFeedback.textContent = `${label} failed`;
    }
  };

  const postDriveCommand = async (speed, steer) => {
    controlFeedback.textContent = `Drive ${speed}/${steer}...`;

    try {
      const body = new URLSearchParams({
        speed: String(speed),
        steer: String(steer),
      });
      const response = await fetch("/api/drive", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
        },
        body,
      });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      controlFeedback.textContent = `Drive ${speed}/${steer} sent at ${new Date().toLocaleTimeString()}`;
    } catch (_error) {
      controlFeedback.textContent = `Drive ${speed}/${steer} failed`;
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
    }, 150);
  };

  const startPolling = () => {
    if (pollTimer !== null) {
      return;
    }

    refresh();
    pollTimer = window.setInterval(refresh, 1000);
  };

  const refresh = async () => {
    try {
      const response = await fetch("/api/status", { cache: "no-store" });

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      renderStatus(await response.json());
      renderHealth();
    } catch (_error) {
      renderError();
    }
  };

  estopButton.addEventListener("click", () => {
    postCommand("/api/estop", "Estop latch");
  });

  clearEstopButton.addEventListener("click", () => {
    postCommand("/api/estop/clear", "Estop clear");
  });

  enableWebControlButton.addEventListener("click", () => {
    postCommand("/api/web-control/enable", "Web control enable");
  });

  disableWebControlButton.addEventListener("click", () => {
    postCommand("/api/web-control/disable", "Web control disable");
  });

  driveButtons.forEach((button) => {
    const speed = button.dataset.driveSpeed;
    const steer = button.dataset.driveSteer;
    const release = () => {
      stopHoldLoop();
      postDriveCommand(0, 0);
    };

    button.addEventListener("pointerdown", () => {
      startHoldLoop(speed, steer);
    });

    button.addEventListener("pointerup", release);
    button.addEventListener("pointerleave", release);
    button.addEventListener("pointercancel", release);
  });

  manualSend.addEventListener("click", sendManualCommand);
  manualInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      sendManualCommand();
    }
  });

  loadLogs();
  startPolling();
})();

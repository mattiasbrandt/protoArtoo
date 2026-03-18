// =============================================================================
// setup.js
//
// Setup page controller — hardware component enable/disable toggles and
// servo/AUX component type selectors. Auto-saves on every change.
// =============================================================================
(() => {
  const featureToggles = {
    arm1:         { input: document.getElementById("enable-arm1"),         status: document.getElementById("status-arm1") },
    arm2:         { input: document.getElementById("enable-arm2"),         status: document.getElementById("status-arm2") },
    aux1:         { input: document.getElementById("enable-aux1"),         status: document.getElementById("status-aux1") },
    aux2:         { input: document.getElementById("enable-aux2"),         status: document.getElementById("status-aux2") },
    aux3:         { input: document.getElementById("enable-aux3"),         status: document.getElementById("status-aux3") },
    dome:         { input: document.getElementById("enable-dome"),         status: document.getElementById("status-dome") },
    rcCh1:        { input: document.getElementById("enable-rc-ch1"),       status: document.getElementById("status-rc-ch1") },
    rcCh2:        { input: document.getElementById("enable-rc-ch2"),       status: document.getElementById("status-rc-ch2") },
    rcCh3:        { input: document.getElementById("enable-rc-ch3"),       status: document.getElementById("status-rc-ch3") },
    rcCh4:        { input: document.getElementById("enable-rc-ch4"),       status: document.getElementById("status-rc-ch4") },
    rcCh5:        { input: document.getElementById("enable-rc-ch5"),       status: document.getElementById("status-rc-ch5") },
    rcCh6:        { input: document.getElementById("enable-rc-ch6"),       status: document.getElementById("status-rc-ch6") },
    s1Hoverboard: { input: document.getElementById("enable-s1-hoverboard"), status: document.getElementById("status-s1-hoverboard") },
    s2Sound:      { input: document.getElementById("enable-s2-sound"),     status: document.getElementById("status-s2-sound") },
    s3DomeCtrl:   { input: document.getElementById("enable-s3-dome-ctrl"), status: document.getElementById("status-s3-dome-ctrl") },
  };

  // Component type selects — maps API key to select element
  const typeSelects = {
    arm1Type: document.getElementById("type-arm1"),
    arm2Type: document.getElementById("type-arm2"),
    aux1Type: document.getElementById("type-aux1"),
    aux2Type: document.getElementById("type-aux2"),
    aux3Type: document.getElementById("type-aux3"),
  };

  const featureFeedback = document.getElementById("feature-feedback");
  const logLevelSelect = document.getElementById("log-level-select");
  const diagFeedback = document.getElementById("diag-feedback");

  // Map from API payload key to featureToggles key
  const TOGGLE_KEY_MAP = {
    enableArm1:        "arm1",
    enableArm2:        "arm2",
    enableAux1:        "aux1",
    enableAux2:        "aux2",
    enableAux3:        "aux3",
    enableDome:        "dome",
    enableRcCh1:       "rcCh1",
    enableRcCh2:       "rcCh2",
    enableRcCh3:       "rcCh3",
    enableRcCh4:       "rcCh4",
    enableRcCh5:       "rcCh5",
    enableRcCh6:       "rcCh6",
    enableS1Hoverboard: "s1Hoverboard",
    enableS2Sound:     "s2Sound",
    enableS3DomeCtrl:  "s3DomeCtrl",
  };

  // Debounce utility for auto-save
  let saveTimeout = null;
  const debounce = (fn, ms) => {
    return (...args) => {
      clearTimeout(saveTimeout);
      saveTimeout = setTimeout(() => fn(...args), ms);
    };
  };

  const updateToggleStatus = (key) => {
    const toggle = featureToggles[key];
    if (!toggle || !toggle.input || !toggle.status) return;
    toggle.status.textContent = toggle.input.checked ? "Enabled" : "Disabled";
    toggle.status.style.color = toggle.input.checked ? "var(--success)" : "var(--text-dim)";
  };

  const renderFeatures = (payload) => {
    Object.entries(TOGGLE_KEY_MAP).forEach(([payloadKey, toggleKey]) => {
      const toggle = featureToggles[toggleKey];
      if (toggle && toggle.input && payload[payloadKey] !== undefined) {
        toggle.input.checked = Boolean(payload[payloadKey]);
        updateToggleStatus(toggleKey);
      }
    });
    // Populate type selects from API payload (convert numeric API values to string option values)
    const typeValueMap = ["none", "mg996r", "mg90s", "rgb"];
    Object.entries(typeSelects).forEach(([apiKey, select]) => {
      if (select && payload[apiKey] !== undefined) {
        const numericValue = parseInt(payload[apiKey], 10);
        select.value = typeValueMap[numericValue] || "none";
      }
    });
    if (featureFeedback) {
      featureFeedback.textContent = `Components loaded at ${new Date().toLocaleTimeString()}`;
      featureFeedback.className = "feedback success";
    }
    // Populate log level dropdown from config
    if (logLevelSelect && payload.logLevel !== undefined) {
      logLevelSelect.value = String(payload.logLevel);
    }
  };

  const loadFeatures = async () => {
    if (featureFeedback) {
      featureFeedback.textContent = "Loading component settings...";
      featureFeedback.className = "feedback";
    }
    try {
      const response = await fetch("/api/config", { cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      renderFeatures(await response.json());
    } catch (_error) {
      if (featureFeedback) {
        featureFeedback.textContent = "Failed to load component settings";
        featureFeedback.className = "feedback error";
      }
    }
  };

  // Auto-save function
  const saveFeatures = async () => {
    if (featureFeedback) {
      featureFeedback.textContent = "Saving...";
      featureFeedback.className = "feedback";
    }
    try {
      const body = new URLSearchParams();
      Object.entries(featureToggles).forEach(([key, toggle]) => {
        if (toggle.input) {
          const paramKey = "enable" + key.charAt(0).toUpperCase() + key.slice(1);
          body.set(paramKey, toggle.input.checked ? "true" : "false");
        }
      });
      Object.entries(typeSelects).forEach(([apiKey, select]) => {
        if (select) body.set(apiKey, select.value);
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
      const data = await response.json();
      renderFeatures(data);
      if (featureFeedback) {
        featureFeedback.textContent = `Saved at ${new Date().toLocaleTimeString()}`;
        featureFeedback.className = "feedback success";
      }
    } catch (error) {
      if (featureFeedback) {
        featureFeedback.textContent = error instanceof Error
          ? error.message : "Failed to save";
        featureFeedback.className = "feedback error";
      }
    }
  };

  const debouncedSave = debounce(saveFeatures, 300);

  // Attach listeners to all toggles and selects
  Object.keys(featureToggles).forEach((key) => {
    const toggle = featureToggles[key];
    if (toggle.input) {
      toggle.input.addEventListener("change", () => {
        updateToggleStatus(key);
        debouncedSave();
      });
    }
  });

  Object.values(typeSelects).forEach((select) => {
    if (select) {
      select.addEventListener("change", debouncedSave);
    }
  });

  // Reboot functionality
  const rebootButton = document.getElementById("reboot-button");
  const rebootFeedback = document.getElementById("reboot-feedback");

  const handleReboot = async () => {
    if (!confirm("Reboot the controller? The web interface will be unavailable for about 10 seconds.")) {
      return;
    }
    if (rebootFeedback) {
      rebootFeedback.textContent = "Sending reboot command...";
      rebootFeedback.className = "feedback";
    }
    try {
      const response = await fetch("/api/reboot", { method: "POST" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      if (rebootFeedback) {
        rebootFeedback.textContent = "Reboot command sent. Wait ~10 seconds and refresh...";
        rebootFeedback.className = "feedback success";
      }
      // Start countdown
      let seconds = 12;
      const countdown = setInterval(() => {
        seconds--;
        if (rebootFeedback && seconds > 0) {
          rebootFeedback.textContent = `Rebooting... ${seconds}s until ready`;
        } else {
          clearInterval(countdown);
          if (rebootFeedback) {
            rebootFeedback.textContent = "Controller should be back online. Refresh the page.";
          }
        }
      }, 1000);
    } catch (error) {
      if (rebootFeedback) {
        rebootFeedback.textContent = error instanceof Error ? error.message : "Failed to send reboot command";
        rebootFeedback.className = "feedback error";
      }
    }
  };

  if (rebootButton) {
    rebootButton.addEventListener("click", handleReboot);
  }

  // --- Diagnostics: log level selector ---
  const saveLogLevel = async () => {
    if (!logLevelSelect) return;
    if (diagFeedback) {
      diagFeedback.textContent = "Saving...";
      diagFeedback.className = "feedback";
    }
    try {
      const body = new URLSearchParams();
      body.set("logLevel", logLevelSelect.value);
      const response = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body,
      });
      if (!response.ok) {
        const err = await response.json().catch(() => null);
        throw new Error(err?.error || `HTTP ${response.status}`);
      }
      const data = await response.json();
      if (data.logLevel !== undefined) logLevelSelect.value = String(data.logLevel);
      if (diagFeedback) {
        diagFeedback.textContent = `Log level saved at ${new Date().toLocaleTimeString()}`;
        diagFeedback.className = "feedback success";
      }
    } catch (error) {
      if (diagFeedback) {
          diagFeedback.textContent = error instanceof Error ? error.message : "Failed to save";
        diagFeedback.className = "feedback error";
      }
    }
  };

  if (logLevelSelect) {
    logLevelSelect.addEventListener("change", saveLogLevel);
  }

  loadFeatures();

  // --- Serial connection status ---
  const serialS1 = document.getElementById("serial-s1-state");
  const serialS2 = document.getElementById("serial-s2-state");
  const serialS3 = document.getElementById("serial-s3-state");
  const serialStatusLine = document.getElementById("serial-status-line");

  const loadSerialStatus = async () => {
    try {
      const res = await fetch("/api/status", { cache: "no-store" });
      if (!res.ok) return;
      const d = await res.json();
      if (serialS1) {
        serialS1.textContent = !d.s1Hoverboard ? "Disabled"
          : d.s1Hoverboard.state === "commanding" ? "Active" : "Enabled / Idle";
        serialS1.style.color = d.s1Hoverboard ? "var(--success)" : "var(--text-dim)";
      }
      if (serialS2) {
        serialS2.textContent = !d.s2Sound ? "Disabled"
          : d.s2Sound.state === "playing" ? "Playing" : "Enabled / Idle";
        serialS2.style.color = d.s2Sound ? "var(--success)" : "var(--text-dim)";
      }
      const s2DriverEl = document.getElementById("s2-driver-label");
      if (s2DriverEl) {
        s2DriverEl.textContent = d.s2Sound?.driver || "";
      }
      if (serialS3) {
        const dl = d.dome_link;
        if (!dl || dl.state === "disabled") {
          serialS3.textContent = "Disabled";
          serialS3.style.color = "var(--text-dim)";
        } else if (dl.state === "connected") {
          serialS3.textContent = `Connected (hb rx ${dl.hb_rx} / tx ${dl.hb_tx})`;
          serialS3.style.color = "var(--success)";
        } else if (dl.state === "lost") {
          serialS3.textContent = `Lost — last seen ${dl.last_rx_ms} ms ago`;
          serialS3.style.color = "var(--danger)";
        } else {
          serialS3.textContent = "Waiting for dome heartbeat";
          serialS3.style.color = "var(--warning)";
        }
      }
      if (serialStatusLine) {
        serialStatusLine.textContent = `Updated ${new Date().toLocaleTimeString()}`;
        serialStatusLine.className = "feedback success";
      }
    } catch (_e) {
      if (serialStatusLine) {
        serialStatusLine.textContent = "Status unavailable";
        serialStatusLine.className = "feedback error";
      }
    }
  };

  loadSerialStatus();
  window.setInterval(loadSerialStatus, 5000);
})();

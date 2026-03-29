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
    const components = payload?.components || {};
    const system = payload?.system || {};

    const togglePayload = {
      enableArm1: components.arm1?.enabled,
      enableArm2: components.arm2?.enabled,
      enableAux1: components.aux1?.enabled,
      enableAux2: components.aux2?.enabled,
      enableAux3: components.aux3?.enabled,
      enableDome: components.dome?.enabled,
      enableRcCh1: components.rcCh1?.enabled,
      enableRcCh2: components.rcCh2?.enabled,
      enableRcCh3: components.rcCh3?.enabled,
      enableRcCh4: components.rcCh4?.enabled,
      enableRcCh5: components.rcCh5?.enabled,
      enableRcCh6: components.rcCh6?.enabled,
      enableS1Hoverboard: components.s1Hoverboard?.enabled,
      enableS2Sound: components.s2Sound?.enabled,
      enableS3DomeCtrl: components.s3DomeCtrl?.enabled,
    };

    Object.entries(TOGGLE_KEY_MAP).forEach(([payloadKey, toggleKey]) => {
      const toggle = featureToggles[toggleKey];
      if (toggle && toggle.input && togglePayload[payloadKey] !== undefined) {
        toggle.input.checked = Boolean(togglePayload[payloadKey]);
        updateToggleStatus(toggleKey);
      }
    });

    const typePayload = {
      arm1Type: components.arm1?.type,
      arm2Type: components.arm2?.type,
      aux1Type: components.aux1?.type,
      aux2Type: components.aux2?.type,
      aux3Type: components.aux3?.type,
    };
    Object.entries(typeSelects).forEach(([apiKey, select]) => {
      if (select && typePayload[apiKey] !== undefined) {
        select.value = String(typePayload[apiKey] || "none");
      }
    });

    if (featureFeedback) {
      featureFeedback.textContent = `Components loaded at ${new Date().toLocaleTimeString()}`;
      featureFeedback.className = "feedback success";
    }
    if (logLevelSelect && system.logLevel !== undefined) {
      logLevelSelect.value = String(system.logLevel);
    }
  };

  const loadFeatures = async () => {
    if (!window.PAApi) return;
    if (featureFeedback) {
      featureFeedback.textContent = "Loading component settings...";
      featureFeedback.className = "feedback";
    }
    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
      renderFeatures(result.data);
    } catch (error) {
      console.error("[setup] loadFeatures failed:", error);
      if (featureFeedback) {
        featureFeedback.textContent = `Failed to load component settings: ${window.PAApi.messageFor(error)}`;
        featureFeedback.className = "feedback error";
      }
    }
  };

  // Auto-save function
  const saveFeatures = async () => {
    if (!window.PAApi) return;
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
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      renderFeatures(result.data);
      if (featureFeedback) {
        featureFeedback.textContent = `Saved at ${new Date().toLocaleTimeString()}`;
        featureFeedback.className = "feedback success";
      }
    } catch (error) {
      console.error("[setup] saveFeatures failed:", error);
      if (featureFeedback) {
        featureFeedback.textContent = window.PAApi.messageFor(error);
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
    if (!window.PAApi) return;
    if (rebootFeedback) {
      rebootFeedback.textContent = "Sending reboot command...";
      rebootFeedback.className = "feedback";
    }
    try {
      await window.PAApi.postForm("/api/reboot", {}, { timeoutMs: 5000 });
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
      console.error("[setup] handleReboot failed:", error);
      if (rebootFeedback) {
        rebootFeedback.textContent = window.PAApi.messageFor(error);
        rebootFeedback.className = "feedback error";
      }
    }
  };

  if (rebootButton) {
    rebootButton.addEventListener("click", handleReboot);
  }

  // --- Diagnostics: log level selector ---
  const saveLogLevel = async () => {
    if (!logLevelSelect || !window.PAApi) return;
    if (diagFeedback) {
      diagFeedback.textContent = "Saving...";
      diagFeedback.className = "feedback";
    }
    try {
      const body = new URLSearchParams();
      body.set("logLevel", logLevelSelect.value);
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      if (result.data?.system?.logLevel !== undefined) {
        logLevelSelect.value = String(result.data.system.logLevel);
      }
      if (diagFeedback) {
        diagFeedback.textContent = `Log level saved at ${new Date().toLocaleTimeString()}`;
        diagFeedback.className = "feedback success";
      }
    } catch (error) {
      console.error("[setup] saveLogLevel failed:", error);
      if (diagFeedback) {
        diagFeedback.textContent = window.PAApi.messageFor(error);
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
  const diagUptime = document.getElementById("diag-uptime");
  const diagHeapFree = document.getElementById("diag-heap-free");
  const diagHeapMin = document.getElementById("diag-heap-min");
  const diagHeapLargest = document.getElementById("diag-heap-largest");
  const diagMemoryNote = document.getElementById("diag-memory-note");

  const formatUptime = (uptimeMs) => {
    const totalSeconds = Math.floor(Number(uptimeMs || 0) / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    return `${hours}h ${minutes}m ${seconds}s`;
  };

  const renderSerialStatus = (d) => {
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
    if (diagUptime) {
      diagUptime.textContent = formatUptime(d.uptimeMs);
      diagUptime.style.color = "var(--success)";
    }

    const heapFreeKb = Math.round((d.heapFree || 0) / 1024);
    const heapMinKb = Math.round((d.heapMin || 0) / 1024);
    const hasLargest = d.heapLargestBlock !== undefined && d.heapLargestBlock !== null;
    const heapLargestKb = hasLargest ? Math.round(d.heapLargestBlock / 1024) : null;

    const heapFreeState = heapFreeKb < 80 ? "critical" : heapFreeKb < 120 ? "watch" : "good";
    const heapMinState = heapMinKb < 64 ? "critical" : heapMinKb < 96 ? "watch" : "good";
    const heapLargestState = !hasLargest ? "na" : heapLargestKb < 30 ? "critical" : heapLargestKb < 50 ? "watch" : "good";

    const colorForState = (state) =>
      state === "critical" ? "var(--danger)"
        : state === "watch" ? "var(--warning)"
          : state === "na" ? "var(--text-dim)" : "var(--success)";

    if (diagHeapFree) {
      const suffix = heapFreeState === "critical" ? "❌ Critical" : heapFreeState === "watch" ? "⚠️ Watch" : "✅ Good";
      diagHeapFree.textContent = `${heapFreeKb} KB ${suffix}`;
      diagHeapFree.style.color = colorForState(heapFreeState);
    }
    if (diagHeapMin) {
      const suffix = heapMinState === "critical" ? "❌ Critical" : heapMinState === "watch" ? "⚠️ Watch" : "✅ Good";
      diagHeapMin.textContent = `${heapMinKb} KB ${suffix}`;
      diagHeapMin.style.color = colorForState(heapMinState);
    }
    if (diagHeapLargest) {
      if (!hasLargest) {
        diagHeapLargest.textContent = "N/A";
      } else {
        const suffix = heapLargestState === "critical" ? "❌ Fragmented" : heapLargestState === "watch" ? "⚠️ Watch" : "✅ Good";
        diagHeapLargest.textContent = `${heapLargestKb} KB ${suffix}`;
      }
      diagHeapLargest.style.color = colorForState(heapLargestState);
    }
    if (diagMemoryNote) {
      diagMemoryNote.textContent = `Memory Min is a historical low-water mark since boot; current low-water mark is ${heapMinKb} KB.`;
    }

    if (serialStatusLine) {
      serialStatusLine.textContent = `Updated ${new Date().toLocaleTimeString()}`;
      serialStatusLine.className = "feedback success";
    }
  };

  const refreshSerialStatus = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
      renderSerialStatus(result.data);
    } catch (error) {
      console.warn("[setup] refreshSerialStatus failed:", error);
      if (serialStatusLine) {
        serialStatusLine.textContent = "Status unavailable";
        serialStatusLine.className = "feedback error";
      }
    }
  };

  // SSE-first serial status updates with visibility-aware fallback polling.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderSerialStatus(payload);
    });
    // One-shot fetch if SSE hasn't delivered a status frame yet.
    if (!window.PAStatusStream.getLastStatus()) {
      refreshSerialStatus().catch(() => {});
    }
  } else {
    // Fallback: poll every 5 s, suspended while the tab is hidden.
    refreshSerialStatus().catch(() => {});
    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshSerialStatus().catch(() => {});
    }, 5000);
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshSerialStatus().catch(() => {});
      }
    });
  }
})();

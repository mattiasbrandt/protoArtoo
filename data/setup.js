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
  const AUX_RGB_SELECT_KEYS = ["aux1Type", "aux2Type", "aux3Type"];
  const AUX_RGB_PIN_BY_KEY = { aux1Type: 1, aux2Type: 2, aux3Type: 3 };
  const AUX_RGB_LABEL_BY_KEY = { aux1Type: "AUX1", aux2Type: "AUX2", aux3Type: "AUX3" };
  const AUX_RGB_TOGGLE_KEY_BY_TYPE = { aux1Type: "aux1", aux2Type: "aux2", aux3Type: "aux3" };

  const featureFeedback = document.getElementById("feature-feedback");
  const logLevelSelect = document.getElementById("log-level-select");
  const diagFeedback = document.getElementById("diag-feedback");
  const auxLedCountInput = document.getElementById("aux-led-count");
  const auxLedRouteStatus = document.getElementById("aux-led-route-status");
  const auxLedRouteBadge = document.getElementById("aux-led-route-badge");
  const auxLedSwatch = document.getElementById("aux-led-swatch");
  const auxLedPreviewText = document.getElementById("aux-led-preview-text");
  const auxLedPreviewNote = document.getElementById("aux-led-preview-note");
  const setupEnabledSummary = document.getElementById("setup-enabled-summary");
  const setupSaveSummary = document.getElementById("setup-save-summary");
  const rebootButton = document.getElementById("reboot-button");
  const rebootFeedback = document.getElementById("reboot-feedback");

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

  let saveInFlight = false;
  let saveQueued = false;
  // Auto-save state
  let saveTimeout = null;

  const setSaveSummary = (message, state = "info") => {
    if (!setupSaveSummary) return;
    const classMap = { ok: "pill-ok", saving: "pill-warn", warn: "pill-warn", error: "pill-error", info: "pill-info" };
    setupSaveSummary.dataset.state = state;
    setupSaveSummary.className = `status-pill ${classMap[state] || classMap.info}`;
    setupSaveSummary.textContent = message;
  };

  const setFeedbackState = (element, message, variant = "") => {
    if (!element) return;
    element.textContent = message;
    element.className = variant ? `feedback mt-12 ${variant}` : "feedback mt-12";
  };

  const setFeatureFeedback = (message, variant = "") => {
    setFeedbackState(featureFeedback, message, variant);
  };

  const setDiagFeedback = (message, variant = "") => {
    setFeedbackState(diagFeedback, message, variant);
  };

  const setSavePending = (pending) => {
    if (rebootButton) {
      rebootButton.disabled = pending;
      rebootButton.title = pending ? "Waiting for settings to save..." : "";
    }
    if (pending) {
      setSaveSummary("💾 Saving...", "saving");
    } else if (setupSaveSummary?.dataset.state === "saving") {
      setSaveSummary("💾 Auto-save ready", "info");
    }
  };

  const sanitizeAuxLedCount = () => {
    if (!auxLedCountInput) return 1;
    const parsed = Number(auxLedCountInput.value);
    const normalized = Number.isFinite(parsed)
      ? Math.max(1, Math.min(255, Math.round(parsed)))
      : 1;
    auxLedCountInput.value = String(normalized);
    return normalized;
  };

  const segmentedTypeControls = Array.from(document.querySelectorAll(".type-segmented[data-target]"));

  const syncSegmentedControl = (control) => {
    if (!control) return;
    const targetId = control.dataset.target || "";
    const target = document.getElementById(targetId);
    if (!target) return;
    const selected = String(target.value || "");
    control.querySelectorAll(".seg-option[data-value]").forEach((button) => {
      const isActive = button.dataset.value === selected;
      button.classList.toggle("is-active", isActive);
      button.setAttribute("aria-pressed", isActive ? "true" : "false");
    });
  };

  const syncAllSegmentedControls = () => {
    segmentedTypeControls.forEach((control) => syncSegmentedControl(control));
  };

  const initSegmentedTypeControls = () => {
    segmentedTypeControls.forEach((control) => {
      const targetId = control.dataset.target || "";
      const target = document.getElementById(targetId);
      if (!target) return;

      control.addEventListener("click", (event) => {
        const button = event.target.closest(".seg-option[data-value]");
        if (!button) return;
        const nextValue = String(button.dataset.value || "");
        if (target.value !== nextValue) {
          target.value = nextValue;
          target.dispatchEvent(new Event("change", { bubbles: true }));
        } else {
          syncSegmentedControl(control);
        }
      });

      target.addEventListener("change", () => syncSegmentedControl(control));
      syncSegmentedControl(control);
    });
  };

  const getRgbAuxKeys = () => AUX_RGB_SELECT_KEYS.filter((key) => {
    const toggleKey = AUX_RGB_TOGGLE_KEY_BY_TYPE[key];
    const enabled = Boolean(featureToggles[toggleKey]?.input?.checked);
    return enabled && typeSelects[key]?.value === "rgb";
  });

  const deriveAuxLedPinFromTypes = () => {
    const rgbKey = getRgbAuxKeys()[0];
    return rgbKey ? AUX_RGB_PIN_BY_KEY[rgbKey] : 0;
  };

  const enforceSingleRgbAux = (changedKey = "") => {
    const rgbKeys = getRgbAuxKeys();
    if (rgbKeys.length <= 1) return;
    const keepKey = changedKey && rgbKeys.includes(changedKey) ? changedKey : rgbKeys[0];
    rgbKeys.forEach((key) => {
      if (key !== keepKey && typeSelects[key]) {
        typeSelects[key].value = "none";
      }
    });
    const keptLabel = AUX_RGB_LABEL_BY_KEY[keepKey] || "selected AUX";
    setFeatureFeedback(`Only one AUX line can drive LED strip output. Keeping ${keptLabel}.`, "warning");
  };

  const updateAuxLedConfigVisibility = () => {
    const rgbKeys = getRgbAuxKeys();
    const rgbKey = rgbKeys[0] || "";
    const hasRgb = Boolean(rgbKey);

    if (auxLedCountInput) {
      auxLedCountInput.disabled = !hasRgb;
    }
    if (auxLedRouteStatus) {
      if (!hasRgb) {
        auxLedRouteStatus.textContent = "Not routed";
        auxLedRouteStatus.style.color = "var(--text-dim)";
      } else {
        auxLedRouteStatus.textContent = `Routed via ${AUX_RGB_LABEL_BY_KEY[rgbKey]} LED`;
        auxLedRouteStatus.style.color = "var(--success)";
      }
    }
    if (auxLedRouteBadge) {
      if (!hasRgb) {
        auxLedRouteBadge.textContent = "🔗 Not routed";
        auxLedRouteBadge.className = "status-pill pill-info status-pill-compact";
      } else {
        auxLedRouteBadge.textContent = `🔗 ${AUX_RGB_LABEL_BY_KEY[rgbKey]}`;
        auxLedRouteBadge.className = "status-pill pill-ok status-pill-compact";
      }
    }
  };
  const updateToggleStatus = (key) => {
    const toggle = featureToggles[key];
    if (!toggle || !toggle.input || !toggle.status) return;
    toggle.status.textContent = toggle.input.checked ? "Enabled" : "Disabled";
    toggle.status.style.color = toggle.input.checked ? "var(--success)" : "var(--text-dim)";
  };

  const updateEnabledSummary = () => {
    if (!setupEnabledSummary) return;
    const toggles = Object.values(featureToggles).filter((toggle) => Boolean(toggle.input));
    const enabledCount = toggles.filter((toggle) => toggle.input.checked).length;
    const total = toggles.length;
    const state = enabledCount > 0 ? "ok" : "info";
    const prefix = enabledCount > 0 ? "✅" : "⚪";
    setupEnabledSummary.className = `status-pill ${state === "ok" ? "pill-ok" : "pill-info"}`;
    setupEnabledSummary.textContent = `${prefix} ${enabledCount}/${total} enabled`;
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

    const auxLedPin = Number(payload?.aux_led_pin || 0);
    const routedRgbKey = auxLedPin >= 1 && auxLedPin <= 3 ? AUX_RGB_SELECT_KEYS[auxLedPin - 1] : "";
    if (routedRgbKey && typeSelects[routedRgbKey]) {
      typeSelects[routedRgbKey].value = "rgb";
    }
    enforceSingleRgbAux(routedRgbKey);
    syncAllSegmentedControls();

    if (auxLedCountInput && payload?.aux_led_count !== undefined) {
      auxLedCountInput.value = String(payload.aux_led_count);
    }
    sanitizeAuxLedCount();
    updateAuxLedConfigVisibility();
    updateEnabledSummary();
    setFeatureFeedback(`Components loaded at ${new Date().toLocaleTimeString()}`, "success");
    if (logLevelSelect && system.logLevel !== undefined) {
      logLevelSelect.value = String(system.logLevel);
    }
  };

  const loadFeatures = async () => {
    if (!window.PAApi) return;
    setFeatureFeedback("Loading component settings...");
    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
      renderFeatures(result.data);
    } catch (error) {
      console.error("[setup] loadFeatures failed:", error);
      setFeatureFeedback(`Failed to load component settings: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  // Auto-save function
  const saveFeatures = async () => {
    if (!window.PAApi) return;
    if (saveInFlight) {
      saveQueued = true;
      return;
    }

    saveInFlight = true;
    setFeatureFeedback("Saving...");
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
      body.set("aux_led_pin", String(deriveAuxLedPinFromTypes()));
      if (auxLedCountInput) {
        body.set("aux_led_count", String(sanitizeAuxLedCount()));
      }
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      renderFeatures(result.data);
      const savedAt = new Date().toLocaleTimeString();
      setFeatureFeedback(`Saved at ${savedAt}`, "success");
      setSaveSummary(`✅ Saved at ${savedAt}`, "ok");
    } catch (error) {
      console.error("[setup] saveFeatures failed:", error);
      setFeatureFeedback(window.PAApi.messageFor(error), "error");
      setSaveSummary("❌ Save failed", "error");
    } finally {
      saveInFlight = false;
      if (saveQueued) {
        saveQueued = false;
        saveFeatures();
        return;
      }
      setSavePending(false);
    }
  };

  const debouncedSave = (...args) => {
    setSavePending(true);
    clearTimeout(saveTimeout);
    saveTimeout = setTimeout(() => saveFeatures(...args), 300);
  };

  // Attach listeners to all toggles and selects
  Object.keys(featureToggles).forEach((key) => {
    const toggle = featureToggles[key];
    if (toggle.input) {
      toggle.input.addEventListener("change", () => {
        updateToggleStatus(key);
        updateEnabledSummary();
        if (["aux1", "aux2", "aux3"].includes(key)) {
          updateAuxLedConfigVisibility();
        }
        debouncedSave();
      });
    }
  });

  Object.entries(typeSelects).forEach(([typeKey, select]) => {
    if (select) {
      select.addEventListener("change", () => {
        if (AUX_RGB_SELECT_KEYS.includes(typeKey)) {
          enforceSingleRgbAux(typeKey);
          updateAuxLedConfigVisibility();
        }
        syncAllSegmentedControls();
        debouncedSave();
      });
    }
  });

  if (auxLedCountInput) {
    auxLedCountInput.addEventListener("change", () => {
      sanitizeAuxLedCount();
      debouncedSave();
    });
  }


  // Reboot functionality

  const handleReboot = async () => {
    if (!confirm("Reboot the controller? The web interface will be unavailable for about 10 seconds.")) {
      return;
    }
    if (!window.PAApi) return;
    setFeedbackState(rebootFeedback, "Sending reboot command...");
    try {
      await window.PAApi.postForm("/api/reboot", {}, { timeoutMs: 5000 });
      setFeedbackState(rebootFeedback, "Reboot command sent. Wait ~10 seconds and refresh...", "success");
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
      setFeedbackState(rebootFeedback, window.PAApi.messageFor(error), "error");
    }
  };

  if (rebootButton) {
    rebootButton.addEventListener("click", handleReboot);
  }

  // --- Diagnostics: log level selector ---
  const saveLogLevel = async () => {
    if (!logLevelSelect || !window.PAApi) return;
    setDiagFeedback("Saving...");
    try {
      const body = new URLSearchParams();
      body.set("logLevel", logLevelSelect.value);
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      if (result.data?.system?.logLevel !== undefined) {
        logLevelSelect.value = String(result.data.system.logLevel);
      }
      setDiagFeedback(`Log level saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[setup] saveLogLevel failed:", error);
      setDiagFeedback(window.PAApi.messageFor(error), "error");
    }
  };

  if (logLevelSelect) {
    logLevelSelect.addEventListener("change", saveLogLevel);
  }

  initSegmentedTypeControls();
  updateEnabledSummary();
  updateAuxLedConfigVisibility();
  setSaveSummary("💾 Auto-save ready", "info");
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

  const renderAuxLedPreview = (status) => {
    if (!auxLedSwatch || !auxLedPreviewText || !auxLedPreviewNote) return;
    const aux = status?.auxLed;
    const pin = Number(aux?.pin || 0);
    const available = aux?.available !== false;
    const effect = String(aux?.effect || "off");
    const r = Math.max(0, Math.min(255, Number(aux?.r || 0)));
    const g = Math.max(0, Math.min(255, Number(aux?.g || 0)));
    const b = Math.max(0, Math.min(255, Number(aux?.b || 0)));

    auxLedSwatch.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
    auxLedSwatch.style.opacity = pin > 0 && available && effect !== "off" ? "1" : "0.35";

    if (pin === 0) {
      auxLedPreviewText.textContent = "";
      auxLedPreviewText.style.color = "var(--text-dim)";
      auxLedPreviewNote.textContent = "";
      return;
    }

    if (!available) {
      auxLedPreviewText.textContent = `LED strip on AUX${pin} unavailable`;
      auxLedPreviewText.style.color = "var(--warning)";
      auxLedPreviewNote.textContent = "Strip configured, but output driver is unavailable.";
      return;
    }

    auxLedPreviewText.textContent = `AUX${pin} LED - ${effect}`;
    auxLedPreviewText.style.color = "var(--success)";
    auxLedPreviewNote.textContent = `Live color ${r},${g},${b} with ${effect} effect.`;
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
    renderAuxLedPreview(d);

    setFeedbackState(serialStatusLine, `Updated ${new Date().toLocaleTimeString()}`, "success");
  };

  const refreshSerialStatus = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
      renderSerialStatus(result.data);
    } catch (error) {
      console.warn("[setup] refreshSerialStatus failed:", error);
      setFeedbackState(serialStatusLine, "Status unavailable", "error");
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

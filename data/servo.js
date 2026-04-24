// =============================================================================
// servo.js
//
// Servos page controller — arm servo controls, AUX output controls, and
// servo calibration (ARM1/ARM2 and AUX servo channels).
// SSE-first status delivery (consume `status` events from PAStatusStream),
// with visibility-aware fallback polling when SSE is unavailable.
// Sends open/close/stop/position commands via POST /api/servo.
// Loads and saves calibration via GET/POST /api/config (auto-save on change).
// Component types (mg996r/mg90s/rgb/none) are read from /api/config to render
// type-appropriate controls per AUX channel.
// =============================================================================
(() => {
  const armControlsCard      = document.getElementById("arm-controls-card");
  const armControlsContainer = document.getElementById("arm-controls-container");
  const noArmsCard           = document.getElementById("no-arms-card");
  const armFeedback          = document.getElementById("arm-feedback");

  const auxControlsCard      = document.getElementById("aux-controls-card");
  const auxControlsContainer = document.getElementById("aux-controls-container");
  const auxFeedback          = document.getElementById("aux-feedback");

  const servoCalibCard       = document.getElementById("servo-calib-card");
  const arm1CalibSection     = document.getElementById("arm1-calib-section");
  const arm2CalibSection     = document.getElementById("arm2-calib-section");
  const aux1CalibSection     = document.getElementById("aux1-calib-section");
  const aux2CalibSection     = document.getElementById("aux2-calib-section");
  const aux3CalibSection     = document.getElementById("aux3-calib-section");

  const arm1OpenUs           = document.getElementById("arm1-open-us");
  const arm1CloseUs          = document.getElementById("arm1-close-us");
  const arm2OpenUs           = document.getElementById("arm2-open-us");
  const arm2CloseUs          = document.getElementById("arm2-close-us");
  const aux1OpenUs           = document.getElementById("aux1-open-us");
  const aux1CloseUs          = document.getElementById("aux1-close-us");
  const aux2OpenUs           = document.getElementById("aux2-open-us");
  const aux2CloseUs          = document.getElementById("aux2-close-us");
  const aux3OpenUs           = document.getElementById("aux3-open-us");
  const aux3CloseUs          = document.getElementById("aux3-close-us");
  const arm1TestUs           = document.getElementById("arm1-test-us");
  const arm2TestUs           = document.getElementById("arm2-test-us");
  const aux1TestUs           = document.getElementById("aux1-test-us");
  const aux2TestUs           = document.getElementById("aux2-test-us");
  const aux3TestUs           = document.getElementById("aux3-test-us");
  const calibFeedback        = document.getElementById("calib-feedback");
  const reloadCalibBtn       = document.getElementById("reload-calib-btn");

  // Component types loaded from /api/config — determines AUX rendering
  let auxTypes = { aux1: "none", aux2: "none", aux3: "none" };
  let auxConfigured = { aux1: false, aux2: false, aux3: false };



  const escapeHtml = (value) => String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");
  // -------------------------------------------------------------------------
  // Servo command helpers
  // -------------------------------------------------------------------------
  const setFeedback = (el, text, cls = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = cls ? `feedback ${cls}` : "feedback";
  };

  const postServoAction = async (arm, action, feedbackEl) => {
    if (!window.PAApi) return;
    const fb = feedbackEl || armFeedback;
    const label = `${arm} ${action}`;
    try {
      await window.PAApi.postForm("/api/servo", { arm, action }, { timeoutMs: 3000 });
      setFeedback(fb, `${label} sent at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      setFeedback(fb, `${label} failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const postServoPosition = async (arm, pulseUs, feedbackEl) => {
    if (!window.PAApi) return;
    const fb = feedbackEl || armFeedback;
    const label = `${arm} → ${pulseUs} µs`;
    try {
      await window.PAApi.postForm("/api/servo",
        { arm, action: "position", positionUs: String(pulseUs) },
        { timeoutMs: 3000 });
      setFeedback(fb, `▶ Test ${label} at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      setFeedback(fb, `Test ${label} failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  // -------------------------------------------------------------------------
  // renderArmControls() — ARM1/ARM2 open/close/stop buttons
  // -------------------------------------------------------------------------
  const ARM_DEFS = [
    { id: "arm1", name: "Left Arm",  label: "ARM1" },
    { id: "arm2", name: "Right Arm", label: "ARM2" },
  ];

  let renderedArmIds = null;

  const renderArmControls = (payload) => {
    const enabled = ARM_DEFS.filter((a) => a.id in payload);
    const ids = enabled.map((a) => a.id).join(",");

    if (noArmsCard)      noArmsCard.classList.toggle("hidden", enabled.length > 0);
    if (armControlsCard) armControlsCard.classList.toggle("hidden", enabled.length === 0);

    if (enabled.length === 0) return;

    if (armControlsContainer && ids !== renderedArmIds) {
      renderedArmIds = ids;
      armControlsContainer.innerHTML = enabled.map((arm) => {
        const detail = escapeHtml(payload[arm.id]?.detail || "");
        return `
          <div class="arm-control-row" id="row-${arm.id}">
            <span class="arm-name">${arm.name}</span>
            <span class="arm-position text-dim" id="pos-${arm.id}">${detail}</span>
            <button class="btn" data-arm="${arm.id}" data-action="open"  type="button">📂 Open</button>
            <button class="btn" data-arm="${arm.id}" data-action="close" type="button">📁 Close</button>
            <button class="btn" data-arm="${arm.id}" data-action="stop"  type="button">⏹️ Stop</button>
          </div>`;
      }).join("");

      armControlsContainer.querySelectorAll("[data-arm]").forEach((btn) => {
        btn.addEventListener("click", () =>
          postServoAction(btn.dataset.arm, btn.dataset.action, armFeedback));
      });
    } else if (armControlsContainer) {
      enabled.forEach((arm) => {
        const el = document.getElementById(`pos-${arm.id}`);
        if (el) el.textContent = payload[arm.id]?.detail || "";
      });
    }
  };

  // -------------------------------------------------------------------------
  // renderAuxControls() — AUX1/2/3 type-appropriate controls
  // -------------------------------------------------------------------------
  const AUX_DEFS = [
    { id: "aux1", name: "AUX 1", label: "AUX1" },
    { id: "aux2", name: "AUX 2", label: "AUX2" },
    { id: "aux3", name: "AUX 3", label: "AUX3" },
  ];

  const setupActionText = window.PAUi?.setupActionText || ((action) => `${action} in Setup`);
  const setupActionHtml = window.PAUi?.setupActionHtml
    || ((action) => `${action} in <a class="setup-link" href="/setup.html">Setup</a>`);
  const isServoType = (type) => type === "mg996r" || type === "mg90s";
  const auxTypeLabel = (type) => type === "mg90s" ? "MG90S servo" : type === "mg996r" ? "MG996R servo" : "";

  const buildAuxLedRow = (aux) => `
    <div class="arm-control-row" id="row-${aux.id}">
      <span class="arm-name">${aux.name}</span>
      <span class="arm-position text-dim">💡 LED strip (${setupActionText("configure")})</span>
    </div>`;

  const buildAuxServoRow = (aux, detail, typeLabel) => {
    const descriptor = typeLabel ? `${typeLabel}${detail ? ` · ${detail}` : ""}` : detail;
    return `
      <div class="arm-control-row" id="row-${aux.id}">
        <span class="arm-name">${aux.name}</span>
        <span class="arm-position text-dim" id="pos-${aux.id}">${descriptor}</span>
        <button class="btn" data-arm="${aux.id}" data-action="open" type="button">📂 Open</button>
        <button class="btn" data-arm="${aux.id}" data-action="close" type="button">📁 Close</button>
        <button class="btn" data-arm="${aux.id}" data-action="stop" type="button">⏹️ Stop</button>
      </div>`;
  };

  const bindAuxActionDelegation = () => {
    if (!auxControlsContainer || auxControlsContainer.dataset.actionsBound === "1") return;
    auxControlsContainer.dataset.actionsBound = "1";
    auxControlsContainer.addEventListener("click", (event) => {
      const button = event.target.closest("button[data-arm][data-action]");
      if (!button || !auxControlsContainer.contains(button)) return;
      postServoAction(button.dataset.arm, button.dataset.action, auxFeedback);
    });
  };

  let renderedAuxIds = null;

  const renderAuxControls = (payload) => {
    const enabled = AUX_DEFS.filter((a) => auxConfigured[a.id] || (a.id in payload));
    if (auxControlsCard) auxControlsCard.classList.remove("hidden");
    if (!auxControlsContainer) return;
    bindAuxActionDelegation();

    if (enabled.length === 0) {
      renderedAuxIds = "none";
      auxControlsContainer.innerHTML =
        `<div class="desc">${setupActionHtml("Enable AUX outputs")} to show controls here.</div>`;
      return;
    }

    const ids = enabled.map((a) => `${a.id}:${auxTypes[a.id]}`).join(",");
    if (ids !== renderedAuxIds) {
      renderedAuxIds = ids;
      const rows = enabled.map((aux) => {
        const type = auxTypes[aux.id] || "none";
        const detail = escapeHtml(payload[aux.id]?.detail || "");
        const typeLabel = auxTypeLabel(type);
        if (type === "rgb") return buildAuxLedRow(aux);
        if (isServoType(type)) return buildAuxServoRow(aux, detail, typeLabel);
        return "";
      }).filter(Boolean);

      auxControlsContainer.innerHTML = rows.length > 0
        ? rows.join("")
        : "<div class=\"desc\">AUX outputs are enabled, but none are configured as controllable servo or LED strip outputs.</div>";
      return;
    }

    enabled.forEach((aux) => {
      const el = document.getElementById(`pos-${aux.id}`);
      if (!el) return;
      const type = auxTypes[aux.id] || "none";
      if (!isServoType(type)) return;
      const typeLabel = auxTypeLabel(type);
      const detail = payload[aux.id]?.detail || "";
      el.textContent = typeLabel ? `${typeLabel}${detail ? ` · ${detail}` : ""}` : detail;
    });
  };

  // -------------------------------------------------------------------------
  // renderCalibSections() — show/hide calibration sections per enabled state + type
  // -------------------------------------------------------------------------
  const renderCalibSections = (payload) => {
    const arm1Present = "arm1" in payload;
    const arm2Present = "arm2" in payload;
    const aux1Present = auxConfigured.aux1 || ("aux1" in payload);
    const aux2Present = auxConfigured.aux2 || ("aux2" in payload);
    const aux3Present = auxConfigured.aux3 || ("aux3" in payload);


    const aux1Servo = aux1Present && isServoType(auxTypes.aux1);
    const aux2Servo = aux2Present && isServoType(auxTypes.aux2);
    const aux3Servo = aux3Present && isServoType(auxTypes.aux3);

    const anyCalib = arm1Present || arm2Present || aux1Servo || aux2Servo || aux3Servo;

    if (servoCalibCard)   servoCalibCard.classList.toggle("hidden", !anyCalib);
    if (arm1CalibSection) arm1CalibSection.classList.toggle("hidden", !arm1Present);
    if (arm2CalibSection) arm2CalibSection.classList.toggle("hidden", !arm2Present);
    if (aux1CalibSection) aux1CalibSection.classList.toggle("hidden", !aux1Servo);
    if (aux2CalibSection) aux2CalibSection.classList.toggle("hidden", !aux2Servo);
    if (aux3CalibSection) aux3CalibSection.classList.toggle("hidden", !aux3Servo);
  };

  // -------------------------------------------------------------------------
  // Status rendering — shared by SSE and fallback polling paths
  // -------------------------------------------------------------------------
  let lastPayload = null;

  const renderStatus = (payload) => {
    lastPayload = payload;
    renderArmControls(payload);
    renderAuxControls(payload);
    renderCalibSections(payload);
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
  };

  // -------------------------------------------------------------------------
  // Calibration load / auto-save
  // -------------------------------------------------------------------------
  const setCalibFeedback = (text, cls = "") => {
    if (!calibFeedback) return;
    calibFeedback.textContent = text;
    calibFeedback.className = cls ? `feedback ${cls}` : "feedback";
  };

  const loadCalib = async () => {
    if (!window.PAApi) return;
    setCalibFeedback("Loading calibration...");
    try {
      const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
      const cfg = result.data;

      // Arm calibration
      if (arm1OpenUs)  arm1OpenUs.value  = cfg.arm1OpenUs  ?? 2000;
      if (arm1CloseUs) arm1CloseUs.value = cfg.arm1CloseUs ?? 1000;
      if (arm2OpenUs)  arm2OpenUs.value  = cfg.arm2OpenUs  ?? 2000;
      if (arm2CloseUs) arm2CloseUs.value = cfg.arm2CloseUs ?? 1000;

      // AUX calibration
      if (aux1OpenUs)  aux1OpenUs.value  = cfg.aux1OpenUs  ?? 2000;
      if (aux1CloseUs) aux1CloseUs.value = cfg.aux1CloseUs ?? 1000;
      if (aux2OpenUs)  aux2OpenUs.value  = cfg.aux2OpenUs  ?? 2000;
      if (aux2CloseUs) aux2CloseUs.value = cfg.aux2CloseUs ?? 1000;
      if (aux3OpenUs)  aux3OpenUs.value  = cfg.aux3OpenUs  ?? 2000;
      if (aux3CloseUs) aux3CloseUs.value = cfg.aux3CloseUs ?? 1000;

      // Pre-populate test inputs at neutral
      if (arm1TestUs) arm1TestUs.value = 1500;
      if (arm2TestUs) arm2TestUs.value = 1500;
      if (aux1TestUs) aux1TestUs.value = 1500;
      if (aux2TestUs) aux2TestUs.value = 1500;
      if (aux3TestUs) aux3TestUs.value = 1500;

      const components = cfg?.components || {};
      auxTypes.aux1 = String(components.aux1?.type || "none");
      auxTypes.aux2 = String(components.aux2?.type || "none");
      auxTypes.aux3 = String(components.aux3?.type || "none");
      auxConfigured.aux1 = Boolean(components.aux1?.enabled);
      auxConfigured.aux2 = Boolean(components.aux2?.enabled);
      auxConfigured.aux3 = Boolean(components.aux3?.enabled);
      // Re-render AUX controls now that types are known
      renderAuxControls(lastPayload || {});
      renderCalibSections(lastPayload || {});

      setCalibFeedback(`Calibration loaded at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[servo] loadCalib failed:", error);
      setCalibFeedback(`Failed to load calibration: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  // Auto-save calibration
  const saveCalib = async () => {
    if (!window.PAApi) return;
    setCalibFeedback("Saving...");
    try {
      const body = new URLSearchParams();
      if (arm1OpenUs  && !arm1CalibSection?.classList.contains("hidden")) {
        body.set("arm1OpenUs",  arm1OpenUs.value);
        body.set("arm1CloseUs", arm1CloseUs.value);
      }
      if (arm2OpenUs  && !arm2CalibSection?.classList.contains("hidden")) {
        body.set("arm2OpenUs",  arm2OpenUs.value);
        body.set("arm2CloseUs", arm2CloseUs.value);
      }
      if (aux1OpenUs  && !aux1CalibSection?.classList.contains("hidden")) {
        body.set("aux1OpenUs",  aux1OpenUs.value);
        body.set("aux1CloseUs", aux1CloseUs.value);
      }
      if (aux2OpenUs  && !aux2CalibSection?.classList.contains("hidden")) {
        body.set("aux2OpenUs",  aux2OpenUs.value);
        body.set("aux2CloseUs", aux2CloseUs.value);
      }
      if (aux3OpenUs  && !aux3CalibSection?.classList.contains("hidden")) {
        body.set("aux3OpenUs",  aux3OpenUs.value);
        body.set("aux3CloseUs", aux3CloseUs.value);
      }
      await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      setCalibFeedback(`Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[servo] saveCalib failed:", error);
      setCalibFeedback(`Save failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const debouncedSave = window.PAUtils.debounce(saveCalib, 500);

  // Attach auto-save listeners to all calibration inputs
  const calibInputs = [arm1OpenUs, arm1CloseUs, arm2OpenUs, arm2CloseUs,
                       aux1OpenUs, aux1CloseUs, aux2OpenUs, aux2CloseUs,
                       aux3OpenUs, aux3CloseUs];
  calibInputs.forEach((input) => {
    if (input) {
      input.addEventListener("input", debouncedSave);
    }
  });

  if (reloadCalibBtn) reloadCalibBtn.addEventListener("click", loadCalib);

  // -------------------------------------------------------------------------
  // Test buttons — send SERVO_CMD_POSITION immediately (does not save)
  // -------------------------------------------------------------------------
  const wireTestBtn = (btnId, armId, getUs, fb) => {
    const btn = document.getElementById(btnId);
    if (btn) btn.addEventListener("click", () =>
      postServoPosition(armId, Number(getUs()), fb || armFeedback));
  };

  wireTestBtn("arm1-test-btn",       "arm1", () => arm1TestUs?.value  || 1500);
  wireTestBtn("arm1-open-test-btn",  "arm1", () => arm1OpenUs?.value  || 2000);
  wireTestBtn("arm1-close-test-btn", "arm1", () => arm1CloseUs?.value || 1000);
  wireTestBtn("arm2-test-btn",       "arm2", () => arm2TestUs?.value  || 1500);
  wireTestBtn("arm2-open-test-btn",  "arm2", () => arm2OpenUs?.value  || 2000);
  wireTestBtn("arm2-close-test-btn", "arm2", () => arm2CloseUs?.value || 1000);
  wireTestBtn("aux1-test-btn",       "aux1", () => aux1TestUs?.value  || 1500, auxFeedback);
  wireTestBtn("aux1-open-test-btn",  "aux1", () => aux1OpenUs?.value  || 2000, auxFeedback);
  wireTestBtn("aux1-close-test-btn", "aux1", () => aux1CloseUs?.value || 1000, auxFeedback);
  wireTestBtn("aux2-test-btn",       "aux2", () => aux2TestUs?.value  || 1500, auxFeedback);
  wireTestBtn("aux2-open-test-btn",  "aux2", () => aux2OpenUs?.value  || 2000, auxFeedback);
  wireTestBtn("aux2-close-test-btn", "aux2", () => aux2CloseUs?.value || 1000, auxFeedback);
  wireTestBtn("aux3-test-btn",       "aux3", () => aux3TestUs?.value  || 1500, auxFeedback);
  wireTestBtn("aux3-open-test-btn",  "aux3", () => aux3OpenUs?.value  || 2000, auxFeedback);
  wireTestBtn("aux3-close-test-btn", "aux3", () => aux3CloseUs?.value || 1000, auxFeedback);

  // -------------------------------------------------------------------------
  // Boot — load config then start status subscription
  // -------------------------------------------------------------------------
  loadCalib();

  // SSE-first status updates with visibility-aware fallback polling.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderStatus(payload);
    });
    // One-shot fetch if SSE hasn't delivered a status frame yet.
    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatusOnce().catch(() => {});
    }
  } else {
    // Fallback: poll every 1 s, suspended while the tab is hidden.
    refreshStatusOnce().catch(() => {});
    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshStatusOnce().catch(() => {});
    }, 1000);
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshStatusOnce().catch(() => {});
      }
    });
  }
})();

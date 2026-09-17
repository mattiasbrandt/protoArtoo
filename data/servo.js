// =============================================================================
// servo.js
//
// Servos page controller — arm servo controls, AUX output controls, and the
// per-output test controls (ARM1/ARM2 and AUX servo channels).
// SSE-first status delivery (consume `status` events from PAStatusStream),
// with visibility-aware fallback polling when SSE is unavailable.
// Sends open/close/stop/position commands via POST /api/servo.
// READS calibration via GET /api/config and never writes it: an end is set on
// Parts, by driving the part and pressing the button for that end, and this
// page's Test Open and Test Close drive to the ends the droid recorded there
// (#400). The `calib` names below are kept because reading the calibration is
// still exactly what they do.
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

  const servoTestCard        = document.getElementById("servo-test-card");
  const arm1TestSection      = document.getElementById("arm1-test-section");
  const arm2TestSection      = document.getElementById("arm2-test-section");
  const aux1TestSection      = document.getElementById("aux1-test-section");
  const aux2TestSection      = document.getElementById("aux2-test-section");
  const aux3TestSection      = document.getElementById("aux3-test-section");

  const arm1TestUs           = document.getElementById("arm1-test-us");
  const arm2TestUs           = document.getElementById("arm2-test-us");
  const aux1TestUs           = document.getElementById("aux1-test-us");
  const aux2TestUs           = document.getElementById("aux2-test-us");
  const aux3TestUs           = document.getElementById("aux3-test-us");
  const calibFeedback        = document.getElementById("calib-feedback");

  // Component types loaded from /api/config — determines AUX rendering
  let auxTypes = { aux1: "none", aux2: "none", aux3: "none" };
  let auxConfigured = { aux1: false, aux2: false, aux3: false };

  // The recorded ends, read from /api/config and never written from here. Test
  // Open and Test Close drive to these, so what a builder sees is what the
  // droid will really do when something says open -- not whatever number a box
  // on this page happened to be holding.
  //
  // The defaults stand in when a field is absent, which is a real answer rather
  // than a missing one: addServoOutputFields() leaves an Output Address with no
  // live row OUT of the document instead of inventing a number, and names this
  // fallback as the reason it may (src/web/api_config.cpp:624).
  const endpoints = {
    arm1Open: 2000, arm1Close: 1000,
    arm2Open: 2000, arm2Close: 1000,
    aux1Open: 2000, aux1Close: 1000,
    aux2Open: 2000, aux2Close: 1000,
    aux3Open: 2000, aux3Close: 1000,
  };


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
    { id: "arm1", name: "Utility Arm 1",  label: "ARM1" },
    { id: "arm2", name: "Utility Arm 2", label: "ARM2" },
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
        const detail = window.PAUtils.escapeHtml(payload[arm.id]?.detail || "");
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
        const detail = window.PAUtils.escapeHtml(payload[aux.id]?.detail || "");
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
  // renderTestSections() — show/hide test sections per enabled state + type
  // -------------------------------------------------------------------------
  const renderTestSections = (payload) => {
    const arm1Present = "arm1" in payload;
    const arm2Present = "arm2" in payload;
    const aux1Present = auxConfigured.aux1 || ("aux1" in payload);
    const aux2Present = auxConfigured.aux2 || ("aux2" in payload);
    const aux3Present = auxConfigured.aux3 || ("aux3" in payload);


    const aux1Servo = aux1Present && isServoType(auxTypes.aux1);
    const aux2Servo = aux2Present && isServoType(auxTypes.aux2);
    const aux3Servo = aux3Present && isServoType(auxTypes.aux3);

    const anyTestable = arm1Present || arm2Present || aux1Servo || aux2Servo || aux3Servo;

    if (servoTestCard)   servoTestCard.classList.toggle("hidden", !anyTestable);
    if (arm1TestSection) arm1TestSection.classList.toggle("hidden", !arm1Present);
    if (arm2TestSection) arm2TestSection.classList.toggle("hidden", !arm2Present);
    if (aux1TestSection) aux1TestSection.classList.toggle("hidden", !aux1Servo);
    if (aux2TestSection) aux2TestSection.classList.toggle("hidden", !aux2Servo);
    if (aux3TestSection) aux3TestSection.classList.toggle("hidden", !aux3Servo);
  };

  // -------------------------------------------------------------------------
  // Status rendering — shared by SSE and fallback polling paths
  // -------------------------------------------------------------------------
  let lastPayload = null;

  const renderStatus = (payload) => {
    lastPayload = payload;
    renderArmControls(payload);
    renderAuxControls(payload);
    renderTestSections(payload);
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
  };

  // -------------------------------------------------------------------------
  // Calibration load — read only
  // -------------------------------------------------------------------------
  const setCalibFeedback = (text, cls = "") => {
    if (!calibFeedback) return;
    calibFeedback.textContent = text;
    calibFeedback.className = cls ? `feedback ${cls}` : "feedback";
  };

  const loadCalib = async ({ handle = null } = {}) => {
    if (!window.PAApi) throw new Error("API helper unavailable");
    setCalibFeedback("Loading calibration...");
    try {
      const api = handle || window.PAApi;
      const result = await api.get("/api/config");
      const cfg = result.data;

      // Arm ends
      endpoints.arm1Open  = cfg.arm1OpenUs  ?? 2000;
      endpoints.arm1Close = cfg.arm1CloseUs ?? 1000;
      endpoints.arm2Open  = cfg.arm2OpenUs  ?? 2000;
      endpoints.arm2Close = cfg.arm2CloseUs ?? 1000;

      // AUX ends
      endpoints.aux1Open  = cfg.aux1OpenUs  ?? 2000;
      endpoints.aux1Close = cfg.aux1CloseUs ?? 1000;
      endpoints.aux2Open  = cfg.aux2OpenUs  ?? 2000;
      endpoints.aux2Close = cfg.aux2CloseUs ?? 1000;
      endpoints.aux3Open  = cfg.aux3OpenUs  ?? 2000;
      endpoints.aux3Close = cfg.aux3CloseUs ?? 1000;

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
      renderTestSections(lastPayload || {});

      setCalibFeedback(`Calibration loaded at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[servo] loadCalib failed:", error);
      setCalibFeedback(`Failed to load calibration: ${window.PAApi.messageFor(error)}`, "error");
      throw error;
    }
  };

  // -------------------------------------------------------------------------
  // Test buttons — send SERVO_CMD_POSITION immediately. Test Open and Test
  // Close drive to the ends `endpoints` holds; nothing on this page writes one.
  // -------------------------------------------------------------------------
  const wireTestBtn = (btnId, armId, getUs, fb) => {
    const btn = document.getElementById(btnId);
    if (btn) btn.addEventListener("click", () =>
      postServoPosition(armId, Number(getUs()), fb || armFeedback));
  };

  wireTestBtn("arm1-test-btn",       "arm1", () => arm1TestUs?.value || 1500);
  wireTestBtn("arm1-open-test-btn",  "arm1", () => endpoints.arm1Open);
  wireTestBtn("arm1-close-test-btn", "arm1", () => endpoints.arm1Close);
  wireTestBtn("arm2-test-btn",       "arm2", () => arm2TestUs?.value || 1500);
  wireTestBtn("arm2-open-test-btn",  "arm2", () => endpoints.arm2Open);
  wireTestBtn("arm2-close-test-btn", "arm2", () => endpoints.arm2Close);
  wireTestBtn("aux1-test-btn",       "aux1", () => aux1TestUs?.value || 1500, auxFeedback);
  wireTestBtn("aux1-open-test-btn",  "aux1", () => endpoints.aux1Open,  auxFeedback);
  wireTestBtn("aux1-close-test-btn", "aux1", () => endpoints.aux1Close, auxFeedback);
  wireTestBtn("aux2-test-btn",       "aux2", () => aux2TestUs?.value || 1500, auxFeedback);
  wireTestBtn("aux2-open-test-btn",  "aux2", () => endpoints.aux2Open,  auxFeedback);
  wireTestBtn("aux2-close-test-btn", "aux2", () => endpoints.aux2Close, auxFeedback);
  wireTestBtn("aux3-test-btn",       "aux3", () => aux3TestUs?.value || 1500, auxFeedback);
  wireTestBtn("aux3-open-test-btn",  "aux3", () => endpoints.aux3Open,  auxFeedback);
  wireTestBtn("aux3-close-test-btn", "aux3", () => endpoints.aux3Close, auxFeedback);

  // -------------------------------------------------------------------------
  // Boot — load config then start status subscription
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API load as a section so the bootstrap
  // can show recovery state if the config fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const SECTIONS = [
    ["servo-calibration", loadCalib, "servo calibration"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadCalib().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/servo.js": "servo control",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  startPageLoad();

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
    // Fallback: poll every 1 s, suspended while the tab is hidden and while the
    // operator is reading another surface -- the shell stops it on the way out
    // and starts it again on the way back (ADR 0048, #360).
    window.PASurface.poll(() => refreshStatusOnce().catch(() => {}), {
      cadenceMs: 1000,
      runOnStart: true,
      refreshOnReturn: true,
    }).start();
  }
})();

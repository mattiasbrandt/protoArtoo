// =============================================================================
// app.js
//
// Home dashboard controller — system health indicators, component status grid,
// and live log console. Polls /api/status every 2 seconds.
// Drive controls and dome controls live in drive.js and dome.js respectively.
// =============================================================================
(() => {
  const healthSummary = document.getElementById("health-summary");
  const logConsole = document.getElementById("log-console");
  const componentStatusCard = document.getElementById("component-status-card");
  const componentStatusGrid = document.getElementById("component-status-grid");

  // Per-component labels: [stateKey, emoji, humanLabel]
  const COMPONENT_LABELS = [
    ["arm1",         "🦾", "Left Arm"],
    ["arm2",         "🦾", "Right Arm"],
    ["aux1",         "🦾", "Aux Servo 1"],
    ["aux2",         "🦾", "Aux Servo 2"],
    ["aux3",         "🦾", "Aux Servo 3"],
    ["dome",         "🔄", "Dome Motor"],
    ["rcCh1",        "🕹️", "RC Channel 1"],
    ["rcCh2",        "🕹️", "RC Channel 2"],
    ["rcCh3",        "🕹️", "RC Channel 3"],
    ["rcCh4",        "🕹️", "RC Channel 4"],
    ["rcCh5",        "🕹️", "RC Channel 5"],
    ["rcCh6",        "🕹️", "RC Channel 6"],
    ["s1Hoverboard", "🔌", "Hoverboard Drive"],
    ["s2Sound",      "🔊", "Sound Module"],
    ["s3DomeCtrl",   "🔌", "Dome Controller"],
  ];

  const setIndicator = (id, state) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.className = `indicator ${state}`;
  };

  const renderHealth = (payload) => {
    setIndicator("h-sbus",  payload.sbusSignalLost || payload.sbusHwFailsafe ? "fail" : "ok");
    setIndicator("h-wifi",  (payload.wifiConnected || payload.wifiClientConnected) ? "ok" : "warn");
    setIndicator("h-fs",    payload.littleFsReady ? "ok" : "fail");
    setIndicator("h-heap",  payload.heapFree > 120000 ? "ok" : payload.heapFree > 80000 ? "warn" : "fail");

    if (!healthSummary) return;

    const heapFreeKb = Math.round(payload.heapFree / 1024);
    const heapMinKb  = Math.round(payload.heapMin  / 1024);

    let heapLabel = "✅ Healthy"; let heapColor = "var(--success)";
    if (heapFreeKb < 80)  { heapLabel = "❌ Critical"; heapColor = "var(--danger)"; }
    else if (heapFreeKb < 120) { heapLabel = "⚠️ Low"; heapColor = "var(--warning)"; }

    let heapMinLabel = "✅ Good"; let heapMinColor = "var(--success)";
    if (heapMinKb < 64)  { heapMinLabel = "❌ Critical"; heapMinColor = "var(--danger)"; }
    else if (heapMinKb < 96) { heapMinLabel = "⚠️ Watch"; heapMinColor = "var(--warning)"; }

    let wifiQuality = "❌ Unknown"; let wifiColor = "var(--danger)";
    if ((payload.wifiConnected || payload.wifiClientConnected) && payload.wifiRssi !== 0) {
      if      (payload.wifiRssi >= -67) { wifiQuality = `✅ Excellent (${payload.wifiRssi} dBm)`; wifiColor = "var(--success)"; }
      else if (payload.wifiRssi >= -75) { wifiQuality = `✅ Good (${payload.wifiRssi} dBm)`;      wifiColor = "var(--success)"; }
      else if (payload.wifiRssi >= -85) { wifiQuality = `⚠️ Fair (${payload.wifiRssi} dBm)`;  wifiColor = "var(--warning)"; }
      else                              { wifiQuality = `❌ Poor (${payload.wifiRssi} dBm)`;       wifiColor = "var(--danger)"; }
    }

    healthSummary.innerHTML =
      `Memory: <span style="color:${heapColor};font-weight:700">${heapFreeKb} KB ${heapLabel}</span><br>` +
      `Memory Min: <span style="color:${heapMinColor};font-weight:700">${heapMinKb} KB ${heapMinLabel}</span><br>` +
      `WiFi: <span style="color:${wifiColor};font-weight:700">${wifiQuality}</span>`;
  };

  const renderComponentStatus = (payload) => {
    if (!componentStatusCard || !componentStatusGrid) return;

    const active = COMPONENT_LABELS.filter(([key]) => key in payload);
    if (active.length === 0) {
      componentStatusCard.classList.add("hidden");
      componentStatusGrid.innerHTML = "";
      return;
    }

    componentStatusCard.classList.remove("hidden");
    componentStatusGrid.innerHTML = active.map(([key, icon, label]) => {
      const entry = payload[key];
      let state  = entry ? "enabled" : "disabled";
      let detail = entry ? "✅ Enabled"  : "⏸️ Disabled";
      if (entry && typeof entry === "object") {
        state  = entry.state  || "enabled";
        detail = entry.detail || "✅ Enabled";
      }
      return `
        <div class="status-item">
          <dt>${icon} ${label}</dt>
          <dd>${state.replace(/_/g, " ")}</dd>
          <div class="desc mt-6">${detail}</div>
        </div>`;
    }).join("");
  };

  const poll = async () => {
    try {
      const response = await fetch("/api/status", { cache: "no-store" });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const payload = await response.json();
      renderHealth(payload);
      renderComponentStatus(payload);
      renderOpMode(payload);
      renderMoodDomeNote(payload);
    } catch (_error) {}
  };

  poll();
  window.setInterval(poll, 2000);

  // ---- Live Log Console (SSE push) ----
  // Backfill and live lines arrive via event:log on /api/events.
  // GET /api/logs is preserved for curl/diagnostics but not used by this page.
  const logPaused = document.getElementById("log-paused");

  function appendLogLine(text) {
    if (!logConsole) return;
    // Trim oldest lines when buffer exceeds 250 to keep the DOM light.
    const raw = logConsole.textContent;
    if (raw.length > 0 && raw !== "Waiting for logs…") {
      const lines = raw.split("\n");
      if (lines.length > 250) {
        logConsole.textContent = lines.slice(lines.length - 200).join("\n") + "\n";
      }
    } else {
      logConsole.textContent = "";
    }
    logConsole.textContent += text + "\n";
    // Auto-scroll unless the user has scrolled up.
    const threshold = 50;
    const atBottom =
      logConsole.scrollTop + logConsole.clientHeight >= logConsole.scrollHeight - threshold;
    if (atBottom) {
      logConsole.scrollTop = logConsole.scrollHeight;
      if (logPaused) logPaused.classList.remove("visible");
    } else {
      if (logPaused) logPaused.classList.add("visible");
    }
  }

  // Resume auto-scroll when the user scrolls back to the bottom.
  logConsole?.addEventListener("scroll", () => {
    const threshold = 50;
    const atBottom =
      logConsole.scrollTop + logConsole.clientHeight >= logConsole.scrollHeight - threshold;
    if (atBottom && logPaused) logPaused.classList.remove("visible");
  });

  if (typeof EventSource !== "undefined" && logConsole) {
    const logSource = new EventSource("/api/events");
    logSource.addEventListener("log", (e) => appendLogLine(e.data));
  }

  // ---- Operation Mode ----
  const opmodeDrive      = document.getElementById("opmode-drive");
  const opmodeStationary = document.getElementById("opmode-stationary");

  function renderOpMode(payload) {
    if (!opmodeDrive || !opmodeStationary) return;
    const isStationary = !!payload.stationary;
    opmodeDrive.classList.toggle("active", !isStationary);
    opmodeStationary.classList.toggle("active", isStationary);
  }

  const sendManualCommand = async (cmd) => {
    const body = new URLSearchParams();
    body.set("command", cmd);
    try {
      await fetch("/api/manual-command", { method: "POST", body });
    } catch (_e) {}
  };

  const setMode = async (mode) => {
    const body = new URLSearchParams();
    body.set("mode", mode);
    try {
      await fetch("/api/mode", { method: "POST", body });
    } catch (_e) {}
  };

  opmodeDrive?.addEventListener("click", () => {
    opmodeDrive.classList.add("active");
    opmodeStationary.classList.remove("active");
    setMode("driving");
  });
  opmodeStationary?.addEventListener("click", () => {
    opmodeStationary.classList.add("active");
    opmodeDrive.classList.remove("active");
    setMode("stationary");
  });

  // ---- Mood Selector ----
  const moodDomeNote = document.getElementById("mood-dome-note");

  function renderMoodDomeNote(payload) {
    if (!moodDomeNote) return;
    const domeConnected = !!payload.domeConnected;
    moodDomeNote.classList.toggle("visible", !domeConnected);
  }

  document.querySelectorAll(".mood-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const cmd = btn.dataset.cmd;
      if (!cmd) return;
      // Optimistic active state — mark clicked button active
      document.querySelectorAll(".mood-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      sendManualCommand(cmd);
    });
  });
})();

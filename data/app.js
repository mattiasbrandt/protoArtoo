// =============================================================================
// app.js
//
// Home dashboard controller.
// - Uses shared status stream (SSE-first, polling fallback)
// - Provides truthful mode/mood command UX with rollback on failure
// - Renders system health, component status, live logs, and status snapshots
// =============================================================================
(() => {
  const logConsole = document.getElementById("log-console");
  const logPaused = document.getElementById("log-paused");
  const logCommandInput = document.getElementById("log-command-input");
  const componentStatusCard = document.getElementById("component-status-card");
  const componentStatusGrid = document.getElementById("component-status-grid");

  const opmodeDrive = document.getElementById("opmode-drive");
  const opmodeStationary = document.getElementById("opmode-stationary");
  const opmodeFeedback = document.getElementById("opmode-feedback");

  const moodFeedback = document.getElementById("mood-feedback");

  const estopToggle = document.getElementById("estop-toggle");
  const estopFeedback = document.getElementById("estop-feedback");
  const sleepToggle = document.getElementById("sleep-toggle");
  const sleepOverlay = document.getElementById("sleep-overlay");
  const sleepOverlayWake = document.getElementById("sleep-overlay-wake");
  const sleepFeedback = document.getElementById("sleep-feedback");
  const topbarReboot = document.getElementById("topbar-reboot");
  const rebootFeedback = document.getElementById("reboot-feedback");
  const staleBanner = document.getElementById('status-stale-banner');
  const setStale = (stale, options = {}) => {
    const rerender = options.rerender !== false;
    statusIsStale = stale === true;
    if (staleBanner) staleBanner.style.display = statusIsStale ? "" : "none";
    if (rerender && lastStatus) renderHealth(lastStatus);
  };
  const snapshotWebControl = document.getElementById("snapshot-web-control");
  const snapshotMode = document.getElementById("snapshot-mode");
  const snapshotEstop = document.getElementById("snapshot-estop");
  const snapshotMood = document.getElementById("snapshot-mood");

  let lastStatus = null;
  let statusIsStale = false;
  let modePending = false;
  let moodPending = false;
  let pollFailCount = 0;
  let estopPending = false;
  let sleepPending = false;
  let isSleeping = false;
  let isEstopLatched = false;
  let estopStateKnown = false;
  let rebootPending = false;

  const INDICATOR_TEXT = {
    'h-sbus':      'ht-sbus',
    'h-wifi':      'ht-wifi',
    'h-fs':        'ht-fs',
    'h-heap':      'ht-heap',
    'h-dome-link': 'ht-dome-link',
    'h-sound':     'ht-sound',
    'h-dome-esc': 'ht-dome-esc',
  };
  const HEALTH_SIGNAL_MODEL = window.PAHealthSignals;
  const INDICATOR_STATE_LABELS = HEALTH_SIGNAL_MODEL?.INDICATOR_STATE_LABELS || {
    ok: "OK",
    warn: "WARN",
    fail: "FAIL",
    off: "OFF",
  };

  const COMPONENT_LABELS = [
    ["arm1", "🦾", "Left Arm"],
    ["arm2", "🦾", "Right Arm"],
    ["aux1", "🦾", "Aux Servo 1"],
    ["aux2", "🦾", "Aux Servo 2"],
    ["aux3", "🦾", "Aux Servo 3"],
    ["dome", "🔄", "Dome Motor"],
    ["rcCh1", "🕹️", "RC Channel 1"],
    ["rcCh2", "🕹️", "RC Channel 2"],
    ["rcCh3", "🕹️", "RC Channel 3"],
    ["rcCh4", "🕹️", "RC Channel 4"],
    ["rcCh5", "🕹️", "RC Channel 5"],
    ["rcCh6", "🕹️", "RC Channel 6"],
    ["s1Hoverboard", "🔌", "Hoverboard Drive"],
    ["s2Sound", "🔊", "Sound Module"],
    ["s3DomeCtrl", "🔌", "protoR2link"],
  ];

  const MOOD_LABELS = {
    0: "Idle 😐",
    10: "Quiet 🤐",
    11: "Full-Awake 😄",
    13: "Mid-Awake 🙂",
    14: "Awake+ ✨",
  };

  const PILL_CLASS_MAP = {
    ok: "pill-ok",
    warn: "pill-warn",
    error: "pill-error",
    info: "pill-info",
  };

  const escapeHtml = (value) => String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");

  const showFeedback = (el, message, level = "") => {
    if (!el) return;
    if (!el.dataset.baseClass) {
      el.dataset.baseClass = el.className || "feedback";
    }
    el.textContent = message;
    el.className = level ? `${el.dataset.baseClass} ${level}` : el.dataset.baseClass;
  };

  const setEstopPending = (pending) => {
    estopPending = pending;
    if (!estopToggle) return;
    estopToggle.disabled = pending;
    estopToggle.classList.toggle("is-pending", pending);
    estopToggle.setAttribute("aria-disabled", pending ? "true" : "false");
  };

  const setEstopUi = (latched) => {
    isEstopLatched = !!latched;
    if (!estopToggle) return;
    estopToggle.classList.toggle("danger", isEstopLatched);
    estopToggle.title = isEstopLatched ? "Clear E-Stop" : "Latch E-Stop";
    estopToggle.setAttribute("aria-pressed", isEstopLatched.toString());
    if (!estopStateKnown) {
      estopStateKnown = true;
      estopToggle.disabled = false;
    }
  };

  const setSleepPending = (pending) => {
    sleepPending = pending;
    [sleepToggle, sleepOverlayWake].forEach((el) => {
      if (!el) return;
      el.disabled = pending;
      el.classList.toggle("is-pending", pending);
      el.setAttribute("aria-disabled", pending ? "true" : "false");
    });
  };

  const setSleepUi = (sleeping) => {
    isSleeping = !!sleeping;
    if (sleepToggle) {
      sleepToggle.textContent = isSleeping ? "💤 Wake" : "💤 Sleep";
      sleepToggle.title = isSleeping ? "Wake droid subsystems" : "Park droid subsystems";
      sleepToggle.classList.toggle("danger", isSleeping);
      sleepToggle.classList.toggle("accent", isSleeping);
      sleepToggle.setAttribute("aria-pressed", isSleeping.toString());
    }
    if (sleepOverlay) {
      sleepOverlay.classList.toggle("active", isSleeping);
      sleepOverlay.setAttribute("aria-hidden", (!isSleeping).toString());
    }
    document.body.classList.toggle("sleep-mode-active", isSleeping);
  };


  const setIndicator = (id, state, reason = "", detail = "") => {
    const el = document.getElementById(id);
    if (!el) return;
    el.className = `indicator ${state}`;
    const textEl = INDICATOR_TEXT[id] ? document.getElementById(INDICATOR_TEXT[id]) : null;
    if (textEl) {
      const label = INDICATOR_STATE_LABELS[state] || String(state).toUpperCase();
      textEl.textContent = reason ? `${label}: ${reason}` : label;

      if (detail) {
        textEl.title = detail;
      } else {
        textEl.removeAttribute("title");
      }
    }
  };

  const renderHealth = (payload) => {
    if (!HEALTH_SIGNAL_MODEL || typeof HEALTH_SIGNAL_MODEL.deriveHealthSignals !== "function") {
      Object.keys(INDICATOR_TEXT).forEach((id) => {
        setIndicator(id, "warn", "Health model missing", "health_signals.js failed to load");
      });
      return;
    }

    const signals = HEALTH_SIGNAL_MODEL.deriveHealthSignals(payload, { stale: statusIsStale });
    signals.forEach(({ id, state, reason, detail }) => setIndicator(id, state, reason, detail));
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
      let state = entry ? "enabled" : "disabled";
      let detail = entry ? "✅ Enabled" : "⏸️ Disabled";
      if (entry && typeof entry === "object") {
        state = entry.state || "enabled";
        detail = entry.detail || "✅ Enabled";
      }
      const stateText = String(state).replace(/_/g, " ");
      const safeState = escapeHtml(stateText);
      const safeDetail = escapeHtml(detail);
      let transportLine = "";
      if (key === "s3DomeCtrl" && payload.dome_link?.state === "connected") {
        const transport = payload.dome_link?.transport;
        const transportText = transport === "uart" ? "via UART (slip ring)"
          : transport === "wifi" ? "via WiFi (fallback)"
          : "";
        if (transportText) {
          transportLine = `<div class="desc mt-6">${escapeHtml(transportText)}</div>`;
        }
      }
      return `
        <div class="status-item">
          <dt>${icon} ${label}</dt>
          <dd>${safeState}</dd>
          <div class="desc mt-6">${safeDetail}</div>${transportLine}
        </div>`;
    }).join("");
  };

  const renderOpMode = (payload) => {
    if (!opmodeDrive || !opmodeStationary) return;
    const isStationary = !!payload.stationary;
    opmodeDrive.classList.toggle("active", !isStationary);
    opmodeStationary.classList.toggle("active", isStationary);
    opmodeDrive.setAttribute("aria-pressed", (!isStationary).toString());
    opmodeStationary.setAttribute("aria-pressed", isStationary.toString());
  };

  const renderActiveMood = (payload) => {
    const activeMood = payload.activeMood || 0;
    document.querySelectorAll(".mood-btn").forEach((btn) => {
      const match = btn.dataset.cmd?.match(/:SE(\d+)/);
      const btnMood = match ? Number.parseInt(match[1], 10) : 0;
      const isActive = btnMood !== 0 && btnMood === activeMood;
      btn.classList.toggle("active", isActive);
      btn.setAttribute("aria-pressed", isActive.toString());
    });
  };

  const setStatusPill = (el, text, state = "info", compact = true) => {
    if (!el) return;
    const sizeClass = compact ? "status-pill status-pill-compact" : "status-pill";
    el.textContent = text;
    el.className = `${sizeClass} ${PILL_CLASS_MAP[state] || PILL_CLASS_MAP.info}`;
  };

  const renderMissionSnapshot = (payload) => {
    const isStationary = !!payload.stationary;
    const moodText = MOOD_LABELS[payload.activeMood] || `Mood ${payload.activeMood || 0}`;

    setStatusPill(
      snapshotWebControl,
      payload.webControlEnabled ? "🕹️ Web control: Enabled" : "🕹️ Web control: Disabled",
      payload.webControlEnabled ? "ok" : "warn",
    );
    setStatusPill(
      snapshotMode,
      isStationary ? "🧭 Mode: Stationary" : "🧭 Mode: Driving",
      isStationary ? "warn" : "ok",
    );
    setStatusPill(
      snapshotEstop,
      payload.estop ? "🛑 E-Stop: Latched" : "🛑 E-Stop: Clear",
      payload.estop ? "error" : "ok",
    );
    setStatusPill(snapshotMood, `🎭 Mood: ${moodText}`, "info");
  };

  const applyStatus = (payload) => {
    lastStatus = payload;
    pollFailCount = 0;
    setStale(false, { rerender: false });
    renderHealth(payload);
    renderComponentStatus(payload);
    renderMissionSnapshot(payload);
    renderOpMode(payload);
    renderActiveMood(payload);
    setEstopUi(!!payload.estop);
    setSleepUi(!!payload.sleepMode);
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { cache: "no-store", timeoutMs: 3000 });
    applyStatus(result.data);
  };

  const toggleSleepWake = async (forceWake = false) => {
    if (!window.PAApi || sleepPending) return;
    const targetSleep = forceWake ? false : !isSleeping;
    setSleepPending(true);
    showFeedback(sleepFeedback, targetSleep ? "Entering sleep mode..." : "Waking droid...");

    try {
      await window.PAApi.postForm(targetSleep ? "/api/sleep" : "/api/wake", {}, { timeoutMs: 3000 });
      await refreshStatusOnce();
      showFeedback(sleepFeedback, targetSleep ? "Sleep mode enabled" : "Droid awake", "success");
    } catch (error) {
      showFeedback(
        sleepFeedback,
        `Sleep toggle failed: ${window.PAApi.messageFor(error)}`,
        "error"
      );
      if (lastStatus) setSleepUi(!!lastStatus.sleepMode);
    } finally {
      setSleepPending(false);
    }
  };

  const toggleEstop = async () => {
    if (!window.PAApi || estopPending) return;
    const targetLatched = !isEstopLatched;
    setEstopPending(true);
    showFeedback(estopFeedback, targetLatched ? "Latching E-Stop..." : "Clearing E-Stop...");

    try {
      await window.PAApi.postForm(targetLatched ? "/api/estop" : "/api/estop/clear", {}, { timeoutMs: 3000 });
      await refreshStatusOnce();
      showFeedback(estopFeedback, targetLatched ? "E-Stop latched" : "E-Stop clear", "success");
    } catch (error) {
      showFeedback(estopFeedback, `E-Stop failed: ${window.PAApi.messageFor(error)}`, "error");
      if (lastStatus) setEstopUi(!!lastStatus.estop);
    } finally {
      setEstopPending(false);
    }
  };

  const rebootController = async () => {
    if (!window.PAApi || rebootPending) return;
    rebootPending = true;
    if (topbarReboot) {
      topbarReboot.disabled = true;
      topbarReboot.classList.add("is-pending");
      topbarReboot.setAttribute("aria-disabled", "true");
    }
    showFeedback(rebootFeedback, "Rebooting...");

    try {
      await window.PAApi.postForm("/api/reboot", {}, { timeoutMs: 3000 });
      showFeedback(rebootFeedback, "Rebooting...", "success");
    } catch (error) {
      showFeedback(rebootFeedback, `Reboot failed: ${window.PAApi.messageFor(error)}`, "error");
      rebootPending = false;
      if (topbarReboot) {
        topbarReboot.disabled = false;
        topbarReboot.classList.remove("is-pending");
        topbarReboot.setAttribute("aria-disabled", "false");
      }
    }
  };

  const setModePending = (pending) => {
    modePending = pending;
    if (opmodeDrive) opmodeDrive.disabled = pending;
    if (opmodeStationary) opmodeStationary.disabled = pending;
    document.querySelectorAll(".opmode-btn").forEach((btn) => {
      btn.classList.toggle("is-pending", pending);
      btn.setAttribute("aria-disabled", pending ? "true" : "false");
    });
  };

  const setMoodPending = (pending) => {
    moodPending = pending;
    document.querySelectorAll(".mood-btn").forEach((btn) => {
      btn.disabled = pending;
      btn.classList.toggle("is-pending", pending);
      btn.setAttribute("aria-disabled", pending ? "true" : "false");
    });
  };

  const setMode = async (mode) => {
    if (!window.PAApi || modePending) return;
    setModePending(true);
    showFeedback(opmodeFeedback, `Setting mode to ${mode}...`);

    try {
      await window.PAApi.postForm("/api/mode", { mode }, { timeoutMs: 3000 });
      await refreshStatusOnce();
      showFeedback(opmodeFeedback, "Mode updated", "success");
    } catch (error) {
      if (lastStatus) renderOpMode(lastStatus);
      showFeedback(opmodeFeedback, `Mode update failed: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      setModePending(false);
    }
  };

  const setMood = async (moodId) => {
    if (!window.PAApi || moodPending) return;
    setMoodPending(true);
    showFeedback(moodFeedback, `Applying mood SE${moodId}...`);

    try {
      await window.PAApi.postForm("/api/mood", { mood: String(moodId) }, { timeoutMs: 3000 });
      await refreshStatusOnce();
      showFeedback(moodFeedback, "Mood updated", "success");
    } catch (error) {
      if (lastStatus) renderActiveMood(lastStatus);
      showFeedback(moodFeedback, `Mood update failed: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      setMoodPending(false);
    }
  };

  const LOG_MAX_LINES = 250;
  const LOG_TRIM_LINES = 200;
  const LOG_EMPTY_TEXT = "No log history available yet.";
  const COMMAND_HISTORY_MAX = 20;
  let logLines = [];
  let commandTokens = [];
  let commandHistory = [];
  let commandHistoryIndex = -1;
  let logSelectionActive = false;

  const normalizeLogMessage = (line) => String(line ?? "").trim();

  const timestampNow = () => new Date().toTimeString().slice(0, 8);

  const levelClassForMessage = (message) => {
    const match = String(message ?? "").match(/^\[([EWID])\]/);
    if (!match) return "";
    if (match[1] === "E") return " log-line-error";
    if (match[1] === "W") return " log-line-warn";
    if (match[1] === "D") return " log-line-debug";
    return "";
  };

  const makeLogEntry = (message, { timestamp = timestampNow(), extraClass = "" } = {}) => ({
    timestamp,
    message: normalizeLogMessage(message),
    extraClass,
  });

  const logEntryHtml = (line) => {
    const classes = `log-line${levelClassForMessage(line.message)}${line.extraClass || ""}`;
    const ts = line.timestamp && line.timestamp !== "--:--:--" ? `[${escapeHtml(line.timestamp)}] ` : "";
    return `<span class="${classes}">${ts}${escapeHtml(line.message)}</span>`;
  };

  const hasActiveLogSelection = () => {
    if (!logConsole || !window.getSelection) return false;
    const selection = window.getSelection();
    if (!selection || selection.isCollapsed || selection.rangeCount === 0) return false;
    const range = selection.getRangeAt(0);
    return logConsole.contains(range.commonAncestorContainer);
  };

  const isLogAtBottom = () => {
    if (!logConsole) return true;
    const threshold = 50;
    return logConsole.scrollTop + logConsole.clientHeight >= logConsole.scrollHeight - threshold;
  };

  const renderLogConsole = (stickToBottom = false) => {
    if (!logConsole) return;
    if (logLines.length === 0) {
      logConsole.innerHTML = `<span class="log-line">${escapeHtml(LOG_EMPTY_TEXT)}</span>`;
    } else {
      logConsole.innerHTML = logLines.map((line) => logEntryHtml(line)).join("\n");
    }
    if (stickToBottom && !hasActiveLogSelection()) {
      logConsole.scrollTop = logConsole.scrollHeight;
      logPaused?.classList.remove("visible");
    } else {
      logPaused?.classList.add("visible");
    }
  };

  const setLogLines = (lines, { forceBottom = true } = {}) => {
    logLines = lines
      .map((line) => makeLogEntry(line, { timestamp: "--:--:--" }))
      .filter((line) => line.message.length > 0)
      .slice(-LOG_MAX_LINES);
    const stickToBottom = (forceBottom || isLogAtBottom()) && !hasActiveLogSelection();
    renderLogConsole(stickToBottom);
  };

  const appendLogLine = (text, options = {}) => {
    if (!logConsole) return;
    const message = normalizeLogMessage(text);
    if (!message) return;

    const stickToBottom = isLogAtBottom() && !hasActiveLogSelection();
    const entry = makeLogEntry(message, options);
    const wasEmpty = logLines.length === 0;
    logLines.push(entry);
    let didTrim = false;
    if (logLines.length > LOG_MAX_LINES) {
      logLines = logLines.slice(logLines.length - LOG_TRIM_LINES);
      didTrim = true;
    }
    if (hasActiveLogSelection()) {
      logSelectionActive = true;
      logPaused?.classList.add("visible");
      return;
    }
    if (didTrim) {
      renderLogConsole(stickToBottom);
      return;
    }
    if (wasEmpty) {
      logConsole.innerHTML = logEntryHtml(entry);
    } else {
      logConsole.insertAdjacentHTML("beforeend", `\n${logEntryHtml(entry)}`);
    }
    if (stickToBottom) {
      logConsole.scrollTop = logConsole.scrollHeight;
      logPaused?.classList.remove("visible");
    } else {
      logPaused?.classList.add("visible");
    }
  };

  const loadRecentLogs = async () => {
    if (!window.PAApi || !logConsole) return;
    try {
      const result = await window.PAApi.get("/api/logs", { cache: "no-store", timeoutMs: 3000 });
      if (logLines.length > 0) return;
      const historyLines = String(result.data ?? "")
        .split(/\r?\n/)
        .map((line) => normalizeLogMessage(line.trimEnd()))
        .filter((line) => line.length > 0);
      setLogLines(historyLines);
    } catch (error) {
      if (logLines.length === 0) {
        setLogLines([]);
      }
    }
  };

  logConsole?.addEventListener("scroll", () => {
    if (isLogAtBottom() && !hasActiveLogSelection()) {
      logPaused?.classList.remove("visible");
    }
  });

  document.addEventListener("selectionchange", () => {
    if (!logConsole) return;
    if (hasActiveLogSelection()) {
      logSelectionActive = true;
      logPaused?.classList.add("visible");
    } else if (logSelectionActive) {
      logSelectionActive = false;
      renderLogConsole(isLogAtBottom());
    }
  });

  const loadCommandTokens = async () => {
    if (!window.PAApi) return;
    try {
      const result = await window.PAApi.get("/api/actions", { cache: "no-store", timeoutMs: 5000 });
      if (!Array.isArray(result.data)) return;
      commandTokens = result.data
        .filter((entry) => entry && entry.testable === true && typeof entry.token === "string")
        .map((entry) => entry.token)
        .sort((a, b) => a.localeCompare(b));
    } catch (_error) {
      commandTokens = [];
    }
  };

  const appendCommandLine = (text, extraClass = " log-line-command") => {
    appendLogLine(text, { extraClass });
  };

  const printCommandHelp = () => {
    if (commandTokens.length === 0) {
      appendCommandLine("[ERROR] action list unavailable", " log-line-command-error");
      return;
    }
    appendCommandLine(`available commands: ${commandTokens.join(" ")}`);
  };

  const rememberCommand = (token) => {
    if (!token) return;
    if (commandHistory[commandHistory.length - 1] !== token) {
      commandHistory.push(token);
      if (commandHistory.length > COMMAND_HISTORY_MAX) {
        commandHistory = commandHistory.slice(commandHistory.length - COMMAND_HISTORY_MAX);
      }
    }
    commandHistoryIndex = commandHistory.length;
  };

  const dispatchConsoleCommand = async (rawToken) => {
    const token = normalizeLogMessage(rawToken);
    if (!token) return;
    rememberCommand(token);
    appendCommandLine(`> ${token}`);

    if (token === "help" || token === "?") {
      printCommandHelp();
      return;
    }

    if (!commandTokens.includes(token)) {
      appendCommandLine(`[ERROR] unknown command: ${token}`, " log-line-command-error");
      return;
    }

    if (!window.PAApi) {
      appendCommandLine("[ERROR] API unavailable", " log-line-command-error");
      return;
    }

    try {
      await window.PAApi.postForm("/api/actions/test", { token }, { timeoutMs: 5000 });
      appendCommandLine(`[OK] ${token}`);
    } catch (error) {
      appendCommandLine(`[ERROR] ${window.PAApi.messageFor(error)}`, " log-line-command-error");
    }
  };

  const commonPrefix = (values) => {
    if (values.length === 0) return "";
    let prefix = values[0];
    for (let i = 1; i < values.length && prefix.length > 0; i += 1) {
      while (!values[i].startsWith(prefix)) {
        prefix = prefix.slice(0, -1);
      }
    }
    return prefix;
  };

  const completeConsoleCommand = () => {
    if (!logCommandInput) return;
    const partial = normalizeLogMessage(logCommandInput.value);
    if (!partial) {
      printCommandHelp();
      return;
    }
    const matches = commandTokens.filter((token) => token.startsWith(partial));
    if (matches.length === 1) {
      logCommandInput.value = matches[0];
      return;
    }
    if (matches.length > 1) {
      const shared = commonPrefix(matches);
      if (shared.length > partial.length) {
        logCommandInput.value = shared;
        return;
      }
      appendCommandLine(matches.join(" "));
      return;
    }
    appendCommandLine(`[ERROR] unknown command: ${partial}`, " log-line-command-error");
  };

  logCommandInput?.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      const token = logCommandInput.value;
      logCommandInput.value = "";
      dispatchConsoleCommand(token);
      return;
    }
    if (event.key === "Tab") {
      event.preventDefault();
      completeConsoleCommand();
      return;
    }
    if (event.key === "ArrowUp") {
      if (commandHistory.length === 0) return;
      event.preventDefault();
      commandHistoryIndex = Math.max(0, commandHistoryIndex - 1);
      logCommandInput.value = commandHistory[commandHistoryIndex] || "";
      logCommandInput.setSelectionRange(logCommandInput.value.length, logCommandInput.value.length);
      return;
    }
    if (event.key === "ArrowDown") {
      if (commandHistory.length === 0) return;
      event.preventDefault();
      commandHistoryIndex = Math.min(commandHistory.length, commandHistoryIndex + 1);
      logCommandInput.value = commandHistoryIndex >= commandHistory.length
        ? ""
        : commandHistory[commandHistoryIndex];
      logCommandInput.setSelectionRange(logCommandInput.value.length, logCommandInput.value.length);
    }
  });


  opmodeDrive?.addEventListener("click", () => setMode("driving"));
  opmodeStationary?.addEventListener("click", () => setMode("stationary"));

  document.querySelectorAll(".mood-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const match = btn.dataset.cmd?.match(/:SE(\d+)/);
      if (!match) return;
      setMood(match[1]);
    });
  });

  estopToggle?.addEventListener("click", toggleEstop);
  sleepToggle?.addEventListener("click", () => toggleSleepWake(false));
  sleepOverlayWake?.addEventListener("click", () => toggleSleepWake(true));
  topbarReboot?.addEventListener("click", rebootController);


  loadRecentLogs();
  loadCommandTokens();

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") {
        applyStatus(payload);
      }
      if (eventType === "log") payload.split("\x01").forEach((line) => appendLogLine(line));
      if (eventType === "stream_error") {
        appendLogLine("[connection lost — retrying…]");
        setStale(true);
      }
    });

    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatusOnce().catch(() => {
        // Fallback polling below handles temporary fetch failures.
      });
    }
  } else {
    const refreshFromFallback = () => {
      refreshStatusOnce().catch(() => {
        pollFailCount++;
        if (pollFailCount >= 2) setStale(true);
      });
    };

    refreshFromFallback();

    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshFromFallback();
    }, 3000);

    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState !== "hidden") {
        refreshFromFallback();
      }
    });
  }
  setEstopUi(false);
  setSleepUi(false);
})();

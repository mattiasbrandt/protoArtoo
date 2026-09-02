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
  const logLevelPill = document.getElementById("log-level-pill");
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
    ["arm1", "🦾", "Utility Arm 1"],
    ["arm2", "🦾", "Utility Arm 2"],
    ["aux1", "🦾", "AUX 1"],
    ["aux2", "🦾", "AUX 2"],
    ["aux3", "🦾", "AUX 3"],
    ["domeEsc", "🔄", "Dome ESC"],
    ["rcCh1", "🕹️", "RC Channel 1"],
    ["rcCh2", "🕹️", "RC Channel 2"],
    ["rcCh3", "🕹️", "RC Channel 3"],
    ["rcCh4", "🕹️", "RC Channel 4"],
    ["rcCh5", "🕹️", "RC Channel 5"],
    ["rcCh6", "🕹️", "RC Channel 6"],
    ["drive", "🔌", "Drive"],
    ["audio", "🔊", "Audio"],
    ["protoR2link", "🔌", "protoR2link"],
  ];

  const MOOD_LABELS = {
    0: "Idle 😐",
    10: "Quiet 🤐",
    11: "Full-Awake 😄",
    13: "Mid-Awake 😐",
    14: "Awake+ 🤩",
  };

  const PILL_CLASS_MAP = {
    ok: "pill-ok",
    warn: "pill-warn",
    error: "pill-error",
    info: "pill-info",
  };

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

  let renderedComponentIds = null;

  const renderComponentStatus = (payload) => {
    if (!componentStatusCard || !componentStatusGrid) return;

    const active = COMPONENT_LABELS.filter(([key]) => key in payload);
    if (active.length === 0) {
      componentStatusCard.classList.add("hidden");
      componentStatusGrid.innerHTML = "";
      renderedComponentIds = null;
      return;
    }

    componentStatusCard.classList.remove("hidden");

    // Build signature: component IDs + flags that affect transport lines
    const transportFlags = [
      payload.dome_link?.state === "connected" && payload.dome_link?.uart_owned_by_dome ? "dome-uart" : "",
      payload.audio?.rx_status === "blocked_by_dome_uart" ? "sound-blocked" : ""
    ].filter(Boolean).join(",");
    const signature = active.map(([key]) => key).join(",") + "|" + transportFlags;

    // Rebuild only if component IDs or transport flags changed
    if (signature !== renderedComponentIds) {
      renderedComponentIds = signature;
      const items = active.map(([key, icon, label]) => {
        const entry = payload[key];
        let state = entry ? "enabled" : "disabled";
        let detail = entry ? "✅ Enabled" : "⏸️ Disabled";
        if (entry && typeof entry === "object") {
          state = entry.state || "enabled";
          detail = entry.detail || "✅ Enabled";
        }
        const stateText = String(state).replace(/_/g, " ");
        const safeState = window.PAUtils.escapeHtml(stateText);
        const safeDetail = window.PAUtils.escapeHtml(detail);
        let transportLine = "";
        if (key === "protoR2link" && payload.dome_link?.state === "connected") {
          if (payload.dome_link?.uart_owned_by_dome === true) {
            transportLine = `<div class="desc mt-6">${window.PAUtils.escapeHtml("UART2 owned by protoR2link")}</div>`;
          }
        }
        if (key === "audio" && entry?.rx_status === "blocked_by_dome_uart") {
          transportLine += `<div class="desc mt-6">${window.PAUtils.escapeHtml("CHIRP RX unavailable while protoR2link owns UART2")}</div>`;
        }
        return `
        <div class="status-item" id="comp-${key}">
          <dt>${icon} ${label}</dt>
          <dd id="state-${key}">${safeState}</dd>
          <div class="desc mt-6" id="detail-${key}">${safeDetail}</div>${transportLine}
        </div>`;
      }).join("");
      componentStatusGrid.innerHTML = `<dl class="status-grid">${items}</dl>`;
    } else {
      // Patch only the text content when component set hasn't changed
      active.forEach(([key]) => {
        const entry = payload[key];
        let state = entry ? "enabled" : "disabled";
        let detail = entry ? "✅ Enabled" : "⏸️ Disabled";
        if (entry && typeof entry === "object") {
          state = entry.state || "enabled";
          detail = entry.detail || "✅ Enabled";
        }
        const stateText = String(state).replace(/_/g, " ");
        const safeState = window.PAUtils.escapeHtml(stateText);
        const safeDetail = window.PAUtils.escapeHtml(detail);

        const stateEl = document.getElementById(`state-${key}`);
        if (stateEl) stateEl.textContent = safeState;

        const detailEl = document.getElementById(`detail-${key}`);
        if (detailEl) detailEl.textContent = safeDetail;
      });
    }
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
    setStatusPill(snapshotMood, `🎬 Mood: ${moodText}`, "info");
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

  const refreshStatusOnce = async ({ handle } = {}) => {
    if (!window.PAApi) return;
    // When called as a section loader, handle is always present and carries the
    // section's deadline. When called from non-section contexts (fallback polling),
    // handle is absent and we use PAApi directly (which uses DEFAULT_TIMEOUT_MS).
    const api = handle || window.PAApi;
    const result = await api.get("/api/status", { cache: "no-store" });
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
      await window.PAApi.estopPostForm(targetLatched ? "/api/estop" : "/api/estop/clear", {}, { timeoutMs: 3000 });
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
  // One phrase for "the page is not getting log data from the controller",
  // shared by the live stream's error path and by a log fetch whose response
  // did not come back as log text (#261). Two phrases for one fact is the copy
  // defect docs/ui-copy-voice.md exists to prevent.
  const LOG_UNREACHABLE_TEXT = "[connection lost — retrying…]";
  const COMMAND_HISTORY_MAX = 20;
  const CONSOLE_HISTORY_STORAGE_KEY = "pa-console-history";
  let logLines = [];
  // Whether a /api/logs body has actually been applied as history. The load
  // guard has to ask THIS, not "is the panel non-empty": a stream-error notice
  // or a streamed line fills logLines without any history behind it, and
  // keying the guard on logLines.length made a successful retry after a
  // refusal return immediately, resolve, and leave the bootstrap reporting the
  // section done with the ring never loaded (#261). Only applyLogHistory()
  // sets it, so it cannot drift from the fact it names.
  let logHistoryLoaded = false;
  let logSelectionActive = false;

  // Persistent Console command history (Up/Down), surviving a page reload.
  // Reads and writes are wrapped defensively: a browser with site data
  // blocked (private mode, storage quota, disabled cookies/storage) must
  // still render and operate the command box - it just keeps history for
  // the current page load only instead of across a reload.
  const readStoredCommandHistory = () => {
    try {
      const raw = window.localStorage.getItem(CONSOLE_HISTORY_STORAGE_KEY);
      if (!raw) return [];
      const parsed = JSON.parse(raw);
      if (!Array.isArray(parsed)) return [];
      return parsed
        .filter((entry) => typeof entry === "string" && entry.length > 0)
        .slice(-COMMAND_HISTORY_MAX);
    } catch (error) {
      return [];
    }
  };

  const writeStoredCommandHistory = (history) => {
    try {
      window.localStorage.setItem(CONSOLE_HISTORY_STORAGE_KEY, JSON.stringify(history));
    } catch (error) {
      // Site data blocked, storage full, or a private-mode restriction:
      // history stays in-memory for this page load rather than failing the
      // command box.
    }
  };

  let commandHistory = readStoredCommandHistory();
  let commandHistoryIndex = commandHistory.length;

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
    const ts = line.timestamp && line.timestamp !== "--:--:--" ? `[${window.PAUtils.escapeHtml(line.timestamp)}] ` : "";
    return `<span class="${classes}">${ts}${window.PAUtils.escapeHtml(line.message)}</span>`;
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
      logConsole.innerHTML = `<span class="log-line">${window.PAUtils.escapeHtml(LOG_EMPTY_TEXT)}</span>`;
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

  // Paints a single line in the log panel without entering it into logLines.
  // A notice is not log output, and the model holds log output: keeping it out
  // means a later load replaces it instead of stranding it inside the history,
  // and it never takes a slot in the trim window.
  //
  // It writes only while the model is empty, for the same reason. With lines
  // already in logLines the panel is not blank and needs no notice, and
  // overwriting innerHTML there would drop rendered lines the model still
  // holds - the next appendLogLine() appends to a panel that no longer matches
  // its own model.
  const renderLogNotice = (message) => {
    if (!logConsole || logLines.length > 0) return;
    logConsole.innerHTML = logEntryHtml(makeLogEntry(message, { timestamp: "--:--:--" }));
  };

  // Applies the log ring as history. It goes in FRONT of whatever is already
  // in the panel rather than replacing it: on the first load nothing can have
  // streamed yet - status_stream.js opens /api/events only once the bootstrap
  // has settled every section (page_bootstrap.js announceAssetsOnce()) - but a
  // retry runs with the stream live, and those lines are newer than the ring.
  // Replacing them would drop log output the operator has already seen.
  const applyLogHistory = (lines) => {
    const history = lines
      .map((line) => makeLogEntry(line, { timestamp: "--:--:--" }))
      .filter((line) => line.message.length > 0);
    logLines = [...history, ...logLines].slice(-LOG_MAX_LINES);
    logHistoryLoaded = true;
    renderLogConsole(!hasActiveLogSelection());
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

  // GET /api/logs answers text/plain on both of its exits - the ring body at
  // 200 and "log buffer unavailable" at 503 (src/web/api_logs.cpp) - so the
  // content type IS the contract, and no shape check over free-form log text
  // could be as reliable: a log line may contain "<div>", and a proxy's plain
  // error page could not be told apart from log output.
  //
  // Anything else arriving at 200 did not come from the log endpoint: a
  // captive portal, an intercepting proxy, or a dev fixture server falling
  // back to index.html. The panel used to split that body on newlines and
  // render it, which is how it filled with 156 lines of the dashboard's own
  // markup (#261; escaped, so wrong content rather than injection).
  const isLogTextResponse = (result) =>
    String(result?.contentType ?? "").split(";")[0].trim().toLowerCase() === "text/plain";

  const loadRecentLogs = async ({ handle = null } = {}) => {
    if (!window.PAApi || !logConsole) throw new Error("API or console unavailable");
    if (logHistoryLoaded) return;
    const api = handle ?? window.PAApi;
    let result;
    try {
      result = await api.get("/api/logs", { cache: "no-store" });
    } catch (error) {
      // Every way the fetch itself can fail - transport, timeout, and the
      // device's own 503 "log buffer unavailable" exit (src/web/api_logs.cpp) -
      // leaves the panel with nothing to show, and used to leave it literally
      // blank once the bootstrap's recovery overlay stopped covering the page.
      // Say so with the same line, then rethrow: the bootstrap still classifies
      // these separately and still honours a 503's Retry-After. Only the panel
      // copy is shared, because "the logs did not load" is one fact here.
      //
      // A cancellation is not a failure - the page cancelled its own run - and
      // an unreachable line for it would be a lie.
      if (error?.kind !== "cancelled") renderLogNotice(LOG_UNREACHABLE_TEXT);
      throw error;
    }
    if (!isLogTextResponse(result)) {
      // Not the empty state: LOG_EMPTY_TEXT would claim the controller has no
      // history, and we do not know that - we know we never reached its log
      // endpoint. Show the page's existing unreachable line and reject, so the
      // bootstrap keeps retrying with backoff instead of leaving a blank panel
      // or a silent no-op. Kind "network" is the honest classification: nothing
      // usable came back from the controller, which is also the reason
      // page_bootstrap.js renders as "Connection to the controller was lost."
      renderLogNotice(LOG_UNREACHABLE_TEXT);
      throw new window.PAApi.ApiError("Log response was not log text", {
        kind: "network",
        status: result?.status ?? 0,
      });
    }
    const historyLines = String(result.data ?? "")
      .split(/\r?\n/)
      .map((line) => normalizeLogMessage(line.trimEnd()))
      .filter((line) => line.length > 0);
    applyLogHistory(historyLines);
  };

  const LOG_LEVELS = {
    1: { label: "Error", icon: "🪵", cls: "pill-error", hint: "Loss of function only" },
    2: { label: "Warning", icon: "🪵", cls: "pill-info", hint: "Faults + safety warnings" },
    3: { label: "Info", icon: "🪵", cls: "pill-info", hint: "Boot + service health" },
    4: { label: "Debug", icon: "🪵", cls: "pill-warn", hint: "Verbose" },
  };
  let currentLogLevel = null;
  let logLevelPending = false;

  const renderLogLevelPill = (level) => {
    if (!logLevelPill) return;
    const info = LOG_LEVELS[level];
    if (!info) {
      logLevelPill.textContent = "🪵 ...";
      logLevelPill.title = "Log level unknown — click to retry";
      logLevelPill.setAttribute("aria-label", "Log level unknown. Click to retry.");
      return;
    }
    logLevelPill.className = `status-pill status-pill-compact ${info.cls}`;
    logLevelPill.textContent = `${info.icon} ${info.label}`;
    logLevelPill.title = `Log level: ${info.label} (${info.hint}) — click to cycle`;
    logLevelPill.setAttribute("aria-label", `Log level: ${info.label}. Click to cycle to the next level.`);
  };

  const loadLogLevel = async ({ handle = null } = {}) => {
    if (!window.PAApi || !logLevelPill) throw new Error("API or pill unavailable");
    const api = handle ?? window.PAApi;
    const result = await api.get("/api/config", { cache: "no-store" });
    const level = Number(result.data?.system?.logLevel);
    if (!LOG_LEVELS[level]) {
      throw new Error(`Unknown log level: ${level}`);
    }
    currentLogLevel = level;
    renderLogLevelPill(level);
  };

  const cycleLogLevel = async () => {
    if (!window.PAApi || !logLevelPill || logLevelPending) return;
    if (!LOG_LEVELS[currentLogLevel]) {
      await loadLogLevel();
      if (!LOG_LEVELS[currentLogLevel]) return;
    }
    const nextLevel = currentLogLevel >= 4 ? 1 : currentLogLevel + 1;
    const previousLevel = currentLogLevel;
    logLevelPending = true;
    currentLogLevel = nextLevel;
    renderLogLevelPill(nextLevel);
    try {
      await window.PAApi.postForm("/api/config", { logLevel: String(nextLevel) }, { timeoutMs: 3000 });
      appendCommandLine(`[UI] Log level set to ${LOG_LEVELS[nextLevel].label}`, " log-line-command");
    } catch (error) {
      currentLogLevel = previousLevel;
      renderLogLevelPill(previousLevel);
      appendCommandLine(
        `[ERROR] log level change failed: ${window.PAApi.messageFor(error)}`,
        " log-line-command-error"
      );
    } finally {
      logLevelPending = false;
    }
  };

  logLevelPill?.addEventListener("click", cycleLogLevel);

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

  // Console Tab completion catalog (ADR 0034, #238). Operation names are
  // fetched once per session and cached; a given operation's argument keys
  // are fetched (and cached) only when the operator actually Tabs one -
  // never all 175+ operations' help up front (the coordinator brief is
  // explicit about this: "fetch help <op> for the single operation being
  // completed").
  let consoleCatalogNames = [];
  const consoleOperationParamCache = new Map();
  const CONSOLE_OPERATION_PARAM_CACHE_MAX = 50;

  // Parses `operations`' item records (docs/console-protocol.md s.2:
  // "name (type[, reason])") down to the bare canonical name. Tab completes
  // canonical operations only, never aliases (the epic acceptance matrix's
  // own wording), so no alias resolution happens here.
  const parseOperationsResponse = (records) => {
    const names = [];
    for (const record of records) {
      if (!record || record.type !== "item" || typeof record.value !== "string") continue;
      const parenIndex = record.value.indexOf(" (");
      names.push(parenIndex === -1 ? record.value : record.value.slice(0, parenIndex));
    }
    return names;
  };

  // Parses `help <op>`'s "params" field (console_module.cpp:
  // "name:type:required|optional|write-excluded" comma-joined) into one
  // descriptor per parameter.
  //
  // The third token is the disposition, and "write-excluded" is this
  // adapter's view of the catalog's write_excluded flag - the same authority
  // the serial adapter reads straight out of the in-image catalog
  // (include/console_write_exclusion.h). Reading the flag through the
  // operation descriptor, rather than matching key names against a "password"
  // pattern, is what keeps the two adapters refusing the SAME lines instead
  // of growing a second vocabulary for "secret" (#227, #206's one-language
  // decision).
  const parseHelpParamsResponse = (records) => {
    for (const record of records) {
      if (record && record.type === "field" && record.name === "params" && typeof record.value === "string") {
        return record.value
          .split(",")
          .map((entry) => entry.split(":"))
          .filter((parts) => parts[0] && parts[0].length > 0)
          .map((parts) => ({ key: parts[0], writeExcluded: parts[2] === "write-excluded" }));
      }
    }
    return [];
  };

  // Section loader (Page Recovery, ADR 0019): registered below like the
  // other startup sections. Replaces the old app-action-tokens section
  // (/api/actions?testable, the curated completion subset the epic's
  // background explicitly supersedes) with the real catalog.
  const loadConsoleCatalog = async ({ handle = null } = {}) => {
    if (!window.PAApi) throw new Error("API unavailable");
    const api = handle ?? window.PAApi;
    const result = await api.postForm("/api/console", { command: "operations" });
    // A section loader that cannot do its job must reject (#107) so the
    // bootstrap shows recovery instead of the page silently carrying on
    // with a permanently-empty completion catalog. This is deliberately
    // stricter than fetchConsoleArgKeyCandidates() below, which is an
    // on-demand per-Tab-press fetch, not a page-load section - it degrades
    // to "no candidates this press" instead of blocking the whole page.
    if (!Array.isArray(result?.data?.records)) {
      throw new Error("Console operations response is not a records array");
    }
    consoleCatalogNames = parseOperationsResponse(result.data.records).sort((a, b) => a.localeCompare(b));
  };

  // Fetches and caches one operation's parameter descriptors on demand.
  // A network/transport failure is deliberately NOT cached, so the next Tab
  // press retries instead of staying broken for the rest of the session; an
  // operation with no params (or a genuine "no params field in the
  // response") IS cached as an empty list - that is a stable fact about the
  // operation, not a transient failure.
  //
  // Returns null when the operation's parameters could not be established at
  // all. That is a different answer from [] ("this operation has none"), and
  // the two mean opposite things to the history rule below - which is why
  // this no longer collapses a failure into an empty candidate list.
  const fetchConsoleOperationParams = async (opName) => {
    if (consoleOperationParamCache.has(opName)) return consoleOperationParamCache.get(opName);
    if (!window.PAApi) return null;
    try {
      const result = await window.PAApi.postForm(
        "/api/console",
        { command: `help ${opName}` },
        { timeoutMs: 5000 }
      );
      const records = Array.isArray(result?.data?.records) ? result.data.records : [];
      const params = parseHelpParamsResponse(records);
      if (consoleOperationParamCache.size >= CONSOLE_OPERATION_PARAM_CACHE_MAX) {
        consoleOperationParamCache.delete(consoleOperationParamCache.keys().next().value);
      }
      consoleOperationParamCache.set(opName, params);
      return params;
    } catch (error) {
      return null;
    }
  };

  // Tab candidates for an operation: "<key>=" for every parameter the Console
  // will accept a value for. A write-excluded key is never offered - the
  // Console can only ever refuse it with secret-not-settable, and completing
  // it would invite the operator to type the secret that refusal exists to
  // keep out. Mirrors consoleOfferedParamAt() on the serial adapter, filter
  // and all, so neither board gets a different completion catalog (#206).
  const fetchConsoleArgKeyCandidates = async (opName) => {
    const params = await fetchConsoleOperationParams(opName);
    if (params === null) return [];
    return params.filter((param) => !param.writeExcluded).map((param) => `${param.key}=`);
  };

  const appendCommandLine = (text, extraClass = " log-line-command") => {
    appendLogLine(text, { extraClass });
  };

  // The argument keys a submitted line assigns to, in order. Walks the line
  // the way consoleParseArgs() does (include/console_args.h): the first token
  // is the operation name, then a key up to "=", then a value that is either
  // a quoted run - with \" and \\ escapes - or a bare run to the next space.
  // Honouring the quoting is what keeps a legitimate line storable: an SSID
  // may itself contain a space and an "=", and mistaking a value's own text
  // for a later key would refuse a line carrying no secret at all.
  const consoleLineArgumentKeys = (line) => {
    const keys = [];
    const isSpace = (index) => /\s/.test(line[index]);
    let i = 0;
    while (i < line.length && isSpace(i)) i += 1;
    while (i < line.length && !isSpace(i)) i += 1;

    while (i < line.length) {
      while (i < line.length && isSpace(i)) i += 1;
      if (i >= line.length) break;

      const keyStart = i;
      while (i < line.length && line[i] !== "=" && !isSpace(i)) i += 1;
      if (line[i] !== "=" || i === keyStart) {
        // A bare word or an empty key ("=value"): not a key=value pair at
        // all. Skip to the next token - a later one can still be the
        // assignment.
        while (i < line.length && !isSpace(i)) i += 1;
        continue;
      }
      keys.push(line.slice(keyStart, i));
      i += 1;

      if (line[i] === '"') {
        const quotedStart = i;
        let closed = false;
        i += 1;
        while (i < line.length) {
          if (line[i] === "\\" && i + 1 < line.length) {
            i += 2;
            continue;
          }
          if (line[i] === '"') {
            i += 1;
            closed = true;
            break;
          }
          i += 1;
        }
        if (!closed) {
          // UNTERMINATED quote. Running to the end of the line here would
          // swallow every later key with it - including a write-excluded one,
          // which is then never examined and the line is stored. That is the
          // fail-OPEN this rule cannot afford, so the unterminated run is
          // rescanned as an ordinary unquoted value: back to the opening
          // quote, forward to the next space, and carry on reading keys.
          // Costs nothing - the firmware's parser rejects an unterminated
          // quote outright, so such a line can never execute. A CLOSED quoted
          // value keeps its meaning and stays storable. Same rule, same
          // wording, as the serial half in
          // include/console_write_exclusion.h.
          i = quotedStart;
          while (i < line.length && !isSpace(i)) i += 1;
        }
      } else {
        while (i < line.length && !isSpace(i)) i += 1;
      }
    }
    return keys;
  };

  // Whether a submitted line assigns a value to a parameter the Console will
  // not accept one for - the browser half of #227's write-exclusion rule,
  // deciding from the same catalog flag the serial adapter reads
  // (include/console_write_exclusion.h).
  //
  // Two deliberate asymmetries with that C++ half, both forced by this
  // adapter seeing the catalog over HTTP rather than in image:
  //  - A line with no key=value pair at all can assign nothing, so it answers
  //    false without a lookup. That keeps the common case (a read, an action)
  //    free of an extra `help <op>` request.
  //  - When the operation's parameters cannot be established (the fetch
  //    failed), this answers TRUE and the line is not kept. Guessing wrong
  //    the other way writes a password into localStorage, where it survives
  //    the reload that a serial session's RAM ring does not.
  // Keys are compared case-insensitively for the reason the C++ half gives:
  // the executor's own secret refusal is case-insensitive, so an exact match
  // here would make history narrower than the refusal it backs.
  const lineAssignsWriteExcludedValue = async (line) => {
    const keys = consoleLineArgumentKeys(line);
    if (keys.length === 0) return false;

    const opName = line.trim().split(/\s+/)[0];
    const params = await fetchConsoleOperationParams(opName);
    if (params === null) return true;

    const excluded = params
      .filter((param) => param.writeExcluded)
      .map((param) => param.key.toLowerCase());
    if (excluded.length === 0) return false;
    return keys.some((key) => excluded.includes(key.toLowerCase()));
  };

  // Asynchronous because the write-exclusion rule may need this operation's
  // parameter dispositions, which cost one `help <op>` fetch the first time
  // an operation with arguments is used. A refused line is never stored at
  // all - not stored and then removed - so nothing has to be scrubbed out of
  // localStorage afterwards.
  const rememberCommand = async (token) => {
    if (!token) return;
    const storable = !(await lineAssignsWriteExcludedValue(token));
    if (storable && commandHistory[commandHistory.length - 1] !== token) {
      commandHistory.push(token);
      if (commandHistory.length > COMMAND_HISTORY_MAX) {
        commandHistory = commandHistory.slice(commandHistory.length - COMMAND_HISTORY_MAX);
      }
      writeStoredCommandHistory(commandHistory);
    }
    commandHistoryIndex = commandHistory.length;
  };

  const dispatchConsoleCommand = async (rawToken) => {
    const token = normalizeLogMessage(rawToken);
    if (!token) return;
    // Deliberately not awaited: the storage decision may need a `help <op>`
    // round trip, and neither the echo below nor the dispatch itself may wait
    // for it. Nothing here can reject - both the fetch and the localStorage
    // write handle their own failures.
    rememberCommand(token);
    appendCommandLine(`> ${token}`);

    if (!window.PAApi) {
      appendCommandLine("[ERROR] API unavailable", " log-line-command-error");
      return;
    }

    try {
      // Send command to the Console endpoint (ADR 0034)
      const result = await window.PAApi.postForm("/api/console", { command: token }, { timeoutMs: 5000 });

      // Parse and display Console Records from the response.
      // postForm returns {ok, status, data}, so access result.data (not result.records).
      if (result?.data?.records && Array.isArray(result.data.records)) {
        for (const record of result.data.records) {
          formatAndAppendConsoleRecord(record);
        }
      } else {
        appendCommandLine("[ERROR] invalid response format", " log-line-command-error");
      }
    } catch (error) {
      appendCommandLine(`[ERROR] ${window.PAApi.messageFor(error)}`, " log-line-command-error");
    }
  };

  // Format a single Console Record and append it to the log (ADR 0034)
  const formatAndAppendConsoleRecord = (record) => {
    if (!record || !record.type) return;

    // Build the record line as shown in docs/console-protocol.md
    // Each record is formatted as key=value pairs prefixed with "< "
    let line = `< id=${record.id} type=${record.type}`;

    if (record.type === "begin") {
      line += ` operation=${record.operation}`;
    } else if (record.type === "field") {
      line += ` name=${record.name} value=${formatConsoleValue(record.value)}`;
    } else if (record.type === "item") {
      line += ` value=${formatConsoleValue(record.value)}`;
    } else if (record.type === "result" || record.type === "end") {
      line += ` status=${record.status} outcome=${record.outcome}`;
      if (record.reason) {
        line += ` reason=${record.reason}`;
      }
    }

    // Determine CSS class based on status
    let extraClass = " log-line-command";
    if (record.status === "err") {
      extraClass = " log-line-command-error";
    }

    appendCommandLine(line, extraClass);
  };

  // Format a console value for display (handle quoting if needed)
  const formatConsoleValue = (value) => {
    if (!value) return '""';
    // If value contains spaces, =, or quotes, wrap in quotes
    if (value.includes(" ") || value.includes("=") || value.includes('"')) {
      return `"${value.replace(/\\/g, "\\\\").replace(/"/g, '\\"')}"`;
    }
    return value;
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

  // Splits the command box's raw value into (beforeToken, token): the
  // current token is the substring after the last space, or the whole
  // value if there is none. Deliberately does NOT trim the value first (a
  // trailing space is exactly what signals "the operator finished the
  // operation name, they are now completing an argument key" - trimming it
  // away would erase that signal). Mirrors the same split the serial
  // adapter's embedded-cli patch uses (lib/embedded-cli's
  // getExternalAutocompletedCommand) - "Ambiguous Tab behaviour matches the
  // browser" is one equivalence claim across both adapters, so both split
  // the line the same way.
  const splitCurrentToken = (value) => {
    const spaceIndex = value.lastIndexOf(" ");
    return spaceIndex === -1
      ? { beforeToken: "", token: value }
      : { beforeToken: value.slice(0, spaceIndex + 1), token: value.slice(spaceIndex + 1) };
  };

  // Resolves the candidate set for the CURRENT token: operation names when
  // nothing is typed before it, or the resolved operation's argument keys
  // when there is - the operation is always the line's first token,
  // regardless of how many argument tokens already follow it (matching
  // include/console_completion.h's rule for the serial adapter).
  const candidatesForCurrentToken = async (value) => {
    const { beforeToken, token } = splitCurrentToken(value);
    if (beforeToken === "") {
      return { beforeToken, token, candidates: consoleCatalogNames };
    }
    const opName = beforeToken.trim().split(" ")[0];
    const candidates = await fetchConsoleArgKeyCandidates(opName);
    return { beforeToken, token, candidates };
  };

  const completeConsoleCommand = async () => {
    if (!logCommandInput) return;
    const value = logCommandInput.value;
    const { beforeToken, token, candidates } = await candidatesForCurrentToken(value);
    // A stale response for a value the operator has since changed (e.g. kept
    // typing while the "help <op>" fetch for a previous token was in
    // flight) must not overwrite what they typed since - drop it silently
    // rather than completing against an outdated token.
    if (logCommandInput.value !== value) return;

    const matches = candidates.filter((candidate) => candidate.startsWith(token));

    if (matches.length === 0) {
      // Matches the serial adapter exactly: zero candidates is a silent
      // no-op (lib/embedded-cli's onAutocompleteRequest returns early with
      // no output when candidateCount is 0), not an error line - the old
      // "[ERROR] unknown command" here was specific to the superseded
      // curated action-token subset, which always had a fixed known list to
      // report the miss against; the catalog has no such notion of "not a
      // command at all" versus "no match at this cursor position".
      return;
    }

    if (matches.length === 1) {
      const candidate = matches[0];
      // An argument-key candidate ends in "=" and IS the separator between
      // key and value (docs/console-protocol.md s.1.2: key=value, no space
      // around "="), so completion does not also add a trailing space there -
      // the same rule the embedded-cli patch applies for the serial adapter.
      const separator = candidate.endsWith("=") ? "" : " ";
      logCommandInput.value = `${beforeToken}${candidate}${separator}`;
      return;
    }

    const shared = commonPrefix(matches);
    if (shared.length > token.length) {
      logCommandInput.value = `${beforeToken}${shared}`;
      return;
    }

    // Several candidates already share the longest common prefix: list them
    // and restore the typed line unchanged (docs/console-protocol.md s.8).
    appendCommandLine(matches.join(" "));
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


  // -------------------------------------------------------------------------
  // Boot — load recent logs, log level, and action tokens
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API loads as sections so the bootstrap
  // can show recovery state if any fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.

  // Initial status fetch: loads the current state when the stream is cold.
  // This is registered as a section so the bootstrap can show recovery state
  // if the fetch fails. For the stream-supported case, this section only runs
  // if the stream has no cached value. For the fallback case, it ensures the
  // page shows data before polling begins.
  const loadInitialStatus = async ({ handle = null } = {}) => {
    const hasStream = window.PAStatusStream?.isSupported();
    const hasCachedStatus = hasStream && window.PAStatusStream?.getLastStatus();
    if (!hasStream || !hasCachedStatus) {
      await refreshStatusOnce({ handle });
    }
  };

  const SECTIONS = [
    ["app-initial-status", loadInitialStatus, "initial status"],
    ["app-recent-logs", loadRecentLogs, "recent logs"],
    ["app-log-level", loadLogLevel, "log level setting"],
    ["app-console-catalog", loadConsoleCatalog, "console commands"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadRecentLogs().catch(() => {});
      loadLogLevel().catch(() => {});
      loadConsoleCatalog().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "controller connection",
      "/diagnostics.js": "diagnostics constants",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/health_signals.js": "health indicator logic",
      "/dome_command_map.js": "dome command map",
      "/dome_panel_model.js": "dome panel state",
      "/dome_layout.js": "dome panel layout",
      "/dome_layout_render.js": "dome panel rendering",
      "/dome_control.js": "dome control",
      "/app.js": "home dashboard",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  startPageLoad();

  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") {
        applyStatus(payload);
      }
      if (eventType === "log") payload.split("\x01").forEach((line) => appendLogLine(line));
      if (eventType === "stream_error") {
        appendLogLine(LOG_UNREACHABLE_TEXT);
        setStale(true);
      }
    });
  } else {
    // Fallback polling for pages without stream support
    const refreshFromFallback = () => {
      return refreshStatusOnce().catch(() => {
        pollFailCount++;
        if (pollFailCount >= 2) setStale(true);
      });
    };

    const fallbackPoll = window.PageBootstrap.createBackgroundPoll(
      refreshFromFallback,
      {
        cadenceMs: 3000,
        refreshOnReturn: true,
      }
    );
    fallbackPoll.start();

    window.addEventListener("beforeunload", () => {
      fallbackPoll.stop();
    });
  }
  setEstopUi(false);
  setSleepUi(false);
})();

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

  const sleepToggle = document.getElementById("sleep-toggle");
  const sleepOverlay = document.getElementById("sleep-overlay");
  const sleepOverlayWake = document.getElementById("sleep-overlay-wake");
  const sleepFeedback = document.getElementById("sleep-feedback");
  const topbarReboot = document.getElementById("topbar-reboot");
  const rebootFeedback = document.getElementById("reboot-feedback");
  // This surface makes no freshness claim of its own. It used to carry a banner
  // saying "The status stream was interrupted. These are the values the droid
  // last sent" - the same fact the Status Plate's one freshness line already
  // states, in a second aria-live region on the same screen, so a screen reader
  // heard it twice. The plate's line is the better of the two because it
  // carries the AGE, which is the half a builder actually needs, and CONTEXT.md
  // "Status Plate" already makes it the surface's one freshness statement
  // (#324, #402). Nothing here replaces it: one fact, one place.
  // The Controls section head's subtitle: a state, in three words, read off the
  // same frame the controls under it render from (ADR 0066). Web control and
  // the estop are not here - both are Status Plate cells, and the plate is on
  // every surface, so repeating them would be one fact said twice (#324).
  const snapshotMode = document.getElementById("snapshot-mode");
  const snapshotMood = document.getElementById("snapshot-mood");
  const snapshotSleep = document.getElementById("snapshot-sleep");
  const opmodeNow = document.getElementById("opmode-now");
  const moodNow = document.getElementById("mood-now");
  const sleepNow = document.getElementById("sleep-now");
  const healthSummary = document.getElementById("health-summary");
  const componentSummary = document.getElementById("component-summary");
  const consoleDisclosure = document.getElementById("console-disclosure");
  const sleepToggleLabel = document.getElementById("sleep-toggle-label");
  // The Sleep switch in Controls: a second entrance to the topbar's one Sleep
  // act, never a second act (operator, 2026-09-29, #399).
  const sleepSwitch = document.getElementById("sleep-switch");

  // The identity plate and the two readouts the Status Plate leaves off. Every
  // value here comes out of the /api/status frame this surface already reads
  // or the /api/config payload it already fetches for the log level, so the
  // plate costs the controller nothing it was not already being asked.
  const buildDesign = document.getElementById("build-design");
  const buildDesignDetail = document.getElementById("build-design-detail");
  const buildFirmware = document.getElementById("build-firmware");
  const buildFirmwareDetail = document.getElementById("build-firmware-detail");
  const buildUptime = document.getElementById("build-uptime");
  const buildUptimeDetail = document.getElementById("build-uptime-detail");
  const readoutHeap = document.getElementById("readout-heap");
  const readoutHeapDetail = document.getElementById("readout-heap-detail");
  const readoutWifi = document.getElementById("readout-wifi");
  const readoutWifiDetail = document.getElementById("readout-wifi-detail");

  let lastStatus = null;
  let modePending = false;
  let moodPending = false;
  let sleepPending = false;
  let isSleeping = false;
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

  // The name, and only the name. Fifteen rows that all wore one of four emoji
  // said nothing the word beside them did not (ADR 0066), and the three that
  // shared an arm glyph were not even the same kind of thing.
  //
  // The Outputs are not in this list: which ones the droid has and what each is
  // called - what its board prints beside the pin, ARM3 on the Artoo PCB, GPIO 4
  // on the FireBeetle 2 - is the firmware's answer, which data/outputs.js reads
  // from GET /api/config (#415), and this page knows no Output of its own (ADR
  // 0033 Amendment 2026-09-19). They wire the card, as the firmware lists
  // them, once that answer has arrived.
  let outputLabels = [];
  // The Component Toggles that are not an Output, by the key the status
  // reports each under, each named by its Setting's label in the one words
  // table (data/web_api.js) - the key with "enable" in front is its form name.
  // Named when drawn, not when this file loads: the words table is PAApi's.
  const SUBSYSTEM_KEYS = ["domeEsc", "rcCh1", "rcCh2", "rcCh3", "rcCh4", "rcCh5", "rcCh6", "drive", "audio",
    "protoR2link"];
  const subsystemLabels = () =>
    SUBSYSTEM_KEYS.map((key) => [key, window.PAApi.labelOf(`enable${key.charAt(0).toUpperCase()}${key.slice(1)}`)]);

  const MOOD_LABELS = {
    0: "Idle",
    10: "Quiet",
    11: "Full-Awake",
    13: "Mid-Awake",
    14: "Awake+",
  };

  const COMPONENT_ENABLED_TEXT = "Enabled";
  const COMPONENT_DISABLED_TEXT = "Disabled";

  const showFeedback = (el, message, level = "") => {
    if (!el) return;
    if (!el.dataset.baseClass) {
      el.dataset.baseClass = el.className || "feedback";
    }
    el.textContent = message;
    el.className = level ? `${el.dataset.baseClass} ${level}` : el.dataset.baseClass;
  };

  const setSleepPending = (pending) => {
    sleepPending = pending;
    [sleepToggle, sleepSwitch, sleepOverlayWake].forEach((el) => {
      if (!el) return;
      el.disabled = pending;
      el.classList.toggle("is-pending", pending);
      el.setAttribute("aria-disabled", pending ? "true" : "false");
    });
  };

  const setSleepUi = (sleeping) => {
    isSleeping = !!sleeping;
    if (sleepToggle) {
      // Only the label moves: the icon beside it is an element, and writing
      // textContent over the button would take it with the word.
      if (sleepToggleLabel) sleepToggleLabel.textContent = isSleeping ? "Wake" : "Sleep";
      sleepToggle.title = isSleeping ? "Wake lights, panels and chatter" : "Rest lights, panels and chatter. Drive stays awake.";
      // Waking a sleeping droid is the one primary act on this surface, so the
      // button takes the interaction blue while the droid is asleep and is a
      // plain control the rest of the time. It used to take `danger` AND
      // `accent` together, which painted it red - and red is reserved for
      // something stopped or refused (#327), which a parked droid is not.
      sleepToggle.classList.toggle("accent", isSleeping);
      sleepToggle.setAttribute("aria-pressed", isSleeping.toString());
    }
    // The switch is on only when the droid says it is asleep: a press moves it
    // through this, from the frame, and not on the press alone.
    sleepSwitch?.setAttribute("aria-checked", isSleeping.toString());
    if (sleepOverlay) {
      sleepOverlay.classList.toggle("active", isSleeping);
      sleepOverlay.setAttribute("aria-hidden", (!isSleeping).toString());
    }
    document.body.classList.toggle("sleep-mode-active", isSleeping);
  };


  const setIndicator = (id, state, reason = "") => {
    const el = document.getElementById(id);
    if (!el) return;
    el.className = `indicator ${state}`;
    const textEl = INDICATOR_TEXT[id] ? document.getElementById(INDICATOR_TEXT[id]) : null;
    if (textEl) {
      // The evaluator's reason, not its state: the signal light beside it
      // already says ok / degraded / faulted / not reporting, and "OK: Frames
      // ok" says one of those twice. The state label is the fallback for an
      // evaluator that returned no reason, so a signal is never wordless.
      // No hover title: the raw key=value detail that used to sit there named
      // firmware fields, not anything a builder reads (#298, #422).
      const label = INDICATOR_STATE_LABELS[state] || String(state).toUpperCase();
      textEl.textContent = reason || label;
    }
  };

  const renderHealth = (payload) => {
    if (!HEALTH_SIGNAL_MODEL || typeof HEALTH_SIGNAL_MODEL.deriveHealthSignals !== "function") {
      Object.keys(INDICATOR_TEXT).forEach((id) => {
        setIndicator(id, "off", "Health model missing");
      });
      return;
    }

    const signals = HEALTH_SIGNAL_MODEL.deriveHealthSignals(payload, { unknown: window.PALiveReading.UNKNOWN });
    signals.forEach(({ id, state, reason }) => setIndicator(id, state, reason));
    renderHealthSummary(signals);
  };

  // The section head's subtitle is a count (ADR 0066), and it counts the states
  // the evaluators actually returned rather than restating how many rows the
  // markup has. A state with nothing in it is left out, so "7 signals - 5 ok"
  // never has to say "0 fail" to be complete.
  const HEALTH_SUMMARY_WORDS = { ok: "ok", warn: "degraded", fail: "faulted", off: "not reporting" };

  const renderHealthSummary = (signals) => {
    if (!healthSummary) return;
    const counted = new Map();
    signals.forEach(({ state }) => counted.set(state, (counted.get(state) || 0) + 1));
    const parts = Object.keys(HEALTH_SUMMARY_WORDS)
      .filter((state) => counted.get(state))
      .map((state) => `${counted.get(state)} ${HEALTH_SUMMARY_WORDS[state]}`);
    healthSummary.textContent = [`${signals.length} signals`, ...parts].join(" \u00b7 ");
  };

  let renderedComponentIds = null;

  // Keyed by the stored id the status frame reports each Output under, and
  // named as data/outputs.js names it.
  const adoptOutputLabels = (outputs) => {
    outputLabels = outputs.filter((output) => output.fromConfig).map((output) => [output.id, output.name]);
    // Whichever arrived first, the card is drawn again from the last status
    // this page applied, so the names follow on every delivery path - the
    // stream and the fallback poll alike.
    if (lastStatus) renderComponentStatus(lastStatus);
  };

  // What one row of the Readouts card says. protoR2link, the sound link, the
  // Dome ESC and the Foot Drive are the health-signal model's word, the same
  // one Health shows (and, for the two links, the Status Plate, Maintenance
  // and Sound: data/health_signals.js, #422, #399). The links carry no line of
  // their own beneath it: the firmware's detail there restated the state in
  // other words. The Dome ESC and the Foot Drive carry what the droid commands
  // as their line ("Target 0%", "Command 120/0"). Every other row is the
  // firmware's state and detail.
  const LINK_COMPONENT_READERS = {
    protoR2link: (payload) => HEALTH_SIGNAL_MODEL.readProtoR2link(payload, { unknown: window.PALiveReading.UNKNOWN }),
    audio: (payload) => HEALTH_SIGNAL_MODEL.readSoundLink(payload, { unknown: window.PALiveReading.UNKNOWN }),
    domeEsc: (payload) => HEALTH_SIGNAL_MODEL.readDomeEsc(payload, { unknown: window.PALiveReading.UNKNOWN }),
    drive: (payload) => HEALTH_SIGNAL_MODEL.readFootDrive(payload),
  };

  // The lamp beside each row's state (CONTEXT.md "Status Color"). The rows
  // above take the health-signal model's own light, so they match Health.
  // Every other row is the firmware's state word (src/web/status_json.cpp),
  // and only a word that reports something heard lights: SBUS frames arriving
  // or lost. Everything else is grey, `ready` above all: an Output says ready
  // whether or not a servo is on it, and PWM channels say it with nothing
  // measured, so a green there would be the "we did not check" the colour must
  // never say. A command is not a report either, which is why the Dome ESC
  // and the Foot Drive are no longer in this table (#399).
  const COMPONENT_STATE_LIGHTS = Object.freeze({
    active: "ok",
    signal_lost: "fail",
  });

  const componentReading = (key, payload) => {
    const readLink = LINK_COMPONENT_READERS[key];
    if (readLink && HEALTH_SIGNAL_MODEL) {
      const { state, word, detail = "" } = readLink(payload);
      return { state: word, detail, light: state };
    }
    const entry = payload[key];
    let state = entry ? "enabled" : "disabled";
    let detail = entry ? COMPONENT_ENABLED_TEXT : COMPONENT_DISABLED_TEXT;
    if (entry && typeof entry === "object") {
      state = entry.state || "enabled";
      detail = entry.detail || COMPONENT_ENABLED_TEXT;
    }
    // The firmware's token as a word, capitalised like the readers' words
    // above, so "Ready" and "No answer" sit side by side as one voice.
    const word = String(state).replace(/_/g, " ");
    return { state: word.charAt(0).toUpperCase() + word.slice(1), detail, light: COMPONENT_STATE_LIGHTS[state] || "off" };
  };

  const renderComponentStatus = (payload) => {
    if (!componentStatusCard || !componentStatusGrid) return;

    const active = [...outputLabels, ...subsystemLabels()].filter(([key]) => key in payload);
    if (active.length === 0) {
      componentStatusCard.classList.add("hidden");
      componentStatusGrid.innerHTML = "";
      renderedComponentIds = null;
      return;
    }

    componentStatusCard.classList.remove("hidden");
    if (componentSummary) {
      componentSummary.textContent = `${active.length} reported`;
    }

    const signature = active.map(([key, label]) => `${key}:${label}`).join(",");

    // Rebuild only if the component set changed
    if (signature !== renderedComponentIds) {
      renderedComponentIds = signature;
      const items = active.map(([key, label]) => {
        const { state, detail, light } = componentReading(key, payload);
        const safeDetail = window.PAUtils.escapeHtml(detail);
        return `
        <div class="readout" id="comp-${key}" data-light="${light}">
          <dt>${label}</dt>
          <dd id="state-${key}">${window.PAUtils.escapeHtml(state)}</dd>${detail
            ? `
          <div class="readout-detail" id="detail-${key}" title="${safeDetail}">${safeDetail}</div>`
            : ""}
        </div>`;
      }).join("");
      componentStatusGrid.innerHTML = `<dl class="readouts">${items}</dl>`;
    } else {
      // Patch only the text content when component set hasn't changed
      active.forEach(([key]) => {
        const { state, detail, light } = componentReading(key, payload);

        const itemEl = document.getElementById(`comp-${key}`);
        if (itemEl) itemEl.dataset.light = light;

        const stateEl = document.getElementById(`state-${key}`);
        if (stateEl) stateEl.textContent = state;

        const detailEl = document.getElementById(`detail-${key}`);
        if (detailEl) {
          detailEl.textContent = detail;
          detailEl.title = detail;
        }
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

  const setText = (el, text) => {
    if (el) el.textContent = text;
  };

  // The Controls section head's subtitle, and the same three words beside the
  // control each of them belongs to. Three postures the operator chose, so
  // none of them takes a color: a chosen posture is a readout, not a symptom
  // (#327 "Status Color", as amended 2026-09-16).
  //
  // Web control and the estop used to be here as two more pills. Both are
  // Status Plate cells (CONTROL, ESTOP), the plate is on every surface, and
  // #324's whole argument is that one fact belongs in one place - so they are
  // read there and not restated here.
  const renderMissionSnapshot = (payload) => {
    const modeText = payload.stationary ? "Stationary" : "Driving";
    // A mood this surface has no name for is not mood zero. The fallback used
    // to print `Mood ${payload.activeMood || 0}`, so a frame that carried no
    // mood at all - the state a page is in before the first one arrives - read
    // "Mood 0" beside two readouts that say Unknown for the same thing.
    // It is also the only place on this surface a raw number would reach the
    // operator, which ADR 0059 keeps behind the mapping table above.
    const moodText = MOOD_LABELS[payload.activeMood] || window.PALiveReading.UNKNOWN;
    const sleepText = payload.sleepMode ? "asleep" : "awake";

    setText(snapshotMode, modeText);
    setText(snapshotMood, moodText);
    setText(snapshotSleep, sleepText);
    setText(opmodeNow, modeText);
    setText(moodNow, moodText);
    setText(sleepNow, sleepText);
  };

  // ---------------------------------------------------------------------------
  // Build, and the two readouts the Status Plate leaves off
  //
  // Both render from the /api/status frame this surface already has. Nothing
  // here asks the controller for anything of its own: the plate's rule is that
  // telemetry belongs to the Dashboard (#324), not that the Dashboard may go
  // and fetch more of it.
  // ---------------------------------------------------------------------------
  const KB = 1024;

  const kilobytes = (bytes) => {
    const value = Number(bytes);
    return Number.isFinite(value) && value >= 0 ? Math.round(value / KB) : null;
  };

  // hh:mm:ss, which is what a builder reads an uptime as. Days are spelled out
  // rather than rolled into the hours, because "73:04:11" is not a number
  // anyone converts in their head.
  const uptimeText = (ms) => {
    const total = Number(ms);
    if (!Number.isFinite(total) || total < 0) return null;
    const seconds = Math.floor(total / 1000);
    const days = Math.floor(seconds / 86400);
    const clock = [Math.floor((seconds % 86400) / 3600), Math.floor((seconds % 3600) / 60), seconds % 60]
      .map((part) => String(part).padStart(2, "0"))
      .join(":");
    return days > 0 ? `${days}d ${clock}` : clock;
  };

  // tools/extract_version.py stamps the filesystem with "fs-" and then the
  // firmware's own version, so the two strings of one build differ by exactly
  // that prefix and must never be compared as they stand.
  const FS_VERSION_PREFIX = "fs-";
  const assetsBuild = (assets) =>
    assets.startsWith(FS_VERSION_PREFIX) ? assets.slice(FS_VERSION_PREFIX.length) : assets;

  const renderIdentityPlate = (payload) => {
    const firmware = String(payload.firmwareVersion || "").trim();
    const assets = String(payload.fsVersion || "").trim();
    setText(buildFirmware, firmware || window.PALiveReading.UNKNOWN);
    // Firmware and web assets are built and flashed separately, so the one
    // thing worth saying about the pair is whether they came from the same
    // build. A mismatch is how a surface ends up talking to an API that moved.
    setText(
      buildFirmwareDetail,
      !firmware || !assets
        ? ""
        : firmware === assetsBuild(assets)
          ? `web assets ${assets} - match`
          : `web assets ${assets} - does not match the firmware`,
    );

    const uptime = uptimeText(payload.uptimeMs);
    setText(buildUptime, uptime || window.PALiveReading.UNKNOWN);
    const reason = String(payload.resetReason || "").trim();
    setText(buildUptimeDetail, reason ? `since a ${reason.toLowerCase()} reset` : "");
  };

  // The Droid Build, from the /api/config payload the log level already
  // fetched - droid_build.js's own documented "a page holding a config payload
  // calls adopt() with it and spends no request at all".
  const renderDroidBuild = (build) => {
    // Both halves or neither: this runs inside the log level's section loader,
    // where a throw becomes a failed section rather than a visible error.
    if (!buildDesign || !buildDesignDetail) return;
    const designs = (window.DroidParts && window.DroidParts.designs) || [];
    const nameOf = (half) => {
      const design = designs.find((candidate) => candidate.id === half.design);
      if (!design) return half.design || "";
      const variant = (design.variants || []).find((candidate) => candidate.id === half.variant);
      return variant ? `${design.label} ${variant.label.toLowerCase()}` : design.label;
    };
    if (!build) {
      setText(buildDesign, "Not answered yet");
      setText(buildDesignDetail, "");
      return;
    }
    const dome = nameOf(build.dome);
    const body = nameOf(build.body);
    setText(buildDesign, dome === body ? dome : `${dome} dome, ${body} body`);
    const fitted = Array.isArray(build.fitted) ? build.fitted.length : 0;
    buildDesignDetail.innerHTML =
      `${fitted} ${fitted === 1 ? "part" : "parts"} fitted. ` +
      window.PAUi.setupActionHtml("Change it");
  };

  const renderReadouts = (payload) => {
    const free = kilobytes(payload.heapFree);
    if (readoutHeap) {
      readoutHeap.innerHTML = free === null
        ? window.PALiveReading.UNKNOWN
        : `${free}<small>kB</small>`;
    }
    // The Internal Data Heap's largest free block, not the total, because that
    // is the number the Health signal beside it judges memory on
    // (data/health_signals.js). A builder looking at plenty free and a red
    // Memory light has to be able to see why from here.
    const largest = kilobytes(payload.heapLargestBlock);
    setText(
      readoutHeapDetail,
      largest === null ? "" : `largest free piece ${largest} kB. The Memory light reads this one.`,
    );

    // wifiRssi is only set while the droid is joined to a network as a station;
    // it is zero in every other case (deriveWiFiConnectivityFields,
    // src/web/api_status_serializers.cpp). A readout may only print what
    // something measured, so zero prints as nothing measured rather than as a
    // very strong signal.
    const rssi = Number(payload.wifiRssi);
    const joined = Number.isFinite(rssi) && rssi !== 0;
    if (readoutWifi) {
      readoutWifi.innerHTML = joined ? `${rssi}<small>dBm</small>` : "--";
    }
    setText(
      readoutWifiDetail,
      joined
        ? "on the network the droid joined"
        : "Not on a network. Nothing to measure.",
    );
  };

  // Before the droid has sent a frame, every readout that waits on one says
  // so in the Live Reading's words, rather than in a placeholder of its own.
  const WAITING_READOUTS = [
    buildFirmware, buildUptime, snapshotMode, snapshotMood, snapshotSleep,
    opmodeNow, moodNow, sleepNow, readoutHeap, readoutWifi,
  ];

  const applyReading = (reading) => {
    const payload = reading.status;
    lastStatus = payload;
    if (payload === null) {
      WAITING_READOUTS.forEach((node) => setText(node, window.PALiveReading.slotText(window.PALiveReading.WAITING)));
      return;
    }
    renderHealth(payload);
    renderComponentStatus(payload);
    renderMissionSnapshot(payload);
    renderIdentityPlate(payload);
    renderReadouts(payload);
    renderOpMode(payload);
    renderActiveMood(payload);
    setSleepUi(!!payload.sleepMode);
    // The run record's name and ending, which the run watch below does not
    // announce when no run of it was ever under way on this page.
    paintShowRun();
  };

  // Asks the droid once, after an act that changed something. The answer is
  // not kept here: it arrives through the Live Reading like every frame, and
  // this surface paints it from there (data/live_reading.js).
  const refreshStatusOnce = () => window.PALiveReading.read();

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
  // Shown under a console reply the controller could not carry whole (#240).
  // "[CUT]" is its own tag rather than "[ERROR]": the command ran, the group
  // closed, and every line printed above this one is real - what is wrong is
  // that there were more of them. Says "some lines are missing" and not which
  // ones, because the controller does not say: the answer it sends back is
  // the newest lines for the log ring (docs/api.md), and nothing more
  // specific is true for whatever query bounds out next.
  const CONSOLE_TRUNCATED_TEXT =
    "[CUT] The Body Controller could not fit the whole answer — some lines are missing from the reply above.";
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

  // The four levels wore the same emoji, so it told an operator which of the
  // four they were on exactly never (ADR 0066). The word does that.
  const LOG_LEVELS = {
    1: { label: "Error", cls: "pill-error", hint: "Loss of function only" },
    2: { label: "Warning", cls: "pill-info", hint: "Faults + safety warnings" },
    3: { label: "Info", cls: "pill-info", hint: "Boot + service health" },
    4: { label: "Debug", cls: "pill-warn", hint: "Verbose" },
  };
  let currentLogLevel = null;
  let logLevelPending = false;

  const renderLogLevelPill = (level) => {
    if (!logLevelPill) return;
    const info = LOG_LEVELS[level];
    if (!info) {
      logLevelPill.textContent = "...";
      logLevelPill.title = "Log level unknown - click to retry";
      logLevelPill.setAttribute("aria-label", "Log level unknown. Click to retry.");
      return;
    }
    logLevelPill.className = `status-pill status-pill-compact ${info.cls}`;
    logLevelPill.textContent = info.label;
    logLevelPill.title = `Log level: ${info.label} (${info.hint}) - click to cycle`;
    logLevelPill.setAttribute("aria-label", `Log level: ${info.label}. Click to cycle to the next level.`);
  };

  const loadLogLevel = async ({ handle = null } = {}) => {
    if (!window.PAApi || !logLevelPill) throw new Error("API or pill unavailable");
    const api = handle ?? window.PAApi;
    // The config alone (PAApi reads no-store by default). The Outputs' names
    // are not on it - an Output is its row in the servo table (ADR 0068) - so
    // they are a section of their own below, and a failed table read cannot
    // take the log level or the Droid Build down with it (#423).
    const answer = await api.get("/api/config");
    const config = answer?.data && typeof answer.data === "object" ? answer.data : {};
    // The identity plate's Droid Build row rides this payload rather than
    // fetching one of its own: droid_build.js's adopt() takes a config the page
    // already holds, and the controller sheds connections under load, so a
    // second GET of the same document would cost a client slot to learn what
    // this one already said.
    renderDroidBuild(window.DroidBuild?.adopt(config) || null);
    adoptStandDown(config);
    const level = Number(config?.system?.logLevel);
    if (!LOG_LEVELS[level]) {
      throw new Error(`Unknown log level: ${level}`);
    }
    currentLogLevel = level;
    renderLogLevelPill(level);
  };

  // The Outputs' names for the Readouts card: the servo table alone, read
  // through data/outputs.js (#415).
  const loadOutputNames = async ({ handle = null } = {}) => {
    adoptOutputLabels(await window.PAOutputs.refresh({ handle: handle ?? window.PAApi }));
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

  // Console Tab completion catalog (ADR 0036, #238). Operation names are
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
      // Send command to the Console endpoint (ADR 0036)
      const result = await window.PAApi.postForm("/api/console", { command: token }, { timeoutMs: 5000 });

      // Parse and display Console Records from the response.
      // postForm returns {ok, status, data}, so access result.data (not result.records).
      if (result?.data?.records && Array.isArray(result.data.records)) {
        for (const record of result.data.records) {
          formatAndAppendConsoleRecord(record);
        }
        // The controller reports a bounded answer on the response ENVELOPE -
        // "truncated":true beside "records", never as a Console Record
        // (docs/api.md, POST /api/console) - so a truncated group still
        // arrives complete-looking: every record well-formed, the `end`
        // present, nothing in the printed transcript to say lines were left
        // out. Reading the flag here is what keeps a bounded reply from
        // reading as a full one (#240).
        //
        // Printed AFTER the records, as a line in the same log, for the same
        // reason tools/console_client.py prints its own capped line there:
        // the notice belongs to the reply it follows, and the panel keeps
        // scrolling. A banner elsewhere on the page would come loose from the
        // answer it describes the moment the next line arrives.
        //
        // Only this dispatch path reads the flag, and that is not an
        // oversight: the page's other two /api/console callers cannot receive
        // it. `operations` (loadConsoleCatalog) is answered by the streaming
        // path, which has no envelope field at all, and `help <op>`
        // (fetchConsoleOperationParams) emits `field` records only - the
        // controller raises this flag exclusively when it drops an `item`
        // (webOnRecordItem_impl, src/web/api_console.cpp). A guard on either
        // would be a branch no firmware can reach.
        if (result.data.truncated === true) {
          appendCommandLine(CONSOLE_TRUNCATED_TEXT, " log-line-command-cut");
        }
      } else {
        appendCommandLine("[ERROR] invalid response format", " log-line-command-error");
      }
    } catch (error) {
      appendCommandLine(`[ERROR] ${window.PAApi.messageFor(error)}`, " log-line-command-error");
    }
  };

  // Format a single Console Record and append it to the log (ADR 0036)
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

  // ---- Sleep switch (Controls) ----
  // It presses the same act as the topbar button, through the same function
  // and its sleepPending guard, so the two can never send twice.
  sleepSwitch?.addEventListener("click", () => toggleSleepWake(false));

  // The Console is a disclosure. It starts open, but an operator can shut it,
  // and while it is shut the log has no layout at all and scrollHeight is 0 -
  // every stick-to-bottom while it was shut left scrollTop at 0. Opening it
  // again therefore has to put the newest line back under the operator's eye,
  // which is the whole reason the log sticks to the bottom in the first place.
  consoleDisclosure?.addEventListener("toggle", () => {
    if (!consoleDisclosure.open || !logConsole) return;
    if (hasActiveLogSelection()) return;
    logConsole.scrollTop = logConsole.scrollHeight;
    logPaused?.classList.remove("visible");
  });

  sleepToggle?.addEventListener("click", () => toggleSleepWake(false));
  sleepOverlayWake?.addEventListener("click", () => toggleSleepWake(true));
  topbarReboot?.addEventListener("click", rebootController);


  // -------------------------------------------------------------------------
  // Sequences: a show run from the Dashboard (#330, #451)
  //
  // Every Sequence on the droid, in the two groups Sequences lists them in:
  // Yours, then the Factory ones yours do not shadow - a Learned name shadows
  // a Factory one, the rule the droid resolves a name by (the removed
  // quick-sequence row, 76d9735c^:data/dome_control.js). Each is a tile: its
  // Play, its name, how long a run is and what it does, as the droid lists it.
  // The one running has Stop instead of Play, whoever started it. A Sequence
  // mapped to an RC Channel says which, from GET /api/rc/map: the RC Map is the
  // running order, so the RC Radio and this list are one list read from two
  // ends, never stored twice.
  //
  // Rest runs the droid's Stand Down Sequence, which is chosen on Sequences;
  // the tile of the one it runs carries the Rest mark, so Rest needs no words
  // of its own beside it (operator, 2026-10-04). The Factory DM:RESET default
  // says on its tile that it leaves the pies open (CONTEXT.md "Stand Down
  // Sequence").
  //
  // What is running is the Live Reading's run watch (data/live_reading.js,
  // "The run watch"), the one the Sequences page reads too. It starts and
  // stops runs, judges a press by the run's start time, and says when a run -
  // from here, an RC Channel or anywhere - begins and ends.
  //
  // A name the RC Map fires that has no Sequence behind it - a Learned one
  // deleted since - is listed and says it will do nothing. POST /api/seq/test
  // would accept it and the dome would ignore it, so the answer cannot say so
  // (src/tasks/sequence_dispatcher.cpp): the page knows from the library, sends
  // nothing, and a press raises the Ignored Input Notice (data/shell.js).
  // -------------------------------------------------------------------------
  const showNow = document.getElementById("show-now");
  const showList = document.getElementById("show-list");
  const showOther = document.getElementById("show-other");
  const showFeedbackEl = document.getElementById("show-feedback");
  const standDownBtn = document.getElementById("standdown-btn");
  const postureBtn = document.getElementById("show-posture");

  // What a never-chosen Stand Down runs: the words table's, one home for the
  // Dashboard and the Sequences page (data/web_api.js, standDownSequence).
  const STAND_DOWN_UNSET = window.PAApi.unsetOf("standDownSequence");
  const NOT_ON_DROID = "Not on the droid. Does nothing.";
  const NEEDS_REPAIR = "Needs repair on Sequences.";
  const LEAVES_PIES_OPEN = "Leaves the pies open.";

  // Each null until the droid has answered it once.
  let showLearned = null;
  let showFactory = null;
  let showMapped = null; // [{ name, channel }] - the RC Map's Sequence bindings
  let standDownChoice = null; // "" when never chosen; null while not known

  const esc = (text) => window.PAUtils.escapeHtml(text);
  const escAttr = (text) => window.PAUtils.escapeAttr(text);
  const icon = (name) => `<svg class="i" aria-hidden="true" focusable="false"><use href="#i-${name}"/></svg>`;
  const actFace = (name, words) => window.PAUi.actFace(name, words);

  const libraryEntry = (name) =>
    showLearned?.find((seq) => seq.name === name) || showFactory?.find((seq) => seq.name === name) || null;
  const libraryAnswered = () => showLearned !== null && showFactory !== null;
  const standDownEffective = () => (standDownChoice === null ? null : standDownChoice || STAND_DOWN_UNSET);

  // Why a name cannot run, as the row says it and as the notice says it, and
  // where it is changed; null when it can run. A name only the RC Map holds is
  // changed on RC; one that needs repair, or the Stand Down Sequence whatever
  // is wrong with it, on Sequences, where it is chosen.
  const refusalOf = (name) => {
    const entry = libraryEntry(name);
    if (!entry) {
      const page = name === standDownEffective() ? "seq" : "rc";
      return { says: NOT_ON_DROID, notice: `${name} is not on the droid`, page };
    }
    if (entry.valid === false) {
      return { says: NEEDS_REPAIR, notice: `${name} fails Protocol Check until it is repaired`, page: "seq" };
    }
    return null;
  };
  const refusedAttrs = (refusal) =>
    (refusal
      ? ` disabled aria-disabled="true" data-ignored-says="${escAttr(refusal.notice)}" data-ignored-page="${refusal.page}"`
      : "");

  // How long a run is: "6 s", "1.5 s" - the same words as Runs on Sequences
  // (data/seq.js lengthWords). The droid sends 0 for a stored Sequence it
  // could not find the end of, which is only ever an invalid one.
  const lengthWords = (entry) =>
    (Number.isInteger(entry?.lengthMs) && entry.lengthMs > 0 ? `${Number((entry.lengthMs / 1000).toFixed(2))} s` : "");

  // A tile: its act first, a round Play that shows its icon alone and says
  // Play <name> as its tooltip and accessible name (operator, 2026-10-04);
  // then the name, the RC Channels that fire it and the Rest mark; then one
  // quiet line - why it does nothing, or how long it runs and what it does,
  // cut to the tile's width (the whole purpose is on Sequences).
  const tileHtml = (name) => {
    const refusal = refusalOf(name);
    const entry = libraryEntry(name);
    const channels = (showMapped || []).filter((mapped) => mapped.name === name).map((mapped) => mapped.channel);
    const isRest = name === standDownEffective();
    const factoryDefault = isRest && name === STAND_DOWN_UNSET && !showLearned.some((seq) => seq.name === name);
    const purpose = entry?.purpose ? `${entry.purpose}${entry.purposeCut ? "..." : ""}` : "";
    const about = factoryDefault ? LEAVES_PIES_OPEN : purpose;
    const line = refusal ? `<span class="why">${refusal.says}</span>`
      : [lengthWords(entry), about].filter(Boolean).map(esc).join(" &middot; ");
    return `
      <li class="show-item${refusal ? " is-inert" : ""}" data-name="${escAttr(name)}">
        <span class="show-item-act">
          <button type="button" class="btn show-play icon-act" data-act="play"${refusedAttrs(refusal)}>${actFace("play", `Play ${name}`)}</button>
          <button type="button" class="btn seq-stop icon-act hidden" data-act="stop">${actFace("stop", `Stop ${name}`)}</button>
        </span>
        <span class="show-item-says">
          <span class="show-item-name">
            <span class="show-name">${esc(name)}</span>
            ${isRest ? `<span class="seq-badge show-rest-mark">${icon("human-handsdown")}Rest</span>` : ""}
            ${channels.map((channel) => `<span class="show-rc" aria-label="RC Channel ${escAttr(channel)}">${esc(channel)}</span>`).join("")}
          </span>
          <span class="show-item-line">
            <span class="seq-row-run hidden"><span class="indicator ok seq-live" aria-hidden="true"></span>Running</span>
            <span class="show-item-about">${line}</span>
          </span>
        </span>
      </li>`;
  };

  const groupHtml = (label, names) => (names.length === 0 ? "" : `
      <section class="show-group">
        <h4 class="show-group-head">${label} <span class="show-group-count">${names.length}</span></h4>
        <ul class="show-items">${names.map(tileHtml).join("")}</ul>
      </section>`);

  // Yours holds the builder's own and every name the droid would fire that
  // it does not hold - one the RC Map binds, or the Stand Down Sequence -
  // which can only ever have been theirs: a Factory name is never missing.
  const renderShowList = () => {
    if (!showList || !libraryAnswered()) return;
    const factoryNames = new Set(showFactory.map(({ name }) => name));
    const yours = [];
    const factory = [];
    const standDown = standDownEffective();
    [...showLearned, ...showFactory, ...(showMapped || []), ...(standDown ? [{ name: standDown }] : [])]
      .forEach(({ name }) => {
        if (!name || yours.includes(name) || factory.includes(name)) return;
        const learned = showLearned.some((seq) => seq.name === name);
        (learned || !factoryNames.has(name) ? yours : factory).push(name);
      });
    showList.innerHTML = groupHtml("Yours", yours) + groupHtml("Factory", factory);
    paintShowRun();
    paintStandDown();
  };

  // What is running, painted in place: the row of the running Sequence trades
  // its Play for Stop, and a run of a name with no row here still gets a Stop.
  // The line for that run is written only when the run it names changes, so a
  // status frame landing mid-press does not replace the Stop being pressed.
  let otherShown = null;
  const paintShowRun = () => {
    const name = runWatch.running();
    let shown = false;
    showList?.querySelectorAll(".show-item").forEach((item) => {
      const running = name !== null && item.dataset.name === name;
      shown = shown || running;
      item.classList.toggle("is-running", running);
      item.querySelector(".seq-row-run")?.classList.toggle("hidden", !running);
      item.querySelector('[data-act="play"]')?.classList.toggle("hidden", running);
      item.querySelector('[data-act="stop"]')?.classList.toggle("hidden", !running);
    });
    const other = name !== null && !shown ? name : null;
    if (showOther && other !== otherShown) {
      otherShown = other;
      showOther.classList.toggle("hidden", other === null);
      showOther.innerHTML = other === null ? ""
        : `<span class="seq-row-run"><span class="indicator ok seq-live" aria-hidden="true"></span>Running ${esc(other)}</span>
           <button type="button" class="btn btn-sm seq-stop icon-act" data-act="stop">${actFace("stop", "Stop")}</button>`;
    }
    if (showNow) {
      const record = runWatch.record();
      const answered = libraryAnswered();
      showNow.classList.toggle("waiting", !answered && name === null && !record);
      // "ended" only when the record says so: out of touch, the last record
      // can still say running, and the run watch then says neither.
      showNow.textContent = name !== null ? `${name} running`
        : record && record.running !== true ? `${record.name} ended`
        : answered ? String(showList?.querySelectorAll(".show-item").length || 0) : "";
    }
  };

  // Rest's own state. The tile of the Sequence it runs says which that is and
  // why it does nothing, if it does nothing; the button itself says Rest.
  const paintStandDown = () => {
    if (!standDownBtn) return;
    const name = standDownEffective();
    const answered = name !== null && libraryAnswered();
    const refusal = answered ? refusalOf(name) : null;
    // Until the droid has said which it is, Rest waits rather than run the
    // default over a choice it has not heard yet.
    standDownBtn.disabled = !answered || Boolean(refusal);
    standDownBtn.classList.toggle("is-pending", !answered);
    standDownBtn.setAttribute("aria-disabled", String(!answered || Boolean(refusal)));
    if (refusal) {
      standDownBtn.dataset.ignoredSays = refusal.notice;
      // The Stand Down Sequence is chosen on Sequences, whatever is wrong with it.
      standDownBtn.dataset.ignoredPage = "seq";
    } else {
      delete standDownBtn.dataset.ignoredSays;
      delete standDownBtn.dataset.ignoredPage;
    }
  };

  // The run whose ending the feedback line holds, so the run being heard
  // again - after a reconnect, say - takes back "Lost touch" rather than leave
  // it under a row that reads Running.
  let endingOf = null;
  const sayShow = (message, level = "") => {
    endingOf = null;
    showFeedback(showFeedbackEl, message, level);
  };
  const runWatch = window.PALiveReading.watchRuns(({ name, running, outcome }) => {
    paintShowRun();
    if (running) {
      if (endingOf === name) sayShow("");
      return;
    }
    const ending = window.PALiveReading.runEnding(outcome, name);
    if (!ending) return;
    sayShow(ending, "error");
    endingOf = name;
  });

  const playSequence = async (name, button) => {
    button.disabled = true;
    button.classList.add("is-pending");
    sayShow("");
    try {
      await runWatch.start(name);
    } catch (error) {
      sayShow(`${name} did not play: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      button.disabled = false;
      button.classList.remove("is-pending");
      // Stand Down's own state is paintStandDown()'s, not this press's.
      paintStandDown();
    }
  };

  const stopSequence = async (button) => {
    button.disabled = true;
    sayShow("");
    try {
      await runWatch.stop();
    } catch (error) {
      sayShow(`Stop failed: ${window.PAApi.messageFor(error)}`, "error");
    } finally {
      button.disabled = false;
    }
  };

  document.getElementById("show-bay")?.addEventListener("click", (event) => {
    const button = event.target.closest?.("button[data-act]");
    if (!button || button.disabled) return;
    if (button.dataset.act === "stop") stopSequence(button);
    else playSequence(button.closest(".show-item").dataset.name, button);
  });

  standDownBtn?.addEventListener("click", () => {
    const name = standDownEffective();
    if (name !== null && !standDownBtn.disabled) playSequence(name, standDownBtn);
  });

  // The posture is the shell's: this asks for it and paints what it says.
  postureBtn?.addEventListener("click", () => {
    const on = !document.body.classList.contains("shell-performing");
    window.dispatchEvent(new CustomEvent("pa:posture", { detail: { on } }));
  });
  window.addEventListener("pa:posture-changed", (event) => {
    if (!postureBtn) return;
    const on = event.detail?.on === true;
    postureBtn.setAttribute("aria-pressed", String(on));
    postureBtn.innerHTML = on ? `${icon("fullscreen-exit")}<span>Leave full screen</span>`
      : `${icon("fullscreen")}<span>Full screen</span>`;
  });

  const loadShowLearned = async ({ handle = null } = {}) => {
    const answer = await (handle ?? window.PAApi).get("/api/seq/list");
    showLearned = Array.isArray(answer?.data) ? answer.data : [];
    renderShowList();
  };

  const loadShowFactory = async ({ handle = null } = {}) => {
    const answer = await (handle ?? window.PAApi).get("/api/seq/builtins");
    showFactory = Array.isArray(answer?.data) ? answer.data : [];
    renderShowList();
  };

  const loadShowMap = async ({ handle = null } = {}) => {
    const answer = await (handle ?? window.PAApi).get("/api/rc/map");
    const map = Array.isArray(answer?.data?.map) ? answer.data.map : [];
    showMapped = map
      .filter((entry) => entry.action === "dome_seq" && entry.payload && window.PAApi.isRcChannelSource(entry.source))
      .map((entry) => ({ name: entry.payload, channel: window.PAApi.rcChannelTitle(entry.source, entry.channel) }));
    renderShowList();
  };

  // The Stand Down choice rides the /api/config payload the log level reads.
  // Only an answer that carries the key is one: a droid whose firmware does
  // not know the Setting has not said "never chosen", so Stand Down waits.
  const adoptStandDown = (config) => {
    const chosen = config?.seq?.standDown;
    if (typeof chosen !== "string") return;
    standDownChoice = chosen;
    // The list draws the Rest mark and may list the name itself.
    renderShowList();
    paintStandDown();
  };

  // Back on the Dashboard after Sequences or RC, the library, the RC Map and
  // the Stand Down choice may have moved: read again each time the Dashboard
  // returns (PASurface starts its polls again then, ADR 0048). The Factory
  // catalog is the firmware's and does not change. The first start is the
  // mount, whose own sections read all of it.
  let showMounted = false;
  window.PASurface.poll(async () => {
    if (!showMounted) {
      showMounted = true;
      return;
    }
    await loadShowLearned();
    await loadShowMap();
    adoptStandDown((await window.PAApi.get("/api/config"))?.data);
  }, { runOnStart: true }).start();

  // -------------------------------------------------------------------------
  // Boot — load recent logs, log level, and action tokens
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API loads as sections so the bootstrap
  // can show recovery state if any fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.

  const SECTIONS = [
    ["app-recent-logs", loadRecentLogs, "recent logs"],
    ["app-log-level", loadLogLevel, "log level setting"],
    ["app-output-names", loadOutputNames, "Output names"],
    ["app-console-catalog", loadConsoleCatalog, "console commands"],
    ["app-seq-learned", loadShowLearned, "your sequences"],
    ["app-seq-factory", loadShowFactory, "factory sequences"],
    ["app-rc-map", loadShowMap, "RC Map"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadRecentLogs().catch(() => {});
      loadLogLevel().catch(() => {});
      loadOutputNames().catch(() => {});
      loadConsoleCatalog().catch(() => {});
      loadShowLearned().catch(() => {});
      loadShowFactory().catch(() => {});
      loadShowMap().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/live_reading.js": "live updates",
      "/dome_bearing.js": "where the dome points",
      "/shell.js": "page layout",
      "/health_signals.js": "health indicator logic",
      "/dome_command_map.js": "dome command map",
      "/dome_panel_model.js": "dome panel state",
      "/dome_layout.js": "dome panel layout",
      "/dome_layout_render.js": "dome panel rendering",
      "/dome_control.js": "dome control",
      "/app.js": "home dashboard",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  // The state to paint before the droid has said anything. It has to be
  // written BEFORE the subscribe below: the Live Reading hands a new
  // subscriber the reading it already holds, synchronously, and the Operator
  // Shell seeds that from its own boot read (ADR 0048). Run after the
  // subscription, it would overwrite a seeded frame -- which once left the
  // Dashboard disabling its estop release on a latched droid until the droid
  // emitted a status, which a quiet latched droid never does (#359).
  setSleepUi(false);

  startPageLoad();

  // Every status this surface paints rides the Live Reading, which owns the
  // stream or the one fallback poll for the whole shell (data/live_reading.js).
  window.PALiveReading.subscribe(applyReading);

  // The log lines are the stream's own events, not the status, so they are
  // read off the stream directly; with no stream there are none to read.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "log") payload.split("\x01").forEach((line) => appendLogLine(line));
      if (eventType === "stream_error") {
        appendLogLine(LOG_UNREACHABLE_TEXT);
      }
    });
  }
})();

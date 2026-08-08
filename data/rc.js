(() => {
  let selectedChannel = null;
  let rcSnapshot = null;
  let channelMap = {};
  let triggerPulseState = {};
  
  // ── Learning mode state ───────────────────────────────────────────────────
  // Flow: idle → learnActive (user clicks Learn) → await signal > threshold
  //       → learnHit (candidate recorded) → selectedChannel (user confirms)
  //       → editor opened. Timeout or user cancel returns to idle.
  let learnActive = false;
  let learnBaseline = null;    // raw snapshot taken when detect mode was entered
  let learnHit = null;         // { source:"sbus1"|"sbus2"|"pwm", channel:1-based, raw:value }
  let learnStartMs = 0;
  const LEARN_TIMEOUT_MS = 30000;
  // SBUS raw threshold: 300 units from baseline (~18% of 1639 full range).
  // Buttons snap 820+ units from center; this filters mild stick drift.
  const LEARN_SBUS_THRESHOLD = 300;
  // PWM threshold: 200 µs from baseline.
  // Buttons go 500+ µs from center (1500); filters cable noise (~10 µs).
  const LEARN_PWM_THRESHOLD = 200;
  // ─────────────────────────────────────────────────────────────────────────
  
  const rcModeCards = document.querySelectorAll(".rc-mode-card");
  const rcInputModeHidden = document.getElementById("rc-input-mode");
  const rcModeFeedback = document.getElementById("rc-mode-feedback");
  const rcResetDefaults = document.getElementById("rc-reset-defaults");
  const rcDisabledCard = document.getElementById("rc-disabled-card");
  
  const singleSbusRecvSection = document.getElementById("single-sbus-recv-section");
  const sbusRecvSel = document.getElementById("sbus-recv-sel");
  const sbusRecvFeedback = document.getElementById("sbus-recv-feedback");
  let sbusRecvFeedbackTimer = null;
  let confirmedSbusRecvValue = null;
  const rcSummaryBody = document.getElementById("rc-summary-body");
  
  const rcChannelItems = document.getElementById("rc-channel-items");
  const rcLivePreviewContent = document.getElementById("rc-live-preview-content");
  const rcPreviewSourceHealth = document.getElementById("rc-preview-source-health");
  const rcEditorContent = document.getElementById("rc-editor-content");
  const rcEditorApply = document.getElementById("rc-editor-apply");
  const rcEditorRevert = document.getElementById("rc-editor-revert");
  const rcEditorFeedback = document.getElementById("rc-editor-feedback");
  const rcEditorDirty = document.getElementById("rc-editor-dirty");
  const rcEditorSavedAt = document.getElementById("rc-editor-saved-at");
  
  const rcLearnBtn    = document.getElementById("rc-learn-btn");
  const rcLearnBanner = document.getElementById("rc-learn-banner");
  const rcLearnStatus = document.getElementById("rc-learn-status");
  const rcLearnStop   = document.getElementById("rc-learn-stop");
  let rcInputsEnabled = true;
  
  const ANALOG_ACTION_TOKENS = new Set(['drive_speed', 'drive_steer', 'dome_speed']);
  // Hardcoded fallback used until GET /api/actions resolves.
  // Matches robotActionIdToString() NVS token keys in rc_mapping.h.
  const HARDCODED_ACTION_TARGETS = [
    { token: 'drive_speed', label: 'Speed', group: 'Movement', description: 'Forward/reverse drive speed (analog axis)', disabled: false, testable: false, safetyCritical: false },
    { token: 'drive_steer', label: 'Steer', group: 'Movement', description: 'Left/right steering (analog axis)', disabled: false, testable: false, safetyCritical: false },
    { token: 'dome_speed', label: 'Dome Speed', group: 'Movement', description: 'Dome rotation speed (analog axis)', disabled: false, testable: false, safetyCritical: false },
    { token: 'op_mode', label: 'Set Mode', group: 'Mode', description: 'Switch between stationary and driving mode', disabled: false, testable: true, safetyCritical: false, oneShot: false },
    { token: 'arm1_toggle', label: 'ARM1 Toggle', group: 'Arms', description: 'Toggle arm 1 servo between open and closed', disabled: false, testable: true, safetyCritical: false, oneShot: false },
    { token: 'arm2_toggle', label: 'ARM2 Toggle', group: 'Arms', description: 'Toggle arm 2 servo between open and closed', disabled: false, testable: true, safetyCritical: false, oneShot: false },
    { token: 'aux1_toggle', label: 'AUX1 Toggle', group: 'Arms', description: 'Toggle aux 1 servo between open and closed', disabled: false, testable: true, safetyCritical: false, oneShot: false },
    { token: 'aux2_toggle', label: 'AUX2 Toggle', group: 'Arms', description: 'Toggle aux 2 servo between open and closed', disabled: false, testable: true, safetyCritical: false, oneShot: false },
    { token: 'aux3_toggle', label: 'AUX3 Toggle', group: 'Arms', description: 'Toggle aux 3 servo between open and closed', disabled: false, testable: true, safetyCritical: false, oneShot: false },
    { token: 'seq', label: 'Marcduino Sequence', group: 'Sequences', description: 'Trigger a raw numbered body sequence payload (typically SE30-SE36)', disabled: false, testable: false, safetyCritical: false },
    { token: 'dome_seq', label: 'Dome Sequence', group: 'Sequences', description: 'Trigger a dome-side panel/light sequence by number', disabled: false, testable: false, safetyCritical: false },
    { token: 'cmd', label: 'Marcduino Command', group: 'Command', description: 'Send a specific Marcduino command string to the dome', disabled: false, testable: false, safetyCritical: false },
    { token: 'sleep_toggle', label: 'Sleep Toggle', group: 'System', description: 'Toggle cosmetic sleep mode while keeping drive safety active', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_general', label: 'Random General', group: 'Sound', description: 'Play one random track from configured general range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_chatty', label: 'Random Chatty', group: 'Sound', description: 'Play one random track from configured chatty range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_happy', label: 'Random Happy', group: 'Sound', description: 'Play one random track from configured happy range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_processing', label: 'Random Processing', group: 'Sound', description: 'Play one random track from configured processing range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_sad', label: 'Random Sad', group: 'Sound', description: 'Play one random track from configured sad range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_sentimental', label: 'Random Sentimental', group: 'Sound', description: 'Play one random track from configured sentimental range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_humming', label: 'Random Humming', group: 'Sound', description: 'Play one random track from configured humming range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_scream', label: 'Random Scream', group: 'Sound', description: 'Play one random track from configured scream range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_surprised', label: 'Random Surprised', group: 'Sound', description: 'Play one random track from configured surprised range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_alert', label: 'Random Alert', group: 'Sound', description: 'Play one random track from configured alert range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_snarky', label: 'Random Snarky', group: 'Sound', description: 'Play one random track from configured snarky range', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_whistle', label: 'Random Whistle', group: 'Sound', description: 'Play one random track from configured whistle range', disabled: false, testable: true, safetyCritical: false },
    { token: 'estop', label: 'Emergency Stop', group: 'Safety', description: 'Immediately stop all drive output and latch estop', disabled: false, testable: false, safetyCritical: true },
    { token: 'droid_seq_scream', label: 'Scream', group: 'Sequences', description: 'SE01 - scream audio and body sequence, then forward :SE01 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_wave', label: 'Wave', group: 'Sequences', description: 'SE02 - body wave sequence, then forward :SE02 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_fast_wave', label: 'Fast Wave', group: 'Sequences', description: 'SE03 - fast wave sequence, then forward :SE03 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_open_wave', label: 'Open Wave', group: 'Sequences', description: 'SE04 - open wave sequence, then forward :SE04 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_beep_cantina', label: 'Beep Cantina', group: 'Sequences', description: 'SE05 - short Cantina audio with body wave, then forward :SE05 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_faint', label: 'Faint', group: 'Sequences', description: 'SE06 - faint audio with body park sequence, then forward :SE06 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_cantina', label: 'Cantina Dance', group: 'Sequences', description: 'SE07 - long Cantina audio with body wave, then forward :SE07 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_leia', label: 'Leia Message', group: 'Sequences', description: 'SE08 - Leia audio with body sequence, then forward :SE08 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_disco', label: 'Disco', group: 'Sequences', description: 'SE09 - disco audio ($D) with body wave, then forward :SE09 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_screams', label: 'Screams', group: 'Sequences', description: 'SE15 - screams audio only on body side, then forward :SE15 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_wiggle', label: 'Panel Wiggle', group: 'Sequences', description: 'SE16 - body wave sequence, then forward :SE16 to dome', disabled: false, testable: true, safetyCritical: false },
    { token: 'speed_preset_cycle', label: 'Speed Preset Cycle', group: 'Movement', description: 'Cycle drive speed preset Slow → Normal → Turbo', disabled: false, testable: true, safetyCritical: false },
  ];

  // Live action targets — replaced on load from GET /api/actions.
  // Falls back to HARDCODED_ACTION_TARGETS if the request fails.
  let actionTargets = HARDCODED_ACTION_TARGETS;
  let actionPickerScrollTop = 0;
  let actionPickerFeedback = null;
  let actionPickerInFlightToken = null;
  let actionPickerLastChannel = null;
  const actionPickerGroupOpen = {};

  let actionPickerQuery = '';
  let recentActionTokens = [];
  const ACTION_RECENTS_KEY = 'pa.rc.recentActionTokens';
  const ACTION_RECENTS_LIMIT = 8;

  const DOMAIN_GROUP = {
    drive: 'Movement',
    servo: 'Arms',
    dome: 'Sequences',
    sound: 'Sound',
    system: 'System',
    aux: 'Aux',
  };
  const ACTION_GROUP_OVERRIDE = {
    'system.action.set-mode': 'Mode',
    'system.action.estop': 'Safety',
    'dome.action.marcduino-command': 'Command',
    'dome.action.set-speed': 'Movement',
  };
  const ACTION_GROUP_ORDER = ['Movement', 'Mode', 'Arms', 'Sound', 'Sequences', 'Command', 'Safety', 'System', 'Aux', 'Other'];
  const DEFAULT_COLLAPSED_GROUPS = new Set(['Sound', 'Sequences']);
  const NON_TESTABLE_TOKENS = new Set(['drive_speed', 'drive_steer', 'dome_speed', 'estop']);

  // dome_seq is now enabled.
  const UNAVAILABLE_TOKENS = new Set();

  const actionGroup = (entry) => {
    if (ACTION_GROUP_OVERRIDE[entry.name]) return ACTION_GROUP_OVERRIDE[entry.name];
    return DOMAIN_GROUP[entry.domain] || 'Other';
  };

  const sortByGroupOrder = (a, b) => {
    const ai = ACTION_GROUP_ORDER.includes(a) ? ACTION_GROUP_ORDER.indexOf(a) : ACTION_GROUP_ORDER.indexOf('Other');
    const bi = ACTION_GROUP_ORDER.includes(b) ? ACTION_GROUP_ORDER.indexOf(b) : ACTION_GROUP_ORDER.indexOf('Other');
    if (ai !== bi) return ai - bi;
    return a.localeCompare(b);
  };

  const loadRecentActionTokens = () => {
    try {
      const parsed = JSON.parse(window.localStorage.getItem(ACTION_RECENTS_KEY) || '[]');
      if (!Array.isArray(parsed)) return;
      recentActionTokens = parsed
        .filter((token) => typeof token === 'string' && token.trim() !== '')
        .slice(0, ACTION_RECENTS_LIMIT);
    } catch (_error) {
      // ignore: localStorage parse error — fall back to empty list
      recentActionTokens = [];
    }
  };

  const saveRecentActionTokens = () => {
    try {
      window.localStorage.setItem(ACTION_RECENTS_KEY, JSON.stringify(recentActionTokens));
    } catch (_error) {
      // ignore: localStorage write failed (e.g., private browsing) — not persisted
    }
  };

  const syncRecentActionTokensWithTargets = () => {
    const validTokens = new Set(actionTargets.map((item) => item.token));
    const filtered = recentActionTokens.filter((token) => validTokens.has(token));
    if (filtered.length !== recentActionTokens.length) {
      recentActionTokens = filtered;
      saveRecentActionTokens();
    }
  };

  const rememberRecentActionToken = (token) => {
    if (!token) return;
    recentActionTokens = [token, ...recentActionTokens.filter((entry) => entry !== token)]
      .slice(0, ACTION_RECENTS_LIMIT);
    saveRecentActionTokens();
  };

  const buildActionTargetsFromApi = (entries) => {
    const targets = [];
    entries.forEach((entry) => {
      const token = typeof entry.token === 'string' ? entry.token : '';
      if (!token) return;
      const unavail = UNAVAILABLE_TOKENS.has(token);
      const safetyCritical = Boolean(entry.safety_critical);
      const testable = typeof entry.testable === 'boolean'
        ? entry.testable
        : !NON_TESTABLE_TOKENS.has(token);
      const oneShot = typeof entry.one_shot === 'boolean'
        ? entry.one_shot
        : !ANALOG_ACTION_TOKENS.has(token);
      const label = entry.display_name || entry.name || token;
      targets.push({
        token,
        label: unavail ? `${label} (Unavailable)` : label,
        group: actionGroup(entry),
        description: entry.description || '',
        disabled: unavail,
        testable: testable && !unavail && !safetyCritical,
        safetyCritical,
        oneShot,
      });
    });
    return targets;
  };

  const loadActionTargets = async () => {
    try {
      const result = await window.PAApi.get('/api/actions', { timeoutMs: 5000 });
      if (Array.isArray(result.data)) {
        actionTargets = buildActionTargetsFromApi(result.data);
      }
      syncRecentActionTokensWithTargets();
    } catch (_err) {
      // fetch error — fall back to hardcoded action targets
      console.warn('[RC] Failed to load action registry, using built-in list');
      syncRecentActionTokensWithTargets();
    }
  };

  const MARCDUINO_SEQUENCES = [
    { id: 30, name: "Utility arm open-and-close", description: "Open both utility arms, then close them." },
    { id: 31, name: "All body panels open and close", description: "Open all body panels, then close them." },
    { id: 32, name: "All body doors wiggle-close", description: "Open all body doors, then close with wiggle timing." },
    { id: 33, name: "Use gripper arm", description: "Run the body sequence that uses ARM1 gripper motion." },
    { id: 34, name: "Use interface tool", description: "Run the body sequence that uses ARM2 interface-tool motion." },
    { id: 35, name: "Ping-pong body doors", description: "Alternate body door motion in ping-pong pattern." },
    { id: 36, name: "BT-1 two-gripper sequence", description: "Run the BT-1 style dual-gripper body sequence." },
  ];

  // Factory dome sequences (fallback when /api/seq/list is unavailable)
  const FACTORY_DOME_SEQUENCES = [
    { payload: 'DM:PIES',      label: 'Pie Panels',           description: 'Toggle pie panels open/close (12 s)' },
    { payload: 'DM:LOW',       label: 'Lower Panels',         description: 'Toggle lower panels open/close (15 s)' },
    { payload: 'DM:OPENALL',   label: 'Open All Panels',      description: 'Toggle all panels open/close (10 s)' },
    { payload: 'DM:FLUTTER',   label: 'Flutter',              description: 'All panels flutter to 75%, snap closed (10 s)' },
    { payload: 'DM:BLOOM',     label: 'Bloom',                description: 'Pie panels ease open, wiggle, close (8 s)' },
    { payload: 'DM:SCREAM',    label: 'Scream',               description: 'All panels burst open, red alert (15 s)' },
    { payload: 'DM:OVERLOAD',  label: 'Overload',             description: 'Failure logics, panels sluggishly drift (12 s)' },
    { payload: 'DM:HEART',     label: 'Heart',                description: 'Rainbow holos, sweet logic message (10 s)' },
    { payload: 'DM:ALARM',     label: 'Alarm',                description: 'Pulsing red holos and logics (10 s)' },
    { payload: 'DM:DISCO',     label: 'Disco',                description: 'Disco sequence delegating to SE09 (46 s)' },
    { payload: 'DM:VADER',     label: 'Imperial March',       description: 'Imperial March -- red logics/holos (47 s)' },
    { payload: 'DM:ROCKMARCH', label: 'Rock March',           description: 'Imperial March alternate visual (47 s)' },
    { payload: 'DM:HELLO',     label: 'Hello There',          description: 'Panel wave + logic scroll greeting (4 s)' },
    { payload: 'DM:LEIA',      label: 'Leia',                 description: 'Front holo Leia effect, logic Leia mode (36 s)' },
    { payload: 'DM:CANTINA',   label: 'Cantina',              description: '130 BPM alternating panel dance (17 s)' },
    { payload: 'DM:RESET',     label: 'Reset All',            description: 'Close all panels, reset all subsystems (4 s)' },
    { payload: 'DM:RANDOM',    label: 'Random',               description: 'Delegate to a random SE sequence' },
  ];

  // Cached learned sequences (fetched on demand)
  let cachedLearnedSequences = null;

  const normalizeMarcduinoSequencePayload = (payload) => {
    const raw = String(payload || "").trim().toUpperCase();
    if (/^\d{2}$/.test(raw)) return raw;
    const match = /^SE(\d{2})$/.exec(raw);
    return match ? match[1] : raw;
  };

  const renderMarcduinoSequenceOptions = (selectedPayload) => {
    const normalizedSelected = normalizeMarcduinoSequencePayload(selectedPayload);
    return MARCDUINO_SEQUENCES.map((entry) => {
      const value = String(entry.id);
      const selected = normalizedSelected === value ? " selected" : "";
      const label = `SE${entry.id} - ${entry.name}`;
      const title = entry.description || "";
      return `<option value="${value}" title="${window.PAUtils.escapeHtml(title)}"${selected}>${window.PAUtils.escapeHtml(label)}</option>`;
    }).join("\n            ");
  };

  const renderDomeSequenceOptions = (selectedPayload) => {
    const sel = String(selectedPayload || '').trim().toUpperCase();
    // Use factory sequences only for initial render
    // Learned sequences will be fetched asynchronously
    return FACTORY_DOME_SEQUENCES.map((entry) => {
      const selected = sel === entry.payload ? ' selected' : '';
      return `<option value="${window.PAUtils.escapeHtml(entry.payload)}" title="${window.PAUtils.escapeHtml(entry.description)}"${selected}>${window.PAUtils.escapeHtml(entry.label)}</option>`;
    }).join('\n            ');
  };

  const updateDomeSequenceOptions = async (selectedPayload) => {
    const sel = String(selectedPayload || '').trim().toUpperCase();

    // Fetch learned sequences if not cached
    if (cachedLearnedSequences === null) {
      try {
        const result = await PAApi.get('/api/seq/list');
        if (result.ok && Array.isArray(result.data)) {
          cachedLearnedSequences = result.data.map((seq) => ({
            payload: seq.name,
            label: seq.name,
            description: `Learned sequence (${seq.stepCount || 0} steps, ${seq.suppressMs}ms suppress)`,
          }));
        } else {
          cachedLearnedSequences = [];
        }
      } catch {
        cachedLearnedSequences = [];
      }
    }

    // Combine factory + learned sequences
    const allSequences = [...FACTORY_DOME_SEQUENCES, ...cachedLearnedSequences];

    // Find the dome_seq select element and update it
    const domeSeqSelect = rcEditorContent.querySelector('[data-cond="dome_seq"] select');
    if (domeSeqSelect) {
      domeSeqSelect.innerHTML = allSequences.map((entry) => {
        const selected = sel === entry.payload ? ' selected' : '';
        return `<option value="${window.PAUtils.escapeHtml(entry.payload)}" title="${window.PAUtils.escapeHtml(entry.description)}"${selected}>${window.PAUtils.escapeHtml(entry.label)}</option>`;
      }).join('\n            ');
    }
  };

  const SOURCE_OPTIONS = {
    standard_pwm: ["pwm"],
    single_sbus: ["sbus1", "sbus2"],
    dual_sbus: ["sbus1", "sbus2"],
  };

  const channelKeyOf = (source, channel) => `${source}:${Number(channel)}`;

  const parseChannelKey = (channelKey) => {
    const [source = "", channelValue = "0"] = String(channelKey || "").split(":");
    const channel = Number.parseInt(channelValue, 10) || 0;
    return { source, channel };
  };

  const getRcModeFromConfig = (cfg) => {
    const mode = cfg?.rc?.inputMode;
    return typeof mode === "string" ? mode : "standard_pwm";
  };

  const rcComponentsEnabled = (cfg) => {
    const c = cfg?.components || {};
    return Boolean(
      c.rcCh1?.enabled || c.rcCh2?.enabled || c.rcCh3?.enabled ||
      c.rcCh4?.enabled || c.rcCh5?.enabled || c.rcCh6?.enabled
    );
  };

  const getSingleSbusRecvCh2 = (cfg) => cfg?.rc?.sbus?.recvCh2 === true;

  const setSingleSbusRecvSelect = (recvCh2) => {
    if (!sbusRecvSel) return;
    sbusRecvSel.value = recvCh2 ? "true" : "false";
  };

  const updateRecvSel = (mode) => {
    if (!singleSbusRecvSection) return;
    const visible = mode === "single_sbus";
    singleSbusRecvSection.classList.toggle("hidden", !visible);
    singleSbusRecvSection.setAttribute("aria-hidden", visible ? "false" : "true");
  };

  const setSbusRecvFeedback = (message, variant = "", clearAfterMs = 0) => {
    if (!sbusRecvFeedback) return;
    if (sbusRecvFeedbackTimer) {
      window.clearTimeout(sbusRecvFeedbackTimer);
      sbusRecvFeedbackTimer = null;
    }
    sbusRecvFeedback.textContent = message;
    sbusRecvFeedback.className = variant ? `feedback mt-8 ${variant}` : "feedback mt-8";
    if (clearAfterMs > 0) {
      sbusRecvFeedbackTimer = window.setTimeout(() => {
        sbusRecvFeedback.textContent = "";
        sbusRecvFeedback.className = "feedback mt-8";
        sbusRecvFeedbackTimer = null;
      }, clearAfterMs);
    }
  };

  const setModeFeedback = (message, variant = '') => {
    if (!rcModeFeedback) return;
    rcModeFeedback.textContent = message;
    rcModeFeedback.className = variant ? `feedback mt-12 ${variant}` : 'feedback mt-12';
  };

  const setEditorFeedback = (message, variant = '') => {
    if (!rcEditorFeedback) return;
    rcEditorFeedback.textContent = message;
    rcEditorFeedback.className = variant ? `feedback mt-12 ${variant}` : 'feedback mt-12';
  };

  const setRcInputsEnabled = (enabled) => {
    rcInputsEnabled = enabled;
    rcDisabledCard?.classList.toggle("hidden", enabled);

    if (rcLearnBtn) {
      rcLearnBtn.disabled = !enabled;
      rcLearnBtn.setAttribute("aria-disabled", enabled ? "false" : "true");
      if (!enabled) rcLearnBtn.textContent = "🔍 Detect channel";
    }
    if (rcLearnStop) {
      rcLearnStop.disabled = !enabled;
      rcLearnStop.setAttribute("aria-disabled", enabled ? "false" : "true");
    }

    if (!enabled && learnActive) {
      exitLearnMode();
    }
  };

  const getEditorMode = () => {
    return rcSnapshot?.mode || rcInputModeHidden?.value || "standard_pwm";
  };

  const normalizeMapEntry = (entry) => {
    const source = typeof entry?.source === 'string' ? entry.source : '';
    const channel = Number.parseInt(entry?.channel, 10) || 0;
    const action = typeof entry?.action === 'string' ? entry.action : '';
    const payload = entry?.payload == null ? '' : String(entry.payload);
    if (!source || channel <= 0 || !action) return null;
    const normalized = { source, channel, action };
    if (payload) normalized.payload = payload;
    return normalized;
  };

  const assignmentForChannel = (channelKey) => channelMap[channelKey] || null;


  const asMapArray = () => Object.values(channelMap);

  const modeMapFromArray = (entries) => {
    const byChannel = {};
    (Array.isArray(entries) ? entries : []).forEach((entry) => {
      const normalized = normalizeMapEntry(entry);
      if (!normalized) return;
      byChannel[channelKeyOf(normalized.source, normalized.channel)] = normalized;
    });
    return byChannel;
  };

  const isAnalogAction = (token) => ANALOG_ACTION_TOKENS.has(token);

  const mapEntryAction = (entry) => String(entry?.action || '');

  const mappedFromRaw = (source, raw) => {
    if (raw == null) return 0;
    if (source === 'pwm') {
      return Math.max(-1, Math.min(1, (Number(raw) - 1500) / 500));
    }
    return Math.max(-1, Math.min(1, (Number(raw) - 992) / 819));
  };

  const rawForChannel = (source, channel) => {
    const sourceRaw = rcSnapshot?.raw?.[source];
    return Array.isArray(sourceRaw) ? sourceRaw[channel - 1] : null;
  };

  const getChannelTelemetry = (channelKey) => {
    if (!rcSnapshot) return null;
    const { source, channel } = parseChannelKey(channelKey);
    if (!source || channel <= 0) return null;
    const raw = rawForChannel(source, channel);
    if (raw == null) return { raw: null, mapped: 0, pressed: false, pressedLevel: false };
    const center = source === 'pwm' ? 1500 : 992;
    const threshold = source === 'pwm' ? 200 : 300;
    const pressedLevel = Math.abs(raw - center) >= threshold;
    return {
      raw,
      mapped: mappedFromRaw(source, raw),
      pressed: pressedLevel,
      pressedLevel,
    };
  };

  let debouncedSaveRcMode = () => {};

  const actionTargetFromToken = (token) => actionTargets.find((item) => item.token === token) || null;

  const actionLabelFromToken = (token) => {
    const found = actionTargetFromToken(token);
    return found ? found.label : token;
  };

  const sourceLabel = (source) => {
    if (source === "pwm") return "PWM";
    if (source === "sbus1") return "SBUS#1";
    if (source === "sbus2") return "SBUS#2";
    return "Unknown";
  };

  const MODE_LABEL = {
    standard_pwm: "🎮 Standard PWM",
    single_sbus: "📻 Single SBUS",
    dual_sbus: "📡 Dual SBUS",
  };

  const modeLabel = (mode) => MODE_LABEL[mode] || mode;

  const channelTitleFromKey = (channelKey) => {
    const { source, channel } = parseChannelKey(channelKey);
    return `${sourceLabel(source)} CH ${channel || '—'}`;
  };

  const setEditorDirtyState = (state, text) => {
    if (!rcEditorDirty) return;
    rcEditorDirty.dataset.state = state;
    rcEditorDirty.textContent = text;
  };

  const setEditorSavedTimestamp = (stamp) => {
    if (!rcEditorSavedAt) return;
    rcEditorSavedAt.textContent = stamp ? `Last saved: ${stamp}` : "Last saved: —";
  };

  const markEditorDirty = () => {
    if (rcEditorApply) rcEditorApply.disabled = false;
    if (rcEditorRevert) rcEditorRevert.disabled = false;
    setEditorDirtyState("dirty", "Unsaved changes");
  };

  const markEditorClean = (savedStamp = null) => {
    if (rcEditorApply) rcEditorApply.disabled = true;
    if (rcEditorRevert) rcEditorRevert.disabled = true;
    setEditorDirtyState("clean", "Saved");
    if (savedStamp !== null) {
      setEditorSavedTimestamp(savedStamp);
    }
  };

  const miniBarHtml = (mapped) => {
    const normalized = Math.max(0, Math.min(1, (Number(mapped) + 1) / 2));
    const pct = Math.round(normalized * 100);
    return `<div class="rc-mini-bar"><div class="rc-mini-fill" style="--mini-pct:${pct}%"></div></div>`;
  };

  const isOneShotActionToken = (token) => {
    if (!token || isAnalogAction(token)) return false;
    const found = actionTargetFromToken(token);
    if (found && typeof found.oneShot === 'boolean') return found.oneShot;
    return true;
  };

  const consumeTriggerPulse = (channelKey, pressedLevel) => {
    if (!channelKey) return false;
    const now = Date.now();
    const state = triggerPulseState[channelKey] || { init: false, lastPressed: false, pulseUntil: 0 };
    if (!state.init) {
      state.init = true;
      state.lastPressed = pressedLevel;
      state.pulseUntil = 0;
      triggerPulseState[channelKey] = state;
      return false;
    }
    if (pressedLevel !== state.lastPressed) {
      state.lastPressed = pressedLevel;
      state.pulseUntil = now + 450;
    }
    triggerPulseState[channelKey] = state;
    return now <= state.pulseUntil;
  };

  const triggerStateHtml = (token, channelKey, telemetry) => {
    const pressedLevel = Boolean(telemetry && telemetry.pressedLevel);
    const pressed = isOneShotActionToken(token)
      ? consumeTriggerPulse(channelKey, pressedLevel)
      : pressedLevel;
    return pressed ? '<span aria-label="Pressed">● PRESSED</span>' : '<span aria-label="Released">○ Released</span>';
  };

  const renderSourceHealth = () => {
    if (!rcPreviewSourceHealth) return;
    const sources = rcSnapshot?.sources || {};
    const names = ["sbus1", "sbus2", "pwm"];
    rcPreviewSourceHealth.innerHTML = names.map(name => {
      const src = sources[name] || {};
      const enabled = Boolean(src.enabled);
      const linked = Boolean(src.linked);
      const age = Number(src.ageMs || 0);
      const state = !enabled ? "disabled" : linked ? "linked" : "waiting";
      return `<div class="rc-source-card rc-source-card-compact">
        <div class="rc-source-card-title">${window.PAUtils.escapeHtml(name.toUpperCase())}</div>
        <div class="rc-source-card-meta">${state} · age ${age}ms</div>
      </div>`;
    }).join("");
  };

  const wireFocusableItem = (el, onActivate) => {
    if (!el) return;
    el.setAttribute("tabindex", "0");
    el.addEventListener("focus", () => {
      el.classList.add("keyboard-focus");
    });
    el.addEventListener("blur", () => {
      el.classList.remove("keyboard-focus");
    });
    el.addEventListener("keydown", (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        onActivate();
      }
    });
  };

  const renderSummaryTable = () => {
    if (!rcSummaryBody) return;
    const rows = asMapArray()
      .slice()
      .sort((a, b) => (a.source === b.source ? a.channel - b.channel : a.source.localeCompare(b.source)));

    if (!rows.length) {
      rcSummaryBody.innerHTML = '<tr><td colspan="3" class="desc">No channels mapped yet.</td></tr>';
      return;
    }

    const liveKeys = new Set(rows.map((entry) => channelKeyOf(entry.source, entry.channel)));
    Object.keys(triggerPulseState).forEach((key) => {
      if (!liveKeys.has(key)) delete triggerPulseState[key];
    });

    rcSummaryBody.innerHTML = rows.map((entry) => {
      const channelKey = channelKeyOf(entry.source, entry.channel);
      const telemetry = getChannelTelemetry(channelKey);
      const token = mapEntryAction(entry);
      const live = isAnalogAction(token)
        ? miniBarHtml(telemetry?.mapped || 0)
        : triggerStateHtml(token, channelKey, telemetry);
      return `<tr data-chkey="${window.PAUtils.escapeHtml(channelKey)}" class="${selectedChannel === channelKey ? 'active-channel' : ''}">
        <td>${window.PAUtils.escapeHtml(actionLabelFromToken(token))}</td>
        <td>${window.PAUtils.escapeHtml(channelTitleFromKey(channelKey))}</td>
        <td>${live}</td>
      </tr>`;
    }).join('');
  };

  const renderChannelList = () => {
    if (!rcChannelItems) return;
    const mode = getEditorMode();
    const snap = rcSnapshot;

    const renderGroup = (title, source, rawArray, channelCount) => {
      const items = [];
      for (let i = 1; i <= channelCount; i++) {
        const channelKey = channelKeyOf(source, i);
        const raw = rawArray ? rawArray[i - 1] : null;
        const entry = assignmentForChannel(channelKey);
        const actionLabel = entry ? actionLabelFromToken(mapEntryAction(entry)) : null;
        const isActive = channelKey === selectedChannel;
        const rawDisplay = raw != null ? raw : '—';
        const barPct = raw != null
          ? (source === 'pwm'
              ? Math.round(((raw - 1000) / 1000) * 100)
              : Math.round(((raw - 172) / (1811 - 172)) * 100))
          : 0;
        const clampedPct = Math.max(0, Math.min(100, barPct));
        items.push(`<div class="rc-channel-item${isActive ? ' active' : ''}" data-chkey="${window.PAUtils.escapeHtml(channelKey)}" role="button" tabindex="0" aria-pressed="${isActive}">
          <div class="rc-channel-item-head">
            <span class="rc-ch-num">CH ${i}</span>
            <span class="rc-ch-raw">${rawDisplay}</span>
          </div>
          <div class="rc-channel-mini-bar"><div class="rc-channel-mini-fill" style="--pct:${clampedPct}%"></div></div>
          <div class="rc-ch-action">${actionLabel ? window.PAUtils.escapeHtml(actionLabel) : '<span class="rc-ch-unassigned">not mapped</span>'}</div>
        </div>`);
      }
      return `<div class="rc-channel-group">
        <div class="rc-channel-group-title">${window.PAUtils.escapeHtml(title)}</div>
        ${items.join('')}
      </div>`;
    };

    let html = '';
    if (mode === 'standard_pwm') {
      html = renderGroup('PWM', 'pwm', snap?.raw?.pwm, 6);
    } else {
      html = renderGroup('SBUS1', 'sbus1', snap?.raw?.sbus1, 6)
           + renderGroup('SBUS2', 'sbus2', snap?.raw?.sbus2, 6);
    }

    rcChannelItems.innerHTML = html;

    const channelNodes = Array.from(rcChannelItems.querySelectorAll('.rc-channel-item'));
    channelNodes.forEach((node, index) => {
      const channelKey = node.dataset.chkey;
      node.addEventListener('click', () => selectChannel(channelKey));
      wireFocusableItem(node, () => selectChannel(channelKey));
      node.addEventListener('keydown', (e) => {
        if (e.key === 'ArrowDown') {
          e.preventDefault();
          channelNodes[Math.min(index + 1, channelNodes.length - 1)]?.focus();
        }
        if (e.key === 'ArrowUp') {
          e.preventDefault();
          channelNodes[Math.max(index - 1, 0)]?.focus();
        }
      });
    });

    applyLearnHighlight();
  };

  // Cheap live update: refresh raw values and bars without full re-render.
  const updateChannelListRaw = () => {
    if (!rcChannelItems || !rcSnapshot?.raw) return;
    rcChannelItems.querySelectorAll('.rc-channel-item').forEach(el => {
      const { source, channel } = parseChannelKey(el.dataset.chkey);
      const raw = rawForChannel(source, channel);
      const rawEl = el.querySelector('.rc-ch-raw');
      if (rawEl) rawEl.textContent = raw != null ? raw : '—';
      const fillEl = el.querySelector('.rc-channel-mini-fill');
      if (fillEl && raw != null) {
        const barPct = source === 'pwm'
          ? Math.round(((raw - 1000) / 1000) * 100)
          : Math.round(((raw - 172) / (1811 - 172)) * 100);
        fillEl.style.setProperty('--pct', `${Math.max(0, Math.min(100, barPct))}%`);
      }
    });
  };

  const renderLivePreview = () => {
    if (!rcLivePreviewContent) return;
    renderSourceHealth();

    if (!selectedChannel) {
      rcLivePreviewContent.innerHTML = '';
      return;
    }

    const entry = assignmentForChannel(selectedChannel) || { ...parseChannelKey(selectedChannel), payload: '' };
    const telemetry = getChannelTelemetry(selectedChannel);
    const mapped = Number(telemetry?.mapped || 0);
    const raw = telemetry?.raw ?? '—';
    const actionToken = mapEntryAction(entry);

    let barHtml = '';
    if (isAnalogAction(actionToken)) {
      const width = Math.min(50, Math.round(Math.abs(mapped) * 50));
      const left = mapped >= 0 ? 50 : 50 - width;
      barHtml = `<div class="rc-preview-bar rc-preview-bar-center">
        <div class="rc-preview-fill signed" style="--bar-left:${left}%;--bar-width:${width}%"></div>
      </div>`;
    }

    rcLivePreviewContent.innerHTML = `
      <h4 class="rc-preview-title">${window.PAUtils.escapeHtml(channelTitleFromKey(selectedChannel))}</h4>
      <div class="rc-preview-stack">
        <div>Action: <strong>${actionToken ? window.PAUtils.escapeHtml(actionLabelFromToken(actionToken)) : 'Not mapped'}</strong></div>
        <div>Raw: <strong>${window.PAUtils.escapeHtml(String(raw))}</strong></div>
        ${isAnalogAction(actionToken) ? `<div>Mapped: <strong>${mapped.toFixed(3)}</strong></div>${barHtml}` : `<div>State: ${triggerStateHtml(actionToken, selectedChannel, telemetry)}</div>`}
      </div>`;
  };

  const groupedActionTargets = () => {
    const groups = new Map();
    actionTargets.forEach((item) => {
      if (!groups.has(item.group)) groups.set(item.group, []);
      groups.get(item.group).push(item);
    });

    return Array.from(groups.keys())
      .sort(sortByGroupOrder)
      .map((name) => ({
        name,
        items: groups.get(name).slice().sort((a, b) => a.label.localeCompare(b.label)),
      }));
  };

  const isActionGroupOpen = (groupName) => {
    if (Object.prototype.hasOwnProperty.call(actionPickerGroupOpen, groupName)) {
      return actionPickerGroupOpen[groupName];
    }
    return !DEFAULT_COLLAPSED_GROUPS.has(groupName);
  };

  const actionMatchesQuery = (item, query) => {
    if (!query) return true;
    const q = query.toLowerCase();
    return item.label.toLowerCase().includes(q)
      || item.token.toLowerCase().includes(q)
      || (item.description || '').toLowerCase().includes(q);
  };

  const renderActionRow = (item, selectedToken) => {
    const selected = item.token === selectedToken;
    const disabled = Boolean(item.disabled);
    const showSafetyPill = Boolean(item.safetyCritical);
    const showTestButton = Boolean(item.testable) && !disabled && !showSafetyPill;
    const inFlight = actionPickerInFlightToken !== null;
    const feedback = actionPickerFeedback && actionPickerFeedback.token === item.token
      ? actionPickerFeedback
      : null;
    const feedbackClass = feedback ? ` ${feedback.kind || ''}` : '';
    const feedbackText = feedback ? feedback.text : '';

    return `<div class="rc-action-row${selected ? ' selected' : ''}${disabled ? ' disabled' : ''}" data-action-token="${window.PAUtils.escapeHtml(item.token)}" data-action-disabled="${disabled ? 'true' : 'false'}" role="option" aria-selected="${selected ? 'true' : 'false'}" aria-disabled="${disabled ? 'true' : 'false'}">
      <button type="button" class="rc-action-select-btn" data-action-select="${window.PAUtils.escapeHtml(item.token)}"${disabled ? ' disabled' : ''}>
        <span class="rc-action-radio" aria-hidden="true">${selected ? '●' : '○'}</span>
        <span class="rc-action-main">
          <span class="rc-action-label">${window.PAUtils.escapeHtml(item.label)}</span>
          <span class="rc-action-desc">${window.PAUtils.escapeHtml(item.description || 'No description available.')}</span>
        </span>
      </button>
      <span class="rc-action-side">
        ${showSafetyPill ? '<span class="rc-action-safety-pill">⚠ Safety critical</span>' : ''}
        ${showTestButton ? `<button type="button" class="rc-action-test-btn" data-action-test="${window.PAUtils.escapeHtml(item.token)}"${inFlight ? ' disabled' : ''}>▶</button>` : ''}
        <span class="rc-action-test-feedback${feedbackClass}" data-action-feedback="${window.PAUtils.escapeHtml(item.token)}">${window.PAUtils.escapeHtml(feedbackText || '')}</span>
      </span>
    </div>`;
  };

  const renderActionPicker = (selectedToken, queryText = '') => {
    const query = String(queryText || '').trim();
    const groups = groupedActionTargets()
      .map(({ name, items }) => ({
        name,
        items: items.filter((item) => actionMatchesQuery(item, query)),
      }))
      .filter(({ items }) => items.length > 0);

    const recentItems = recentActionTokens
      .map((token) => actionTargets.find((item) => item.token === token))
      .filter((item) => Boolean(item) && actionMatchesQuery(item, query));

    const hasMatches = recentItems.length > 0 || groups.length > 0;
    const recentBlock = recentItems.length > 0
      ? `<details class="rc-action-group rc-action-group-recent" open>
          <summary>🕘 Recently used</summary>
          <div class="rc-action-group-rows">${recentItems.map((item) => renderActionRow(item, selectedToken)).join('')}</div>
        </details>`
      : '';

    return `<div class="rc-action-picker" role="listbox" aria-label="Select action" tabindex="0">
      <div class="rc-action-search-row">
        <input class="rc-action-search-input" data-action-search type="search" placeholder="Search actions..." value="${window.PAUtils.escapeHtml(query)}" autocomplete="off">
        <button type="button" class="rc-action-search-clear" data-action-search-clear${query ? '' : ' disabled'}>✕</button>
      </div>
      ${recentBlock}
      ${groups.map(({ name, items }) => {
        const openAttr = query ? ' open' : (isActionGroupOpen(name) ? ' open' : '');
        return `<details class="rc-action-group" data-action-group="${window.PAUtils.escapeHtml(name)}"${openAttr}>
          <summary>${window.PAUtils.escapeHtml(name)}</summary>
          <div class="rc-action-group-rows">${items.map((item) => renderActionRow(item, selectedToken)).join('')}</div>
        </details>`;
      }).join('')}
      ${hasMatches ? '' : `<div class="rc-action-empty">No actions match "${window.PAUtils.escapeHtml(query)}".</div>`}
    </div>`;
  };

  const renderEditor = () => {
    if (!rcEditorContent) return;
    if (!selectedChannel) {
      actionPickerFeedback = null;
      actionPickerInFlightToken = null;
      actionPickerLastChannel = null;
      rcEditorContent.innerHTML = '';
      if (rcEditorApply) rcEditorApply.disabled = true;
      if (rcEditorRevert) rcEditorRevert.disabled = true;
      setEditorDirtyState('clean', 'Select a channel to edit');
      return;
    }

    if (actionPickerLastChannel !== selectedChannel) {
      actionPickerFeedback = null;
      actionPickerInFlightToken = null;
      actionPickerScrollTop = 0;
      actionPickerQuery = '';
      Object.keys(actionPickerGroupOpen).forEach((group) => delete actionPickerGroupOpen[group]);
      actionPickerLastChannel = selectedChannel;
    } else {
      const previousPicker = rcEditorContent.querySelector('.rc-action-picker');
      if (previousPicker) {
        actionPickerScrollTop = previousPicker.scrollTop;
        previousPicker.querySelectorAll('[data-action-group]').forEach((groupNode) => {
          actionPickerGroupOpen[groupNode.dataset.actionGroup] = groupNode.open;
        });
      }
    }

    const { source, channel } = parseChannelKey(selectedChannel);
    const entry = assignmentForChannel(selectedChannel) || { source, channel, payload: '' };
    const displayToken = mapEntryAction(entry);

    rcEditorContent.innerHTML = `
      <h4 class="rc-editor-title">Edit ${window.PAUtils.escapeHtml(channelTitleFromKey(selectedChannel))}</h4>
      <div class="desc mt-4">Source: <strong>${window.PAUtils.escapeHtml(sourceLabel(source))}</strong> · CH <strong>${window.PAUtils.escapeHtml(String(channel))}</strong></div>
      <label class="rc-action-label-head mt-8">Action</label>
      <input data-field="target" type="hidden" value="${window.PAUtils.escapeHtml(displayToken)}">
      ${renderActionPicker(displayToken, actionPickerQuery)}
      <div class="mt-8"><button type="button" class="btn btn-sm" data-action-unmap>Unmap channel</button></div>
      <div data-cond="seq" class="rc-editor-cond ${displayToken === 'seq' ? 'block' : 'hidden'}">
        <label>Marcduino Sequence
          <select data-field="payload">
            ${renderMarcduinoSequenceOptions(entry.payload)}
          </select>
        </label>
      </div>
      <div data-cond="dome_seq" class="rc-editor-cond ${displayToken === 'dome_seq' ? 'block' : 'hidden'}">
        <label>Dome Sequence
          <select data-field="payload">
            ${renderDomeSequenceOptions(entry.payload)}
          </select>
        </label>
      </div>
      <div data-cond="cmd" class="rc-editor-cond ${displayToken === 'cmd' ? 'block' : 'hidden'}">
        <label>Marcduino Command
          <input data-field="payload" type="text" value="${window.PAUtils.escapeHtml(entry.payload || '')}" placeholder=":OP01">
        </label>
      </div>
      <div data-cond="estop" class="rc-editor-cond ${displayToken === 'estop' ? 'block' : 'hidden'}">
        <label><input data-field="estop-confirm" type="checkbox"> I understand this latches estop.</label>
      </div>`;

    const updateConditionalFields = () => {
      const targetEl = rcEditorContent.querySelector('[data-field="target"]');
      const target = targetEl ? targetEl.value : displayToken;
      const seq = rcEditorContent.querySelector('[data-cond="seq"]');
      const domeSeq = rcEditorContent.querySelector('[data-cond="dome_seq"]');
      const cmd = rcEditorContent.querySelector('[data-cond="cmd"]');
      const estop = rcEditorContent.querySelector('[data-cond="estop"]');
      if (seq) seq.className = `rc-editor-cond ${target === 'seq' ? 'block' : 'hidden'}`;
      if (domeSeq) domeSeq.className = `rc-editor-cond ${target === 'dome_seq' ? 'block' : 'hidden'}`;
      if (cmd) cmd.className = `rc-editor-cond ${target === 'cmd' ? 'block' : 'hidden'}`;
      if (estop) estop.className = `rc-editor-cond ${target === 'estop' ? 'block' : 'hidden'}`;
    };

    const refreshActionPickerSelectionUi = () => {
      const targetEl = rcEditorContent.querySelector('[data-field="target"]');
      const selectedToken = targetEl ? targetEl.value : '';
      rcEditorContent.querySelectorAll('[data-action-token]').forEach((row) => {
        const rowToken = row.dataset.actionToken;
        const selected = rowToken === selectedToken;
        row.classList.toggle('selected', selected);
        row.setAttribute('aria-selected', selected ? 'true' : 'false');
        const radio = row.querySelector('.rc-action-radio');
        if (radio) radio.textContent = selected ? '●' : '○';
      });

      const picker = rcEditorContent.querySelector('.rc-action-picker');
      if (picker) picker.scrollTop = actionPickerScrollTop;
    };

    const syncActionTestUi = () => {
      const inFlight = actionPickerInFlightToken !== null;
      rcEditorContent.querySelectorAll('[data-action-test]').forEach((btn) => {
        btn.disabled = inFlight;
      });
      rcEditorContent.querySelectorAll('[data-action-feedback]').forEach((node) => {
        const token = node.dataset.actionFeedback;
        const feedback = token && actionPickerFeedback && actionPickerFeedback.token === token
          ? actionPickerFeedback
          : null;
        node.textContent = feedback ? feedback.text : '';
        node.className = `rc-action-test-feedback${feedback ? ` ${feedback.kind || ''}` : ''}`;
      });
    };

    const collapseExpandedActionGroups = () => {
      rcEditorContent.querySelectorAll('[data-action-group]').forEach((groupNode) => {
        if (groupNode.open) groupNode.open = false;
        actionPickerGroupOpen[groupNode.dataset.actionGroup] = false;
      });
    };

    const setActionToken = (token, keepFocus = false) => {
      const targetEl = rcEditorContent.querySelector('[data-field="target"]');
      if (!targetEl || targetEl.value === token) return;
      targetEl.value = token;
      rememberRecentActionToken(token);
      actionPickerFeedback = null;
      updateConditionalFields();
      refreshActionPickerSelectionUi();
      syncActionTestUi();
      markEditorDirty();
      if (keepFocus) {
        rcEditorContent.querySelector('.rc-action-picker')?.focus();
      }
      // Fetch learned sequences if dome_seq is selected
      if (token === 'dome_seq') {
        const currentPayload = rcEditorContent.querySelector('[data-cond="dome_seq"] select')?.value || '';
        updateDomeSequenceOptions(currentPayload);
      }
    };

    const runActionTest = async (token) => {
      if (actionPickerInFlightToken) return;
      const channelAtStart = selectedChannel;
      actionPickerInFlightToken = token;
      actionPickerFeedback = { token, kind: 'info', text: 'Testing...' };
      syncActionTestUi();
      try {
        await window.PAApi.postForm('/api/actions/test', { token }, { timeoutMs: 5000 });
        if (selectedChannel !== channelAtStart) return;
        actionPickerFeedback = { token, kind: 'success', text: 'Dispatched' };
      } catch (error) {
        if (selectedChannel !== channelAtStart) return;
        actionPickerFeedback = { token, kind: 'error', text: window.PAApi.messageFor(error) };
      } finally {
        if (actionPickerInFlightToken === token) actionPickerInFlightToken = null;
        if (selectedChannel === channelAtStart) syncActionTestUi();
      }
    };

    rcEditorContent.querySelectorAll('[data-field]').forEach((field) => {
      field.addEventListener('change', () => {
        markEditorDirty();
        if (field.dataset.field === 'target') updateConditionalFields();
      });
      field.addEventListener('input', () => {
        markEditorDirty();
      });
    });

    const picker = rcEditorContent.querySelector('.rc-action-picker');
    if (picker) {
      picker.addEventListener('scroll', () => {
        actionPickerScrollTop = picker.scrollTop;
      });

      const searchInput = picker.querySelector('[data-action-search]');
      if (searchInput) {
        searchInput.addEventListener('input', () => {
          const nextQuery = searchInput.value || '';
          if (nextQuery === actionPickerQuery) return;
          actionPickerQuery = nextQuery;
          actionPickerScrollTop = 0;
          renderEditor();
          const nextInput = rcEditorContent.querySelector('[data-action-search]');
          if (nextInput) {
            nextInput.focus();
            nextInput.setSelectionRange(actionPickerQuery.length, actionPickerQuery.length);
          }
        });
      }

      const clearSearchBtn = picker.querySelector('[data-action-search-clear]');
      if (clearSearchBtn) {
        clearSearchBtn.addEventListener('click', () => {
          if (!actionPickerQuery) return;
          actionPickerQuery = '';
          actionPickerScrollTop = 0;
          renderEditor();
          rcEditorContent.querySelector('[data-action-search]')?.focus();
        });
      }

      picker.querySelectorAll('[data-action-group]').forEach((groupNode) => {
        groupNode.addEventListener('toggle', () => {
          actionPickerGroupOpen[groupNode.dataset.actionGroup] = groupNode.open;
        });
      });

      picker.querySelectorAll('[data-action-select]').forEach((btn) => {
        btn.addEventListener('click', () => {
          const token = btn.dataset.actionSelect;
          if (!token) return;
          setActionToken(token);
        });
      });

      const unmapBtn = rcEditorContent.querySelector('[data-action-unmap]');
      if (unmapBtn) {
        unmapBtn.addEventListener('click', () => setActionToken(''));
      }

      picker.querySelectorAll('[data-action-test]').forEach((btn) => {
        btn.addEventListener('click', (event) => {
          event.stopPropagation();
          const token = btn.dataset.actionTest;
          if (!token) return;
          runActionTest(token);
        });
      });

      picker.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
          event.preventDefault();
          collapseExpandedActionGroups();
          return;
        }

        if (event.target instanceof Element && event.target.closest('.rc-action-test-btn')) return;
        if (event.target instanceof Element && event.target.closest('.rc-action-search-input')) return;
        if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp' && event.key !== 'Enter') return;
        event.preventDefault();

        const selectable = actionTargets
          .filter((item) => !item.disabled && actionMatchesQuery(item, actionPickerQuery))
          .map((item) => item.token);
        if (!selectable.length) return;

        const targetEl = rcEditorContent.querySelector('[data-field="target"]');
        const currentToken = targetEl ? targetEl.value : '';
        let index = selectable.indexOf(currentToken);
        if (index < 0) index = 0;

        if (event.key === 'ArrowDown') {
          index = Math.min(index + 1, selectable.length - 1);
          setActionToken(selectable[index], true);
          return;
        }
        if (event.key === 'ArrowUp') {
          index = Math.max(index - 1, 0);
          setActionToken(selectable[index], true);
          return;
        }
        if (event.key === 'Enter') {
          setActionToken(selectable[index], true);
        }
      });
    }

    updateConditionalFields();
    refreshActionPickerSelectionUi();
    syncActionTestUi();
  };

  const updateSummaryMiniBar = () => {
    if (!rcSummaryBody || !rcSnapshot) return;
    rcSummaryBody.querySelectorAll('tr[data-chkey]').forEach((row) => {
      const channelKey = row.dataset.chkey;
      const entry = assignmentForChannel(channelKey);
      if (!entry) return;
      const telemetry = getChannelTelemetry(channelKey);
      const token = mapEntryAction(entry);
      const cell = row.children[2];
      if (!cell) return;
      cell.innerHTML = isAnalogAction(token) ? miniBarHtml(telemetry?.mapped || 0) : triggerStateHtml(token, channelKey, telemetry);
    });
  };

  const saveRcMode = async () => {
    if (!rcInputModeHidden) return;
    const mode = rcInputModeHidden.value;
    setModeFeedback('Saving...');
    try {
      const result = await window.PAApi.postForm('/api/config', { rcInputMode: mode }, { timeoutMs: 5000 });
      const savedMode = getRcModeFromConfig(result.data);
      switchRcMode(savedMode);
      await loadMappings();
      setModeFeedback(`${modeLabel(savedMode)} saved at ${new Date().toLocaleTimeString()}`, 'success');
    } catch (error) {
      setModeFeedback(`❌ ${window.PAApi.messageFor(error)}`, 'error');
    }
  };

  debouncedSaveRcMode = window.PAUtils.debounce(saveRcMode, 250);

  const switchRcMode = (mode) => {
    if (rcInputModeHidden) rcInputModeHidden.value = mode;
    rcModeCards.forEach((card) => {
      const selected = card.dataset.mode === mode;
      card.classList.toggle('selected', selected);
      card.setAttribute('aria-pressed', selected ? 'true' : 'false');
    });
    updateRecvSel(mode);
    selectedChannel = null;
    document.querySelectorAll('.rc-channel-item').forEach((el) => el.classList.remove('active'));
    renderSummaryTable();
    renderChannelList();
    renderLivePreview();
    renderEditor();
  };

  rcModeCards.forEach((card) => {
    card.addEventListener('click', () => {
      const mode = card.dataset.mode;
      if (mode && rcInputModeHidden) {
        rcInputModeHidden.value = mode;
        switchRcMode(mode);
        debouncedSaveRcMode();
      }
    });

    card.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        const mode = card.dataset.mode;
        if (mode && rcInputModeHidden) {
          rcInputModeHidden.value = mode;
          switchRcMode(mode);
          debouncedSaveRcMode();
        }
      }
    });
  });

  const loadRcMode = async () => {
    try {
      const result = await window.PAApi.get('/api/config', { timeoutMs: 5000 });
      const data = result.data;
      const mode = getRcModeFromConfig(data);
      if (rcInputModeHidden) rcInputModeHidden.value = mode;
      rcModeCards.forEach((card) => {
        const selected = card.dataset.mode === mode;
        card.classList.toggle('selected', selected);
        card.setAttribute('aria-pressed', selected ? 'true' : 'false');
      });
      setSingleSbusRecvSelect(getSingleSbusRecvCh2(data));
      confirmedSbusRecvValue = sbusRecvSel?.value ?? null;
      updateRecvSel(mode);
      setModeFeedback(`Receiver type: ${modeLabel(mode)}`, 'success');
      setRcInputsEnabled(rcComponentsEnabled(data));
    } catch (error) {
      const fallbackMode = rcInputModeHidden?.value || 'standard_pwm';
      switchRcMode(fallbackMode);
      setModeFeedback(`Receiver config unavailable: ${window.PAApi.messageFor(error)}. Showing ${modeLabel(fallbackMode)} draft controls.`, 'warning');
    }
  };

  const loadMappings = async () => {
    try {
      const result = await window.PAApi.get('/api/rc/map', { timeoutMs: 5000 });
      const payload = result.data || {};
      const mode = typeof payload.mode === 'string' ? payload.mode : getEditorMode();
      channelMap = modeMapFromArray(payload.map);
      triggerPulseState = {};
      if (rcInputModeHidden?.value !== mode) switchRcMode(mode);
      if (selectedChannel) {
        const { source } = parseChannelKey(selectedChannel);
        const allowedSources = SOURCE_OPTIONS[mode] || SOURCE_OPTIONS.standard_pwm;
        if (!allowedSources.includes(source)) selectedChannel = null;
      }
      renderSummaryTable();
      renderChannelList();
      renderLivePreview();
      renderEditor();
    } catch (error) {
      channelMap = {};
      triggerPulseState = {};
      renderSummaryTable();
      renderChannelList();
      renderLivePreview();
      renderEditor();
      setEditorFeedback(`Failed to load RC map: ${window.PAApi.messageFor(error)}`, 'error');
    }
  };

  const selectChannel = (channelKey) => {
    selectedChannel = channelKey;
    document.querySelectorAll('.rc-channel-item').forEach((el) => {
      el.classList.toggle('active', el.dataset.chkey === channelKey);
    });
    document.querySelectorAll('.rc-summary-table tr[data-chkey]').forEach((row) => {
      row.classList.toggle('active-channel', row.dataset.chkey === channelKey);
    });
    markEditorClean();
    renderLivePreview();
    renderEditor();
  };

  // ── RC Channel detect mode ────────────────────────────────────────────────
  //
  // Scans raw channel arrays (sbus1[], sbus2[], pwm[]) in the SSE rc payload.
  // Pure logic is tested in test/test_rc_learn/index.html — keep in sync with
  // threshold constants and computeDetectHit() if changed here.

  // Pure: given a raw snapshot baseline and a current snapshot, return the
  // source+channel with the greatest deviation from baseline, or null if none
  // exceeds the configured threshold.
  //
  // Returns: { source: "sbus1"|"sbus2"|"pwm", channel: 1-based, raw: value,
  //            baseline: baselineValue, delta: absDeviation }  or null.
  const computeDetectHit = (baseline, curr) => {
    if (!baseline || !curr) return null;
    const raw = curr.raw;
    if (!raw || typeof raw !== 'object') return null;
    const baseRaw = baseline.raw;
    if (!baseRaw || typeof baseRaw !== 'object') return null;

    let best = null;

    ['sbus1', 'sbus2'].forEach((src) => {
      const currArr = Array.isArray(raw[src]) ? raw[src] : [];
      const baseArr = Array.isArray(baseRaw[src]) ? baseRaw[src] : [];
      currArr.forEach((val, idx) => {
        const base = baseArr[idx] ?? val;
        const delta = Math.abs(val - base);
        if (delta >= LEARN_SBUS_THRESHOLD && (!best || delta > best.delta)) {
          best = { source: src, channel: idx + 1, raw: val, baseline: base, delta };
        }
      });
    });

    const currPwm = Array.isArray(raw.pwm) ? raw.pwm : [];
    const basePwm = Array.isArray(baseRaw.pwm) ? baseRaw.pwm : [];
    currPwm.forEach((val, idx) => {
      if (val === 0) return;
      const base = basePwm[idx] ?? val;
      if (base === 0) return;
      const delta = Math.abs(val - base);
      if (delta >= LEARN_PWM_THRESHOLD && (!best || delta > best.delta)) {
        best = { source: 'pwm', channel: idx + 1, raw: val, baseline: base, delta };
      }
    });

    return best;
  };

  const mappedActionsForDetectHit = (hit) => {
    if (!hit) return [];
    return asMapArray()
      .filter((entry) => entry.source === hit.source && Number(entry.channel) === hit.channel)
      .map((entry) => actionLabelFromToken(mapEntryAction(entry)));
  };

  const detectHitLabel = (hit) => {
    if (!hit) return '';
    const srcLabel = { sbus1: 'SBUS1', sbus2: 'SBUS2', pwm: 'PWM' }[hit.source] || hit.source.toUpperCase();
    return `${srcLabel} CH ${hit.channel}`;
  };

  const applyLearnHighlight = () => {
    if (!rcChannelItems) return;
    const channelKey = learnActive && learnHit ? channelKeyOf(learnHit.source, learnHit.channel) : null;
    rcChannelItems.querySelectorAll('.rc-channel-item').forEach((el) => {
      el.classList.toggle('learn-hot', channelKey !== null && el.dataset.chkey === channelKey);
    });
  };

  const updateLearnBanner = () => {
    if (!rcLearnStatus) return;
    if (!learnHit) {
      const hasRaw = rcSnapshot?.raw && Object.keys(rcSnapshot.raw).length > 0;
      rcLearnStatus.textContent = hasRaw
        ? 'Listening… press a switch or button on your transmitter'
        : 'No RC signal — connect your receiver first';
      return;
    }
    const label = detectHitLabel(learnHit);
    const mappedActions = mappedActionsForDetectHit(learnHit);
    if (mappedActions.length > 0) {
      rcLearnStatus.textContent = `Detected: ${label} → already assigned to ${mappedActions.join(', ')}`;
    } else {
      rcLearnStatus.textContent = `Detected: ${label} — unassigned. Select this channel to configure it.`;
    }
  };

  const rcEnabledFromStatus = (payload) => {
    const anyDirect = Boolean(
      payload?.rcCh1 || payload?.rcCh2 || payload?.rcCh3 ||
      payload?.rcCh4 || payload?.rcCh5 || payload?.rcCh6
    );
    if (anyDirect) return true;

    const components = payload?.components || {};
    return Boolean(
      components.rcCh1?.enabled || components.rcCh2?.enabled || components.rcCh3?.enabled ||
      components.rcCh4?.enabled || components.rcCh5?.enabled || components.rcCh6?.enabled
    );
  };

  const enterLearnMode = () => {
    if (!rcInputsEnabled) {
      setEditorFeedback('Detect mode unavailable: enable an RC channel in Setup.', 'warning');
      return;
    }
    learnActive = true;
    learnBaseline = rcSnapshot;
    learnHit = null;
    learnStartMs = Date.now();
    if (rcLearnBtn) rcLearnBtn.textContent = '🔍 Detecting…';
    if (rcLearnBanner) rcLearnBanner.hidden = false;
    updateLearnBanner();
    applyLearnHighlight();
  };

  const exitLearnMode = () => {
    learnActive = false;
    learnBaseline = null;
    learnHit = null;
    if (rcLearnBtn) rcLearnBtn.textContent = '🔍 Detect channel';
    if (rcLearnBanner) rcLearnBanner.hidden = true;
    applyLearnHighlight();
  };

  const processLearnTick = (currSnapshot) => {
    if (Date.now() - learnStartMs > LEARN_TIMEOUT_MS) {
      exitLearnMode();
      return;
    }
    const nextHit = computeDetectHit(learnBaseline, currSnapshot);
    const changed = JSON.stringify(nextHit) !== JSON.stringify(learnHit);
    learnHit = nextHit;
    if (changed) {
      updateLearnBanner();
      applyLearnHighlight();
    }
  };

  const renderRcDiagnostics = (payload) => {
    rcSnapshot = payload;
    renderSourceHealth();
    renderSummaryTable();
    renderChannelList();
    if (selectedChannel) {
      renderLivePreview();
      renderEditor();
    }
  };

  const loadRcDiagnostics = async () => {
    try {
      const result = await window.PAApi.get('/api/rc', { timeoutMs: 5000 });
      renderRcDiagnostics(result.data);
    } catch (error) {
      setEditorFeedback(`Failed to load RC diagnostics: ${window.PAApi.messageFor(error)}`, 'error');
    }
  };

  const subscribeRcEvents = () => {
    if (!window.PAStatusStream?.isSupported()) return false;

    window.PAStatusStream.subscribe((eventType, payload) => {
      try {
        if (eventType === 'status') {
          const data = typeof payload === 'string' ? JSON.parse(payload) : payload;
          setRcInputsEnabled(rcEnabledFromStatus(data));
          return;
        }
        if (eventType !== 'rc') return;
        const data = typeof payload === 'string' ? JSON.parse(payload) : payload;

        if (learnActive) processLearnTick(data);

        rcSnapshot = data;
        renderSourceHealth();
        updateSummaryMiniBar();
        updateChannelListRaw();
        if (selectedChannel) renderLivePreview();
      } catch (_error) {
        setEditorFeedback('Received malformed RC event payload.', 'error');
      }
    });

    return true;
  };

  const saveMapping = async () => {
    if (!selectedChannel || !rcEditorContent) return;

    const { source, channel } = parseChannelKey(selectedChannel);
    const mode = getEditorMode();
    const target = rcEditorContent.querySelector('[data-field="target"]')?.value || '';
    const payloadField = rcEditorContent.querySelector(`.rc-editor-cond[data-cond="${target}"] [data-field="payload"]`);
    const payload = payloadField ? payloadField.value : '';

    if (target === 'cmd' && payload && !/^[:$#]/.test(payload)) {
      setEditorFeedback('Marcduino command must start with :, $, or #', 'error');
      return;
    }
    if (target === 'estop') {
      const confirmCheckbox = rcEditorContent.querySelector('[data-field="estop-confirm"]');
      if (!confirmCheckbox || !confirmCheckbox.checked) {
        setEditorFeedback('E-Stop action requires confirmation checkbox', 'error');
        return;
      }
    }

    const allowedSources = SOURCE_OPTIONS[mode] || SOURCE_OPTIONS.standard_pwm;
    if (!allowedSources.includes(source)) {
      setEditorFeedback(`Channel source ${sourceLabel(source)} is not valid in ${modeLabel(mode)} mode.`, 'error');
      return;
    }

    const nextMap = { ...channelMap };
    if (!target) {
      delete nextMap[selectedChannel];
    } else {
      nextMap[selectedChannel] = normalizeMapEntry({ source, channel, action: target, payload });
    }

    setEditorDirtyState('saving', 'Saving changes…');
    setEditorFeedback('Saving...');

    try {
      const result = await window.PAApi.postForm('/api/rc/map', { plain: JSON.stringify({ map: Object.values(nextMap) }) }, { timeoutMs: 5000 });
      const serverMap = Array.isArray(result.data?.map) ? result.data.map : Object.values(nextMap);
      channelMap = modeMapFromArray(serverMap);
      const savedAt = new Date().toLocaleTimeString();
      setEditorFeedback(`✓ Saved at ${savedAt}`, 'success');
      markEditorClean(savedAt);
      renderSummaryTable();
      renderChannelList();
      renderLivePreview();
      renderEditor();
    } catch (error) {
      setEditorDirtyState('error', 'Save failed — unsaved changes');
      if (rcEditorApply) rcEditorApply.disabled = false;
      if (rcEditorRevert) rcEditorRevert.disabled = false;
      setEditorFeedback(`Failed to save: ${window.PAApi.messageFor(error)}`, 'error');
    }
  };

  const revertMapping = async () => {
    await loadMappings();
    setEditorFeedback('Reverted to last saved mapping.');
    markEditorClean();
  };

  const resetToDefaults = async () => {
    if (!confirm('Are you sure you want to clear all RC mappings?')) return;
    setEditorFeedback('Clearing mappings...');
    try {
      await window.PAApi.postForm('/api/rc/map', { plain: JSON.stringify({ map: [] }) }, { timeoutMs: 5000 });
      channelMap = {};
      const savedAt = new Date().toLocaleTimeString();
      setEditorFeedback('✓ Cleared all mappings', 'success');
      markEditorClean(savedAt);
      renderSummaryTable();
      renderChannelList();
      renderLivePreview();
      renderEditor();
    } catch (error) {
      setEditorFeedback(`Failed to clear mappings: ${window.PAApi.messageFor(error)}`, 'error');
    }
  };

  if (rcEditorApply) {
    rcEditorApply.addEventListener("click", saveMapping);
  }

  if (rcEditorRevert) {
    rcEditorRevert.addEventListener("click", revertMapping);
  }

  if (rcResetDefaults) {
    rcResetDefaults.addEventListener("click", resetToDefaults);
  }

  if (rcLearnBtn) {
    rcLearnBtn.addEventListener("click", () => {
      if (learnActive) exitLearnMode();
      else enterLearnMode();
    });
  }

  if (rcLearnStop) {
    rcLearnStop.addEventListener("click", exitLearnMode);
  }

  if (sbusRecvSel) {
    sbusRecvSel.addEventListener("change", async () => {
      const recvCh2 = sbusRecvSel.value === "true";
      setSbusRecvFeedback("Saving...");
      try {
        await window.PAApi.postJson("/api/config", { rc: { sbus: { recvCh2 } } }, { timeoutMs: 5000 });
        setSbusRecvFeedback(`\u2713 Saved at ${new Date().toLocaleTimeString()}`, "success", 2000);
        confirmedSbusRecvValue = sbusRecvSel.value;
      } catch (error) {
        sbusRecvSel.value = confirmedSbusRecvValue;
        setSbusRecvFeedback(`\u274c ${window.PAApi.messageFor(error)}`, "error", 2000);
      }
    });
  }

  const setRcDebugMode = async (enabled) => {
    try {
      const result = await window.PAApi.postJson("/api/rc/debug", { enabled }, { timeoutMs: 3000 });
      console.log(`[RC] Debug mode ${enabled ? 'enabled' : 'disabled'}:`, result.data);
    } catch (error) {
      console.warn("[RC] Failed to toggle debug mode:", window.PAApi.messageFor(error));
    }
  };

  setRcDebugMode(true).catch(err => console.error("[RC] Debug mode init failed:", err));

  window.addEventListener("beforeunload", () => {
    const body = new Blob([JSON.stringify({ enabled: false })], { type: "application/json" });
    navigator.sendBeacon("/api/rc/debug", body);
  });

  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState !== "hidden") {
      loadRcDiagnostics();
    }
  });

  setEditorDirtyState("clean", "Saved");
  setEditorSavedTimestamp(null);

  // -------------------------------------------------------------------------
  // Boot — load RC configuration and diagnostics
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API loads as sections so the bootstrap
  // can show recovery state if any fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const loadRcModeAndMappings = async () => {
    await loadRcMode();
    await loadMappings();
  };

  const loadRcDiagnosticsWithFallback = async () => {
    await loadRcDiagnostics();
  };

  const loadActionTargetsWithFallback = async () => {
    await loadActionTargets();
    if (selectedChannel) renderEditor();
  };

  const SECTIONS = [
    ["rc-mode-mapping", loadRcModeAndMappings, "RC receiver type and mapping"],
    ["rc-diagnostics", loadRcDiagnosticsWithFallback, "RC channel diagnostics"],
    ["rc-action-targets", loadActionTargetsWithFallback, "RC action registry"],
  ];

  const startPageLoad = () => {
    loadRecentActionTokens();
    switchRcMode(rcInputModeHidden?.value || "standard_pwm");

    if (!window.PABootstrap) {
      loadRcMode().finally(() => {
        loadMappings();
      });
      loadRcDiagnostics();
      loadActionTargets().then(() => { if (selectedChannel) renderEditor(); });
      return;
    }

    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/rc.js": "RC control",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  startPageLoad();

  const hasRcStream = subscribeRcEvents();
  if (!hasRcStream) {
    const refreshFromFallback = () => {
      loadRcDiagnostics();
    };

    window.setInterval(() => {
      if (document.visibilityState === "hidden") return;
      refreshFromFallback();
    }, 1000);
  }
})();

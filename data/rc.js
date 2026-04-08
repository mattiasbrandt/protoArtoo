(() => {
  let selectedSlot = null;
  let rcSnapshot = null;
  let configCache = null;
  let mappingDraft = {};

  // ── Learning mode state ───────────────────────────────────────────────────
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

  const rcSlotItems = document.getElementById("rc-slot-items");
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

  const SUPPORTED_SLOTS = [
    "driveSpeed", "driveSteer", "driveLimit", "domeSpeed",
    "arm1", "arm2", "aux1", "aux2", "aux3", "sound",
    "opMode", "free0", "free1", "free2", "free3"
  ];

  const isSlotSupported = (slotKey) => {
    return SUPPORTED_SLOTS.includes(slotKey);
  };

  const BACKBONE_SLOTS = ["driveSpeed", "driveSteer", "domeSpeed", "driveLimit"];

  // Hardcoded fallback used until GET /api/actions resolves.
  // Matches robotActionIdToString() NVS token keys in rc_mapping.h.
  const HARDCODED_ACTION_TARGETS = [
    { token: 'none', label: 'Disabled', group: 'Off', description: 'Slot is not bound to any action.', disabled: false, testable: false, safetyCritical: false },
    { token: 'drive_speed', label: 'Speed', group: 'Movement', description: 'Forward/reverse drive speed (analog axis)', disabled: false, testable: false, safetyCritical: false },
    { token: 'drive_steer', label: 'Steer', group: 'Movement', description: 'Left/right steering (analog axis)', disabled: false, testable: false, safetyCritical: false },
    { token: 'dome_speed', label: 'Dome Speed', group: 'Movement', description: 'Dome rotation speed (analog axis)', disabled: false, testable: false, safetyCritical: false },
    { token: 'speed_limit', label: 'Speed Limit', group: 'Movement', description: 'Maximum drive speed as a fraction of full range', disabled: false, testable: false, safetyCritical: false },
    { token: 'op_mode', label: 'Set Mode', group: 'Mode', description: 'Switch between stationary and driving mode', disabled: false, testable: true, safetyCritical: false },
    { token: 'arm1_toggle', label: 'ARM1 Toggle', group: 'Arms', description: 'Toggle arm 1 servo between open and closed', disabled: false, testable: true, safetyCritical: false },
    { token: 'arm2_toggle', label: 'ARM2 Toggle', group: 'Arms', description: 'Toggle arm 2 servo between open and closed', disabled: false, testable: true, safetyCritical: false },
    { token: 'aux1_toggle', label: 'AUX1 Toggle', group: 'Arms', description: 'Toggle aux 1 servo between open and closed', disabled: false, testable: true, safetyCritical: false },
    { token: 'aux2_toggle', label: 'AUX2 Toggle', group: 'Arms', description: 'Toggle aux 2 servo between open and closed', disabled: false, testable: true, safetyCritical: false },
    { token: 'aux3_toggle', label: 'AUX3 Toggle', group: 'Arms', description: 'Toggle aux 3 servo between open and closed', disabled: false, testable: true, safetyCritical: false },
    { token: 'seq', label: 'Marcduino Sequence', group: 'Sequences', description: 'Trigger a raw numbered body sequence payload (typically SE30-SE36)', disabled: false, testable: false, safetyCritical: false },
    { token: 'dome_seq', label: 'Dome Sequence (Unavailable)', group: 'Sequences', description: 'Trigger a dome-side panel/light sequence by number', disabled: true, testable: false, safetyCritical: false },
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
  ];

  // Live action targets — replaced on load from GET /api/actions.
  // Falls back to HARDCODED_ACTION_TARGETS if the request fails.
  let actionTargets = HARDCODED_ACTION_TARGETS;
  let actionPickerScrollTop = 0;
  let actionPickerFeedback = null;
  let actionPickerInFlightToken = null;
  let actionPickerLastSlot = null;
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
  const ACTION_GROUP_ORDER = ['Off', 'Movement', 'Mode', 'Arms', 'Sound', 'Sequences', 'Command', 'Safety', 'System', 'Aux', 'Other'];
  const DEFAULT_COLLAPSED_GROUPS = new Set(['Sound', 'Sequences']);
  const NON_TESTABLE_TOKENS = new Set(['drive_speed', 'drive_steer', 'dome_speed', 'speed_limit', 'estop']);

  // dome_seq is blocked at save time (api_config.cpp); show as unavailable.
  const UNAVAILABLE_TOKENS = new Set(['dome_seq']);

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
      recentActionTokens = [];
    }
  };

  const saveRecentActionTokens = () => {
    try {
      window.localStorage.setItem(ACTION_RECENTS_KEY, JSON.stringify(recentActionTokens));
    } catch (_error) {
      // Ignore localStorage persistence failures.
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
    if (!token || token === 'none') return;
    recentActionTokens = [token, ...recentActionTokens.filter((entry) => entry !== token)]
      .slice(0, ACTION_RECENTS_LIMIT);
    saveRecentActionTokens();
  };

  const buildActionTargetsFromApi = (entries) => {
    const targets = [HARDCODED_ACTION_TARGETS[0]];
    entries.forEach((entry) => {
      const token = typeof entry.token === 'string' ? entry.token : '';
      if (!token || token === 'none') return;
      const unavail = UNAVAILABLE_TOKENS.has(token);
      const safetyCritical = Boolean(entry.safety_critical);
      const testable = typeof entry.testable === 'boolean'
        ? entry.testable
        : !NON_TESTABLE_TOKENS.has(token);
      const label = entry.display_name || entry.name || token;
      targets.push({
        token,
        label: unavail ? `${label} (Unavailable)` : label,
        group: actionGroup(entry),
        description: entry.description || '',
        disabled: unavail,
        testable: testable && !unavail && !safetyCritical,
        safetyCritical,
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
      console.warn('[RC] Failed to load action registry, using built-in list');
      syncRecentActionTokensWithTargets();
    }
  };

  const MARCDUINO_SEQUENCES = [30, 31, 32, 33, 34, 35, 36];

  const SOURCE_OPTIONS = {
    standard_pwm: ["none", "pwm"],
    single_sbus: ["none", "sbus1"],
    dual_sbus: ["none", "sbus1", "sbus2"],
  };

  const DEFAULT_BINDING = {
    source: "none",
    channel: 0,
    min: 1000,
    center: 1500,
    max: 2000,
    deadband: 0,
    reverse: false,
    target: "none",
    payload: ""
  };

  const RC_ACTIONS = {
    standard_pwm: [
      { key: "driveSpeed", field: "rcPwmDriveSpeed", label: "Drive speed", type: "backbone" },
      { key: "driveSteer", field: "rcPwmDriveSteer", label: "Drive steer", type: "backbone" },
      { key: "driveLimit", field: "rcPwmDriveLimit", label: "Speed limit", type: "backbone" },
      { key: "domeSpeed", field: "rcPwmDomeSpeed", label: "Dome speed", type: "backbone" },
      { key: "arm1", field: "rcArm1", label: "ARM1 trigger", type: "trigger" },
      { key: "arm2", field: "rcArm2", label: "ARM2 trigger", type: "trigger" },
      { key: "aux1", field: "rcAux1", label: "AUX1 trigger", type: "trigger" },
      { key: "aux2", field: "rcAux2", label: "AUX2 trigger", type: "trigger" },
      { key: "aux3", field: "rcAux3", label: "AUX3 trigger", type: "trigger" },
      { key: "sound", field: "rcSound", label: "Sound trigger", type: "trigger" },
      { key: "opMode", field: "rcOpMode", label: "Op-mode switch", type: "trigger" },
      { key: "free0", field: "rcFree0", label: "Free slot 0", type: "trigger" },
      { key: "free1", field: "rcFree1", label: "Free slot 1", type: "trigger" },
      { key: "free2", field: "rcFree2", label: "Free slot 2", type: "trigger" },
      { key: "free3", field: "rcFree3", label: "Free slot 3", type: "trigger" },
    ],
    single_sbus: [
      { key: "driveSpeed", field: "rcSbusDriveSpeed", label: "Drive speed", type: "backbone" },
      { key: "driveSteer", field: "rcSbusDriveSteer", label: "Drive steer", type: "backbone" },
      { key: "driveLimit", field: "rcSbusDriveLimit", label: "Speed limit", type: "backbone" },
      { key: "domeSpeed", field: "rcSbusDomeSpeed", label: "Dome speed", type: "backbone" },
      { key: "arm1", field: "rcArm1", label: "ARM1 trigger", type: "trigger" },
      { key: "arm2", field: "rcArm2", label: "ARM2 trigger", type: "trigger" },
      { key: "aux1", field: "rcAux1", label: "AUX1 trigger", type: "trigger" },
      { key: "aux2", field: "rcAux2", label: "AUX2 trigger", type: "trigger" },
      { key: "aux3", field: "rcAux3", label: "AUX3 trigger", type: "trigger" },
      { key: "sound", field: "rcSound", label: "Sound trigger", type: "trigger" },
      { key: "opMode", field: "rcOpMode", label: "Op-mode switch", type: "trigger" },
      { key: "free0", field: "rcFree0", label: "Free slot 0", type: "trigger" },
      { key: "free1", field: "rcFree1", label: "Free slot 1", type: "trigger" },
      { key: "free2", field: "rcFree2", label: "Free slot 2", type: "trigger" },
      { key: "free3", field: "rcFree3", label: "Free slot 3", type: "trigger" },
    ],
    dual_sbus: [
      { key: "driveSpeed", field: "rcSbusDriveSpeed", label: "Drive speed", type: "backbone" },
      { key: "driveSteer", field: "rcSbusDriveSteer", label: "Drive steer", type: "backbone" },
      { key: "driveLimit", field: "rcSbusDriveLimit", label: "Speed limit", type: "backbone" },
      { key: "domeSpeed", field: "rcSbusDomeSpeed", label: "Dome speed", type: "backbone" },
      { key: "arm1", field: "rcArm1", label: "ARM1 trigger", type: "trigger" },
      { key: "arm2", field: "rcArm2", label: "ARM2 trigger", type: "trigger" },
      { key: "aux1", field: "rcAux1", label: "AUX1 trigger", type: "trigger" },
      { key: "aux2", field: "rcAux2", label: "AUX2 trigger", type: "trigger" },
      { key: "aux3", field: "rcAux3", label: "AUX3 trigger", type: "trigger" },
      { key: "sound", field: "rcSound", label: "Sound trigger", type: "trigger" },
      { key: "opMode", field: "rcOpMode", label: "Op-mode switch", type: "trigger" },
      { key: "free0", field: "rcFree0", label: "Free slot 0", type: "trigger" },
      { key: "free1", field: "rcFree1", label: "Free slot 1", type: "trigger" },
      { key: "free2", field: "rcFree2", label: "Free slot 2", type: "trigger" },
      { key: "free3", field: "rcFree3", label: "Free slot 3", type: "trigger" },
    ],
  };

  const RC_BINDING_PATHS = {
    rcPwmDriveSpeed: ["rc", "pwm", "driveSpeed"],
    rcPwmDriveSteer: ["rc", "pwm", "driveSteer"],
    rcPwmDriveLimit: ["rc", "pwm", "driveLimit"],
    rcPwmDomeSpeed: ["rc", "pwm", "domeSpeed"],
    rcPwmArm1: ["rc", "pwm", "arm1"],
    rcPwmArm2: ["rc", "pwm", "arm2"],
    rcPwmSound: ["rc", "pwm", "sound"],
    rcSbusDriveSpeed: ["rc", "sbus", "driveSpeed"],
    rcSbusDriveSteer: ["rc", "sbus", "driveSteer"],
    rcSbusDriveLimit: ["rc", "sbus", "driveLimit"],
    rcSbusDomeSpeed: ["rc", "sbus", "domeSpeed"],
    rcSbusArm1: ["rc", "sbus", "arm1"],
    rcSbusArm2: ["rc", "sbus", "arm2"],
    rcSbusSound: ["rc", "sbus", "sound"],
    rcArm1: ["rc", "triggers", "arm1"],
    rcArm2: ["rc", "triggers", "arm2"],
    rcAux1: ["rc", "triggers", "aux1"],
    rcAux2: ["rc", "triggers", "aux2"],
    rcAux3: ["rc", "triggers", "aux3"],
    rcSound: ["rc", "triggers", "sound"],
    rcOpMode: ["rc", "triggers", "opMode"],
    rcFree0: ["rc", "triggers", "free0"],
    rcFree1: ["rc", "triggers", "free1"],
    rcFree2: ["rc", "triggers", "free2"],
    rcFree3: ["rc", "triggers", "free3"],
  };

  const readPath = (obj, path) => {
    let cur = obj;
    for (let i = 0; i < path.length; i += 1) {
      if (cur == null || typeof cur !== "object") return undefined;
      cur = cur[path[i]];
    }
    return cur;
  };

  const getConfigBindingString = (cfg, field) => {
    const path = RC_BINDING_PATHS[field];
    if (!path) return "";
    const value = readPath(cfg, path);
    return typeof value === "string" ? value : "";
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
  const escapeHtml = (value) => String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");

  const parseBindingString = (value) => {
    if (typeof value !== "string" || value.trim() === "") {
      return { ...DEFAULT_BINDING };
    }

    const parts = value.split(":");
    if (parts.length >= 9) {
      const source = parts[0] || "none";
      const channel = parts[1] || "0";
      const target = parts[2] || "none";
      const payload = parts.slice(3, parts.length - 5).join(":");
      const min = parts[parts.length - 5] || "1000";
      const center = parts[parts.length - 4] || "1500";
      const max = parts[parts.length - 3] || "2000";
      const deadband = parts[parts.length - 2] || "0";
      const reverse = parts[parts.length - 1] || "0";

      const normalizedTarget = target === "marcduino" ? "cmd" : target;

      return {
        source,
        channel: Number.parseInt(channel, 10) || 0,
        target: normalizedTarget,
        payload,
        min: Number.parseInt(min, 10) || 1000,
        center: Number.parseInt(center, 10) || 1500,
        max: Number.parseInt(max, 10) || 2000,
        deadband: Number.parseInt(deadband, 10) || 0,
        reverse: reverse === "1" || reverse === "true",
      };
    } else {
      const [source = "none", channel = "0", min = "1000", center = "1500", max = "2000",
        deadband = "0", reverse = "0"] = value.split(":");

      return {
        source,
        channel: Number.parseInt(channel, 10) || 0,
        target: "none",
        payload: "",
        min: Number.parseInt(min, 10) || 1000,
        center: Number.parseInt(center, 10) || 1500,
        max: Number.parseInt(max, 10) || 2000,
        deadband: Number.parseInt(deadband, 10) || 0,
        reverse: reverse === "1" || reverse === "true",
      };
    }
  };

  const formatBindingString = (binding, isBackbone = false) => {
    const source = binding.source || "none";
    const channel = source === "none" ? 0 : Number.parseInt(binding.channel, 10) || 0;
    const min = Number.parseInt(binding.min, 10) || 1000;
    const center = Number.parseInt(binding.center, 10) || 1500;
    const max = Number.parseInt(binding.max, 10) || 2000;
    const deadband = Number.parseInt(binding.deadband, 10) || 0;
    const reverse = binding.reverse ? 1 : 0;

    if (isBackbone) {
      // Old format for backbone bindings: source:channel:min:center:max:deadband:reverse
      return [source, channel, min, center, max, deadband, reverse].join(":");
    } else {
      // New format for trigger bindings: source:channel:target:payload:min:center:max:deadband:reverse
      const target = binding.target || "none";
      const payload = binding.payload || "";
      return [source, channel, target, payload, min, center, max, deadband, reverse].join(":");
    }
  };

  const getEditorMode = () => {
    return rcSnapshot?.mode || "standard_pwm";
  };

  const cloneModeDraft = (mode) => {
    const draft = {};
    (RC_ACTIONS[mode] || []).forEach(({ key, field }) => {
      draft[key] = parseBindingString(getConfigBindingString(configCache, field));
    });
    return draft;
  };

  const findChannelForSlot = (slotKey, snapshot) => {
    if (!snapshot || !snapshot.channels) return null;

    if (BACKBONE_SLOTS.includes(slotKey)) {
      return snapshot.channels.find(ch => ch.name === slotKey);
    }

    return snapshot.channels.find(ch => ch.name === slotKey);
  };

  let saveTimeout = null;
  const debounce = (fn, ms) => {
    return (...args) => {
      clearTimeout(saveTimeout);
      saveTimeout = setTimeout(() => fn(...args), ms);
    };
  };

  let debouncedSaveRcMode = () => {};

  const getActionsForMode = (mode = getEditorMode()) => RC_ACTIONS[mode] || RC_ACTIONS.standard_pwm;

  const isBackboneSlot = (slotKey) => BACKBONE_SLOTS.includes(slotKey);

  const toActionToken = (slotKey, binding) => {
    if (slotKey === "driveSpeed") return "drive_speed";
    if (slotKey === "driveSteer") return "drive_steer";
    if (slotKey === "domeSpeed") return "dome_speed";
    if (slotKey === "driveLimit") return "speed_limit";
    return binding?.target || "none";
  };

  const actionLabelFromToken = (token) => {
    const found = actionTargets.find(item => item.token === token);
    return found ? found.label : token;
  };

  const sourceLabel = (source) => {
    if (source === "pwm") return "PWM";
    if (source === "sbus1") return "SBUS#1";
    if (source === "sbus2") return "SBUS#2";
    return "Disabled";
  };


  const MODE_LABEL = {
    standard_pwm: "🎮 Standard PWM",
    single_sbus: "📻 Single SBUS",
    dual_sbus: "📡 Dual SBUS",
  };

  const modeLabel = (mode) => MODE_LABEL[mode] || mode;

  const slotLabelForKey = (slotKey) => {
    const found = getActionsForMode().find((action) => action.key === slotKey);
    return found ? found.label : slotKey;
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

  const bindingForSlot = (slotKey) => {
    const mode = getEditorMode();
    if (!mappingDraft[mode]) {
      mappingDraft[mode] = cloneModeDraft(mode);
    }
    return mappingDraft[mode][slotKey] || { ...DEFAULT_BINDING };
  };

  const normalizeSlotName = (name) => String(name || "").replace(/[_\s-]/g, "").toLowerCase();

  const getChannelTelemetry = (slotKey) => {
    if (!rcSnapshot) return null;
    const channels = Array.isArray(rcSnapshot.channels) ? rcSnapshot.channels : [];
    const wanted = normalizeSlotName(slotKey);
    return channels.find(ch => normalizeSlotName(ch.name) === wanted) || null;
  };

  const miniBarHtml = (mapped) => {
    const normalized = Math.max(0, Math.min(1, (Number(mapped) + 1) / 2));
    const pct = Math.round(normalized * 100);
    return `<div class="rc-mini-bar"><div class="rc-mini-fill" style="--mini-pct:${pct}%"></div></div>`;
  };

  const triggerStateHtml = (channel) => {
    const pressed = Boolean(channel && channel.pressed);
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
        <div class="rc-source-card-title">${escapeHtml(name.toUpperCase())}</div>
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
    const actions = getActionsForMode();
    if (!actions.length) {
      rcSummaryBody.innerHTML = '<tr><td colspan="5">No slots for this mode.</td></tr>';
      return;
    }

    rcSummaryBody.innerHTML = actions.map(({ key, label }) => {
      const binding = bindingForSlot(key);
      const telemetry = getChannelTelemetry(key);
      const actionToken = toActionToken(key, binding);
      const live = isBackboneSlot(key) ? miniBarHtml(telemetry?.mapped || 0) : triggerStateHtml(telemetry);
      const channel = binding.source === "none" ? "—" : String(binding.channel || "—");
      return `<tr data-slot="${escapeHtml(key)}">
        <td>${escapeHtml(label)}</td>
        <td>${escapeHtml(actionLabelFromToken(actionToken))}</td>
        <td><span class="rc-map-source-badge" data-source="${escapeHtml(binding.source)}">${escapeHtml(sourceLabel(binding.source))}</span></td>
        <td>${escapeHtml(channel)}</td>
        <td>${live}</td>
      </tr>`;
    }).join("");

    rcSummaryBody.querySelectorAll("tr[data-slot]").forEach(row => {
      const key = row.dataset.slot;
      row.classList.add("row-clickable");
      row.classList.toggle("active-slot", selectedSlot === key);
      row.addEventListener("click", () => selectSlot(key));
      wireFocusableItem(row, () => selectSlot(key));
    });
  };

  const renderSlotList = () => {
    if (!rcSlotItems) return;
    const actions = getActionsForMode();
    const backbone = actions.filter(a => isBackboneSlot(a.key));
    const trigger = actions.filter(a => !isBackboneSlot(a.key));

    const renderItems = (items) => items.map(({ key, label }) => {
      const binding = bindingForSlot(key);
      const telemetry = getChannelTelemetry(key);
      const live = isBackboneSlot(key)
        ? miniBarHtml(telemetry?.mapped || 0)
        : `<span>${triggerStateHtml(telemetry)}</span>`;

      return `<div class="rc-slot-item${selectedSlot === key ? " active" : ""}" data-slot="${escapeHtml(key)}" role="button" aria-pressed="${selectedSlot === key ? "true" : "false"}">
        <div class="rc-slot-item-head">
          <strong>${escapeHtml(label)}</strong>
          <span class="rc-map-source-badge" data-source="${escapeHtml(binding.source)}">${escapeHtml(sourceLabel(binding.source))}</span>
        </div>
        <div class="rc-slot-item-meta">
          <span>CH ${binding.source === "none" ? "—" : escapeHtml(String(binding.channel || "—"))}</span>
          ${live}
        </div>
      </div>`;
    }).join("");

    rcSlotItems.innerHTML = `
      <div class="rc-slot-group-title">Backbone Channels</div>
      ${renderItems(backbone)}
      <div class="rc-slot-group-title trigger">Trigger/Button Channels</div>
      ${renderItems(trigger)}
    `;

    const slotNodes = Array.from(rcSlotItems.querySelectorAll(".rc-slot-item"));
    slotNodes.forEach((node, index) => {
      const key = node.dataset.slot;
      node.addEventListener("click", () => selectSlot(key));
      wireFocusableItem(node, () => selectSlot(key));
      node.addEventListener("keydown", (e) => {
        if (e.key === "ArrowDown") {
          e.preventDefault();
          const next = slotNodes[Math.min(index + 1, slotNodes.length - 1)];
          next?.focus();
          next?.click();
        }
        if (e.key === "ArrowUp") {
          e.preventDefault();
          const prev = slotNodes[Math.max(index - 1, 0)];
          prev?.focus();
          prev?.click();
        }
      });
    });

    // Re-apply learn-hot after innerHTML wipe — defined below, called safely
    // because renderSlotList() is only invoked after all functions are declared.
    applyLearnHighlight();
  };

  const renderLivePreview = () => {
    if (!rcLivePreviewContent) return;
    renderSourceHealth();

    if (!selectedSlot) {
      rcLivePreviewContent.innerHTML = '<div class="desc">Select a channel from the list to see live data.</div>';
      return;
    }

    const binding = bindingForSlot(selectedSlot);
    const telemetry = getChannelTelemetry(selectedSlot);
    const mapped = Number(telemetry?.mapped || 0);
    const raw = telemetry?.raw ?? 0;
    const rawUs = telemetry?.rawUs ?? 1500;

    let barHtml = "";
    if (selectedSlot === "driveSpeed" || selectedSlot === "driveSteer") {
      const width = Math.min(50, Math.round(Math.abs(mapped) * 50));
      const left = mapped >= 0 ? 50 : 50 - width;
      barHtml = `<div class="rc-preview-bar rc-preview-bar-center">
        <div class="rc-preview-fill signed" style="--bar-left:${left}%;--bar-width:${width}%"></div>
      </div>`;
    } else if (selectedSlot === "domeSpeed") {
      const width = Math.round(Math.abs(mapped) * 100);
      const dir = mapped < 0 ? "Reverse" : mapped > 0 ? "Forward" : "Stopped";
      barHtml = `<div class="rc-preview-bar">
        <div class="rc-preview-fill" style="--bar-width:${Math.max(0, Math.min(100, width))}%"></div>
      </div><div class="rc-preview-dir">Direction: ${dir}</div>`;
    } else if (selectedSlot === "driveLimit") {
      const width = Math.round(((mapped + 1) / 2) * 100);
      barHtml = `<div class="rc-preview-bar">
        <div class="rc-preview-fill" style="--bar-width:${Math.max(0, Math.min(100, width))}%"></div>
      </div>`;
    }

    if (isBackboneSlot(selectedSlot)) {
      rcLivePreviewContent.innerHTML = `
        <h4 class="rc-preview-title">${escapeHtml(slotLabelForKey(selectedSlot))}</h4>
        <div class="rc-preview-stack">
          <div>Source: <strong>${escapeHtml(sourceLabel(binding.source))}</strong> · CH ${binding.source === "none" ? "—" : escapeHtml(String(binding.channel || "—"))}</div>
          <div>Raw: <strong>${escapeHtml(String(raw))}</strong> · ${escapeHtml(String(rawUs))} us</div>
          <div>Mapped: <strong>${mapped.toFixed(3)}</strong></div>
          ${barHtml}
        </div>`;
    } else {
      rcLivePreviewContent.innerHTML = `
        <h4 class="rc-preview-title">${escapeHtml(slotLabelForKey(selectedSlot))}</h4>
        <div class="rc-preview-stack">
          <div>Source: <strong>${escapeHtml(sourceLabel(binding.source))}</strong> · CH ${binding.source === "none" ? "—" : escapeHtml(String(binding.channel || "—"))}</div>
          <div>State: ${triggerStateHtml(telemetry)}</div>
          <div>Raw: <strong>${escapeHtml(String(raw))}</strong></div>
        </div>`;
    }
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

    return `<div class="rc-action-row${selected ? ' selected' : ''}${disabled ? ' disabled' : ''}" data-action-token="${escapeHtml(item.token)}" data-action-disabled="${disabled ? 'true' : 'false'}" role="option" aria-selected="${selected ? 'true' : 'false'}" aria-disabled="${disabled ? 'true' : 'false'}">
      <button type="button" class="rc-action-select-btn" data-action-select="${escapeHtml(item.token)}"${disabled ? ' disabled' : ''}>
        <span class="rc-action-radio" aria-hidden="true">${selected ? '●' : '○'}</span>
        <span class="rc-action-main">
          <span class="rc-action-label">${escapeHtml(item.label)}</span>
          <span class="rc-action-desc">${escapeHtml(item.description || 'No description available.')}</span>
        </span>
      </button>
      <span class="rc-action-side">
        ${showSafetyPill ? '<span class="rc-action-safety-pill">⚠ Safety critical</span>' : ''}
        ${showTestButton ? `<button type="button" class="rc-action-test-btn" data-action-test="${escapeHtml(item.token)}"${inFlight ? ' disabled' : ''}>▶</button>` : ''}
        <span class="rc-action-test-feedback${feedbackClass}" data-action-feedback="${escapeHtml(item.token)}">${escapeHtml(feedbackText || '')}</span>
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
        <input class="rc-action-search-input" data-action-search type="search" placeholder="Search actions..." value="${escapeHtml(query)}" autocomplete="off">
        <button type="button" class="rc-action-search-clear" data-action-search-clear${query ? '' : ' disabled'}>✕</button>
      </div>
      ${recentBlock}
      ${groups.map(({ name, items }) => {
        const openAttr = query ? ' open' : (isActionGroupOpen(name) ? ' open' : '');
        return `<details class="rc-action-group" data-action-group="${escapeHtml(name)}"${openAttr}>
          <summary>${escapeHtml(name)}</summary>
          <div class="rc-action-group-rows">${items.map((item) => renderActionRow(item, selectedToken)).join('')}</div>
        </details>`;
      }).join('')}
      ${hasMatches ? '' : `<div class="rc-action-empty">No actions match "${escapeHtml(query)}".</div>`}
    </div>`;
  };

  const renderEditor = () => {
    if (!rcEditorContent) return;
    if (!selectedSlot) {
      actionPickerFeedback = null;
      actionPickerInFlightToken = null;
      actionPickerLastSlot = null;
      rcEditorContent.innerHTML = '<div class="desc">Select a channel from the list to edit its mapping.</div>';
      if (rcEditorApply) rcEditorApply.disabled = true;
      if (rcEditorRevert) rcEditorRevert.disabled = true;
      setEditorDirtyState("clean", "Select a slot to edit");
      return;
    }

    if (actionPickerLastSlot !== selectedSlot) {
      actionPickerFeedback = null;
      actionPickerInFlightToken = null;
      actionPickerScrollTop = 0;
      actionPickerQuery = '';
      Object.keys(actionPickerGroupOpen).forEach((group) => delete actionPickerGroupOpen[group]);
      actionPickerLastSlot = selectedSlot;
    } else {
      const previousPicker = rcEditorContent.querySelector('.rc-action-picker');
      if (previousPicker) {
        actionPickerScrollTop = previousPicker.scrollTop;
        previousPicker.querySelectorAll('[data-action-group]').forEach((groupNode) => {
          actionPickerGroupOpen[groupNode.dataset.actionGroup] = groupNode.open;
        });
      }
    }

    const binding = { ...bindingForSlot(selectedSlot) };
    const mode = getEditorMode();
    const sourceOptions = SOURCE_OPTIONS[mode] || SOURCE_OPTIONS.standard_pwm;
    const isBackbone = isBackboneSlot(selectedSlot);
    const slotLabel = slotLabelForKey(selectedSlot);

    const sourceSelect = sourceOptions.map(src =>
      `<option value="${escapeHtml(src)}"${binding.source === src ? " selected" : ""}>${escapeHtml(sourceLabel(src))}</option>`
    ).join("");

    const channelMax = binding.source === "pwm" ? 6 : 18;

    rcEditorContent.innerHTML = `
      <h4 class="rc-editor-title">Edit ${escapeHtml(slotLabel)}</h4>
      <label>Source
        <select data-field="source">${sourceSelect}</select>
      </label>
      <label>Channel
        <input data-field="channel" type="number" min="0" max="${channelMax}" value="${escapeHtml(String(binding.channel || 0))}">
      </label>
      ${isBackbone ? "" : `<label class="rc-action-label-head">Action</label><input data-field="target" type="hidden" value="${escapeHtml(binding.target || 'none')}">${renderActionPicker(binding.target || 'none', actionPickerQuery)}`}
      <div data-cond="seq" class="rc-editor-cond ${binding.target === "seq" ? "block" : "hidden"}">
        <label>Marcduino Sequence
          <select data-field="payload">
            ${MARCDUINO_SEQUENCES.map(n => `<option value="${n}"${binding.payload === String(n) ? " selected" : ""}>SE${n}</option>`).join("\n            ")}
          </select>
        </label>
      </div>
      <div data-cond="cmd" class="rc-editor-cond ${binding.target === "cmd" ? "block" : "hidden"}">
        <label>Marcduino Command
          <input data-field="payload" type="text" value="${escapeHtml(binding.payload || "")}" placeholder=":OP01">
        </label>
      </div>
      <div data-cond="estop" class="rc-editor-cond ${binding.target === "estop" ? "block" : "hidden"}">
        <label><input data-field="estop-confirm" type="checkbox"> I understand this latches estop.</label>
      </div>
      <details>
        <summary>Calibration</summary>
        <label>Min <input data-field="min" type="number" value="${escapeHtml(String(binding.min || 1000))}"></label>
        <label>Center <input data-field="center" type="number" value="${escapeHtml(String(binding.center || 1500))}"></label>
        <label>Max <input data-field="max" type="number" value="${escapeHtml(String(binding.max || 2000))}"></label>
        <label>Deadband <input data-field="deadband" type="number" value="${escapeHtml(String(binding.deadband || 0))}"></label>
        <label><input data-field="reverse" type="checkbox"${binding.reverse ? " checked" : ""}> Reverse</label>
      </details>
    `;

    const updateConditionalFields = () => {
      const targetEl = rcEditorContent.querySelector('[data-field="target"]');
      const target = targetEl ? targetEl.value : binding.target;
      const seq = rcEditorContent.querySelector('[data-cond="seq"]');
      const cmd = rcEditorContent.querySelector('[data-cond="cmd"]');
      const estop = rcEditorContent.querySelector('[data-cond="estop"]');
      if (seq) seq.className = `rc-editor-cond ${target === "seq" ? "block" : "hidden"}`;
      if (cmd) cmd.className = `rc-editor-cond ${target === "cmd" ? "block" : "hidden"}`;
      if (estop) estop.className = `rc-editor-cond ${target === "estop" ? "block" : "hidden"}`;
    };

    const refreshActionPickerSelectionUi = () => {
      const targetEl = rcEditorContent.querySelector('[data-field="target"]');
      const selectedToken = targetEl ? targetEl.value : 'none';
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
      binding.target = token;
      rememberRecentActionToken(token);
      actionPickerFeedback = null;
      updateConditionalFields();
      refreshActionPickerSelectionUi();
      syncActionTestUi();
      markEditorDirty();
      if (keepFocus) {
        const picker = rcEditorContent.querySelector('.rc-action-picker');
        picker?.focus();
      }
    };

    const runActionTest = async (token) => {
      if (actionPickerInFlightToken) return;
      const slotAtStart = selectedSlot;
      actionPickerInFlightToken = token;
      actionPickerFeedback = { token, kind: 'info', text: 'Testing...' };
      syncActionTestUi();
      try {
        await window.PAApi.postForm('/api/actions/test', { token }, { timeoutMs: 5000 });
        if (selectedSlot !== slotAtStart) return;
        actionPickerFeedback = { token, kind: 'success', text: 'Dispatched' };
      } catch (error) {
        if (selectedSlot !== slotAtStart) return;
        actionPickerFeedback = { token, kind: 'error', text: window.PAApi.messageFor(error) };
      } finally {
        if (actionPickerInFlightToken === token) {
          actionPickerInFlightToken = null;
        }
        if (selectedSlot === slotAtStart) {
          syncActionTestUi();
        }
      }
    };

    rcEditorContent.querySelectorAll("[data-field]").forEach(field => {
      field.addEventListener("change", () => {
        markEditorDirty();
        if (field.dataset.field === "target") {
          updateConditionalFields();
        }
      });
      field.addEventListener("input", () => {
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

        if (event.target instanceof Element && event.target.closest('.rc-action-test-btn')) {
          return;
        }
        if (event.target instanceof Element && event.target.closest('.rc-action-search-input')) {
          return;
        }

        if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp' && event.key !== 'Enter') {
          return;
        }
        event.preventDefault();

        const selectable = actionTargets
          .filter((item) => !item.disabled && actionMatchesQuery(item, actionPickerQuery))
          .map((item) => item.token);
        if (!selectable.length) return;

        const targetEl = rcEditorContent.querySelector('[data-field="target"]');
        const currentToken = targetEl ? targetEl.value : 'none';
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
    const rows = rcSummaryBody.querySelectorAll("tr[data-slot]");
    rows.forEach(row => {
      const slot = row.dataset.slot;
      const telemetry = getChannelTelemetry(slot);
      const cell = row.children[4];
      if (!cell) return;
      if (isBackboneSlot(slot)) {
        cell.innerHTML = miniBarHtml(telemetry?.mapped || 0);
      } else {
        cell.innerHTML = triggerStateHtml(telemetry);
      }
    });
  };

  const saveRcMode = async () => {
    if (!rcInputModeHidden) return;
    const mode = rcInputModeHidden.value;
    setModeFeedback("Saving...");
    try {
      const result = await window.PAApi.postForm("/api/config", { rcInputMode: mode }, { timeoutMs: 5000 });
      switchRcMode(getRcModeFromConfig(result.data));
      setModeFeedback(`${modeLabel(mode)} saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      setModeFeedback(`❌ ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  debouncedSaveRcMode = debounce(saveRcMode, 250);

  const switchRcMode = (mode) => {
    if (rcInputModeHidden) rcInputModeHidden.value = mode;
    rcModeCards.forEach((card) => {
      const selected = card.dataset.mode === mode;
      card.classList.toggle("selected", selected);
      card.setAttribute("aria-pressed", selected ? "true" : "false");
    });
    updateRecvSel(mode);

    // Clear selected slot when mode changes
    selectedSlot = null;
    document.querySelectorAll('.rc-slot-item').forEach(el => {
      el.classList.remove('active');
    });
    document.querySelectorAll('.rc-summary-table tr[data-slot]').forEach(tr => {
      tr.classList.remove('active-slot');
    });

    // Re-render all panels to reflect the new mode
    renderSummaryTable();
    renderSlotList();
    renderLivePreview();
    renderEditor();
  };

  // Set up event listeners for mode cards
  rcModeCards.forEach(card => {
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
      const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
      const data = result.data;
      const mode = getRcModeFromConfig(data);
      if (rcInputModeHidden) rcInputModeHidden.value = mode;
      rcModeCards.forEach((card) => {
        const selected = card.dataset.mode === mode;
        card.classList.toggle("selected", selected);
        card.setAttribute("aria-pressed", selected ? "true" : "false");
      });
      configCache = data;
      setSingleSbusRecvSelect(getSingleSbusRecvCh2(data));
      confirmedSbusRecvValue = sbusRecvSel?.value ?? null;
      updateRecvSel(mode);
      setModeFeedback(`Receiver type: ${modeLabel(mode)}`, "success");
      setRcInputsEnabled(rcComponentsEnabled(data));
    } catch (error) {
      const fallbackMode = rcInputModeHidden?.value || "standard_pwm";
      switchRcMode(fallbackMode);
      setModeFeedback(`Receiver config unavailable: ${window.PAApi.messageFor(error)}. Showing ${modeLabel(fallbackMode)} draft controls.`, "warning");
    }
  };

  const selectSlot = (key) => {
    // Prevent selecting unsupported slots
    if (!isSlotSupported(key)) {
      return;
    }

    selectedSlot = key;
    markEditorClean();
    document.querySelectorAll('.rc-slot-item').forEach(el => {
      el.classList.toggle('active', el.dataset.slot === key);
    });
    document.querySelectorAll('.rc-summary-table tr[data-slot]').forEach(tr => {
      tr.classList.toggle('active-slot', tr.dataset.slot === key);
    });
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

    // SBUS sources — raw units 172-1811, center ~992
    ['sbus1', 'sbus2'].forEach(src => {
      const currArr = Array.isArray(raw[src]) ? raw[src] : [];
      const baseArr = Array.isArray(baseRaw[src]) ? baseRaw[src] : [];
      currArr.forEach((val, idx) => {
        const base = baseArr[idx] ?? val;  // if no baseline, deviation = 0
        const delta = Math.abs(val - base);
        if (delta >= LEARN_SBUS_THRESHOLD) {
          if (!best || delta > best.delta) {
            best = { source: src, channel: idx + 1, raw: val, baseline: base, delta };
          }
        }
      });
    });

    // PWM — microseconds 1000-2000, center ~1500
    {
      const currArr = Array.isArray(raw.pwm) ? raw.pwm : [];
      const baseArr = Array.isArray(baseRaw.pwm) ? baseRaw.pwm : [];
      currArr.forEach((val, idx) => {
        if (val === 0) return;          // 0 = no valid pulse on this channel
        const base = baseArr[idx] ?? val;
        if (base === 0) return;
        const delta = Math.abs(val - base);
        if (delta >= LEARN_PWM_THRESHOLD) {
          if (!best || delta > best.delta) {
            best = { source: 'pwm', channel: idx + 1, raw: val, baseline: base, delta };
          }
        }
      });
    }

    return best;
  };

  // Find all slot keys whose current binding matches the detected source+channel.
  const slotsForDetectHit = (hit) => {
    if (!hit) return [];
    const mode = getEditorMode();
    const slots = RC_ACTIONS[mode] || RC_ACTIONS.dual_sbus;
    return slots
      .filter(({ key }) => {
        const b = bindingForSlot(key);
        return b.source === hit.source && Number(b.channel) === hit.channel;
      })
      .map(({ key }) => key);
  };

  // Human-readable label for a detect hit.
  const detectHitLabel = (hit) => {
    if (!hit) return '';
    const srcLabel = { sbus1: 'SBUS1', sbus2: 'SBUS2', pwm: 'PWM' }[hit.source] || hit.source.toUpperCase();
    return `${srcLabel} CH ${hit.channel}`;
  };

  // Apply/remove learn-hot class on slot cards that are already bound to the
  // detected source+channel. Uses direct classList toggle — no re-render.
  const applyLearnHighlight = () => {
    if (!rcSlotItems) return;
    const bound = learnActive && learnHit ? slotsForDetectHit(learnHit) : [];
    rcSlotItems.querySelectorAll('.rc-slot-item').forEach(el => {
      el.classList.toggle('learn-hot', bound.includes(el.dataset.slot));
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
    const bound = slotsForDetectHit(learnHit);
    if (bound.length > 0) {
      const slotLabels = bound.map(k => {
        const mode = getEditorMode();
        const slots = RC_ACTIONS[mode] || RC_ACTIONS.dual_sbus;
        return slots.find(s => s.key === k)?.label || k;
      }).join(', ');
      rcLearnStatus.textContent = `Detected: ${label} → already assigned to ${slotLabels}`;
    } else {
      rcLearnStatus.textContent = `Detected: ${label} — not assigned to any slot. Select a slot to configure it.`;
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
      setEditorFeedback("Detect mode unavailable: enable an RC channel in Setup.", "warning");
      return;
    }
    learnActive  = true;
    learnBaseline = rcSnapshot;   // snapshot at mode entry — baseline for deviation
    learnHit     = null;
    learnStartMs = Date.now();
    if (rcLearnBtn)    rcLearnBtn.textContent = '🔍 Detecting…';
    if (rcLearnBanner) rcLearnBanner.hidden = false;
    updateLearnBanner();
    applyLearnHighlight();
  };

  const exitLearnMode = () => {
    learnActive  = false;
    learnBaseline = null;
    learnHit     = null;
    if (rcLearnBtn)    rcLearnBtn.textContent = '🔍 Detect channel';
    if (rcLearnBanner) rcLearnBanner.hidden = true;
    applyLearnHighlight();
  };

  // Called on every SSE rc tick when learnActive is true.
  const processLearnTick = (currSnapshot) => {
    if (Date.now() - learnStartMs > LEARN_TIMEOUT_MS) {
      exitLearnMode();
      return;
    }

    const newHit = computeDetectHit(learnBaseline, currSnapshot);
    const prevHit = learnHit;
    learnHit = newHit;

    // Update DOM only when result changes to avoid needless repaints.
    const changed = JSON.stringify(newHit) !== JSON.stringify(prevHit);
    if (changed) {
      updateLearnBanner();
      applyLearnHighlight();
    }
  };

  // ─────────────────────────────────────────────────────────────────────────

  const renderRcDiagnostics = (payload) => {
    rcSnapshot = payload;

    renderSummaryTable();
    renderSlotList();
    if (selectedSlot) {
      renderLivePreview();
      renderEditor();
    }
  };

  const loadRcDiagnostics = async () => {
    try {
      const result = await window.PAApi.get("/api/rc", { timeoutMs: 5000 });
      renderRcDiagnostics(result.data);
    } catch (error) {
      setEditorFeedback(`Failed to load RC diagnostics: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const subscribeRcEvents = () => {
    if (!window.PAStatusStream?.isSupported()) return false;

    window.PAStatusStream.subscribe((eventType, payload) => {
      try {
        if (eventType === "status") {
          const data = typeof payload === "string" ? JSON.parse(payload) : payload;
          setRcInputsEnabled(rcEnabledFromStatus(data));
          return;
        }
        if (eventType !== "rc") return;
        const data = typeof payload === "string" ? JSON.parse(payload) : payload;

        // Process learning mode tick: compare current raw channels against the
        // baseline snapshot captured when detect mode was entered.
        if (learnActive) {
          processLearnTick(data);
        }

        rcSnapshot = data;

        // Fast path updates for frequently changing elements
        updateSummaryMiniBar();
        if (selectedSlot) {
          renderLivePreview();
        }
        // Note: Not re-rendering slot list or editor on every SSE tick for performance
      } catch (_error) {
        setEditorFeedback("Received malformed RC event payload.", "error");
      }
    });

    return true;
  };
  

  const saveMapping = async () => {
    const mode = getEditorMode();

    const formFields = rcEditorContent.querySelectorAll('[data-field]');
    const binding = { ...DEFAULT_BINDING };

    formFields.forEach((field) => {
      const fieldName = field.dataset.field;
      if (fieldName === "estop-confirm" || fieldName === "payload") {
        return;
      }

      if (field.type === "checkbox") {
        binding[fieldName] = field.checked;
      } else {
        binding[fieldName] = field.value;
      }

      if (["channel", "min", "center", "max", "deadband"].includes(fieldName)) {
        binding[fieldName] = Number.parseInt(field.value, 10) || 0;
      }
    });

    const activePayloadField = rcEditorContent.querySelector(
      `.rc-editor-cond[data-cond="${binding.target}"] [data-field="payload"]`,
    );
    binding.payload = activePayloadField ? activePayloadField.value : "";

    const allowedSources = SOURCE_OPTIONS[mode] || SOURCE_OPTIONS.standard_pwm;
    if (!allowedSources.includes(binding.source)) {
      setEditorFeedback(`Source must be one of: ${allowedSources.join(", ")}.`, "error");
      return;
    }

    // Validate Marcduino command prefix
    if (binding.target === "cmd" && binding.payload) {
      if (!/^[:$#]/.test(binding.payload)) {
        setEditorFeedback("Marcduino command must start with :, $, or #", "error");
        return;
      }
    }

    if (binding.target === "dome_seq") {
      setEditorFeedback("Dome sequence trigger mapping is not available in this firmware build.", "warning");
      return;
    }

    // Validate E-Stop confirmation
    if (binding.target === "estop") {
      const confirmCheckbox = rcEditorContent.querySelector('[data-field="estop-confirm"]');
      if (!confirmCheckbox || !confirmCheckbox.checked) {
        setEditorFeedback("E-Stop action requires confirmation checkbox", "error");
        return;
      }
    }

    if (!mappingDraft[mode]) mappingDraft[mode] = {};
    mappingDraft[mode][selectedSlot] = binding;

    // Per-slot save semantics: only save the currently selected slot
    const action = RC_ACTIONS[mode]?.find(a => a.key === selectedSlot);
    if (!action) return;

    const body = new URLSearchParams();
    const isBackbone = action.type === "backbone";
    body.set(action.field, formatBindingString(binding, isBackbone));

    setEditorDirtyState("saving", "Saving changes…");
    setEditorFeedback("Saving...");

    try {
      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      configCache = result.data;

      const savedAt = new Date().toLocaleTimeString();
      setEditorFeedback(`✓ Saved at ${savedAt}`, "success");
      markEditorClean(savedAt);

      await loadRcDiagnostics();
    } catch (error) {
      setEditorDirtyState("error", "Save failed — unsaved changes");
      if (rcEditorApply) rcEditorApply.disabled = false;
      if (rcEditorRevert) rcEditorRevert.disabled = false;
      setEditorFeedback(`Failed to save: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const revertMapping = () => {
    const mode = getEditorMode();
    mappingDraft[mode] = cloneModeDraft(mode);
    renderEditor();

    setEditorFeedback("Reverted to saved configuration.");

    markEditorClean();
  };

  const resetToDefaults = async () => {
    if (!confirm("Are you sure you want to reset all RC mappings to their default values?")) {
      return;
    }

    setEditorFeedback("Resetting to defaults...");

    try {
      const body = new URLSearchParams();
      const mode = getEditorMode();

      if (mode === "standard_pwm") {
        body.set("rcPwmDriveSpeed", "pwm:1:1000:1500:2000:0:0");
        body.set("rcPwmDriveSteer", "pwm:2:1000:1500:2000:0:0");
        body.set("rcPwmDriveLimit", "none:0:1000:1500:2000:0:0");
        body.set("rcPwmDomeSpeed", "pwm:3:1000:1500:2000:0:0");
        body.set("rcArm1", "pwm:4:arm1_toggle::1000:1500:2000:0:0");
        body.set("rcArm2", "pwm:5:arm2_toggle::1000:1500:2000:0:0");
      } else if (mode === "single_sbus") {
        body.set("rcSbusDriveSpeed", "sbus1:1:172:992:1811:0:0");
        body.set("rcSbusDriveSteer", "sbus1:2:172:992:1811:0:0");
        body.set("rcSbusDriveLimit", "sbus1:8:172:992:1811:0:0");
        body.set("rcSbusDomeSpeed", "sbus1:3:172:992:1811:0:0");
        body.set("rcArm1", "sbus1:4:arm1_toggle::172:992:1811:0:0");
        body.set("rcArm2", "sbus1:5:arm2_toggle::172:992:1811:0:0");
      } else {
        body.set("rcSbusDriveSpeed", "sbus1:1:172:992:1811:0:0");
        body.set("rcSbusDriveSteer", "sbus1:2:172:992:1811:0:0");
        body.set("rcSbusDriveLimit", "sbus1:8:172:992:1811:0:0");
        body.set("rcSbusDomeSpeed", "sbus2:1:172:992:1811:0:0");
        body.set("rcArm1", "sbus1:4:arm1_toggle::172:992:1811:0:0");
        body.set("rcArm2", "sbus1:5:arm2_toggle::172:992:1811:0:0");
      }

      body.set("rcAux1", "none:0:none::1000:1500:2000:0:0");
      body.set("rcAux2", "none:0:none::1000:1500:2000:0:0");
      body.set("rcAux3", "none:0:none::1000:1500:2000:0:0");
      body.set("rcSound", "none:0:none::1000:1500:2000:0:0");
      body.set("rcOpMode", "none:0:none::1000:1500:2000:0:0");
      body.set("rcFree0", "none:0:none::1000:1500:2000:0:0");
      body.set("rcFree1", "none:0:none::1000:1500:2000:0:0");
      body.set("rcFree2", "none:0:none::1000:1500:2000:0:0");
      body.set("rcFree3", "none:0:none::1000:1500:2000:0:0");

      const result = await window.PAApi.postForm("/api/config", body, { timeoutMs: 5000 });
      configCache = result.data;

      const savedAt = new Date().toLocaleTimeString();
      setEditorFeedback("✓ Reset to defaults successful", "success");
      markEditorClean(savedAt);

      await loadRcDiagnostics();
    } catch (error) {
      setEditorFeedback(`Failed to reset to defaults: ${window.PAApi.messageFor(error)}`, "error");
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

  loadRecentActionTokens();
  switchRcMode(rcInputModeHidden?.value || "standard_pwm");
  loadRcMode();
  loadRcDiagnostics();
  loadActionTargets().then(() => { if (selectedSlot) renderEditor(); });

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

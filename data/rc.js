(() => {
  let selectedChannel = null;
  let rcSnapshot = null;
  let channelMap = {};
  // Whether channelMap is a map the droid answered with. A save posts the
  // whole map, so one posted before it ever loaded would erase every other
  // binding on the droid (#355).
  let channelMapLoaded = false;
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
  
  const rcInputModeHidden = document.getElementById("rc-input-mode");
  const rcModeFeedback = document.getElementById("rc-mode-feedback");
  const rcModeSummary = document.getElementById("rc-mode-summary");
  const rcModeWaiting = document.getElementById("rc-mode-waiting");
  const rcResetDefaults = document.getElementById("rc-reset-defaults");
  const rcDisabledCard = document.getElementById("rc-disabled-card");
  const rcInputSummary = document.getElementById("rc-input-summary");
  const rcRadioCard = document.getElementById("rc-radio-card");
  const rcReceiverCard = document.getElementById("rc-receiver-card");
  
  const singleSbusRecvSection = document.getElementById("single-sbus-recv-section");
  const sbusRecvSeg = document.getElementById("sbus-recv-seg");
  const sbusRecvFeedback = document.getElementById("sbus-recv-feedback");
  let sbusRecvFeedbackTimer = null;
  let confirmedSbusRecvCh2 = null;
  const rcSummaryBody = document.getElementById("rc-summary-body");
  const rcSummaryCount = document.getElementById("rc-summary-count");
  const rcCapacity = document.getElementById("rc-capacity");
  
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
  
  // A stick, not a press: the three axes, and a puppet string, which moves one
  // Part as far as the stick is pushed (#442). Never fired once, so never
  // tried, and never bound to a droid condition.
  const ANALOG_ACTION_TOKENS = new Set(['drive_speed', 'drive_steer', 'dome_speed', 'puppet_part']);
  // The three axes: each has a place of its own in the RC Map, outside the 11
  // trigger bindings (src/web/api_config.cpp, assignRcMapEntryToSnapshot()).
  const ANALOG_AXIS_TOKENS = new Set(['drive_speed', 'drive_steer', 'dome_speed']);
  // Hardcoded fallback used until GET /api/actions resolves.
  // Matches robotActionIdToString() NVS token keys in rc_mapping.h.
  // It carries no action about one Output (the toggles): those are named by
  // what the running board prints beside the Output, which only the firmware
  // knows, so they appear when GET /api/actions answers (ADR 0033 Amendment
  // 2026-09-19; tools/check_action_registry_drift.py holds this file to it).
  const HARDCODED_ACTION_TARGETS = [
    { token: 'drive_speed', label: 'Speed', group: 'Movement', description: 'Forward and back on the feet. Bind it to a stick.', disabled: false, testable: false, safetyCritical: false },
    { token: 'drive_steer', label: 'Steer', group: 'Movement', description: 'Left and right on the feet. Bind it to a stick.', disabled: false, testable: false, safetyCritical: false },
    { token: 'dome_speed', label: 'Dome Speed', group: 'Movement', description: 'Turn the dome. Bind it to a stick.', disabled: false, testable: false, safetyCritical: false },
    { token: 'op_mode', label: 'Set Mode', group: 'Mode', description: 'Switch between Stationary and Driving. Stationary locks the feet.', disabled: false, testable: true, safetyCritical: false, oneShot: false },
    { token: 'seq', label: 'Marcduino Sequence', group: 'Sequences', description: 'Play a numbered body sequence, usually SE30 to SE36.', disabled: false, testable: false, safetyCritical: false },
    { token: 'dome_seq', label: 'Dome Sequence', group: 'Sequences', description: 'Play a dome show by name, like DM:FLUTTER. The Sequences page lists them all.', disabled: false, testable: false, safetyCritical: false },
    { token: 'cmd', label: 'Marcduino Command', group: 'Command', description: 'Send one Marcduino command to the dome.', disabled: false, testable: false, safetyCritical: false },
    { token: 'sleep_toggle', label: 'Sleep Toggle', group: 'System', description: 'Sleep or wake. Drive stays awake either way.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_general', label: 'Random General', group: 'Sound', description: 'Play a random General sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_chatty', label: 'Random Chatty', group: 'Sound', description: 'Play a random Chatty sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_happy', label: 'Random Happy', group: 'Sound', description: 'Play a random Happy sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_processing', label: 'Random Processing', group: 'Sound', description: 'Play a random Processing sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_sad', label: 'Random Sad', group: 'Sound', description: 'Play a random Sad sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_sentimental', label: 'Random Sentimental', group: 'Sound', description: 'Play a random Sentimental sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_humming', label: 'Random Humming', group: 'Sound', description: 'Play a random Humming sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_scream', label: 'Random Scream', group: 'Sound', description: 'Play a random Scream sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_surprised', label: 'Random Surprised', group: 'Sound', description: 'Play a random Surprised sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_alert', label: 'Random Alert', group: 'Sound', description: 'Play a random Alert sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_snarky', label: 'Random Snarky', group: 'Sound', description: 'Play a random Snarky sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_rand_whistle', label: 'Random Whistle', group: 'Sound', description: 'Play a random Whistle sound.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_next', label: 'Next Sound', group: 'Sound', description: 'Play the sound after the last one played.', disabled: false, testable: true, safetyCritical: false },
    { token: 'sound_previous', label: 'Previous Sound', group: 'Sound', description: 'Play the sound before the last one played.', disabled: false, testable: true, safetyCritical: false },
    { token: 'estop', label: 'Emergency Stop', group: 'Safety', description: 'Stop the feet now and latch the estop.', disabled: false, testable: false, safetyCritical: true },
    { token: 'droid_seq_scream', label: 'Scream', group: 'Sequences', description: 'SE01. A scream, and the body and dome join in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_wave', label: 'Wave', group: 'Sequences', description: 'SE02. A body wave, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_fast_wave', label: 'Fast Wave', group: 'Sequences', description: 'SE03. A fast wave, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_open_wave', label: 'Open Wave', group: 'Sequences', description: 'SE04. An open wave, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_beep_cantina', label: 'Beep Cantina', group: 'Sequences', description: 'SE05. Short Cantina with a body wave, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_faint', label: 'Faint', group: 'Sequences', description: 'SE06. A faint: the body parks, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_cantina', label: 'Cantina Dance', group: 'Sequences', description: 'SE07. Long Cantina with a body wave, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_leia', label: 'Leia Message', group: 'Sequences', description: 'SE08. The Leia message, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_disco', label: 'Disco', group: 'Sequences', description: 'SE09. Disco music with a body wave, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_screams', label: 'Screams', group: 'Sequences', description: 'SE15. Screams from the body, and the dome does its part.', disabled: false, testable: true, safetyCritical: false },
    { token: 'droid_seq_wiggle', label: 'Panel Wiggle', group: 'Sequences', description: 'SE16. A body wave, and the dome joins in.', disabled: false, testable: true, safetyCritical: false },
    { token: 'speed_preset_cycle', label: 'Speed Preset Cycle', group: 'Movement', description: 'Step the speed preset: Slow, Normal, Turbo, and round again.', disabled: false, testable: true, safetyCritical: false },
    { token: 'puppet_part', label: 'Perform a Part', group: 'Outputs', description: 'Move one Part with a stick: push to open it that far. Let go and it closes.', disabled: false, testable: false, safetyCritical: false },
  ];

  // ==== ACTION TEST OUTCOME (#220) BEGIN ====
  // Maps POST /api/actions/test's real outcome (docs/api.md; mirrors
  // docs/console-protocol.md's outcome vocabulary) onto the picker's
  // feedback pill. Pure and DOM-free on purpose - runActionTest() below is
  // its only caller, and test/test_web/test_rc_action_test_outcome.js
  // extracts this exact block (by the BEGIN/END markers, matching
  // test/test_web/helpers/page_module_env.js's PART-1 extraction of
  // page_bootstrap.js) and evaluates it standalone, since a click on a
  // dynamically-rendered action-picker button is not reachable through the
  // shared page-module test harness (querySelectorAll on rendered innerHTML
  // is stubbed empty there - #220 does not attempt to fix that harness gap).
  // An outcome this function does not recognize (a future addition, or an
  // older/mismatched firmware) must not read as success - the whole point of
  // #220 is that the picker stops claiming "Dispatched" on a dropped command.
  const actionTestFeedbackForOutcome = (outcome) => {
    if (outcome === 'queued') return { kind: 'success', text: 'Dispatched' };
    if (outcome === 'queue-full') return { kind: 'error', text: 'Queue full - try again' };
    if (outcome === 'unavailable') return { kind: 'error', text: 'Unavailable right now' };
    return { kind: 'error', text: 'Unexpected response' };
  };
  // ==== ACTION TEST OUTCOME (#220) END ====

  // ==== DRIVE ON ONE RECEIVER (#389) BEGIN ====
  // Speed and Steer read one RC Receiver: POST /api/rc/map refuses a map that
  // puts them on two sources ("drive speed and steer must be on the same
  // receiver"). Given the map (channel key -> { source, action }), the drive
  // axis being bound and the channel key it would go on, this returns the
  // binding of the OTHER drive axis when it sits on another source, else null.
  // Pure and DOM-free so test/test_web/test_rc_page.js runs this exact block.
  const driveSplitWith = (map, token, channelKey) => {
    const pair = { drive_speed: 'drive_steer', drive_steer: 'drive_speed' };
    const other = pair[token];
    if (!other) return null;
    const source = String(channelKey || '').split(':')[0];
    const found = Object.entries(map || {})
      .find(([key, entry]) => key !== channelKey && String(entry?.action || '') === other);
    return found && found[1].source !== source ? found[1] : null;
  };
  // The drive reads SBUS1: only the drive receiver carries the drive
  // watchdog and the failsafe stop, so the droid refuses a drive axis on SBUS2
  // (operator, 2026-10-09 on #389).
  const DRIVE_TOKENS = new Set(['drive_speed', 'drive_steer']);
  const driveOnSbus2 = (token, channelKey) => DRIVE_TOKENS.has(token)
    && String(channelKey || '').split(':')[0] === 'sbus2';
  // ==== DRIVE ON ONE RECEIVER (#389) END ====

  // The droid's refusal of a drive split across two receivers, in the
  // builder's words.
  const DRIVE_SPLIT_REFUSAL = /must be on the same receiver/;
  const DRIVE_SPLIT_TEXT = 'Not saved: Speed and Steer share one RC Receiver.';
  const DRIVE_SBUS2_REFUSAL = /drive reads SBUS1/;
  const DRIVE_SBUS2_TEXT = 'Not saved: Speed and Steer read SBUS1.';

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
    servo: 'Outputs',
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
  const ACTION_GROUP_ORDER = ['Movement', 'Mode', 'Outputs', 'Sound', 'Sequences', 'Command', 'Safety', 'System', 'Aux', 'Other'];
  const DEFAULT_COLLAPSED_GROUPS = new Set(['Sound', 'Sequences']);
  const NON_TESTABLE_TOKENS = new Set(['drive_speed', 'drive_steer', 'dome_speed', 'puppet_part', 'estop']);

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

  const loadActionTargets = async ({ handle = null } = {}) => {
    try {
      const api = handle || window.PAApi;
      const result = await api.get('/api/actions');
      if (Array.isArray(result.data)) {
        actionTargets = buildActionTargetsFromApi(result.data);
      }
      syncRecentActionTokensWithTargets();
    } catch (error) {
      // fetch error — fall back to hardcoded action targets
      console.warn('[RC] Failed to load action registry, using built-in list');
      syncRecentActionTokensWithTargets();
      throw error;
    }
  };

  // The seven body routines. Each description is the purpose the firmware
  // catalog gives DM:SE<id> (src/tasks/sequence_catalog.cpp), word for word
  // after its ":SE<id> - " prefix, so the picker and the droid describe the same
  // routine. No test holds the two together (#406: copy is read in the diff),
  // so a change to either is made to both by hand (#354).
  const MARCDUINO_SEQUENCES = [
    { id: 30, name: "Utility arm open-and-close", description: "Both utility arms swing out, then flick in and out twice before they close (5 s)." },
    { id: 31, name: "All body panels open and close", description: "Every body door and arm opens and works, then folds away in order (14 s)." },
    { id: 32, name: "All body doors wiggle-close", description: "The breadpan doors, dataport and utility arms spring open, then wiggle shut (4 s)." },
    { id: 33, name: "Use gripper arm", description: "The left breadpan door opens and the gripper arm rises and snaps its claw three times, then folds away (8 s)." },
    { id: 34, name: "Use interface tool", description: "The right breadpan door opens and the interface arm rises and works its tool three times, then folds away (9 s)." },
    { id: 35, name: "Ping-pong body doors", description: "The two breadpan doors take turns opening, faster and then slower, then both close (13 s)." },
    { id: 36, name: "BT-1 two-gripper sequence", description: "Both breadpan doors open and both grippers snap together five times, then fold away (7 s)." },
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
    { payload: 'DM:DISCO',     label: 'Disco',                description: 'Disco lights and music (46 s)' },
    { payload: 'DM:VADER',     label: 'Imperial March',       description: 'Imperial March -- red logics/holos (47 s)' },
    { payload: 'DM:ROCKMARCH', label: 'Rock March',           description: 'Imperial March alternate visual (47 s)' },
    { payload: 'DM:HELLO',     label: 'Hello There',          description: 'Logic text greeting, then P1 opens and closes (4 s)' },
    { payload: 'DM:LEIA',      label: 'Leia',                 description: 'Front holo Leia effect, logic Leia mode (36 s)' },
    { payload: 'DM:CANTINA',   label: 'Cantina',              description: '130 BPM alternating panel dance (17 s)' },
    { payload: 'DM:RESET',     label: 'Reset All',            description: 'Close all panels, reset all subsystems (4 s)' },
    { payload: 'DM:RANDOM',    label: 'Random',               description: 'Plays a random sequence' },
  ];

  // Cached learned sequences (fetched on demand)
  let cachedLearnedSequences = null;

  // Sequence names are matched whatever their case, as this page always has.
  const sameSequenceName = (a, b) => String(a || '').toUpperCase() === String(b || '').toUpperCase();

  const normalizeMarcduinoSequencePayload = (payload) => {
    const raw = String(payload || "").trim().toUpperCase();
    if (/^\d{2}$/.test(raw)) return raw;
    const match = /^SE(\d{2})$/.exec(raw);
    return match ? match[1] : raw;
  };

  // A payload picker: one choice among many, as pills that wrap, with the
  // picked value in a hidden field the save reads. `options` are
  // { value, label, title }. With `allowNone`, a value none of them carries
  // picks nothing, and the save asks for a pick.
  const payloadPillsHtml = (name, options, selectedValue, { allowNone = false } = {}) => {
    const fallback = allowNone ? '' : (options[0]?.value ?? '');
    const picked = options.some((option) => option.value === selectedValue)
      ? selectedValue
      : fallback;
    const pills = options.map((option) => {
      const on = option.value === picked;
      return `<button type="button" class="part-pill${on ? ' active' : ''}" role="radio" aria-checked="${on ? 'true' : 'false'}" data-payload-pick="${window.PAUtils.escapeHtml(option.value)}" title="${window.PAUtils.escapeHtml(option.title || '')}">${window.PAUtils.escapeHtml(option.label)}</button>`;
    }).join('');
    return `<input data-field="payload" type="hidden" value="${window.PAUtils.escapeHtml(picked)}">
        <div class="part-pills rc-pills" role="radiogroup" aria-label="${window.PAUtils.escapeHtml(name)}">${pills}</div>`;
  };

  const marcduinoSequencePills = (selectedPayload) => payloadPillsHtml(
    'Marcduino Sequence',
    MARCDUINO_SEQUENCES.map((entry) => ({
      value: String(entry.id),
      label: `SE${entry.id} ${entry.name}`,
      title: entry.description || '',
    })),
    normalizeMarcduinoSequencePayload(selectedPayload));

  // The Factory sequences, the droid's own once it has listed them, and the
  // one this binding already names if it is in neither: a sequence that was
  // deleted stays offered, marked missing, so opening the editor never
  // rewrites the binding to something else.
  const domeSequencePills = (selectedPayload) => {
    const sel = String(selectedPayload || '').trim();
    const options = [...FACTORY_DOME_SEQUENCES, ...(cachedLearnedSequences || [])]
      .map((entry) => ({ value: entry.payload, label: entry.label, title: entry.description }));
    const known = options.find((option) => sameSequenceName(option.value, sel));
    if (sel && !known) {
      // Kept as its own pill either way, so the binding is never rewritten.
      // Called missing only once the droid has listed its sequences.
      const missing = cachedLearnedSequences !== null;
      options.push({
        value: sel,
        label: missing ? `${sel} (missing)` : sel,
        title: missing ? 'This sequence is not on the droid.' : '',
      });
    }
    return payloadPillsHtml('Dome Sequence', options, known ? known.value : sel);
  };

  // The Parts a puppet string can move: every Part on a servo Output, as the
  // droid reports its rows (data/outputs.js), in the droid's Output order.
  // What a Part is CALLED comes from the catalog (data/droid_parts.js). Null
  // until the droid has answered, so no stored string is called unwired early.
  let puppetPartIds = null;
  // Which servo Output each of those Parts is on, by its address: two Parts
  // on one Output cannot move apart, so two strings on them take turns.
  let puppetPartOutput = new Map();
  const catalogPartById = new Map((window.DroidParts?.parts || []).map((part) => [part.id, part]));
  const partLabel = (id) => {
    const part = catalogPartById.get(id);
    if (!part) return id;
    return part.shorthand ? `${part.name} (${part.shorthand})` : part.name;
  };

  const loadPuppetParts = async () => {
    if (!window.PAOutputs) return;
    try {
      const outputs = await window.PAOutputs.refresh();
      const servos = outputs.filter((output) => !output.light);
      puppetPartIds = servos.flatMap((output) => output.parts);
      puppetPartOutput = new Map(servos.flatMap((output) => output.parts.map((id) => [id, output.address])));
    } catch (error) {
      console.warn('[RC] Outputs not loaded:', window.PAApi.messageFor(error));
    }
  };

  // The Part this string already names stays offered when nothing drives it
  // any more, marked, so opening the editor never moves the string elsewhere.
  const puppetPartPills = (selectedPart) => {
    const sel = String(selectedPart || '');
    const ids = puppetPartIds || [];
    const options = ids.map((id) => ({ value: id, label: partLabel(id), title: '' }));
    if (sel && !ids.includes(sel)) {
      const unwired = puppetPartIds !== null;
      options.push({
        value: sel,
        label: unwired ? `${partLabel(sel)} (not wired)` : partLabel(sel),
        title: unwired ? 'No servo moves this Part.' : '',
      });
    }
    if (!options.length) {
      return `<input data-field="payload" type="hidden" value="">
        <p class="hint">${puppetPartIds === null
          ? 'Reading the Parts on this droid...'
          : 'No Part is on a servo yet. Put one on in <a class="setup-link" href="#wiring">Wiring</a>.'}</p>`;
    }
    // A new string starts on the first Part no other channel moves, or on
    // none, so the move question is never raised about a Part nobody picked.
    const strung = new Set(Object.entries(channelMap)
      .filter(([key, entry]) => key !== selectedChannel && mapEntryAction(entry) === 'puppet_part')
      .map(([, entry]) => entry.payload));
    const free = options.find((option) => !strung.has(option.value));
    return payloadPillsHtml('Part', options, sel || (free ? free.value : ''), { allowNone: true });
  };

  // The Parts arrived while the editor is open: only the Part pills are drawn
  // again, around whatever is picked, so a draft is kept.
  const refreshPuppetPartPills = () => {
    const group = rcEditorContent?.querySelector('[data-cond="puppet_part"]');
    if (!group) return;
    const picked = group.querySelector('[data-field="payload"]')?.value || '';
    group.innerHTML = `<span class="rc-action-label-head">Part</span>
        ${puppetPartPills(picked)}`;
    wirePayloadPills(group);
  };

  // A pill picks its payload: the hidden field beside it takes the value.
  const wirePayloadPills = (root) => {
    root.querySelectorAll('[data-payload-pick]').forEach((pill) => {
      pill.addEventListener('click', () => {
        const group = pill.closest('.rc-editor-cond');
        const field = group?.querySelector('[data-field="payload"]');
        if (!field || field.value === pill.dataset.payloadPick) return;
        field.value = pill.dataset.payloadPick;
        group.querySelectorAll('[data-payload-pick]').forEach((other) => {
          const on = other === pill;
          other.classList.toggle('active', on);
          other.setAttribute('aria-checked', on ? 'true' : 'false');
        });
        markEditorDirty();
      });
    });
  };

  // The droid's sequences arrived while the editor is open: only the Dome
  // Sequence pills are drawn again, around whatever is picked. The editor is
  // never re-rendered for it - that would throw away a draft.
  const refreshDomeSequencePills = () => {
    const group = rcEditorContent?.querySelector('[data-cond="dome_seq"]');
    if (!group) return;
    const picked = group.querySelector('[data-field="payload"]')?.value || '';
    group.innerHTML = `<span class="rc-action-label-head">Dome Sequence</span>
        ${domeSequencePills(picked)}`;
    wirePayloadPills(group);
  };

  // The droid's own sequences, asked for once. Left null when the droid does
  // not answer: an empty list would mark every binding to one as missing.
  const loadLearnedSequences = async () => {
    if (cachedLearnedSequences !== null) return;
    try {
      const result = await window.PAApi.get('/api/seq/list');
      if (result.ok && Array.isArray(result.data)) {
        cachedLearnedSequences = result.data.map((seq) => ({
          payload: seq.name,
          label: seq.name,
          description: `Learned sequence (${seq.stepCount || 0} steps, ${seq.suppressMs}ms suppress)`,
        }));
      }
    } catch (error) {
      console.warn('[RC] Sequence list not loaded:', window.PAApi.messageFor(error));
    }
  };

  // The firmware's own default receiver type (src/config_settings.cpp): the
  // mode this page shows until the droid has said which one it holds.
  const DEFAULT_RC_MODE = "dual_sbus";
  const DETECT_ACT = "Detect RC Channel";

  const SOURCE_OPTIONS = {
    standard_pwm: ["pwm"],
    // One receiver, and it is SBUS1 whichever header it is wired to: the
    // CH2 header pick moves the wire, never the name (operator, 2026-10-09 on
    // #389). An sbus2 binding reads nothing here.
    single_sbus: ["sbus1"],
    dual_sbus: ["sbus1", "sbus2"],
    // An ELRS receiver the controller reads nothing from yet (#369): no
    // channel arrives, so there is none to map.
    elrs: [],
    // No Radio Controller fitted: nothing arrives at all.
    not_fitted: [],
  };

  // The droid's own conditions (ADR 0053, #450): a binding on one is a
  // Reaction, which the droid fires itself, radio or no radio. So they are
  // offered in every receiver mode, beside whatever SOURCE_OPTIONS gives it.
  // `source` and `channel` are the stored pair (include/rc_binding_types.h); a
  // condition has no channel to ask for, so each row is one pair, named.
  // `threshold` is the one number a condition takes, shown in the builder's
  // unit: `scale` stored units to one of theirs. The list has nothing for the
  // room or for how loud a sound is, because the droid cannot sense either.
  const SPEED_THRESHOLD = { label: 'Speed at or over', unit: 'of 1000', scale: 1, min: 1, max: 1000, fallback: 300 };
  const WHEEL_SPEED_THRESHOLD = { label: 'Speed at or over', unit: 'RPM', scale: 1, min: 1, max: 1000, fallback: 30 };
  const WHEEL_AMPS_THRESHOLD = { label: 'Current at or over', unit: 'A', scale: 100, min: 1, max: 5000, fallback: 300 };
  const DROID_CONDITIONS = [
    { source: 'speed', channel: 1, label: 'Drive speed', threshold: SPEED_THRESHOLD },
    { source: 'hstop', channel: 1, label: 'Hard stop', threshold: { ...SPEED_THRESHOLD, label: 'Stopping from', fallback: 400 } },
    { source: 'rest', channel: 1, label: 'Comes to rest', threshold: { label: 'At rest for', unit: 's', scale: 10, min: 1, max: 600, fallback: 20 } },
    { source: 'track', channel: 1, label: 'Track starts', threshold: null },
    { source: 'wspeed', channel: 1, label: 'Left wheel speed', threshold: WHEEL_SPEED_THRESHOLD },
    { source: 'wspeed', channel: 2, label: 'Right wheel speed', threshold: WHEEL_SPEED_THRESHOLD },
    { source: 'wamps', channel: 1, label: 'Left wheel current', threshold: WHEEL_AMPS_THRESHOLD },
    { source: 'wamps', channel: 2, label: 'Right wheel current', threshold: WHEEL_AMPS_THRESHOLD },
  ];
  const DROID_SOURCES = new Set(DROID_CONDITIONS.map((condition) => condition.source));
  const REACTION_QUIET = { min: 1, max: 3600, fallback: 5 };
  // What a Reaction may not do: the estop, and the two that change how the
  // droid drives (robotActionValidForReaction(), src/rc_action_types.cpp).
  const REACTION_BLOCKED_TOKENS = new Set(['estop', 'op_mode', 'speed_preset_cycle']);

  const isDroidSource = (source) => DROID_SOURCES.has(source);

  const channelKeyOf = (source, channel) => `${source}:${Number(channel)}`;

  const droidConditionFor = (channelKey) => DROID_CONDITIONS
    .find((condition) => channelKeyOf(condition.source, condition.channel) === channelKey) || null;

  // Whether a source can be bound in this receiver mode.
  const sourceAllowedInMode = (source, mode) =>
    isDroidSource(source) || (SOURCE_OPTIONS[mode] || SOURCE_OPTIONS[DEFAULT_RC_MODE]).includes(source);

  const parseChannelKey = (channelKey) => {
    const [source = "", channelValue = "0"] = String(channelKey || "").split(":");
    const channel = Number.parseInt(channelValue, 10) || 0;
    return { source, channel };
  };

  const getRcModeFromConfig = (cfg) => {
    const mode = cfg?.rc?.inputMode;
    return typeof mode === "string" ? mode : DEFAULT_RC_MODE;
  };

  const rcComponentsEnabled = (cfg) => {
    const c = cfg?.components || {};
    return Boolean(
      c.rcCh1?.enabled || c.rcCh2?.enabled || c.rcCh3?.enabled ||
      c.rcCh4?.enabled || c.rcCh5?.enabled || c.rcCh6?.enabled
    );
  };

  const rcSourcesEnabled = (diagnostics) => Object.values(diagnostics?.sources || {})
    .some((source) => source?.enabled === true);

  const getSingleSbusRecvCh2 = (cfg) => cfg?.rc?.sbus?.recvCh2 === true;

  // Which SBUS input the one receiver is on, painted on the segmented control.
  const paintSbusRecv = (recvCh2) => {
    sbusRecvSeg?.querySelectorAll("button").forEach((button) => {
      const on = (button.dataset.value === "true") === recvCh2;
      button.classList.toggle("active", on);
      button.setAttribute("aria-checked", on ? "true" : "false");
    });
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
    sbusRecvFeedback.className = variant ? `feedback ${variant}` : "feedback";
    if (clearAfterMs > 0) {
      sbusRecvFeedbackTimer = window.setTimeout(() => {
        sbusRecvFeedback.textContent = "";
        sbusRecvFeedback.className = "feedback";
        sbusRecvFeedbackTimer = null;
      }, clearAfterMs);
    }
  };

  const setModeFeedback = (message, variant = '') => {
    if (!rcModeFeedback) return;
    rcModeFeedback.textContent = message;
    rcModeFeedback.className = variant ? `feedback ${variant}` : 'feedback';
  };

  const setEditorFeedback = (message, variant = '') => {
    if (!rcEditorFeedback) return;
    rcEditorFeedback.textContent = message;
    rcEditorFeedback.className = variant ? `feedback ${variant}` : 'feedback';
  };

  const setRcInputsEnabled = (enabled) => {
    rcInputsEnabled = enabled;
    rcDisabledCard?.classList.toggle("hidden", enabled);
    if (rcInputSummary) rcInputSummary.textContent = enabled ? "live" : "no source is live";

    if (rcLearnBtn) {
      rcLearnBtn.disabled = !enabled;
      rcLearnBtn.setAttribute("aria-disabled", enabled ? "false" : "true");
      if (!enabled) window.PAUi.setAct(rcLearnBtn, DETECT_ACT);
    }
    if (rcLearnStop) {
      rcLearnStop.disabled = !enabled;
      rcLearnStop.setAttribute("aria-disabled", enabled ? "false" : "true");
    }

    if (!enabled && learnActive) {
      exitLearnMode();
    }
  };

  // The receiver type this page maps for is the SAVED one (GET /api/config
  // rc.inputMode, and GET /api/rc/map's own mode): it is the one the map is
  // kept for. The droid may still run another until it restarts - GET /api/rc
  // `mode` and rc.activeInputMode say which - and the RC Receiver card says so.
  const getEditorMode = () => rcInputModeHidden?.value || DEFAULT_RC_MODE;

  const normalizeMapEntry = (entry) => {
    const source = typeof entry?.source === 'string' ? entry.source : '';
    const channel = Number.parseInt(entry?.channel, 10) || 0;
    const action = typeof entry?.action === 'string' ? entry.action : '';
    const payload = entry?.payload == null ? '' : String(entry.payload);
    if (!source || channel <= 0 || !action) return null;
    const normalized = { source, channel, action };
    if (payload) normalized.payload = payload;
    if (isDroidSource(source)) {
      if (Number.isFinite(Number(entry.threshold))) normalized.threshold = Number(entry.threshold);
      if (Number.isFinite(Number(entry.quietS))) normalized.quietS = Number(entry.quietS);
    }
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
    const raw = Array.isArray(sourceRaw) ? sourceRaw[channel - 1] : null;
    return raw == null ? null : raw;
  };

  // An SBUS frame carries 16 stick channels and two on/off ones, CH17 and
  // CH18 (GLOSSARY.md "RC Channel"). GET /api/rc sends the 16 in `raw` and the
  // two in `rawDigital` ([CH17, CH18] per receiver, whatever binds them). A
  // firmware without `rawDigital` says an on/off one only where a binding in
  // `digital` reads it, so there an unbound CH17 is not known, never "off".
  const SBUS_CHANNELS = 18;
  const SBUS_ANALOG_CHANNELS = 16;
  const isOnOffChannel = (source, channel) =>
    (source === 'sbus1' || source === 'sbus2') && channel > SBUS_ANALOG_CHANNELS;

  const onOffForChannel = (source, channel) => {
    if (rcSnapshot?.rawDigital && typeof rcSnapshot.rawDigital === 'object') {
      const pair = rcSnapshot.rawDigital[source];
      const on = Array.isArray(pair) ? pair[channel - SBUS_ANALOG_CHANNELS - 1] : undefined;
      return typeof on === 'boolean' ? on : null;
    }
    const found = Object.values(rcSnapshot?.digital || {})
      .find((entry) => entry?.activeSource === source && Number(entry?.bindingChannel) === channel);
    return found ? Boolean(found.pressed) : null;
  };

  // What one RC Channel reads, as the channel list shows it: the number or
  // the on/off word, and how full its bar is.
  const channelReading = (source, channel) => {
    if (isOnOffChannel(source, channel)) {
      const on = onOffForChannel(source, channel);
      return { text: on === null ? '—' : (on ? 'On' : 'Off'), pct: on ? 100 : 0 };
    }
    const raw = rawForChannel(source, channel);
    if (raw == null) return { text: '—', pct: 0 };
    const pct = source === 'pwm'
      ? Math.round(((raw - 1000) / 1000) * 100)
      : Math.round(((raw - 172) / (1811 - 172)) * 100);
    return { text: String(raw), pct: Math.max(0, Math.min(100, pct)) };
  };

  const getChannelTelemetry = (channelKey) => {
    if (!rcSnapshot) return null;
    const { source, channel } = parseChannelKey(channelKey);
    if (!source || channel <= 0 || isDroidSource(source)) return null;
    if (isOnOffChannel(source, channel)) {
      const on = onOffForChannel(source, channel);
      return { raw: on === null ? null : (on ? 'On' : 'Off'), mapped: on ? 1 : 0, pressed: Boolean(on), pressedLevel: Boolean(on) };
    }
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

  const actionTargetFromToken = (token) => actionTargets.find((item) => item.token === token) || null;

  const actionLabelFromToken = (token) => {
    const found = actionTargetFromToken(token);
    return found ? found.label : token;
  };

  // Whether a Dome Sequence binding points at a sequence the droid no longer
  // has. Only once the droid has listed its own (null: not known yet), so a
  // slow answer never marks one missing that is there.
  const domeSequenceMissing = (payload) => {
    if (!payload || cachedLearnedSequences === null) return false;
    return ![...FACTORY_DOME_SEQUENCES, ...cachedLearnedSequences]
      .some((sequence) => sameSequenceName(sequence.payload, payload));
  };

  // What a binding does, as the table names it: the action, and what it was
  // given. A sequence that is gone stays named, and says so.
  const bindingLabel = (entry) => {
    const token = mapEntryAction(entry);
    const label = actionLabelFromToken(token);
    if (!entry?.payload) return label;
    if (token === 'seq') return `${label} · SE${entry.payload}`;
    if (token === 'puppet_part') return `${label} · ${partLabel(entry.payload)}`;
    if (token === 'dome_seq') {
      return `${label} · ${entry.payload}${domeSequenceMissing(entry.payload) ? ' (missing)' : ''}`;
    }
    return `${label} · ${entry.payload}`;
  };

  // An RC Channel's words are the one table's (data/web_api.js), so this page
  // and the Dashboard's Sequences say a channel the same way (#451).
  const sourceLabel = (source) => window.PAApi.rcSourceLabel(source);

  const MODE_LABEL = {
    standard_pwm: "Standard PWM",
    single_sbus: "Single SBUS",
    dual_sbus: "Dual SBUS",
    elrs: "ELRS, not read yet",
    not_fitted: "Not fitted",
  };

  const modeLabel = (mode) => MODE_LABEL[mode] || mode;

  const channelTitleFromKey = (channelKey) => {
    const condition = droidConditionFor(channelKey);
    if (condition) return condition.label;
    const { source, channel } = parseChannelKey(channelKey);
    return window.PAApi.rcChannelTitle(source, channel);
  };

  // What the droid last said about the Reaction on a condition (the
  // `reactions` array of GET /api/rc and the rc event), or null.
  const reactionStatusFor = (channelKey) => {
    const list = Array.isArray(rcSnapshot?.reactions) ? rcSnapshot.reactions : [];
    return list.find((status) => channelKeyOf(status.source, status.channel) === channelKey) || null;
  };

  // Why a Reaction is not armed: the short word for a row, and for the two
  // the builder can do something about, the move (GLOSSARY.md "Availability
  // Family": every no names the next move). `no-feedback` is this firmware's
  // drive, so its move is another image and the route is Firmware, in the
  // words data/feature_availability.js gives that destination. `no-current`
  // is the drive board's own firmware, which no page here uploads, so it has
  // a move and no route. The two waiting ones ask nothing of anybody.
  const REACTION_REASON = {
    'no-feedback': {
      text: 'No wheel readings in this firmware',
      route: { href: '#firmware', label: 'Open Firmware' },
    },
    'no-current': {
      text: 'No current from this drive',
      note: 'This drive\'s firmware sends no current. One that does will arm this.',
    },
    'feedback-stale': { text: 'Waiting for the drive' },
    'no-play-state': { text: 'Waiting for the sound module' },
    // The gate: the droid can sense the condition and is holding every
    // Reaction back. Never Armed, never green, while one of these is on.
    estop: { text: 'Held: estop is on' },
    sleep: { text: 'Held: asleep' },
    'radio-lost': { text: 'Held: radio lost' },
  };

  // A Reaction's state. Armed is a Health Signal and lights green; held by the
  // estop, Sleep or a lost radio, its lamp is unlit. Not armed is an
  // Availability Family, told apart by treatment and never by hue: waiting
  // keeps an unlit lamp, and a settled no has none.
  const reactionState = (channelKey) => {
    const status = reactionStatusFor(channelKey);
    if (!status) return null;
    const refused = Number(status.refusedWhileDriving || 0);
    const held = refused > 0 ? ` · held back ${refused} while driving` : '';
    if (status.state === 'ready') {
      const fires = Number(status.fires || 0);
      return { lamp: 'indicator ok', family: '', text: `${fires > 0 ? `Armed · fired ${fires}` : 'Armed'}${held}`, refused };
    }
    const reason = REACTION_REASON[status.reason] || { text: 'Not armed' };
    const waiting = status.state === 'waiting';
    const settled = status.state === 'not-in-this-build';
    return {
      lamp: settled ? '' : 'indicator',
      family: waiting ? 'availability-waiting' : (settled ? 'availability-settled-no' : ''),
      text: `${reason.text}${held}`,
      note: reason.note || '',
      route: reason.route || null,
      refused,
    };
  };

  const reactionStateHtml = (channelKey) => {
    const state = reactionState(channelKey);
    if (!state) return '<span class="rc-trigger-state waiting"></span>';
    const lamp = state.lamp ? `<span class="${state.lamp}" aria-hidden="true"></span>` : '';
    return `<span class="rc-trigger-state">${lamp}${window.PAUtils.escapeHtml(state.text)}</span>`;
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

  // A puppet string's share of the throw: only the positive half moves its
  // Part (include/rc_puppet.h), so the bar fills from closed at the left.
  const puppetBarHtml = (mapped) => {
    const pct = Math.round(Math.max(0, Math.min(1, Number(mapped))) * 100);
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
    return pressed
      ? '<span class="rc-trigger-state"><span class="indicator ok" aria-hidden="true"></span>Pressed</span>'
      : '<span class="rc-trigger-state"><span class="indicator" aria-hidden="true"></span>Released</span>';
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
      // A receiver being heard or not is a Health Signal, so it reads as a
      // droid LED and the color IS the reading (GLOSSARY.md "Health Signal"):
      // linked is nominal, waiting is degraded and something the builder can
      // act on, and a source nobody switched on is unlit rather than green.
      // These three rows used to be three words on three plain plates, with
      // two dead [data-state] rules in the stylesheet that nothing ever set.
      const state = !enabled ? "disabled" : linked ? "linked" : "waiting";
      const lamp = !enabled ? "off" : linked ? "ok" : "warn";
      // An SBUS receiver's frame rate, beside its link: a receiver heard at
      // 7 frames a second is linked and still too slow to drive on.
      const frames = typeof src.framesPerSecond === "number" ? ` · ${src.framesPerSecond} frames/s` : "";
      const reading = !enabled ? "not switched on" : `${state}${frames} · ${age}ms old`;
      return `<div class="health-item">
        <div class="indicator ${lamp}" aria-hidden="true"></div>
        <span>${window.PAUtils.escapeHtml(name.toUpperCase())}</span>
        <span class="indicator-text">${window.PAUtils.escapeHtml(reading)}</span>
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

  // The Bindings head's subtitle, counted from the map the table is drawn from
  // rather than typed into the markup (ADR 0066, docs/ui-copy-voice.md rule 8).
  const setSummaryCount = (count) => {
    if (!rcSummaryCount) return;
    rcSummaryCount.textContent = count === 0 ? "nothing mapped yet" : `${count} mapped`;
  };

  // How many of the RC Map's trigger bindings are used, up front where a
  // source is picked (operator, 2026-10-09 on #389). GET /api/rc/map's
  // capacity counts the three axes too, which have places of their own, so
  // the count is of everything else against what is left of the total. Said
  // only once the droid has given a total.
  let mapCapacityTotal = null;
  const paintCapacity = () => {
    if (!rcCapacity) return;
    // Called once the map has answered: a droid that gives no total leaves
    // the head empty, never waiting dots that do not resolve.
    rcCapacity.classList.remove('waiting');
    if (!Number.isFinite(mapCapacityTotal) || mapCapacityTotal <= ANALOG_AXIS_TOKENS.size) {
      rcCapacity.textContent = '';
      return;
    }
    const used = asMapArray().filter((entry) => !ANALOG_AXIS_TOKENS.has(mapEntryAction(entry))).length;
    rcCapacity.textContent = `${used} of ${mapCapacityTotal - ANALOG_AXIS_TOKENS.size} used`;
  };

  // The Live cell of one binding: a stick's travel, a switch's press, or what
  // a Reaction is doing.
  const liveCellHtml = (token, channelKey, telemetry) => {
    if (droidConditionFor(channelKey)) return reactionStateHtml(channelKey);
    if (token === 'puppet_part') return puppetBarHtml(telemetry?.mapped || 0);
    return isAnalogAction(token)
      ? miniBarHtml(telemetry?.mapped || 0)
      : triggerStateHtml(token, channelKey, telemetry);
  };

  const renderSummaryTable = () => {
    if (!rcSummaryBody) return;
    const rows = asMapArray()
      .slice()
      .sort((a, b) => (a.source === b.source ? a.channel - b.channel : a.source.localeCompare(b.source)));

    setSummaryCount(rows.length);

    if (!rows.length) {
      rcSummaryBody.innerHTML =
        '<tr><td colspan="3"><b>Nothing is mapped yet.</b> Pick a source below and say what it should do.</td></tr>';
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
      const live = liveCellHtml(token, channelKey, telemetry);
      return `<tr data-chkey="${window.PAUtils.escapeHtml(channelKey)}" class="${selectedChannel === channelKey ? 'active-channel' : ''}">
        <td>${window.PAUtils.escapeHtml(bindingLabel(entry))}</td>
        <td>${window.PAUtils.escapeHtml(channelTitleFromKey(channelKey))}</td>
        <td>${live}</td>
      </tr>`;
    }).join('');
  };

  // ── The three axes: their ends, their direction, and the boot hold ──────
  //
  // Drive speed, drive steer and dome speed each read one stick between a MIN,
  // a CENTER and a MAX, optionally reversed. GET /api/rc carries them in
  // mappingProfile.channels; POST /api/rc/map takes them in `calibration`
  // beside the map, keyed by the action token, for an axis the same map binds
  // (#389, src/web/api_rc_map_apply.cpp). An end is set from the stick: the
  // live reading the page already shows, never a typed number.
  const AXES = [
    { token: 'drive_speed', profile: 'driveSpeed' },
    { token: 'drive_steer', profile: 'driveSteer' },
    { token: 'dome_speed', profile: 'domeSpeed' },
  ];
  const AXIS_ENDS = [
    { key: 'min', label: 'MIN' },
    { key: 'center', label: 'CENTER' },
    { key: 'max', label: 'MAX' },
  ];
  const AXIS_ORDER_TEXT = {
    min: 'Not saved: MIN must read below CENTER.',
    center: 'Not saved: CENTER must read between MIN and MAX.',
    max: 'Not saved: MAX must read above CENTER.',
  };
  const AXIS_DEADBAND_TEXT = 'Not saved: CENTER sits too close to an end.';
  const rcAxes = document.getElementById('rc-axes');
  const rcAxesSummary = document.getElementById('rc-axes-summary');
  const rcDriveHold = document.getElementById('rc-drive-hold');
  // One save at a time, and what each axis last said about its own.
  let axisSaveInFlight = false;
  // A map save or clear is in flight, read-back included. A stick Set posts
  // the whole map beside its calibration, so it waits for the stored map, and
  // Apply waits for a Set: neither may post the map the other is replacing
  // (#483 review).
  let mapWriteInFlight = false;
  const axisNotes = {};

  const axisBinding = (axis) => asMapArray().find((entry) => mapEntryAction(entry) === axis.token) || null;

  // The ends the droid holds for this axis, or null when its profile is for
  // another channel than the map binds (not read back yet).
  const axisEnds = (axis, binding) => {
    const profile = rcSnapshot?.mappingProfile?.channels?.[axis.profile];
    if (!binding || !profile || profile.source !== binding.source || Number(profile.channel) !== binding.channel) return null;
    return { min: Number(profile.min), center: Number(profile.center), max: Number(profile.max), deadband: Number(profile.deadband) || 0, reverse: Boolean(profile.reverse) };
  };

  // The stick's live reading, or null with none (a PWM pulse of 0 is none).
  const axisLive = (binding) => {
    if (!binding || isOnOffChannel(binding.source, binding.channel)) return null;
    // Only a receiver that is heard and not in failsafe gives a reading to
    // set an end from: a cached or held number is not where the stick is.
    const health = rcSnapshot?.sources?.[binding.source];
    if (!health?.linked || health?.failsafe) return null;
    const raw = rawForChannel(binding.source, binding.channel);
    if (raw == null || (binding.source === 'pwm' && Number(raw) === 0)) return null;
    return Number(raw);
  };

  // A stick read past an end maps to full travel there - a HotRC trigger
  // resting at its end reads as full throttle and keeps the boot hold on.
  const axisRestWarning = (ends, raw) => {
    if (!ends || raw == null) return '';
    if (raw < ends.min) return 'Past MIN, so it reads as full travel.';
    if (raw > ends.max) return 'Past MAX, so it reads as full travel.';
    return '';
  };

  // The body POST /api/rc/map takes to change one axis: the map the droid
  // holds, unchanged, and the axis's new fields. null when that map does not
  // bind the axis, which the droid would refuse.
  const axisCalibrationBody = (mapEntries, token, fields) => {
    if (!mapEntries.some((entry) => mapEntryAction(entry) === token)) return null;
    return { map: mapEntries, calibration: { [token]: fields } };
  };

  // The droid's refusals of a calibration, in the builder's words.
  const axisRefusalText = (error) => {
    const message = window.PAApi.messageFor(error);
    if (/min < center < max/.test(message)) return 'Not saved: MIN, CENTER and MAX must rise in that order.';
    if (/out of range/.test(message)) return 'Not saved: that reading is out of range.';
    if (/does not bind/.test(message)) return 'Not saved: map this axis first.';
    if (/no travel past the deadband/.test(message)) return AXIS_DEADBAND_TEXT;
    if (DRIVE_SPLIT_REFUSAL.test(message)) return DRIVE_SPLIT_TEXT;
    if (DRIVE_SBUS2_REFUSAL.test(message)) return DRIVE_SBUS2_TEXT;
    return `Not saved: ${message}`;
  };

  const axisEndsText = (ends) => (ends
    ? `MIN ${ends.min} · CENTER ${ends.center} · MAX ${ends.max}`
    : 'Ends not read yet.');

  const axisTileHtml = (axis) => {
    const esc = window.PAUtils.escapeHtml;
    const name = actionLabelFromToken(axis.token);
    const binding = axisBinding(axis);
    if (!binding) {
      return `<div class="rc-axis" data-axis="${axis.token}">
        <div class="rc-axis-head"><span class="rc-axis-name">${esc(name)}</span></div>
        <p class="hint">Not mapped.</p>
      </div>`;
    }
    // A drive stored on SBUS2 before #483: the droid does not read it.
    if (driveOnSbus2(axis.token, channelKeyOf(binding.source, binding.channel))) {
      return `<div class="rc-axis" data-axis="${axis.token}">
        <div class="rc-axis-head"><span class="rc-axis-name">${esc(name)}</span>
          <span class="rc-axis-ch">${esc(channelTitleFromKey(channelKeyOf(binding.source, binding.channel)))}</span></div>
        <p class="rc-axis-warn" role="status">Not read: Speed and Steer read SBUS1. Map it there.</p>
      </div>`;
    }
    const ends = axisEnds(axis, binding);
    const raw = axisLive(binding);
    const idle = axisSaveInFlight || mapWriteInFlight || !channelMapLoaded;
    const note = axisNotes[axis.token];
    const switchId = `rc-axis-rev-${axis.token}`;
    const sets = AXIS_ENDS.map((end) => `<button class="cal-set" type="button" data-axis="${axis.token}" data-axis-set="${end.key}"${idle || raw == null ? ' disabled' : ''}>Set ${end.label}</button>`).join('');
    return `<div class="rc-axis" data-axis="${axis.token}">
      <div class="rc-axis-head">
        <span class="rc-axis-name">${esc(name)}</span>
        <span class="rc-axis-ch">${esc(channelTitleFromKey(channelKeyOf(binding.source, binding.channel)))}</span>
        <span class="rc-axis-raw cal-readout">${raw == null ? '—' : raw}</span>
      </div>
      <div class="seg cal-sets" role="group" aria-label="Set an end of ${esc(name)} from the stick">${sets}</div>
      <p class="cal-ends">${esc(axisEndsText(ends))}</p>
      <div class="rc-axis-reverse">
        <button class="sleep-switch" id="${switchId}" type="button" role="switch" aria-checked="${ends?.reverse ? 'true' : 'false'}" aria-labelledby="${switchId}-label" data-axis="${axis.token}" data-axis-reverse${idle || !ends ? ' disabled' : ''}><span class="sleep-switch-knob"></span></button>
        <span id="${switchId}-label">Reversed</span>
      </div>
      <p class="rc-axis-warn" role="status">${esc(axisRestWarning(ends, raw))}</p>
      <p class="cal-note${note ? ` ${note.kind}` : ''}" role="status" aria-live="polite">${esc(note?.text || '')}</p>
    </div>`;
  };

  // RC drive holds at zero from boot until both drive sticks have been at
  // center once (GET /api/rc driveAwaitingCentre). Empty, the line is not drawn.
  const paintDriveHold = () => {
    if (!rcDriveHold) return;
    rcDriveHold.textContent = rcSnapshot?.driveAwaitingCentre === true ? 'Center both drive sticks to drive.' : '';
  };

  const renderAxes = () => {
    paintDriveHold();
    if (rcAxesSummary) {
      const bound = AXES.filter((axis) => axisBinding(axis)).length;
      rcAxesSummary.textContent = channelMapLoaded ? `${bound} of ${AXES.length} mapped` : '';
    }
    if (rcAxes) rcAxes.innerHTML = AXES.map(axisTileHtml).join('');
  };

  // A stream frame: the readings and the warnings, without drawing the tiles
  // again under a builder's pointer or focus.
  const updateAxesLive = () => {
    paintDriveHold();
    if (!rcAxes) return;
    AXES.forEach((axis) => {
      const tile = rcAxes.querySelector(`.rc-axis[data-axis="${axis.token}"]`);
      const binding = axisBinding(axis);
      if (!tile || !binding) return;
      const raw = axisLive(binding);
      const rawEl = tile.querySelector('.rc-axis-raw');
      if (rawEl) rawEl.textContent = raw == null ? '—' : String(raw);
      const warnEl = tile.querySelector('.rc-axis-warn');
      if (warnEl) warnEl.textContent = axisRestWarning(axisEnds(axis, binding), raw);
      tile.querySelectorAll('[data-axis-set]').forEach((button) => {
        button.disabled = axisSaveInFlight || mapWriteInFlight || !channelMapLoaded || raw == null;
      });
    });
  };

  // One change to one axis, saved at once and read back.
  const saveAxis = async (axis, fields, said) => {
    const body = axisCalibrationBody(asMapArray(), axis.token, fields);
    if (!body) {
      axisNotes[axis.token] = { kind: 'error', text: 'Not saved: map this axis first.' };
      renderAxes();
      return;
    }
    axisSaveInFlight = true;
    axisNotes[axis.token] = { kind: '', text: 'Saving...' };
    renderAxes();
    try {
      await window.PAApi.postForm('/api/rc/map', { plain: JSON.stringify(body) }, { timeoutMs: 5000 });
      axisNotes[axis.token] = { kind: 'success', text: `Saved ${said}.` };
    } catch (error) {
      axisNotes[axis.token] = { kind: 'error', text: axisRefusalText(error) };
      axisSaveInFlight = false;
      renderAxes();
      return;
    }
    axisSaveInFlight = false;
    try {
      // The ends the droid now holds come back with its diagnostics.
      await loadRcDiagnostics();
    } catch (_error) {
      // loadRcDiagnostics() has said so in the editor feedback; the tile
      // keeps its saved note and the ends it last read.
      renderAxes();
    }
  };

  const setAxisEnd = (axis, key) => {
    const binding = axisBinding(axis);
    const raw = axisLive(binding);
    const end = AXIS_ENDS.find((each) => each.key === key);
    if (!end) return;
    if (raw == null) {
      axisNotes[axis.token] = { kind: 'error', text: 'No reading from this stick.' };
      renderAxes();
      return;
    }
    // The order the droid keeps, checked here so a capture the droid would
    // refuse is never sent.
    const ends = axisEnds(axis, binding);
    if (ends) {
      const next = { ...ends, [key]: raw };
      if (!(next.min < next.center && next.center < next.max)) {
        axisNotes[axis.token] = { kind: 'error', text: AXIS_ORDER_TEXT[key] };
        renderAxes();
        return;
      }
      // A side shorter than the dead zone would move nothing; the droid
      // refuses it too (src/web/api_rc_map_apply.cpp).
      if (next.center - next.min <= next.deadband || next.max - next.center <= next.deadband) {
        axisNotes[axis.token] = { kind: 'error', text: AXIS_DEADBAND_TEXT };
        renderAxes();
        return;
      }
    }
    saveAxis(axis, { [key]: raw }, `${end.label} ${raw}`);
  };

  const setAxisReverse = (axis) => {
    const ends = axisEnds(axis, axisBinding(axis));
    if (!ends) return;
    const reverse = !ends.reverse;
    saveAxis(axis, { reverse }, reverse ? 'reversed' : 'not reversed');
  };

  // One listener for every tile: the tiles are drawn again whole.
  rcAxes?.addEventListener('click', (event) => {
    const target = event.target;
    const set = target?.closest?.('[data-axis-set]');
    const reverse = set ? null : target?.closest?.('[data-axis-reverse]');
    const control = set || reverse;
    if (!control || control.disabled || axisSaveInFlight || mapWriteInFlight) return;
    const axis = AXES.find((each) => each.token === control.dataset.axis);
    if (!axis) return;
    if (set) setAxisEnd(axis, control.dataset.axisSet);
    else setAxisReverse(axis);
  });

  const renderChannelList = () => {
    if (!rcChannelItems) return;
    const mode = getEditorMode();

    const renderGroup = (title, source, channelCount) => {
      const items = [];
      for (let i = 1; i <= channelCount; i++) {
        const channelKey = channelKeyOf(source, i);
        const reading = channelReading(source, i);
        const entry = assignmentForChannel(channelKey);
        const actionLabel = entry ? actionLabelFromToken(mapEntryAction(entry)) : null;
        const isActive = channelKey === selectedChannel;
        items.push(`<div class="rc-channel-item${isActive ? ' active' : ''}" data-chkey="${window.PAUtils.escapeHtml(channelKey)}" role="button" tabindex="0" aria-pressed="${isActive}">
          <div class="rc-channel-item-head">
            <span class="rc-ch-num">CH ${i}</span>
            <span class="rc-ch-raw">${window.PAUtils.escapeHtml(reading.text)}</span>
          </div>
          <div class="rc-channel-mini-bar"><div class="rc-channel-mini-fill" style="--pct:${reading.pct}%"></div></div>
          <div class="rc-ch-action">${actionLabel ? window.PAUtils.escapeHtml(actionLabel) : '<span class="rc-ch-unassigned">not mapped</span>'}</div>
        </div>`);
      }
      return `<div class="rc-channel-group">
        <div class="rc-channel-group-title">${window.PAUtils.escapeHtml(title)}</div>
        <div class="rc-channel-group-items">${items.join('')}</div>
      </div>`;
    };

    // The droid's own conditions, the same in every mode. A bound one shows
    // its state where a channel shows its raw value; one the droid cannot
    // sense on this build is drawn in its Availability Family, not as armed.
    const renderDroidGroup = () => {
      const items = DROID_CONDITIONS.map((condition) => {
        const channelKey = channelKeyOf(condition.source, condition.channel);
        const entry = assignmentForChannel(channelKey);
        const state = entry ? reactionState(channelKey) : null;
        const isActive = channelKey === selectedChannel;
        return `<div class="rc-channel-item${isActive ? ' active' : ''}${state?.family ? ` ${state.family}` : ''}" data-chkey="${window.PAUtils.escapeHtml(channelKey)}" role="button" tabindex="0" aria-pressed="${isActive}">
          <div class="rc-channel-item-head">
            <span class="rc-ch-num">${window.PAUtils.escapeHtml(condition.label)}</span>
          </div>
          <div class="rc-ch-action">${entry ? window.PAUtils.escapeHtml(bindingLabel(entry)) : '<span class="rc-ch-unassigned">not mapped</span>'}</div>
          ${state ? `<div class="rc-ch-state">${window.PAUtils.escapeHtml(state.text)}</div>` : ''}
        </div>`;
      });
      return `<div class="rc-channel-group">
        <div class="rc-channel-group-title">Droid</div>
        <div class="rc-channel-group-items">${items.join('')}</div>
        <p class="hint">The droid cannot sense the room.</p>
      </div>`;
    };

    let html = '';
    if ((SOURCE_OPTIONS[mode] || []).length === 0) {
      // A receiver the controller reads nothing from (ELRS, #369), or none
      // fitted: no radio channel arrives, so none is offered to map. The
      // droid's own conditions below still are.
      html = `<p class="hint">${window.PAUtils.escapeHtml(modeLabel(mode))}: no radio channel arrives.</p>`;
    } else {
      html = SOURCE_OPTIONS[mode]
        .map((source) => renderGroup(source.toUpperCase(), source, source === 'pwm' ? 6 : SBUS_CHANNELS))
        .join('');
    }
    html += renderDroidGroup();

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
    renderAxes();
  };

  // Cheap live update: refresh raw values and bars without full re-render.
  const updateChannelListRaw = () => {
    if (!rcChannelItems || !rcSnapshot?.raw) return;
    rcChannelItems.querySelectorAll('.rc-channel-item').forEach(el => {
      if (droidConditionFor(el.dataset.chkey)) {
        const state = assignmentForChannel(el.dataset.chkey) ? reactionState(el.dataset.chkey) : null;
        const stateEl = el.querySelector('.rc-ch-state');
        if (stateEl && state) stateEl.textContent = state.text;
        el.classList.toggle('availability-waiting', state?.family === 'availability-waiting');
        el.classList.toggle('availability-settled-no', state?.family === 'availability-settled-no');
        return;
      }
      const { source, channel } = parseChannelKey(el.dataset.chkey);
      const reading = channelReading(source, channel);
      const rawEl = el.querySelector('.rc-ch-raw');
      if (rawEl) rawEl.textContent = reading.text;
      const fillEl = el.querySelector('.rc-channel-mini-fill');
      if (fillEl) fillEl.style.setProperty('--pct', `${reading.pct}%`);
    });
  };

  const renderLivePreview = () => {
    if (!rcLivePreviewContent) return;
    renderSourceHealth();

    if (!selectedChannel) {
      rcLivePreviewContent.innerHTML =
        '<p class="note"><b>Nothing selected.</b> Pick a source below.</p>';
      return;
    }

    const entry = assignmentForChannel(selectedChannel) || { ...parseChannelKey(selectedChannel), payload: '' };

    if (droidConditionFor(selectedChannel)) {
      const bound = Boolean(mapEntryAction(entry));
      const state = bound ? reactionState(selectedChannel) : null;
      const route = state?.route
        ? `<a class="setup-link" href="${state.route.href}">${window.PAUtils.escapeHtml(state.route.label)}.</a>`
        : '';
      rcLivePreviewContent.innerHTML = `
        <h4 class="rc-preview-title">${window.PAUtils.escapeHtml(channelTitleFromKey(selectedChannel))}</h4>
        <div class="rc-preview-stack">
          <div>Action: <strong>${bound ? window.PAUtils.escapeHtml(bindingLabel(entry)) : 'Not mapped'}</strong></div>
          ${bound ? `<div>State: ${reactionStateHtml(selectedChannel)}</div>` : ''}
          ${state?.note || route ? `<p class="note">${window.PAUtils.escapeHtml(state.note)}${state.note && route ? ' ' : ''}${route}</p>` : ''}
          ${state?.refused > 0 ? '<p class="note">No body part opens while the droid drives.</p>' : ''}
        </div>`;
      return;
    }

    const telemetry = getChannelTelemetry(selectedChannel);
    const mapped = Number(telemetry?.mapped || 0);
    const raw = telemetry?.raw ?? '—';
    const actionToken = mapEntryAction(entry);

    let barHtml = '';
    if (actionToken === 'puppet_part') {
      const width = Math.round(Math.max(0, Math.min(1, mapped)) * 100);
      barHtml = `<div class="rc-preview-bar"><div class="rc-preview-fill" style="--bar-width:${width}%"></div></div>`;
    } else if (isAnalogAction(actionToken)) {
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
        ${actionToken === 'puppet_part' ? `<div>Throw: <strong>${Math.round(Math.max(0, Math.min(1, mapped)) * 100)}%</strong></div>${barHtml}`
          : isAnalogAction(actionToken) ? `<div>Mapped: <strong>${mapped.toFixed(3)}</strong></div>${barHtml}` : `<div>State: ${triggerStateHtml(actionToken, selectedChannel, telemetry)}</div>`}
      </div>`;
  };

  // What the selected source may be bound to. A radio channel: everything. A
  // droid condition: no axis, and not the three a Reaction may not do.
  const actionAllowedOnSelected = (item) => {
    // A puppet string moves only from an SBUS stick channel: PWM input runs no
    // string, and CH17/CH18 are on/off (rcPuppetChannelCanMove()).
    if (item.token === 'puppet_part') {
      const { source, channel } = parseChannelKey(selectedChannel);
      return getEditorMode() !== 'standard_pwm' && (source === 'sbus1' || source === 'sbus2')
        && channel >= 1 && channel <= 16;
    }
    // The other drive axis is on another receiver: this one goes beside it.
    if (driveSplitWith(channelMap, item.token, selectedChannel)) return false;
    if (driveOnSbus2(item.token, selectedChannel)) return false;
    if (!droidConditionFor(selectedChannel)) return true;
    return !ANALOG_ACTION_TOKENS.has(item.token) && !REACTION_BLOCKED_TOKENS.has(item.token);
  };

  const groupedActionTargets = () => {
    const groups = new Map();
    actionTargets.filter(actionAllowedOnSelected).forEach((item) => {
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
        <span class="rc-action-radio" aria-hidden="true"></span>
        <span class="rc-action-main">
          <span class="rc-action-label">${window.PAUtils.escapeHtml(item.label)}</span>
          <span class="rc-action-desc">${window.PAUtils.escapeHtml(item.description || 'No description available.')}</span>
        </span>
      </button>
      <span class="rc-action-side">
        ${showSafetyPill ? '<span class="rc-action-safety-pill">Safety critical</span>' : ''}
        ${showTestButton ? `<button type="button" class="rc-action-test-btn icon-act act-keeps-words" data-action-test="${window.PAUtils.escapeHtml(item.token)}"${inFlight ? ' disabled' : ''}>${window.PAUi.actFace('play', 'Try it')}</button>` : ''}
        <span class="rc-action-test-feedback${feedbackClass}" data-action-feedback="${window.PAUtils.escapeHtml(item.token)}">${window.PAUtils.escapeHtml(feedbackText || '')}</span>
      </span>
    </div>`;
  };

  // The picker's inside: the search row and the action rows. A search draws
  // only this again, inside a picker node that stays (refreshActionPicker()).
  // The box shows the query as typed and the rows match it trimmed: a box
  // drawn back trimmed ate every space as it was typed.
  const actionPickerBodyHtml = (selectedToken, queryText = '') => {
    const typed = String(queryText || '');
    const query = typed.trim();
    const groups = groupedActionTargets()
      .map(({ name, items }) => ({
        name,
        items: items.filter((item) => actionMatchesQuery(item, query)),
      }))
      .filter(({ items }) => items.length > 0);

    const recentItems = recentActionTokens
      .map((token) => actionTargets.find((item) => item.token === token))
      .filter((item) => Boolean(item) && actionAllowedOnSelected(item) && actionMatchesQuery(item, query));

    const hasMatches = recentItems.length > 0 || groups.length > 0;
    const recentBlock = recentItems.length > 0
      ? `<details class="rc-action-group rc-action-group-recent" open>
          <summary>Recently used</summary>
          <div class="rc-action-group-rows">${recentItems.map((item) => renderActionRow(item, selectedToken)).join('')}</div>
        </details>`
      : '';

    return `<div class="rc-action-search-row">
        <input class="rc-action-search-input" data-action-search type="search" placeholder="Search actions..." value="${window.PAUtils.escapeHtml(typed)}" autocomplete="off">
        <button type="button" class="rc-action-search-clear icon-act" data-action-search-clear${typed ? '' : ' disabled'}>${window.PAUi.actFace('eraser', 'Clear')}</button>
      </div>
      ${recentBlock}
      ${groups.map(({ name, items }) => {
        const openAttr = query ? ' open' : (isActionGroupOpen(name) ? ' open' : '');
        return `<details class="rc-action-group" data-action-group="${window.PAUtils.escapeHtml(name)}"${openAttr}>
          <summary>${window.PAUtils.escapeHtml(name)}</summary>
          <div class="rc-action-group-rows">${items.map((item) => renderActionRow(item, selectedToken)).join('')}</div>
        </details>`;
      }).join('')}
      ${hasMatches ? '' : `<div class="rc-action-empty">No actions match "${window.PAUtils.escapeHtml(query)}".</div>`}`;
  };

  const renderActionPicker = (selectedToken, queryText = '') =>
    `<div class="rc-action-picker" role="listbox" aria-label="Select action" tabindex="0">
      ${actionPickerBodyHtml(selectedToken, queryText)}
    </div>`;

  const collapseExpandedActionGroups = () => {
    rcEditorContent.querySelectorAll('[data-action-group]').forEach((groupNode) => {
      if (groupNode.open) groupNode.open = false;
      actionPickerGroupOpen[groupNode.dataset.actionGroup] = false;
    });
  };

  // The action picker's Escape folds its open groups away, through the one
  // shared guard (data/overlay.js escGuard()), so a question asked over RC
  // takes the key first and the picker never hears it. The key is the
  // picker's only while focus is in it, as when it was read on the picker
  // itself, and only while a group is open to fold; otherwise it passes on.
  // One guard for the surface, bound while an editor is drawn: every
  // renderEditor() replaces the picker node, so its isOpen() looks it up.
  const actionPickerEscape = window.PAOverlay.escGuard(
    () => {
      const picker = rcEditorContent?.querySelector('.rc-action-picker');
      return Boolean(picker?.isConnected
        && picker.contains(document.activeElement)
        && picker.querySelector('[data-action-group][open]'));
    },
    collapseExpandedActionGroups
  );

  // The draft's action: the open editor's hidden target field, which a pick
  // writes and Apply and save reads.
  const draftActionToken = () => rcEditorContent?.querySelector('[data-field="target"]')?.value || '';

  const updateConditionalFields = () => {
    const target = draftActionToken();
    const seq = rcEditorContent.querySelector('[data-cond="seq"]');
    const domeSeq = rcEditorContent.querySelector('[data-cond="dome_seq"]');
    const cmd = rcEditorContent.querySelector('[data-cond="cmd"]');
    const puppet = rcEditorContent.querySelector('[data-cond="puppet_part"]');
    const estop = rcEditorContent.querySelector('[data-cond="estop"]');
    if (seq) seq.className = `rc-editor-cond ${target === 'seq' ? 'block' : 'hidden'}`;
    if (domeSeq) domeSeq.className = `rc-editor-cond ${target === 'dome_seq' ? 'block' : 'hidden'}`;
    if (cmd) cmd.className = `rc-editor-cond ${target === 'cmd' ? 'block' : 'hidden'}`;
    if (puppet) puppet.className = `rc-editor-cond ${target === 'puppet_part' ? 'block' : 'hidden'}`;
    if (estop) estop.className = `rc-editor-cond ${target === 'estop' ? 'block' : 'hidden'}`;
  };

  const refreshActionPickerSelectionUi = () => {
    const selectedToken = draftActionToken();
    rcEditorContent.querySelectorAll('[data-action-token]').forEach((row) => {
      const rowToken = row.dataset.actionToken;
      const selected = rowToken === selectedToken;
      row.classList.toggle('selected', selected);
      row.setAttribute('aria-selected', selected ? 'true' : 'false');
      // The radio is drawn by the stylesheet off the row's own .selected
      // class, so there is nothing to write here any more.
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
      // The refusal's route, where the droid's answer carried one (#348): a
      // "no" that names the builder's next move takes them to it, so the
      // destination is a link rather than a sentence they have to go and find.
      node.textContent = feedback ? feedback.text : '';
      node.className = `rc-action-test-feedback${feedback ? ` ${feedback.kind || ''}` : ''}`;
      if (feedback?.route) {
        node.textContent = `${node.textContent} `;
        const link = document.createElement('a');
        link.className = 'setup-link';
        link.setAttribute('href', feedback.route.href);
        link.textContent = `${feedback.route.label}.`;
        node.appendChild(link);
      }
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
  };

  const runActionTest = async (token) => {
    if (actionPickerInFlightToken) return;
    const channelAtStart = selectedChannel;
    actionPickerInFlightToken = token;
    actionPickerFeedback = { token, kind: 'info', text: 'Testing...' };
    syncActionTestUi();
    try {
      const result = await window.PAApi.postForm('/api/actions/test', { token }, { timeoutMs: 5000 });
      if (selectedChannel !== channelAtStart) return;
      actionPickerFeedback = { token, ...actionTestFeedbackForOutcome(result?.data?.outcome) };
    } catch (error) {
      if (selectedChannel !== channelAtStart) return;
      // A refusal the droid named in its own vocabulary comes back as a
      // sentence and, where there is one, the route to the next move
      // (data/web_api.js). Everything else is the ordinary transport message.
      const refusal = window.PAApi.refusalFor(error);
      actionPickerFeedback = refusal
        ? { token, kind: 'error', text: refusal.text, route: refusal.route }
        : { token, kind: 'error', text: window.PAApi.messageFor(error) };
    } finally {
      if (actionPickerInFlightToken === token) actionPickerInFlightToken = null;
      if (selectedChannel === channelAtStart) syncActionTestUi();
    }
  };

  // The listeners on the picker's inside, wired again every time it is drawn:
  // by renderEditor() and by refreshActionPicker().
  const wireActionPickerBody = (picker) => {
    const searchInput = picker.querySelector('[data-action-search]');
    if (searchInput) {
      searchInput.addEventListener('input', () => {
        const nextQuery = searchInput.value || '';
        if (nextQuery === actionPickerQuery) return;
        actionPickerQuery = nextQuery;
        actionPickerScrollTop = 0;
        refreshActionPicker();
      });
    }

    const clearSearchBtn = picker.querySelector('[data-action-search-clear]');
    if (clearSearchBtn) {
      clearSearchBtn.addEventListener('click', () => {
        if (!actionPickerQuery) return;
        actionPickerQuery = '';
        actionPickerScrollTop = 0;
        refreshActionPicker();
        picker.querySelector('[data-action-search]')?.focus();
      });
    }

    // A search draws every matching group open, and a group drawn open fires
    // its own toggle: only a fold made with no search running is the
    // builder's, so only that one is kept for when the search is cleared.
    picker.querySelectorAll('[data-action-group]').forEach((groupNode) => {
      groupNode.addEventListener('toggle', () => {
        if (actionPickerQuery.trim()) return;
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
  };

  // A search, and the firmware's action list arriving, draw only the picker's
  // inside again, around the draft's action. The editor is never drawn again
  // for either: renderEditor() builds every field from the saved binding, so
  // it would throw away the pick, a payload, the Command text, a threshold and
  // the quiet time while the editor still said "Unsaved changes" (#355).
  // The search box is drawn again too, so a builder typing in it - when the
  // action list arrives mid-word as much as on their own keystroke - gets the
  // focus and the caret back at the end of what they typed.
  const refreshActionPicker = () => {
    const picker = rcEditorContent?.querySelector('.rc-action-picker');
    if (!picker) return;
    const searchHadFocus = Boolean(document.activeElement?.matches('[data-action-search]')
      && picker.contains(document.activeElement));
    picker.innerHTML = actionPickerBodyHtml(draftActionToken(), actionPickerQuery);
    wireActionPickerBody(picker);
    refreshActionPickerSelectionUi();
    syncActionTestUi();
    if (searchHadFocus) {
      const searchInput = picker.querySelector('[data-action-search]');
      if (searchInput) {
        searchInput.focus();
        searchInput.setSelectionRange(actionPickerQuery.length, actionPickerQuery.length);
      }
    }
  };

  const renderEditor = () => {
    if (!rcEditorContent) return;
    if (!selectedChannel) {
      actionPickerEscape.unbind();
      actionPickerFeedback = null;
      actionPickerInFlightToken = null;
      actionPickerLastChannel = null;
      rcEditorContent.innerHTML = '';
      if (rcEditorApply) rcEditorApply.disabled = true;
      if (rcEditorRevert) rcEditorRevert.disabled = true;
      setEditorDirtyState('clean', 'Pick a source to edit');
      return;
    }

    // Which groups are open is kept by the groups' own toggle listener
    // (wireActionPickerBody()), so it is not read back off the old picker
    // here: under a search every group is drawn open.
    if (actionPickerLastChannel !== selectedChannel) {
      actionPickerFeedback = null;
      actionPickerInFlightToken = null;
      actionPickerScrollTop = 0;
      actionPickerQuery = '';
      Object.keys(actionPickerGroupOpen).forEach((group) => delete actionPickerGroupOpen[group]);
      actionPickerLastChannel = selectedChannel;
    } else {
      const previousPicker = rcEditorContent.querySelector('.rc-action-picker');
      if (previousPicker) actionPickerScrollTop = previousPicker.scrollTop;
    }

    const { source, channel } = parseChannelKey(selectedChannel);
    const entry = assignmentForChannel(selectedChannel) || { source, channel, payload: '' };
    const displayToken = mapEntryAction(entry);

    // A droid condition has no channel to name. It has a threshold, where the
    // condition takes one, and how long it stays quiet after firing.
    const condition = droidConditionFor(selectedChannel);
    const numberField = (field, label, value, unit, min, step, max) => `<label class="rc-reaction-field">
          <span>${window.PAUtils.escapeHtml(label)}</span>
          <input data-field="${field}" type="number" inputmode="decimal" min="${min}" max="${max}" step="${step}" value="${value}">
          <span class="rc-reaction-unit">${window.PAUtils.escapeHtml(unit)}</span>
        </label>`;
    const threshold = condition?.threshold || null;
    const reactionFields = condition
      ? `<div class="rc-reaction-fields">
        ${threshold ? numberField('threshold', threshold.label,
          (entry.threshold ?? threshold.fallback) / threshold.scale, threshold.unit,
          threshold.min / threshold.scale, 1 / threshold.scale, threshold.max / threshold.scale) : ''}
        ${numberField('quietS', 'Quiet after firing', entry.quietS ?? REACTION_QUIET.fallback, 's', REACTION_QUIET.min, 1, REACTION_QUIET.max)}
      </div>`
      : '';

    rcEditorContent.innerHTML = `
      <h4 class="rc-editor-title">Edit ${window.PAUtils.escapeHtml(channelTitleFromKey(selectedChannel))}</h4>
      ${reactionFields}
      <label class="rc-action-label-head">Action</label>
      <input data-field="target" type="hidden" value="${window.PAUtils.escapeHtml(displayToken)}">
      ${renderActionPicker(displayToken, actionPickerQuery)}
      <div data-cond="seq" class="rc-editor-cond ${displayToken === 'seq' ? 'block' : 'hidden'}">
        <span class="rc-action-label-head">Marcduino Sequence</span>
        ${marcduinoSequencePills(entry.payload)}
      </div>
      <div data-cond="dome_seq" class="rc-editor-cond ${displayToken === 'dome_seq' ? 'block' : 'hidden'}">
        <span class="rc-action-label-head">Dome Sequence</span>
        ${domeSequencePills(entry.payload)}
      </div>
      <div data-cond="puppet_part" class="rc-editor-cond ${displayToken === 'puppet_part' ? 'block' : 'hidden'}">
        <span class="rc-action-label-head">Part</span>
        ${puppetPartPills(entry.payload)}
      </div>
      <div data-cond="cmd" class="rc-editor-cond ${displayToken === 'cmd' ? 'block' : 'hidden'}">
        <label class="rc-reaction-field">
          <span>Marcduino Command</span>
          <input data-field="payload" type="text" value="${window.PAUtils.escapeHtml(entry.payload || '')}" placeholder=":OP01">
        </label>
      </div>
      <div data-cond="estop" class="rc-editor-cond ${displayToken === 'estop' ? 'block' : 'hidden'}">
        <label><input data-field="estop-confirm" type="checkbox"> I understand this latches estop.</label>
      </div>
      <div class="rc-editor-unmap"><button type="button" class="btn btn-sm btn-quiet icon-act" data-action-unmap>${window.PAUi.actFace('link-variant-off', 'Unmap')}</button></div>`;

    wirePayloadPills(rcEditorContent);

    rcEditorContent.querySelectorAll('[data-field]').forEach((field) => {
      field.addEventListener('change', () => {
        markEditorDirty();
        if (field.dataset.field === 'target') updateConditionalFields();
      });
      field.addEventListener('input', () => {
        markEditorDirty();
      });
    });

    const unmapBtn = rcEditorContent.querySelector('[data-action-unmap]');
    if (unmapBtn) {
      unmapBtn.addEventListener('click', () => setActionToken(''));
    }

    // The picker node lives as long as this editor; a search draws only its
    // inside again, so its own listeners are wired once here.
    const picker = rcEditorContent.querySelector('.rc-action-picker');
    if (picker) {
      picker.addEventListener('scroll', () => {
        actionPickerScrollTop = picker.scrollTop;
      });

      wireActionPickerBody(picker);

      picker.addEventListener('keydown', (event) => {
        if (event.target instanceof Element && event.target.closest('.rc-action-test-btn')) return;
        if (event.target instanceof Element && event.target.closest('.rc-action-search-input')) return;
        if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp' && event.key !== 'Enter') return;
        event.preventDefault();

        const selectable = actionTargets
          .filter((item) => !item.disabled && actionAllowedOnSelected(item) && actionMatchesQuery(item, actionPickerQuery.trim()))
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
    actionPickerEscape.bind();
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
      cell.innerHTML = liveCellHtml(token, channelKey, telemetry);
    });
  };

  // Which receiver this page is about, painted in one place: the section head
  // that names it. The receiver is chosen on Configuration's Radio Controller
  // cards (#369); this page reads it and never writes it.
  // The receiver type the droid started with, from rc.activeInputMode, else
  // from GET /api/rc's own `mode`. null until one of them has answered: a
  // firmware that says neither leaves nothing read as waiting.
  let configRunningMode = null;
  const runningMode = () => configRunningMode || (typeof rcSnapshot?.mode === 'string' ? rcSnapshot.mode : null);

  const paintModeSelection = (mode) => {
    const running = runningMode();
    const waiting = Boolean(running) && running !== mode;
    if (rcModeSummary) {
      rcModeSummary.textContent = waiting ? `${modeLabel(mode)} saved · ${modeLabel(running)} running` : modeLabel(mode);
    }
    // Configuration's own words for a restart-required change still waiting
    // (data/apply_timing.js), with its route to Maintenance.
    if (rcModeWaiting) {
      if (waiting && window.PAApplyTiming) {
        window.PAApplyTiming.paint(rcModeWaiting, window.PAApplyTiming.RESTART_REQUIRED, { pending: true });
      } else {
        rcModeWaiting.textContent = '';
        rcModeWaiting.classList.add('hidden');
      }
    }
  };

  const switchRcMode = (mode) => {
    if (rcInputModeHidden) rcInputModeHidden.value = mode;
    paintModeSelection(mode);
    updateRecvSel(mode);
    selectedChannel = null;
    document.querySelectorAll('.rc-channel-item').forEach((el) => el.classList.remove('active'));
    renderSummaryTable();
    renderChannelList();
    renderLivePreview();
    renderEditor();
  };

  // The radio and the receiver the droid holds, each as the card Configuration
  // shows it - its photo and its name, drawn by data/component_picker.js from
  // the same lineup, so there is no second product-to-picture map here. Chosen
  // only on Configuration; a family with nothing picked says where to pick it.
  // Until the droid has answered both the lineup and the config, a card shows
  // the waiting dots: a null then means "not known yet", never "none picked",
  // and saying the second would be a false state for one load cycle. A droid
  // with no radio fitted has answered, and is not "not picked yet" either.
  const paintProductCards = () => {
    const picker = window.ComponentPicker;
    if (!picker) return;
    const notFitted = picker.answered() && picker.isRadioNotFitted();
    const show = (host, part, missing) => {
      if (!host) return;
      if (!picker.answered()) {
        host.innerHTML = '<p class="hint waiting"></p>';
      } else if (notFitted) {
        host.innerHTML = '<p class="hint">Not fitted. Change it in <a class="setup-link" href="#configuration">Configuration</a>.</p>';
      } else if (part) {
        host.replaceChildren(picker.shownCard(part));
      } else {
        host.innerHTML = `<p class="hint">${missing} Pick it in <a class="setup-link" href="#configuration">Configuration</a>.</p>`;
      }
    };
    show(rcRadioCard, picker.chosenPart("radio_controller"), "No radio picked yet.");
    show(rcReceiverCard, picker.chosenReceiverPart(), "No RC Receiver picked yet.");
  };
  window.ComponentPicker?.onChange(paintProductCards);

  const loadRcMode = async ({ handle = null } = {}) => {
    try {
      const api = handle || window.PAApi;
      const result = await api.get('/api/config');
      const data = result.data;
      const mode = getRcModeFromConfig(data);
      if (typeof data?.rc?.activeInputMode === 'string') configRunningMode = data.rc.activeInputMode;
      if (rcInputModeHidden) rcInputModeHidden.value = mode;
      paintModeSelection(mode);
      confirmedSbusRecvCh2 = getSingleSbusRecvCh2(data);
      paintSbusRecv(confirmedSbusRecvCh2);
      updateRecvSel(mode);
      setModeFeedback(`RC Receiver: ${modeLabel(mode)}`, 'success');
      setRcInputsEnabled(rcComponentsEnabled(data));
      // The radio and receiver cards read the same config (data/component_picker.js).
      window.ComponentPicker?.adopt(data);
    } catch (error) {
      const fallbackMode = rcInputModeHidden?.value || DEFAULT_RC_MODE;
      switchRcMode(fallbackMode);
      setModeFeedback(`RC Receiver not read: ${window.PAApi.messageFor(error)}. Showing ${modeLabel(fallbackMode)}.`, 'warning');
      throw error;
    }
  };

  const loadMappings = async ({ handle = null } = {}) => {
    try {
      const api = handle || window.PAApi;
      const result = await api.get('/api/rc/map');
      const payload = result.data || {};
      const mode = typeof payload.mode === 'string' ? payload.mode : getEditorMode();
      // An entry the droid says it does not read ("read": false - a save would
      // refuse it, ADR 0070) is left out of the map this page posts, or the
      // droid would refuse the whole map over it.
      const entries = Array.isArray(payload.map) ? payload.map : [];
      const unread = entries.filter((entry) => entry?.read === false)
        .map((entry) => channelTitleFromKey(channelKeyOf(entry.source, entry.channel)));
      channelMap = modeMapFromArray(entries.filter((entry) => entry?.read !== false));
      channelMapLoaded = true;
      mapCapacityTotal = Number(payload.capacity?.total);
      paintCapacity();
      triggerPulseState = {};
      if (rcInputModeHidden?.value !== mode) switchRcMode(mode);
      if (selectedChannel && !sourceAllowedInMode(parseChannelKey(selectedChannel).source, mode)) {
        selectedChannel = null;
      }
      renderSummaryTable();
      renderChannelList();
      renderLivePreview();
      renderEditor();
      if (unread.length > 0) {
        setEditorFeedback(`Not read by the droid: ${unread.join(', ')}. Apply drops ${unread.length === 1 ? 'it' : 'them'}.`, 'warning');
      }
    } catch (error) {
      // The last map the droid answered with is kept: an empty one here was
      // what the next Apply posted as the whole map (#355).
      triggerPulseState = {};
      renderSummaryTable();
      renderChannelList();
      renderLivePreview();
      renderEditor();
      setEditorFeedback(`Failed to load RC map: ${window.PAApi.messageFor(error)}`, 'error');
      throw error;
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

  // CH17/CH18 have no number to move past a threshold: a hit is either one
  // reading other than it was when Detect started (GET /api/rc rawDigital).
  const computeOnOffDetectHit = (baseline, curr, sources) => {
    const before = baseline?.rawDigital;
    const now = curr?.rawDigital;
    if (!before || !now) return null;
    for (const source of sources) {
      if (!Array.isArray(before[source]) || !Array.isArray(now[source])) continue;
      for (let i = 0; i < 2; i += 1) {
        if (typeof now[source][i] === 'boolean' && typeof before[source][i] === 'boolean' && now[source][i] !== before[source][i]) {
          return { source, channel: SBUS_ANALOG_CHANNELS + 1 + i, raw: now[source][i] ? 'On' : 'Off', baseline: before[source][i] ? 'On' : 'Off' };
        }
      }
    }
    return null;
  };

  const mappedActionsForDetectHit = (hit) => {
    if (!hit) return [];
    return asMapArray()
      .filter((entry) => entry.source === hit.source && Number(entry.channel) === hit.channel)
      .map((entry) => actionLabelFromToken(mapEntryAction(entry)));
  };

  const detectHitLabel = (hit) => (hit ? channelTitleFromKey(channelKeyOf(hit.source, hit.channel)) : '');

  const applyLearnHighlight = () => {
    if (!rcChannelItems) return;
    const channelKey = learnActive && learnHit ? channelKeyOf(learnHit.source, learnHit.channel) : null;
    rcChannelItems.querySelectorAll('.rc-channel-item').forEach((el) => {
      const hot = channelKey !== null && el.dataset.chkey === channelKey;
      // A hit the builder cannot see is no hit: the list scrolls to it.
      if (hot && !el.classList.contains('learn-hot')) el.scrollIntoView?.({ block: 'nearest' });
      el.classList.toggle('learn-hot', hot);
    });
  };

  const updateLearnBanner = () => {
    if (!rcLearnStatus) return;
    if (!learnHit) {
      const hasRaw = rcSnapshot?.raw && Object.keys(rcSnapshot.raw).length > 0;
      rcLearnStatus.textContent = hasRaw
        ? 'Listening. Flip a switch on the RC Radio.'
        : 'No RC signal. Wire the RC Receiver first.';
      return;
    }
    const label = detectHitLabel(learnHit);
    const mappedActions = mappedActionsForDetectHit(learnHit);
    if (mappedActions.length > 0) {
      rcLearnStatus.textContent = `Detected ${label}: ${mappedActions.join(', ')}.`;
    } else {
      rcLearnStatus.textContent = `Detected ${label}: not mapped yet.`;
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
      setEditorFeedback('Nothing to detect: switch an RC Channel on in Configuration.', 'warning');
      return;
    }
    learnActive = true;
    learnBaseline = rcSnapshot;
    learnHit = null;
    learnStartMs = Date.now();
    if (rcLearnBtn) window.PAUi.setAct(rcLearnBtn, 'Detecting…');
    if (rcLearnBanner) rcLearnBanner.hidden = false;
    updateLearnBanner();
    applyLearnHighlight();
  };

  const exitLearnMode = () => {
    learnActive = false;
    learnBaseline = null;
    learnHit = null;
    if (rcLearnBtn) window.PAUi.setAct(rcLearnBtn, DETECT_ACT);
    if (rcLearnBanner) rcLearnBanner.hidden = true;
    applyLearnHighlight();
  };

  const processLearnTick = (currSnapshot) => {
    if (Date.now() - learnStartMs > LEARN_TIMEOUT_MS) {
      exitLearnMode();
      return;
    }
    // Only the sources this receiver type reads: a stale sbus2 array on a
    // single SBUS droid must not win the detect.
    const allowed = SOURCE_OPTIONS[getEditorMode()] || [];
    const raw = currSnapshot?.raw && typeof currSnapshot.raw === 'object' ? currSnapshot.raw : {};
    const heard = { ...currSnapshot, raw: Object.fromEntries(Object.entries(raw).filter(([source]) => allowed.includes(source))) };
    // A CH17/CH18 switch flipped is the plainest press there is: it wins over
    // a stick that has wandered.
    const nextHit = computeOnOffDetectHit(learnBaseline, currSnapshot, allowed) || computeDetectHit(learnBaseline, heard);
    const changed = JSON.stringify(nextHit) !== JSON.stringify(learnHit);
    learnHit = nextHit;
    if (changed) {
      updateLearnBanner();
      applyLearnHighlight();
    }
  };

  const renderRcDiagnostics = (payload) => {
    // Detect listens to every reading, polled as much as streamed: without
    // the stream it never heard a switch at all.
    if (learnActive) processLearnTick(payload);
    rcSnapshot = payload;
    paintModeSelection(getEditorMode());
    setRcInputsEnabled(rcSourcesEnabled(payload));
    renderSourceHealth();
    renderSummaryTable();
    renderChannelList();
    // The editor is not drawn again here. It reads one thing the diagnostics
    // carry, the receiver mode (getEditorMode()), for which actions the picker
    // offers (actionAllowedOnSelected()); a new mode reaches the picker at its
    // next draw. Drawing the editor here rebuilt it from the saved binding and
    // threw a draft away on every poll and every return to the tab (#355).
    if (selectedChannel) renderLivePreview();
  };

  const loadRcDiagnostics = async ({ handle = null } = {}) => {
    try {
      const api = handle || window.PAApi;
      const result = await api.get('/api/rc');
      renderRcDiagnostics(result.data);
    } catch (error) {
      setEditorFeedback(`Failed to load RC diagnostics: ${window.PAApi.messageFor(error)}`, 'error');
      throw error;
    }
  };

  const subscribeRcEvents = () => {
    if (!window.PAStatusStream?.isSupported()) return false;

    // Whether an RC source is enabled is a status field, read off the Live
    // Reading like every other (data/live_reading.js). The rc events below
    // are the stream's own and are read off it directly.
    window.PALiveReading.subscribe((reading) => {
      if (reading.status !== null) setRcInputsEnabled(rcEnabledFromStatus(reading.status));
    });

    window.PAStatusStream.subscribe((eventType, payload) => {
      try {
        if (eventType !== 'rc') return;
        const data = typeof payload === 'string' ? JSON.parse(payload) : payload;

        if (learnActive) processLearnTick(data);

        rcSnapshot = data;
        renderSourceHealth();
        updateSummaryMiniBar();
        updateChannelListRaw();
        updateAxesLive();
        if (selectedChannel) renderLivePreview();
      } catch (_error) {
        setEditorFeedback('Received malformed RC event payload.', 'error');
      }
    });

    return true;
  };

  const saveMapping = async () => {
    if (!selectedChannel || !rcEditorContent) return;
    if (!channelMapLoaded) {
      setEditorFeedback('Not saved: the droid\'s map has not loaded yet.', 'error');
      return;
    }
    if (axisSaveInFlight || mapWriteInFlight) {
      setEditorFeedback('Not saved: another save is still going. Try again.', 'error');
      return;
    }

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

    if (target === 'puppet_part' && !payload) {
      setEditorFeedback('Pick the Part this stick moves.', 'error');
      return;
    }

    if (!sourceAllowedInMode(source, mode)) {
      setEditorFeedback(`${sourceLabel(source)} reads nothing on ${modeLabel(mode)}.`, 'error');
      return;
    }

    // A Reaction's two numbers, from the builder's units to the stored ones.
    const reaction = {};
    const condition = droidConditionFor(selectedChannel);
    if (condition && target) {
      const numberOf = (field) => Number(rcEditorContent.querySelector(`[data-field="${field}"]`)?.value);
      if (condition.threshold) {
        const { label, unit, scale, min, max } = condition.threshold;
        const stored = Math.round(numberOf('threshold') * scale);
        if (!Number.isFinite(stored) || stored < min || stored > max) {
          setEditorFeedback(`${label}: ${min / scale} to ${max / scale} ${unit}.`, 'error');
          return;
        }
        reaction.threshold = stored;
      }
      const quietS = Math.round(numberOf('quietS'));
      if (!Number.isFinite(quietS) || quietS < REACTION_QUIET.min || quietS > REACTION_QUIET.max) {
        setEditorFeedback(`Quiet after firing: ${REACTION_QUIET.min} to ${REACTION_QUIET.max} s.`, 'error');
        return;
      }
      reaction.quietS = quietS;
    }

    const nextMap = { ...channelMap };
    if (!target) {
      delete nextMap[selectedChannel];
    } else {
      nextMap[selectedChannel] = normalizeMapEntry({ source, channel, action: target, payload, ...reaction });
    }

    // Speed and Steer on SBUS1, and on one RC Receiver, as the droid requires.
    if (driveOnSbus2(target, selectedChannel)) {
      setEditorFeedback(DRIVE_SBUS2_TEXT, 'error');
      return;
    }
    const split = driveSplitWith(nextMap, target, selectedChannel);
    if (split) {
      setEditorFeedback(`Not saved: ${actionLabelFromToken(split.action)} is on ${sourceLabel(split.source)}. Speed and Steer share one RC Receiver.`, 'error');
      return;
    }

    // One Part, one stick (#442): a Part another channel already moves leaves
    // it. Asked first, naming both channels, because the other one goes
    // unmapped.
    let moved = '';
    if (target === 'puppet_part') {
      const from = Object.keys(nextMap).find((key) => key !== selectedChannel
        && mapEntryAction(nextMap[key]) === 'puppet_part' && nextMap[key].payload === payload);
      if (from) {
        const go = await window.PAOverlay.ask({
          title: `Move ${partLabel(payload)} to ${channelTitleFromKey(selectedChannel)}?`,
          body: `${channelTitleFromKey(from)} moves it now, and is left unmapped.`,
          yes: 'Move it',
          yesIcon: 'transfer',
          no: 'Keep it there',
          near: rcEditorApply,
        });
        if (!go) return;
        delete nextMap[from];
        moved = ` ${channelTitleFromKey(from)} is unmapped.`;
      }
      // Another string on a Part that shares this Part's servo: the droid
      // sends one stick's target to a servo per frame, so the two sticks take
      // turns. Asked, not refused - wiring two Parts to one servo is legal.
      const servo = puppetPartOutput.get(payload);
      const shared = servo === undefined ? null : Object.keys(nextMap).find((key) => key !== selectedChannel
        && mapEntryAction(nextMap[key]) === 'puppet_part' && nextMap[key].payload !== payload
        && puppetPartOutput.get(nextMap[key].payload) === servo);
      if (shared) {
        const keep = await window.PAOverlay.ask({
          title: `${partLabel(payload)} shares a servo with ${partLabel(nextMap[shared].payload)}`,
          body: `${channelTitleFromKey(shared)} moves that servo already. Two sticks on one servo take turns.`,
          yes: 'Map it anyway',
          yesIcon: 'link-variant',
          no: 'Leave it',
          near: rcEditorApply,
        });
        if (!keep) return;
      }
    }

    setEditorDirtyState('saving', 'Saving changes…');
    setEditorFeedback('Saving...');

    mapWriteInFlight = true;
    renderAxes();
    try {
      await window.PAApi.postForm('/api/rc/map', { plain: JSON.stringify({ map: Object.values(nextMap) }) }, { timeoutMs: 5000 });
    } catch (error) {
      mapWriteInFlight = false;
      renderAxes();
      setEditorDirtyState('error', 'Save failed — unsaved changes');
      if (rcEditorApply) rcEditorApply.disabled = false;
      if (rcEditorRevert) rcEditorRevert.disabled = false;
      setEditorFeedback(mapRefusalText(error), 'error');
      return;
    }
    const savedAt = new Date().toLocaleTimeString();
    const shown = await showStoredMap(nextMap);
    mapWriteInFlight = false;
    renderAxes();
    setEditorFeedback(`Saved at ${savedAt}.${moved}${shown.note}`, shown.ok ? 'success' : 'warning');
    markEditorClean(savedAt);
  };

  // A refused save, in the builder's words where the droid's are about room.
  const mapRefusalText = (error) => {
    const message = window.PAApi.messageFor(error);
    if (/no trigger slot available|exceeds capacity/.test(message)) return 'No room for one more. Unmap another switch or condition first.';
    if (DRIVE_SPLIT_REFUSAL.test(message)) return DRIVE_SPLIT_TEXT;
    if (DRIVE_SBUS2_REFUSAL.test(message)) return DRIVE_SBUS2_TEXT;
    return `Failed to save: ${message}`;
  };

  // After a save the droid answers only {ok:true}, so what it stored is read
  // back and drawn (GET /api/rc/map), never the page's own copy. A read that
  // fails leaves the page's copy on screen and says so.
  const showStoredMap = async (sentMap) => {
    try {
      await loadMappings();
      return { ok: true, note: '' };
    } catch (error) {
      // The droid said ok to this map, so it is the one to post next time.
      channelMap = { ...sentMap };
      channelMapLoaded = true;
      paintCapacity();
      renderSummaryTable();
      renderChannelList();
      renderLivePreview();
      renderEditor();
      return { ok: false, note: ` The droid's map did not load back: ${window.PAApi.messageFor(error)}.` };
    }
  };

  // loadMappings() rethrows for the bootstrap's section retry. A Revert has no
  // retry to hand it to, and the failure is already in the editor feedback, so
  // the rejection stops here and the editor stays dirty.
  const revertMapping = async () => {
    try {
      await loadMappings();
    } catch (_error) {
      return;
    }
    setEditorFeedback('Reverted to last saved mapping.');
    markEditorClean();
  };

  const resetToDefaults = async () => {
    const clear = await window.PAOverlay.ask({
      title: 'Clear every mapping?',
      body: 'Every switch loses its action, and the droid\'s own conditions go too. This cannot be taken back.',
      yes: 'Clear them',
      yesIcon: 'link-variant-off',
      no: 'Keep them',
      danger: true,
      near: rcResetDefaults,
    });
    if (!clear) return;
    if (axisSaveInFlight || mapWriteInFlight) {
      setEditorFeedback('Not cleared: another save is still going. Try again.', 'error');
      return;
    }
    setEditorFeedback('Clearing mappings...');
    mapWriteInFlight = true;
    renderAxes();
    try {
      await window.PAApi.postForm('/api/rc/map', { plain: JSON.stringify({ map: [] }) }, { timeoutMs: 5000 });
    } catch (error) {
      mapWriteInFlight = false;
      renderAxes();
      setEditorFeedback(`Failed to clear mappings: ${window.PAApi.messageFor(error)}`, 'error');
      return;
    }
    const savedAt = new Date().toLocaleTimeString();
    const shown = await showStoredMap({});
    mapWriteInFlight = false;
    renderAxes();
    setEditorFeedback(`Cleared all mappings.${shown.note}`, shown.ok ? 'success' : 'warning');
    markEditorClean(savedAt);
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

  sbusRecvSeg?.querySelectorAll("button").forEach((button) => {
    button.addEventListener("click", async () => {
      const recvCh2 = button.dataset.value === "true";
      if (recvCh2 === confirmedSbusRecvCh2) return;
      paintSbusRecv(recvCh2);
      setSbusRecvFeedback("Saving...");
      try {
        await window.PAApi.postJson("/api/config", { rc: { sbus: { recvCh2 } } }, { timeoutMs: 5000 });
        setSbusRecvFeedback(`Saved at ${new Date().toLocaleTimeString()}. Restart the Body Controller to apply.`, "success");
        confirmedSbusRecvCh2 = recvCh2;
      } catch (error) {
        paintSbusRecv(confirmedSbusRecvCh2 === true);
        setSbusRecvFeedback(`Not saved: ${window.PAApi.messageFor(error)}`, "error", 2000);
      }
    });
  });

  // The droid's verbose RC logs (POST /api/rc/debug, runtime only) are on
  // while RC is on screen, and only then. Inside the shell a surface is left
  // without the document unloading, so beforeunload alone left them on for
  // the rest of the session (#355).
  //
  // On: a surface poll that asks once on arrival -- the first mount and every
  // return, since the shell starts this surface's polls again on the way back
  // (#360). The toggle is not a reading, so this is the one poll that catches
  // its own failure: left to the registry, a refused toggle would hold RC's
  // "last reading from before you left" note up over diagnostics that had
  // answered.
  // Off: the unmount question the shell asks the surface being left, which
  // is how a surface hears it is leaving. Never a hold -- leaving is always
  // allowed. beforeunload stays for a real unload of the document.
  const askVerboseLogs = (enabled) =>
    window.PAApi.postJson("/api/rc/debug", { enabled }, { timeoutMs: 3000 });

  window.PASurface.poll(() => askVerboseLogs(true).catch((error) => {
    console.warn("[RC] Verbose logs not turned on:", window.PAApi.messageFor(error));
  }), { runOnStart: true }).start();

  window.PASurface.holdUnmount(() => {
    askVerboseLogs(false).catch((error) => {
      console.warn("[RC] Verbose logs not turned off:", window.PAApi.messageFor(error));
    });
    return false;
  });

  window.addEventListener("beforeunload", () => {
    const body = new Blob([JSON.stringify({ enabled: false })], { type: "application/json" });
    navigator.sendBeacon("/api/rc/debug", body);
  });

  setEditorDirtyState("clean", "Saved");
  setEditorSavedTimestamp(null);

  // -------------------------------------------------------------------------
  // Boot — load RC configuration and diagnostics
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API loads as sections so the bootstrap
  // can show recovery state if any fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const loadRcModeAndMappings = async ({ handle = null } = {}) => {
    await loadRcMode({ handle });
    await loadMappings({ handle });
  };

  const loadRcDiagnosticsWithFallback = async ({ handle = null } = {}) => {
    await loadRcDiagnostics({ handle });
  };

  // Every view that names an action is drawn again once the firmware's list
  // arrives. The fallback carries no action about one Output - its name is the
  // running board's (ADR 0033 Amendment 2026-09-19) - so a binding to one read
  // as its bare token until this redraw, whichever section finished first. An
  // open editor has only its picker drawn again, so it keeps its draft.
  const redrawActionNames = () => {
    renderSummaryTable();
    renderChannelList();
    renderLivePreview();
    refreshActionPicker();
  };

  // The droid's own sequences arrived: what names one is drawn again, and an
  // open editor keeps its draft.
  const redrawSequenceNames = () => {
    renderSummaryTable();
    renderChannelList();
    renderLivePreview();
    refreshDomeSequencePills();
  };

  // The droid's Parts arrived: the editor's Part pills are drawn again around
  // the draft.
  const redrawPartNames = () => {
    refreshPuppetPartPills();
  };

  const loadActionTargetsWithFallback = async ({ handle = null } = {}) => {
    await loadActionTargets({ handle });
    redrawActionNames();
  };

  const SECTIONS = [
    ["rc-mode-mapping", loadRcModeAndMappings, "RC Receiver and RC Map"],
    ["rc-diagnostics", loadRcDiagnosticsWithFallback, "RC channel diagnostics"],
    ["rc-action-targets", loadActionTargetsWithFallback, "RC action registry"],
  ];

  const startPageLoad = () => {
    loadRecentActionTokens();
    switchRcMode(rcInputModeHidden?.value || DEFAULT_RC_MODE);

    if (!window.PABootstrap) {
      loadRcMode().finally(() => {
        loadMappings();
      });
      loadRcDiagnostics();
      loadActionTargets().then(redrawActionNames);
      loadLearnedSequences().then(redrawSequenceNames);
      loadPuppetParts().then(redrawPartNames);
      return;
    }

    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/live_reading.js": "live updates",
      "/shell.js": "page layout",
      "/rc.js": "RC control",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
    // Not sections: the page works without them. Until each answers, no
    // binding is called missing or unwired.
    loadLearnedSequences().then(redrawSequenceNames);
    loadPuppetParts().then(redrawPartNames);
  };

  startPageLoad();

  const hasRcStream = subscribeRcEvents();

  // RC diagnostics is one of the two surfaces an operator opens when something
  // is already wrong, and the one that asks hardest -- so it is owned by this
  // surface, and the shell stops it the moment the operator reads something
  // else (ADR 0048, #360). With the shared stream there is no cadence at all:
  // what remains is the refresh on returning to the tab or to this screen,
  // which used to be a document-level visibilitychange handler that kept firing
  // long after the operator had left RC behind.
  //
  // loadRcDiagnostics() rethrows so the bootstrap's section can see a failure
  // and retry it, and the rejection is left to PASurface.poll(): a background
  // refresh has already said so in the editor feedback line and has nobody to
  // hand a rejection to, but it is the registry that has to hear it. Catching
  // it here is what used to tell RC its diagnostics were current when nothing
  // had answered (#360).
  window.PASurface.poll(loadRcDiagnostics, {
    cadenceMs: hasRcStream ? 0 : 1000,
    refreshOnReturn: true,
  }).start();
})();

// =============================================================================
// data/seq_protocol_check.js
//
// Client-side validation mirror for Learned Sequences.
// Mirrors server Protocol Check rules for instant feedback without roundtrips.
// Server remains authoritative on save; client prevents obvious errors.
// =============================================================================

(() => {
  const REGEX_NAME = /^DM:[A-Z0-9_]{1,18}$/;
  const SUPPRESS_MS_MIN = 1000;
  const SUPPRESS_MS_MAX = 120000;
  const TOGGLE_GROUPS = ["none", "pies", "low", "all"];
  const STEP_TYPES = ["audio", "dome", "loop", "random", "audioCat", "domeRotate", "end"];
  const AUDIO_CATEGORIES = [
    "alert",
    "chatty",
    "general",
    "happy",
    "humming",
    "processing",
    "sad",
    "sentimental",
    "scream",
    "surprised",
    "whistle",
  ];
  const RANDOM_SETS  = ["ring", "pie", "all", "hold"];
  const RANDOM_MODES = ["flutter", "open", "close"];

  // audioCat "fallback" is a NAMED SLOT (the clip played when the chosen category
  // has no available track), not a "$" sound. Values mirror the server slot table
  // in src/seq_json.cpp (slotToString/slotFromString); "none" = no fallback clip.
  const AUDIO_FALLBACK_SLOTS = [
    "none", "scream", "faint", "leia", "cantina_s", "sw_theme",
    "imp_march", "cantina_l", "startup", "disco", "happy",
  ];

  // Allowed panel intent targets for :OP/:CL/:OF commands.
  // Ring panels: numeric slot IDs present on a standard astromech ring.
  // Pie / top panels: PP1-PP6 use the explicit "P1"-"P6" aliases (not numeric 08-13).
  // Groups: 00=all, 14=pie/top group, 15=ring/bottom group.
  const PANEL_INTENT_TARGETS = new Set([
    "00", "14", "15",
    "01", "02", "03", "04", "07", "11", "13",
    "P1", "P2", "P3", "P4", "P5", "P6",
  ]);

  // Dome visual presets (DV:<NAME>) — logic/PSI/holo only, closed set owned by
  // the dome. Mirrors the server whitelist in src/protocol_check.cpp.
  const DV_PRESETS = new Set([
    "ROCKMARCH", "VADER", "ALARM", "LEIA", "HEART", "CANTINA",
    "SCREAM", "OVERLOAD", "HELLO", "RESET_VISUALS",
  ]);

  // Logic/PSI Mode (DL:) — structured control for dome logic/PSI animations.
  // Grammar: DL:<target>:<mode>[:<color>[:<durationSec>]]
  // Mirrors src/protocol_check.cpp validation.
  const DL_TARGETS = new Set([
    "FLD", "RLD", "LOGIC", "FPSI", "RPSI", "PSI", "ALL",
  ]);
  const DL_MODES = new Set([
    "NORMAL", "ALARM", "FAILURE", "LEIA", "MARCH", "FLASHCOLOR",
    "REDALERT", "RAINBOW", "LIGHTSOUT",
  ]);
  const DL_COLORS = new Set([
    "DEFAULT", "RED", "BLUE", "GREEN", "WHITE", "YELLOW", "ORANGE", "PURPLE",
  ]);

  // Logic Text (DT:) — multi-line text display on FLD/RLD.
  // Grammar: DT:<target>:<color>:<durationSec>:<speed>:<encodedText>
  // Text is percent-encoded; newline=%0A, %=%25, :=%3A; spaces literal.
  // Encoded text <= 40 chars; decoded text <= 32 chars; max one newline.
  const DT_TARGETS = new Set([
    "FLD", "RLD", "LOGIC",
  ]);
  const DT_COLORS = new Set([
    "DEFAULT", "RED", "BLUE", "GREEN", "WHITE", "YELLOW", "ORANGE", "PURPLE",
  ]);

  // Holo Effect (DH:) — holoprojector effects.
  // Grammar: DH:<target>:<effect>[:<color>[:<durationOrCount>]]
  const DH_TARGETS = new Set([
    "F", "R", "T", "A",
  ]);
  const DH_EFFECTS = new Set([
    "OFF", "ON", "RESET", "RANDOM", "WAG", "NOD", "PULSE", "RAINBOW",
    "FLASH", "SHORTCIRCUIT", "SOLID",
  ]);
  const DH_COLORS = new Set([
    "DEFAULT", "RED", "BLUE", "GREEN", "WHITE", "YELLOW", "ORANGE", "PURPLE", "RANDOM",
  ]);
  // Per-effect color + duration matrix — mirrors the AstroPixelsPlus dome
  // (docs/dome-visual-authoring-contract.md, issue #11). The dome accepts the
  // global color enum, then applies these effect-specific constraints; the body
  // mirrors them so unsupported combos (e.g. DH:A:RAINBOW:RED) are rejected before
  // send rather than relying on the dome to reject. colors = allowed color set for
  // the effect; duration "none" = must be omitted or 0; "range" = 0..99 allowed
  // (WAG/NOD count, FLASH seconds).
  const DH_EFFECT_RULES = {
    RESET:        { colors: new Set(["DEFAULT"]),                 duration: "none" },
    OFF:          { colors: new Set(["DEFAULT"]),                 duration: "none" },
    ON:           { colors: DH_COLORS,                            duration: "none" },
    SOLID:        { colors: DH_COLORS,                            duration: "none" },
    RANDOM:       { colors: new Set(["DEFAULT"]),                 duration: "none" },
    WAG:          { colors: new Set(["DEFAULT"]),                 duration: "range" },
    NOD:          { colors: new Set(["DEFAULT"]),                 duration: "range" },
    PULSE:        { colors: new Set(["DEFAULT", "RANDOM"]),       duration: "none" },
    RAINBOW:      { colors: new Set(["DEFAULT"]),                 duration: "none" },
    FLASH:        { colors: new Set(["DEFAULT", "WHITE", "RED"]), duration: "range" },
    SHORTCIRCUIT: { colors: new Set(["DEFAULT", "RANDOM"]),       duration: "none" },
  };

  // Classify a panel intent target into its group for :OF cleanup tracking.
  function panelGroup(target) {
    if (target === "00") return "all";
    if (target === "14") return "pie_group";
    if (target === "15") return "ring_group";
    if (["01", "02", "03", "04", "07", "11", "13"].indexOf(target) !== -1) return "ring";
    if (["P1", "P2", "P3", "P4", "P5", "P6"].indexOf(target) !== -1) return "pie";
    return "unknown";
  }

  // Returns true if a :CL<closeTarget> satisfies the cleanup requirement for
  // a :OF<flutterTarget> step with the given group classification.
  function closeSatisfiesFlutter(flutterTarget, flutterGroup, closeTarget) {
    if (closeTarget === "00") return true;               // all-close satisfies everything
    if (closeTarget === flutterTarget) return true;      // exact same target
    if (flutterGroup === "ring"       && closeTarget === "15") return true;
    if (flutterGroup === "pie"        && closeTarget === "14") return true;
    if (flutterGroup === "pie_group"  && closeTarget === "14") return true;
    if (flutterGroup === "ring_group" && closeTarget === "15") return true;
    return false;
  }

  const SeqProtocolCheck = {
    /**
     * Validate sequence name format.
     * @param {string} name
     * @returns {{ok: boolean, error?: string}}
     */
    validateName(name) {
      if (!name) return { ok: false, error: "Name is required" };
      if (!REGEX_NAME.test(name)) {
        return {
          ok: false,
          error:
            "The name must start with DM: then 1-18 capital letters, numbers, or underscores (for example, DM:ROCKMARCH)",
        };
      }
      return { ok: true };
    },

    /**
     * Validate suppressMs against end time.
     * @param {number} suppressMs
     * @param {number} endT - end step time (t value)
     * @returns {{ok: boolean, error?: string}}
     */
    validateSuppressMs(suppressMs, endT) {
      if (suppressMs < SUPPRESS_MS_MIN || suppressMs > SUPPRESS_MS_MAX) {
        return {
          ok: false,
          error: `The mute time must be between ${SUPPRESS_MS_MIN} and ${SUPPRESS_MS_MAX} milliseconds`,
        };
      }
      if (suppressMs < endT) {
        return {
          ok: false,
          error: `The mute time (${suppressMs}ms) must be at least as long as the whole sequence (${endT}ms)`,
        };
      }
      return { ok: true };
    },

    /**
     * Validate a single step.
     * @param {object} step
     * @param {number} stepIndex
     * @param {array} allSteps
     * @param {boolean} isBranchRoot - true inside a loop body (skip outer non-decreasing check)
     * @returns {{ok: boolean, field?: string, error?: string}}
     */
    validateStep(step, stepIndex, allSteps = [], isBranchRoot = false) {
      if (!step || typeof step !== "object") {
        return { ok: false, error: "This step is missing its details" };
      }

      const { t, type } = step;

      // Timing
      if (typeof t !== "number" || t < 0 || t > 120000) {
        return {
          ok: false,
          field: "t",
          error: "Step time must be between 0 and 120000 milliseconds",
        };
      }

      // Non-decreasing check (outer sequence only; loop body steps use relative time)
      if (!isBranchRoot && stepIndex > 0) {
        const prevT = allSteps[stepIndex - 1]?.t || 0;
        if (t < prevT) {
          return {
            ok: false,
            field: "t",
            error: `This step must happen at or after the previous step (${prevT}ms)`,
          };
        }
      }

      // Type
      if (!STEP_TYPES.includes(type)) {
        return {
          ok: false,
          field: "type",
          error: `Choose a valid step type: ${STEP_TYPES.join(", ")}`,
        };
      }

      switch (type) {
        case "audio":    return this._validateAudioStep(step);
        case "dome":     return this._validateDomeStep(step);
        case "loop":     return this._validateLoopStep(step, allSteps);
        case "random":   return this._validateRandomStep(step);
        case "audioCat": return this._validateAudioCatStep(step);
        case "domeRotate": return this._validateDomeRotateStep(step);
        case "end":      return { ok: true };
        default:         return { ok: true };
      }
    },

    _validateAudioStep(step) {
      const { cmd } = step;
      if (!cmd || typeof cmd !== "string") {
        return { ok: false, field: "cmd", error: "Choose a sound for this step" };
      }
      if (!cmd.startsWith("$")) {
        return {
          ok: false,
          field: "cmd",
          error: "A sound name must start with $ (for example, $H for the Happy sound)",
        };
      }
      return { ok: true };
    },

    _validateDomeStep(step) {
      const { cmd } = step;
      if (!cmd || typeof cmd !== "string") {
        return { ok: false, field: "cmd", error: "Choose a dome action for this step" };
      }

      // Explicit rejection with clear actionable message
      if (cmd.startsWith(":SM")) {
        return {
          ok: false,
          field: "cmd",
          error:
            ":SM is only for calibration. To move panels in a sequence, use an Open, Close, or Flutter panel action instead.",
        };
      }
      if (/^DM:/.test(cmd)) {
        return {
          ok: false,
          field: "cmd",
          error: "DM:... names a whole sequence, so it can't be used as a single step",
        };
      }

      // DV:<NAME> — dome visual preset (logic/PSI/holo only). Closed name set
      // owned by the dome; only a known preset may persist in a sequence.
      if (cmd.startsWith("DV:")) {
        if (!DV_PRESETS.has(cmd.slice(3))) {
          return {
            ok: false,
            field: "cmd",
            error:
              `"${cmd.slice(3)}" is not a known dome visual preset. ` +
              "Choose one of: " + Array.from(DV_PRESETS).join(", "),
          };
        }
        return { ok: true };
      }

      // DL:<target>:<mode>[:<color>[:<durationSec>]] — Logic/PSI mode
      if (cmd.startsWith("DL:")) {
        return this._validateDLLogicCommand(cmd);
      }

      // DT:<target>:<color>:<durationSec>:<speed>:<encodedText> — Logic Text
      if (cmd.startsWith("DT:")) {
        return this._validateDTTextCommand(cmd);
      }

      // DH:<target>:<effect>[:<color>[:<durationOrCount>]] — Holo Effect
      if (cmd.startsWith("DH:")) {
        return this._validateDHHoloCommand(cmd);
      }

      // Panel intent: :OP<target>, :CL<target>, :OF<target>
      if (cmd.startsWith(":OP") || cmd.startsWith(":CL") || cmd.startsWith(":OF")) {
        const target = cmd.slice(3);
        if (!PANEL_INTENT_TARGETS.has(target)) {
          const numericAmbiguous = /^\d{2}$/.test(target);
          const hint = numericAmbiguous
            ? " (08-10 and 12 are unclear - use P1-P6 to pick a pie panel)"
            : "";
          return {
            ok: false,
            field: "cmd",
            error:
              `"${target}" is not a panel I can target${hint}. ` +
              "Pick a ring (01-04, 07, 11, 13), a pie (P1-P6), or a group (00 = all, 14 = pies, 15 = rings)",
          };
        }
        return { ok: true };
      }

      // Non-panel dome effects (Advanced mode only)
      if (cmd.startsWith("@")) return { ok: true };  // logic / PSI commands
      if (cmd.startsWith("*")) return { ok: true };  // holo / HP commands

      // :SE## — legacy Marcduino sequence trigger (Advanced only, not for panel control)
      if (cmd.startsWith(":SE")) {
        const seNum = cmd.slice(3);
        if (!/^\d{2}$/.test(seNum)) {
          return {
            ok: false,
            field: "cmd",
            error: "A Marcduino sequence needs exactly two digits, like :SE07",
          };
        }
        return { ok: true };
      }

      return {
        ok: false,
        field: "cmd",
        error:
          "That dome command isn't recognized. Use a panel action (Open, Close, Flutter), a dome visual preset (DV:...), or an advanced code (@ for logic/PSI, * for holos, :SE## for Marcduino).",
      };
    },

    _validateLoopStep(step, _allSteps) {
      const { body, periodMs, durationMs } = step;

      if (typeof body !== "number" || body < 1 || body > 96) {
        return {
          ok: false,
          field: "body",
          error: "A loop must repeat between 1 and 96 steps",
        };
      }

      if (typeof periodMs !== "number" || periodMs < 100 || periodMs > 60000) {
        return {
          ok: false,
          field: "periodMs",
          error: "The loop's repeat interval must be between 100 and 60000 milliseconds",
        };
      }

      if (typeof durationMs !== "number" || durationMs < 100 || durationMs > 120000) {
        return {
          ok: false,
          field: "durationMs",
          error: "The loop must run for between 100 and 120000 milliseconds",
        };
      }

      if (periodMs > durationMs) {
        return {
          ok: false,
          field: "periodMs",
          error: "The repeat interval can't be longer than the loop's total run time",
        };
      }

      return { ok: true };
    },

    _validateRandomStep(step) {
      const { set, mode, moveMs, jitterMs, distinct } = step;

      // Reject legacy pulse fields from old random step format
      if (typeof step.pulseMin !== "undefined" || typeof step.pulseMax !== "undefined") {
        return {
          ok: false,
          field: "pulseMin",
          error:
            "This older random format is no longer supported. Choose a motion (flutter, open, or close) and which panels to move.",
        };
      }

      if (!RANDOM_SETS.includes(set)) {
        return {
          ok: false,
          field: "set",
          error: `Choose which panels move: ${RANDOM_SETS.join(", ")}`,
        };
      }

      if (!RANDOM_MODES.includes(mode)) {
        return {
          ok: false,
          field: "mode",
          error: `Choose a motion type: ${RANDOM_MODES.join(", ")}`,
        };
      }

      if (typeof moveMs !== "number" || moveMs < 0 || moveMs > 5000) {
        return {
          ok: false,
          field: "moveMs",
          error: "Move time must be between 0 and 5000 milliseconds",
        };
      }

      if (typeof jitterMs !== "number" || jitterMs < 0 || jitterMs > 2000) {
        return {
          ok: false,
          field: "jitterMs",
          error: "Jitter must be between 0 and 2000 milliseconds",
        };
      }

      if (typeof distinct !== "boolean") {
        return {
          ok: false,
          field: "distinct",
          error: "The distinct option must be on or off",
        };
      }

      return { ok: true };
    },

    _validateAudioCatStep(step) {
      const { category, fallback } = step;

      if (!AUDIO_CATEGORIES.includes(category)) {
        return {
          ok: false,
          field: "category",
          error: `Choose a sound category: ${AUDIO_CATEGORIES.join(", ")}`,
        };
      }

      if (typeof fallback !== "string" || !AUDIO_FALLBACK_SLOTS.includes(fallback)) {
        return {
          ok: false,
          field: "fallback",
          error: `Choose a backup sound from the list (${AUDIO_FALLBACK_SLOTS.join(", ")})`,
        };
      }

      return { ok: true };
    },

    _validateDomeRotateStep(step) {
      const { speedPct, durationMs } = step;

      // Validate speedPct: must be in -100..100
      if (typeof speedPct !== "number" || speedPct < -100 || speedPct > 100) {
        return {
          ok: false,
          field: "speedPct",
          error: "Speed must be between -100% and 100%",
        };
      }

      // Validate durationMs: must be positive, EXCEPT the explicit neutral stop
      // (speedPct == 0 && durationMs == 0 is the only valid zero case)
      if (typeof durationMs !== "number") {
        return {
          ok: false,
          field: "durationMs",
          error: "Enter how long the dome should spin, in milliseconds",
        };
      }

      if (speedPct === 0 && durationMs === 0) {
        // Explicit neutral stop — valid
      } else if (durationMs === 0) {
        // Non-zero speed with zero duration — reject
        return {
          ok: false,
          field: "durationMs",
          error:
            "Enter a spin time greater than 0, or set both speed and time to 0 to stop the dome",
        };
      } else if (speedPct === 0 && durationMs > 0) {
        // Zero speed with positive duration — reject (ambiguous intent)
        return {
          ok: false,
          field: "speedPct",
          error:
            "Set a speed for the dome to spin, or set both speed and time to 0 to stop it",
        };
      } else if (durationMs < 0) {
        return {
          ok: false,
          field: "durationMs",
          error: "Spin time can't be negative",
        };
      }

      return { ok: true };
    },

    _validateDLLogicCommand(cmd) {
      // DL:<target>:<mode>[:<color>[:<durationSec>]]
      // Parse the command into its components
      const parts = cmd.split(":");
      if (parts.length < 3 || parts[0] !== "DL") {
        return {
          ok: false,
          field: "cmd",
          error: "Logic/PSI command must be in the format DL:TARGET:MODE[:COLOR[:DURATION]]",
        };
      }

      const target = parts[1];
      const mode = parts[2];
      const color = parts[3] || "DEFAULT";
      const durationStr = parts[4];

      // Validate command length (must be <= 63)
      if (cmd.length > 63) {
        return {
          ok: false,
          field: "cmd",
          error: "Logic/PSI command is too long (must be 63 characters or less)",
        };
      }

      // Validate target
      if (!DL_TARGETS.has(target)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${target}" is not a valid target. Choose one of: ${Array.from(DL_TARGETS).join(", ")}`,
        };
      }

      // Validate mode
      if (!DL_MODES.has(mode)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${mode}" is not a valid mode. Choose one of: ${Array.from(DL_MODES).join(", ")}`,
        };
      }

      // Validate color (optional, default DEFAULT)
      if (color && !DL_COLORS.has(color)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${color}" is not a valid color. Choose one of: ${Array.from(DL_COLORS).join(", ")}`,
        };
      }

      // Validate duration (optional, 0-99 seconds)
      if (durationStr !== undefined) {
        const duration = parseInt(durationStr, 10);
        if (isNaN(duration) || duration < 0 || duration > 99) {
          return {
            ok: false,
            field: "cmd",
            error: "Duration must be a number between 0 and 99 seconds",
          };
        }
      }

      // Reject extra fields
      if (parts.length > 5) {
        return {
          ok: false,
          field: "cmd",
          error: "Logic/PSI command has too many fields",
        };
      }

      return { ok: true };
    },

    _validateDTTextCommand(cmd) {
      // DT:<target>:<color>:<durationSec>:<speed>:<encodedText>
      // Text is percent-encoded; newline=%0A, %=%25, :=%3A; spaces literal
      // Encoded text <= 40 chars; decoded text <= 32 chars; max one newline
      const parts = cmd.split(":");
      if (parts.length < 5 || parts[0] !== "DT") {
        return {
          ok: false,
          field: "cmd",
          error: "Logic Text command must be in the format DT:TARGET:COLOR:DURATION:SPEED:TEXT",
        };
      }

      const target = parts[1];
      const color = parts[2];
      const durationStr = parts[3];
      const speedStr = parts[4];
      const encodedText = parts.slice(5).join(":");

      // Validate command length (must be <= 63)
      if (cmd.length > 63) {
        return {
          ok: false,
          field: "cmd",
          error: "Logic Text command is too long (must be 63 characters or less)",
        };
      }

      // Validate target
      if (!DT_TARGETS.has(target)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${target}" is not a valid target. Choose one of: ${Array.from(DT_TARGETS).join(", ")}`,
        };
      }

      // Validate color
      if (!DT_COLORS.has(color)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${color}" is not a valid color. Choose one of: ${Array.from(DT_COLORS).join(", ")}`,
        };
      }

      // Validate duration (0-99 seconds)
      const duration = parseInt(durationStr, 10);
      if (isNaN(duration) || duration < 0 || duration > 99) {
        return {
          ok: false,
          field: "cmd",
          error: "Duration must be a number between 0 and 99 seconds",
        };
      }

      // Validate speed (0-9)
      const speed = parseInt(speedStr, 10);
      if (isNaN(speed) || speed < 0 || speed > 9) {
        return {
          ok: false,
          field: "cmd",
          error: "Scroll speed must be a number between 0 and 9",
        };
      }

      // Validate encoded text length (max 40 chars encoded)
      if (encodedText.length > 40) {
        return {
          ok: false,
          field: "cmd",
          error: "Text is too long when encoded (max 40 characters)",
        };
      }

      // Decode and validate the text
      let decodedText = "";
      try {
        decodedText = decodeURIComponent(encodedText);
      } catch (e) {
        return {
          ok: false,
          field: "cmd",
          error: "Text encoding is invalid; use percent-encoding for special characters",
        };
      }

      // Validate decoded text length (max 32 chars)
      if (decodedText.length > 32) {
        return {
          ok: false,
          field: "cmd",
          error: "Text is too long when decoded (max 32 characters)",
        };
      }

      // Reject empty text
      if (decodedText.length === 0) {
        return {
          ok: false,
          field: "cmd",
          error: "Text can't be empty",
        };
      }

      // Check for control characters (except newline)
      for (let i = 0; i < decodedText.length; i++) {
        const ch = decodedText.charCodeAt(i);
        if (ch < 32 && ch !== 10) {
          // < 32 is control char; 10 is newline (allowed)
          return {
            ok: false,
            field: "cmd",
            error: "Text contains invalid control characters",
          };
        }
      }

      // Check for carriage return (explicitly disallowed)
      if (decodedText.includes("\r")) {
        return {
          ok: false,
          field: "cmd",
          error: "Text contains carriage return (CR); use only newlines",
        };
      }

      // Check for max one newline
      const newlineCount = (decodedText.match(/\n/g) || []).length;
      if (newlineCount > 1) {
        return {
          ok: false,
          field: "cmd",
          error: "Text can contain at most one line break",
        };
      }

      return { ok: true };
    },

    _validateDHHoloCommand(cmd) {
      // DH:<target>:<effect>[:<color>[:<durationOrCount>]]
      // Parse the command into its components
      const parts = cmd.split(":");
      if (parts.length < 3 || parts[0] !== "DH") {
        return {
          ok: false,
          field: "cmd",
          error: "Holo Effect command must be in the format DH:TARGET:EFFECT[:COLOR[:DURATION_OR_COUNT]]",
        };
      }

      const target = parts[1];
      const effect = parts[2];
      const color = parts[3] || "DEFAULT";
      const durationOrCountStr = parts[4];

      // Validate command length (must be <= 63)
      if (cmd.length > 63) {
        return {
          ok: false,
          field: "cmd",
          error: "Holo Effect command is too long (must be 63 characters or less)",
        };
      }

      // Validate target
      if (!DH_TARGETS.has(target)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${target}" is not a valid holo target. Choose one of: ${Array.from(DH_TARGETS).join(", ")}`,
        };
      }

      // Validate effect
      if (!DH_EFFECTS.has(effect)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${effect}" is not a valid holo effect. Choose one of: ${Array.from(DH_EFFECTS).join(", ")}`,
        };
      }

      // Validate color (optional, default DEFAULT)
      if (color && !DH_COLORS.has(color)) {
        return {
          ok: false,
          field: "cmd",
          error: `"${color}" is not a valid color. Choose one of: ${Array.from(DH_COLORS).join(", ")}`,
        };
      }

      // Effect-specific color matrix — the dome rejects unsupported effect/color
      // combinations (e.g. DH:A:RAINBOW:RED). Mirror that here so authors get a
      // plain-language error before the command ever reaches the dome.
      const rule = DH_EFFECT_RULES[effect];
      if (rule && !rule.colors.has(color)) {
        return {
          ok: false,
          field: "cmd",
          error: `${color} is not supported for the ${effect} holo effect. Allowed: ${Array.from(rule.colors).join(", ")}.`,
        };
      }

      // Validate durationOrCount (optional, 0-99)
      if (durationOrCountStr !== undefined) {
        const durationOrCount = parseInt(durationOrCountStr, 10);
        if (isNaN(durationOrCount) || durationOrCount < 0 || durationOrCount > 99) {
          return {
            ok: false,
            field: "cmd",
            error: "Duration or count must be a number between 0 and 99",
          };
        }
        // Effect-specific duration matrix — effects with no timed behavior take no
        // duration/count (must be omitted or 0); only WAG/NOD (count) and FLASH
        // (seconds) accept a non-zero value.
        if (rule && rule.duration === "none" && durationOrCount !== 0) {
          return {
            ok: false,
            field: "cmd",
            error: `The ${effect} holo effect does not take a duration or count.`,
          };
        }
      }

      // Reject extra fields
      if (parts.length > 5) {
        return {
          ok: false,
          field: "cmd",
          error: "Holo Effect command has too many fields",
        };
      }

      return { ok: true };
    },

    // Check :OF cleanup within a single branch (flat list of steps).
    // Every :OF<target> step must be followed by a matching :CL command in
    // the same branch. See the panel intent contract in docs/adr/0008.
    _checkBranchOfCleanup(steps) {
      const pending = []; // { target, group }

      for (const step of steps) {
        if (step.type !== "dome" || !step.cmd) continue;
        const cmd = step.cmd;

        if (cmd.startsWith(":OF")) {
          const target = cmd.slice(3);
          if (PANEL_INTENT_TARGETS.has(target)) {
            pending.push({ target, group: panelGroup(target) });
          }
        } else if (cmd.startsWith(":CL")) {
          const closeTarget = cmd.slice(3);
          for (let i = pending.length - 1; i >= 0; i--) {
            const f = pending[i];
            if (closeSatisfiesFlutter(f.target, f.group, closeTarget)) {
              pending.splice(i, 1);
            }
          }
        }
      }

      if (pending.length > 0) {
        const targets = pending.map((f) => `:OF${f.target}`).join(", ");
        return {
          ok: false,
          field: "steps",
          error: `These panels are left fluttering and never closed: ${targets}. Add a Close action for each one later in the sequence.`,
        };
      }
      return { ok: true };
    },

    /**
     * Validate entire sequence.
     * @param {object} seq
     * @returns {{ok: boolean, field?: string, error?: string, warnings?: string[]}}
     */
    validateSequence(seq) {
      if (!seq || typeof seq !== "object") {
        return { ok: false, error: "This sequence is missing its details" };
      }

      const { name, suppressMs, toggleGroup, steps } = seq;

      // Name
      const nameVal = this.validateName(name);
      if (!nameVal.ok) return { ok: false, field: "name", error: nameVal.error };

      // Suppress window
      let endT = 0;
      if (steps && steps.length > 0) {
        const endStep = steps.find((s) => s.type === "end");
        if (endStep) endT = endStep.t || 0;
      }
      const suppressVal = this.validateSuppressMs(suppressMs, endT);
      if (!suppressVal.ok) {
        return { ok: false, field: "suppressMs", error: suppressVal.error };
      }

      // Toggle group
      if (!TOGGLE_GROUPS.includes(toggleGroup)) {
        return {
          ok: false,
          field: "toggleGroup",
          error: `Choose a valid interrupt group: ${TOGGLE_GROUPS.join(", ")}`,
        };
      }

      // Steps array
      if (!Array.isArray(steps)) {
        return { ok: false, field: "steps", error: "This sequence has no steps" };
      }
      if (steps.length === 0) {
        return { ok: false, error: "Add at least one step to the sequence" };
      }
      if (steps.length > 96) {
        return { ok: false, error: "A sequence can have at most 96 steps" };
      }

      // Must end with 'end' type
      const lastStep = steps[steps.length - 1];
      if (lastStep.type !== "end") {
        return { ok: false, error: "The sequence must finish with a Sequence End step" };
      }

      // Identify loop body step indices so we can skip outer non-decreasing time
      // check for them — body step times are relative to the loop iteration.
      const bodyStepIndices = new Set();
      {
        let j = 0;
        while (j < steps.length) {
          const s = steps[j];
          if (s.type === "loop" && typeof s.body === "number" && s.body > 0) {
            const count = Math.min(s.body, steps.length - j - 1);
            for (let k = 1; k <= count; k++) bodyStepIndices.add(j + k);
            j += count + 1;
          } else {
            j++;
          }
        }
      }

      // Validate each step individually
      const warnings = [];
      let lastOuterT = -1;
      for (let i = 0; i < steps.length; i++) {
        const isBody = bodyStepIndices.has(i);
        // Pass isBranchRoot=true to suppress validateStep's built-in non-decreasing
        // check; outer-sequence ordering is enforced below instead.
        const stepVal = this.validateStep(steps[i], i, steps, true);
        if (!stepVal.ok) {
          return {
            ok: false,
            field: stepVal.field || `steps[${i}]`,
            error: stepVal.error,
          };
        }
        if (!isBody) {
          if (lastOuterT >= 0 && steps[i].t < lastOuterT) {
            return {
              ok: false,
              field: `steps[${i}].t`,
              error: `This step must happen at or after the previous step (${lastOuterT}ms)`,
            };
          }
          lastOuterT = steps[i].t;
        }
      }

      // :OF cleanup check — outer branch (all non-body steps)
      const outerSteps = steps.filter((_, i) => !bodyStepIndices.has(i));
      const outerCleanup = this._checkBranchOfCleanup(outerSteps);
      if (!outerCleanup.ok) return outerCleanup;

      // :OF cleanup check — each loop body independently
      {
        let j = 0;
        while (j < steps.length) {
          const s = steps[j];
          if (s.type === "loop" && typeof s.body === "number" && s.body > 0) {
            const count = Math.min(s.body, steps.length - j - 1);
            const bodySteps = steps.slice(j + 1, j + 1 + count);
            const bodyCleanup = this._checkBranchOfCleanup(bodySteps);
            if (!bodyCleanup.ok) return bodyCleanup;
            j += count + 1;
          } else {
            j++;
          }
        }
      }

      return { ok: true, warnings };
    },

    /**
     * Estimate sequence duration by finding the latest step time.
     * @param {array} steps
     * @returns {number} milliseconds
     */
    estimateDuration(steps) {
      if (!Array.isArray(steps) || steps.length === 0) return 0;
      let maxT = 0;
      steps.forEach((step) => {
        if (typeof step.t === "number") maxT = Math.max(maxT, step.t);
      });
      return maxT;
    },
  };

  window.SeqProtocolCheck = SeqProtocolCheck;
})();

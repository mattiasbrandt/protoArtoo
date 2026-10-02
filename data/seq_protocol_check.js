// =============================================================================
// data/seq_protocol_check.js
//
// Client-side validation mirror for Learned Sequences.
// Mirrors server Protocol Check rules for instant feedback without roundtrips.
// Server remains authoritative on save; client prevents obvious errors.
// =============================================================================

(() => {
  // How many characters a sequence's name holds after its DM:
  // (PC_NAME_BODY_MAX, include/protocol_check.h). The one copy in the browser:
  // both patterns below are built from it, and the editor reads it from here.
  const NAME_CHARS_MAX = 18;
  const NAME_PATTERN = `DM:[A-Z0-9_]{1,${NAME_CHARS_MAX}}`;
  const REGEX_NAME = new RegExp(`^${NAME_PATTERN}$`);
  const SUPPRESS_MS_MIN = 1000;
  const SUPPRESS_MS_MAX = 120000;
  const TOGGLE_GROUPS = ["none", "pies", "low", "all"];
  const STEP_TYPES = ["audio", "dome", "loop", "random", "audioCat", "domeRotate", "body", "gesture", "sequence", "end"];
  // A Body Step's Move Shapes, and how long a body flutter may last
  // (PC_BODY_FLUTTER_MS_MIN / _MAX, include/protocol_check.h).
  const BODY_SHAPES = ["open", "close", "flutter"];
  const BODY_FLUTTER_MS = Object.freeze([50, 60000]);
  // A sequence reference: a name, or a saved sequence's id (protocolCheckSeqRefValid()).
  const SEQ_REF = new RegExp(`^(${NAME_PATTERN}|[0-9a-z]{1,16})$`);
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

  // A Sequence Tempo (ADR 0058). Bounds mirror include/seq_tempo.h, where each
  // is derived: a beat lasts 100..60000 ms (the loop period bounds), so a tempo
  // runs 1..600 BPM, and the longest sequence at the fastest tempo is 1200
  // beats. The BPM is stored to one decimal and counted in tenths, so a beat
  // resolves to the same whole millisecond here as on the droid.
  const TEMPO_SOURCES = ["typed", "tapped", "analysed"];
  const TEMPO_BPM_TENTHS_MIN = 10;
  const TEMPO_BPM_TENTHS_MAX = 6000;
  const TEMPO_BEAT_MAX = 1200;
  const TEMPO_BAR_LEN_MAX = 16;
  const TEMPO_PHASE_MAX_MS = 120000;
  const TEMPO_DURATION_MAX_MS = 3600000;
  const TEMPO_HASH = /^[0-9a-f]{1,64}$/;

  const isWhole = (value) => Number.isInteger(value);

  // A logic text as the bytes the droid decodes it to, or null where it
  // refuses the encoding (percentDecode(), src/protocol_check.cpp): an escape
  // is % and two hex digits whose letters are all capitals or all small - it
  // has no reading for %aC - and stands for any byte but a carriage return;
  // anything else is printable ASCII as typed, and never a colon.
  const DT_ESCAPE = /^(?:[0-9A-F]{2}|[0-9a-f]{2})$/;
  const decodeTextBytes = (encoded) => {
    const bytes = [];
    for (let i = 0; i < encoded.length; i++) {
      const ch = encoded[i];
      if (ch === "%") {
        const hex = encoded.slice(i + 1, i + 3);
        if (!DT_ESCAPE.test(hex) || parseInt(hex, 16) === 0x0d) return null;
        bytes.push(parseInt(hex, 16));
        i += 2;
      } else if (ch === ":" || ch < " " || ch > "~") {
        return null;
      } else {
        bytes.push(ch.charCodeAt(0));
      }
    }
    return bytes;
  };
  const bpmTenths = (tempo) => Math.round(Number(tempo?.bpm) * 10);

  // seqTempoSpanMs() / seqTempoBeatMs(): round(beats * 60000 / bpm), in the
  // same integer steps the firmware takes.
  const tempoSpanMs = (tempo, beats) => {
    const tenths = bpmTenths(tempo);
    if (!(tenths > 0)) return 0;
    return Math.floor((2 * beats * 600000 + tenths) / (2 * tenths));
  };
  const tempoBeatMs = (tempo, beat) => (Number(tempo?.phase) || 0) + tempoSpanMs(tempo, beat);

  // How many steps the loop at `at` takes as its body, in a list that may not
  // have that many after it. validateStep() refuses a loop that reaches past
  // the last step; this reading is for everything that walks a sequence
  // whether it is valid or not, so it never reaches past the list itself.
  const loopBodyCount = (steps, at) => Math.min(steps[at].body, steps.length - at - 1);

  // The indices of steps inside a loop body: timed from the loop pass, so a
  // beat there counts from nothing the grid knows.
  const loopBodyIndices = (steps) => {
    const body = new Set();
    let j = 0;
    while (j < steps.length) {
      const s = steps[j];
      if (s && s.type === "loop" && typeof s.body === "number" && s.body > 0) {
        const count = loopBodyCount(steps, j);
        for (let k = 1; k <= count; k++) body.add(j + k);
        j += count + 1;
      } else {
        j++;
      }
    }
    return body;
  };
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
  // Encoded text <= 40 chars; decoded text <= 32 bytes; max one newline.
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

  // The dome controller's own light vocabulary, with the words a builder reads.
  // The tokens are the ones src/protocol_check.cpp validates, above; the labels
  // are the ones a builder reads when authoring a step. Published because
  // Lights offers the same modes and colors as a live control (#410, ADR
  // 0067), and two surfaces naming one mode two things is the drift ADR 0045
  // exists to stop. This is the one copy: data/seq.js reads every label it
  // shows from here - a step's words, the step list's pickers and the Picked
  // block tab (#441) - and data/lights.js reads the logic and PSI groups.
  //
  // A group is named for the tokens it labels: the logic and PSI groups
  // (targets, modes, colors) are DL:'s, the text groups DT:'s, the holo groups
  // DH:'s. `holoSides` is a holo target as a choice under a row already named
  // Holo, where "Front holo" would say the row's name twice.
  const DOME_LIGHT_LABELS = {
    targets: {
      FLD: "Front logic", RLD: "Rear logic", LOGIC: "Both logic",
      FPSI: "Front PSI", RPSI: "Rear PSI", PSI: "Both PSI",
      ALL: "All logic + PSI",
    },
    modes: {
      NORMAL: "Normal", ALARM: "Alarm", FAILURE: "Failure", LEIA: "Leia",
      MARCH: "March", FLASHCOLOR: "Flash Color", REDALERT: "Red Alert",
      RAINBOW: "Rainbow", LIGHTSOUT: "Lights Out",
    },
    colors: {
      DEFAULT: "Default", RED: "Red", BLUE: "Blue", GREEN: "Green",
      WHITE: "White", YELLOW: "Yellow", ORANGE: "Orange", PURPLE: "Purple",
    },
    presets: {
      ROCKMARCH: "Rock March", VADER: "Vader", ALARM: "Alarm", LEIA: "Leia",
      HEART: "Heart", CANTINA: "Cantina", SCREAM: "Scream",
      OVERLOAD: "Overload", HELLO: "Hello", RESET_VISUALS: "Reset Visuals",
    },
    textTargets: {
      FLD: "Front display", RLD: "Rear display", LOGIC: "Both displays",
    },
    textColors: {
      DEFAULT: "Default", RED: "Red", BLUE: "Blue", GREEN: "Green",
      WHITE: "White", YELLOW: "Yellow", ORANGE: "Orange", PURPLE: "Purple",
    },
    holoTargets: {
      F: "Front holo", R: "Rear holo", T: "Top holo", A: "All holos",
    },
    holoSides: {
      F: "Front", R: "Rear", T: "Top", A: "All",
    },
    holoEffects: {
      OFF: "Off", ON: "On", RESET: "Reset", RANDOM: "Random", WAG: "Wag",
      NOD: "Nod", PULSE: "Pulse", RAINBOW: "Rainbow", FLASH: "Flash",
      SHORTCIRCUIT: "Short Circuit", SOLID: "Solid",
    },
    holoColors: {
      DEFAULT: "Default", RED: "Red", BLUE: "Blue", GREEN: "Green",
      WHITE: "White", YELLOW: "Yellow", ORANGE: "Orange", PURPLE: "Purple",
      RANDOM: "Random",
    },
  };

  // What a holo effect's number counts, where its rule takes one
  // (DH_EFFECT_RULES): how many times a WAG or a NOD, how many seconds a
  // FLASH. Words for the control that asks for it; no rule reads this.
  const DH_COUNTED = new Set(["WAG", "NOD"]);
  const holoCounts = (effect, rule) =>
    (rule.duration !== "range" ? null : DH_COUNTED.has(effect) ? "times" : "seconds");

  const frozenList = (tokens) => Object.freeze(Array.from(tokens));

  const SeqProtocolCheck = {
    // The most steps a sequence may hold: PC_MAX_STEPS (include/protocol_check.h).
    // The Rehearsal's size figure reads it from here rather than keep its own.
    MAX_STEPS: 96,

    // How many characters a sequence's name holds after its DM:.
    NAME_CHARS_MAX,

    // How long a body flutter may last, in ms, as [least, most]: the bounds a
    // control that sets one offers, read from here rather than kept again.
    BODY_FLUTTER_MS,

    // How many beats a span, or a Gesture's pace, repeat or extent, may be, as
    // [least, most] (_validateBeats()): what a control that sets one offers.
    SPAN_BEATS: Object.freeze([1, TEMPO_BEAT_MAX]),

    /**
     * The dome's light vocabulary: which targets it answers to, the modes and
     * colors each takes, and the label to show for every token. Frozen, so a
     * caller cannot edit the vocabulary it was handed.
     *
     * `targets`, `modes` and `colors` are a logic or PSI mode's (DL:);
     * `presets` a visual preset's (DV:); `textTargets` and `textColors` a
     * logic text's (DT:); `holoTargets`, `holoEffects` and `holoColors` a holo
     * effect's (DH:). `holoRules` says, for each holo effect, the colors it
     * takes, whether it takes a duration or count ("none" or "range"), and
     * where it does, which the number is: "seconds" or "times".
     */
    domeLights: Object.freeze({
      targets: frozenList(DL_TARGETS),
      modes: frozenList(DL_MODES),
      colors: frozenList(DL_COLORS),
      presets: frozenList(DV_PRESETS),
      textTargets: frozenList(DT_TARGETS),
      textColors: frozenList(DT_COLORS),
      holoTargets: frozenList(DH_TARGETS),
      holoEffects: frozenList(DH_EFFECTS),
      holoColors: frozenList(DH_COLORS),
      holoRules: Object.freeze(Object.fromEntries(Object.entries(DH_EFFECT_RULES).map(([effect, rule]) =>
        [effect, Object.freeze({ colors: frozenList(rule.colors), duration: rule.duration, counts: holoCounts(effect, rule) })]))),
      label: (group, token) => DOME_LIGHT_LABELS[group]?.[token] || token || "",
    }),

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
            `The name must start with DM: then 1-${NAME_CHARS_MAX} capital letters, numbers, or underscores (for example, DM:ROCKMARCH)`,
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
        case "loop":     return this._validateLoopStep(step, stepIndex, allSteps);
        case "random":   return this._validateRandomStep(step);
        case "audioCat": return this._validateAudioCatStep(step);
        case "domeRotate": return this._validateDomeRotateStep(step);
        case "body":     return this._validateBodyStep(step, stepIndex, allSteps);
        case "gesture":  return this._validateGestureStep(step, stepIndex, allSteps);
        case "sequence":
          return typeof step.ref === "string" && SEQ_REF.test(step.ref)
            ? { ok: true }
            : { ok: false, field: "ref", error: "Pick a sequence" };
        case "end":      return { ok: true };
        default:         return { ok: true };
      }
    },

    // A Body Step's form: the wire's rules (parseStep(), src/seq_json.cpp) and
    // Protocol Check's (STEP_BODY in protocolCheckBranch(),
    // src/protocol_check.cpp), which is the one place the firmware judges one.
    // Form and only form (ADR 0044): whether an Output claims the Part is the
    // Rehearsal's, and a Part nothing is wired to still saves.
    //
    // A key the wire reads as absent - missing, or null - is absent here. The
    // catalog is checked where the page has loaded it, as the Gesture's is.
    _validateBodyStep(step, stepIndex, allSteps) {
      const fail = (field, error) => ({ ok: false, field, error });
      const said = (value) => value !== undefined && value !== null;
      const catalog = window.DroidParts?.parts;
      if (typeof step.part !== "string" || step.part === "") return fail("part", "Pick a part");
      if (Array.isArray(catalog) && !catalog.some((part) => part.id === step.part)) {
        return fail("part", "The part must be one from the parts list");
      }
      if (said(step.shape) && !BODY_SHAPES.includes(step.shape)) return fail("shape", "Pick open, close or flutter");
      if (said(step.howFar) && !(isWhole(step.howFar) && step.howFar >= 1 && step.howFar <= 100)) {
        return fail("howFar", "How far is 1 to 100 percent");
      }
      // An absent duration is stored as 0, and the firmware judges the 0: a
      // flutter with none is refused, and any other shape may say 0.
      const flutterMs = said(step.flutterMs) ? step.flutterMs : 0;
      if (step.shape !== "flutter") {
        return flutterMs === 0 ? { ok: true } : fail("flutterMs", "Only a flutter lasts a time");
      }
      const [least, most] = BODY_FLUTTER_MS;
      if (!isWhole(flutterMs) || flutterMs < least || flutterMs > most) {
        return fail("flutterMs", `A flutter lasts ${least} to ${most} ms`);
      }
      // A flutter ends open and owes a close (ADR 0049): a later body step in
      // the same branch that closes this Part.
      const closes = (other) => other && other.type === "body" && other.shape === "close" && other.part === step.part;
      if (!allSteps.slice(stepIndex + 1).some(closes)) {
        return fail("shape", "A flutter must be closed later. Add a close of the same part.");
      }
      return { ok: true };
    },

    // A Gesture's form (checkGesture(), src/protocol_check.cpp). Whether the
    // dome can perform the pair is the Rehearsal's and never refuses a save.
    _validateGestureStep(step, stepIndex, allSteps) {
      const G = window.SeqGesture;
      const parts = window.DroidParts;
      const fail = (field, error) => ({ ok: false, field, error });
      const hasSet = typeof step.set === "string";
      const hasParts = Array.isArray(step.parts);
      if (hasSet === hasParts) return fail("set", "Pick a set of parts, or list them");
      if (G && parts) {
        if (hasSet && !G.setOf(step.set)) return fail("set", "Pick a set of parts from the list");
        if (hasParts) {
          if (step.parts.length === 0 || step.parts.some((id) => !G.partOf(id))) {
            return fail("parts", "Every part must be one from the parts list");
          }
          if (new Set(step.parts).size !== step.parts.length) return fail("parts", "A part is listed twice");
          if (step.parts.length > G.MEMBERS_MAX) return fail("parts", `A gesture moves at most ${G.MEMBERS_MAX} parts`);
          if (new Set(step.parts.map((id) => G.partOf(id).half)).size > 1) {
            return fail("parts", "The parts must all be on the dome, or all on the body");
          }
          if (step.parts.join(",").length > 63) return fail("parts", "Too many parts to list. Pick a set instead.");
        }
      }
      const known = (value, list) => value === undefined || list.includes(value);
      if (!known(step.shape, ["open", "close", "flutter"])) return fail("shape", "Pick open, close or flutter");
      if (!known(step.spread, ["together", "wave", "chase", "alternate", "pulse"])) return fail("spread", "Pick how it travels");
      if (!known(step.direction, ["cw", "ccw"])) return fail("direction", "Pick a direction");
      if (!known(step.start, ["front", "right", "rear", "left"])) return fail("start", "Pick where it starts");
      if (!known(step.easing, ["none", "soft", "overshoot"])) return fail("easing", "Pick an easing");
      const inRange = (value, lo, hi) => value === undefined || (isWhole(value) && value >= lo && value <= hi);
      // A time the wire reads as absent - missing, null or 0 - is the
      // Gesture's default and is held to no bound (parseGestureMs(),
      // src/seq_json.cpp; checkGesture() bounds a time only when it is not 0).
      // How far is not one of them: a stated 0 is refused there.
      const timed = (value, lo, hi) => value === null || value === 0 || inRange(value, lo, hi);
      if (!inRange(step.howFar, 1, 100)) return fail("howFar", "How far is 1 to 100 percent");
      if (!timed(step.stepMs, 50, 60000)) return fail("stepMs", "The pace is 50 to 60000 ms");
      if (!timed(step.speedMs, 50, 5000)) return fail("speedMs", "A full throw takes 50 to 5000 ms");
      if (!timed(step.repeatMs, 100, 60000)) return fail("repeatMs", "It repeats every 100 to 60000 ms");
      if (!timed(step.extentMs, 0, 120000)) return fail("extentMs", "It repeats for at most 120000 ms");
      if (step.extentMs && !step.repeatMs) return fail("extentMs", "Set how often it repeats first");
      // An absent duration is stored as 0, and the firmware judges the 0.
      // Any other shape may say 0, as on a Body Step. A flutter is where the
      // two part: a Body Step's flutter with no length is refused, and a
      // Gesture's is accepted - only one that says a time is held to a
      // flutter's bounds.
      const flutterMs = step.flutterMs !== undefined && step.flutterMs !== null ? step.flutterMs : 0;
      if (step.shape !== "flutter") {
        if (flutterMs !== 0) return fail("flutterMs", "Only a flutter lasts a time");
      } else if (flutterMs !== 0) {
        const [least, most] = BODY_FLUTTER_MS;
        if (!isWhole(flutterMs) || flutterMs < least || flutterMs > most) {
          return fail("flutterMs", `A flutter lasts ${least} to ${most} ms`);
        }
      }
      // A flutter ends open and owes a close, unless the spread brings each
      // part back itself: a later close gesture over the same parts.
      const spread = step.spread || "together";
      if (step.shape === "flutter" && (spread === "together" || spread === "wave")) {
        const same = (other) =>
          other && other.type === "gesture" && other.shape === "close" &&
          (hasSet ? other.set === step.set : Array.isArray(other.parts) && other.parts.join(",") === step.parts.join(","));
        if (!allSteps.slice(stepIndex + 1).some(same)) {
          return fail("shape", "A flutter must be closed later. Add a close gesture over the same parts.");
        }
      }
      return { ok: true };
    },

    _validateAudioStep(step) {
      const { cmd, boundAudio } = step;
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
      if (boundAudio !== undefined && typeof boundAudio !== "boolean") {
        return { ok: false, field: "boundAudio", error: "Must be a boolean (true or false)" };
      }
      return { ok: true };
    },

    _validateDomeStep(step) {
      const { cmd } = step;
      if (!cmd || typeof cmd !== "string") {
        return { ok: false, field: "cmd", error: "Choose a dome action for this step" };
      }
      // How far is said on a panel open or close (include/sequence_dome_how_far.h).
      if (step.howFar !== undefined) {
        const panel = /^:(OP|CL)([0-9A-Z]{2})$/.exec(cmd);
        if (!panel || !PANEL_INTENT_TARGETS.has(panel[2])) {
          return { ok: false, field: "howFar", error: "Only a panel open or close says how far" };
        }
        if (!isWhole(step.howFar) || step.howFar < 1 || step.howFar > 100) {
          return { ok: false, field: "howFar", error: "How far is 1 to 100 percent" };
        }
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

    _validateLoopStep(step, stepIndex, allSteps) {
      const { body, periodMs, durationMs } = step;

      if (typeof body !== "number" || body < 1 || body > 96) {
        return {
          ok: false,
          field: "body",
          error: "A loop must repeat between 1 and 96 steps",
        };
      }

      // The steps it repeats are the ones after it, and it may not reach past
      // the last step of its branch ("loop body overruns the branch",
      // protocolCheckBranch(), src/protocol_check.cpp).
      if (stepIndex + body >= allSteps.length) {
        return {
          ok: false,
          field: "body",
          error: "A loop can't repeat more steps than come after it",
        };
      }

      // No loop inside another's body ("nested loops are not allowed").
      if (allSteps.slice(stepIndex + 1, stepIndex + 1 + body).some((inner) => inner && inner.type === "loop")) {
        return {
          ok: false,
          field: "body",
          error: "A loop can't repeat another loop",
        };
      }

      if (typeof periodMs !== "number" || periodMs < 100 || periodMs > 60000) {
        return {
          ok: false,
          field: "periodMs",
          error: "The loop's repeat interval must be between 100 and 60000 milliseconds",
        };
      }

      // 1..120000, and no rule ties it to the interval: the droid takes a loop
      // that runs for less than one interval, which makes the one pass
      // (protocolCheckBranch(), src/protocol_check.cpp).
      if (typeof durationMs !== "number" || durationMs < 1 || durationMs > 120000) {
        return {
          ok: false,
          field: "durationMs",
          error: "The loop must run for between 1 and 120000 milliseconds",
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
      // Encoded text <= 40 chars; decoded text <= 32 bytes; max one newline
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
      // The text is everything after the fifth colon, so a colon typed into
      // it raw shows up as a field too many.
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

      // What may stand in the text as typed (percentDecode(),
      // src/protocol_check.cpp): printable ASCII, and never a colon, which
      // would read as the next field. Anything else travels as an escape.
      if (parts.length > 6) {
        return {
          ok: false,
          field: "cmd",
          error: "Write a colon in the text as %3A",
        };
      }
      if (/[^\x20-\x7E]/.test(encodedText)) {
        return {
          ok: false,
          field: "cmd",
          error: "Text has a character that must be percent-encoded",
        };
      }

      // Decoded as the droid decodes it, byte for byte: a %FF or a %09 is a
      // byte like any other, and a carriage return is the one it refuses.
      const decoded = decodeTextBytes(encodedText);
      if (decoded === null) {
        return {
          ok: false,
          field: "cmd",
          error: "Text encoding is invalid; use percent-encoding for special characters",
        };
      }

      // The droid decodes into 32 bytes. A character outside ASCII travels as
      // two to four escapes, so it counts for that many.
      if (decoded.length > 32) {
        return {
          ok: false,
          field: "cmd",
          error: "Text is too long (max 32 characters; an accented letter or a symbol counts for more than one)",
        };
      }

      // Reject empty text
      if (decoded.length === 0) {
        return {
          ok: false,
          field: "cmd",
          error: "Text can't be empty",
        };
      }

      // Check for max one newline
      if (decoded.filter((byte) => byte === 0x0a).length > 1) {
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

    // Check :OF cleanup over one branch, every step of it in stored order.
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
     * The tempo block's form (protocolCheckTempo(), src/protocol_check.cpp, and
     * the wire rules in src/seq_json.cpp parseTempo()). A low confidence or a
     * stale hash is the Rehearsal's, never this gate's.
     * @param {object} tempo
     * @returns {{ok: boolean, field?: string, error?: string}}
     */
    validateTempo(tempo) {
      const fail = (field, error) => ({ ok: false, field: `tempo.${field}`, error });
      if (!tempo || typeof tempo !== "object" || Array.isArray(tempo)) {
        return { ok: false, field: "tempo", error: "The tempo is missing its details" };
      }
      const tenths = bpmTenths(tempo);
      if (typeof tempo.bpm !== "number" || !(tenths >= TEMPO_BPM_TENTHS_MIN && tenths <= TEMPO_BPM_TENTHS_MAX)) {
        return fail("bpm", "The tempo must be between 1 and 600 BPM");
      }
      const phase = tempo.phase ?? 0;
      if (!isWhole(phase) || phase < 0 || phase > TEMPO_PHASE_MAX_MS) {
        return fail("phase", "Beat 1 must sit within the first 120000 ms");
      }
      const barLen = tempo.barLen ?? 4;
      if (!isWhole(barLen) || barLen < 1 || barLen > TEMPO_BAR_LEN_MAX) {
        return fail("barLen", "A bar is 1 to 16 beats");
      }
      const barPhase = tempo.barPhase ?? 0;
      if (!isWhole(barPhase) || barPhase < 0 || barPhase >= barLen) {
        return fail("barPhase", "The downbeat must be a beat of the bar");
      }
      const duration = tempo.duration ?? 0;
      if (!isWhole(duration) || duration < 0 || duration > TEMPO_DURATION_MAX_MS) {
        return fail("duration", "The track length must be under an hour");
      }
      if (!TEMPO_SOURCES.includes(tempo.source)) {
        return fail("source", "The tempo must say whether it was typed, tapped or analysed");
      }
      const confidence = Math.round(Number(tempo.confidence) * 1000);
      if (typeof tempo.confidence !== "number" || !(confidence >= 0 && confidence <= 1000)) {
        return fail("confidence", "The tempo's confidence must be between 0 and 1");
      }
      if (tempo.hash !== undefined) {
        if (typeof tempo.hash !== "string" || !TEMPO_HASH.test(tempo.hash)) {
          return fail("hash", "The track fingerprint is damaged");
        }
        if (tempo.source !== "analysed") {
          return fail("hash", "Only an analyzed tempo carries a track fingerprint");
        }
      }
      return { ok: true };
    },

    /** The indices of steps inside a loop body, which are timed from a pass. */
    loopBodySteps: loopBodyIndices,
    /** Where beat `beat` falls, in ms, on this tempo (seqTempoBeatMs()). */
    tempoBeatMs,
    /** How long `beats` beats last, in ms, on this tempo (seqTempoSpanMs()). */
    tempoSpanMs,

    /**
     * The sequence as the droid will run it: every step placed on a beat at
     * the millisecond its beat resolves to, and every span in beats as the
     * duration it resolves to (src/seq_json.cpp parseStepBeats()). A Gesture's
     * pace, repeat and extent in beats resolve the same way; and on the run's
     * reading (the default) a Gesture that states no pace takes one beat, and
     * one that repeats with no extent runs to the end step or the track's end,
     * whichever comes first (resolveGestureExtents()).
     *
     * `{ written: true }` resolves only what the builder wrote in beats and
     * leaves every default unstated, which is what the editor keeps: a
     * default written down would stop following the tempo.
     *
     * Returns a copy; the builder's own object, beats and all, is never
     * rewritten. A sequence with no valid tempo comes back as it went in,
     * apart from the run's defaults that need no tempo.
     * @param {object} seq
     * @param {{written?: boolean}} options
     * @returns {object}
     */
    resolveBeats(seq, options = {}) {
      if (!seq || !Array.isArray(seq.steps)) return seq;
      const tempo = seq.tempo !== undefined && this.validateTempo(seq.tempo).ok ? seq.tempo : null;
      const run = !options.written;
      if (!tempo && !run) return seq;
      const resolveBranch = (steps) => {
        const out = steps.map((step) => {
          if (!step || typeof step !== "object") return step;
          const next = { ...step };
          if (tempo) {
            if (isWhole(step.beat)) next.t = tempoBeatMs(tempo, step.beat);
            if (isWhole(step.spanBeats)) {
              const ms = tempoSpanMs(tempo, step.spanBeats);
              if (step.type === "domeRotate") next.durationMs = ms;
              else if (step.type === "body" && step.shape === "flutter") next.flutterMs = ms;
            }
            if (step.type === "gesture") {
              if (isWhole(step.stepBeats)) next.stepMs = tempoSpanMs(tempo, step.stepBeats);
              if (isWhole(step.repeatBeats)) next.repeatMs = tempoSpanMs(tempo, step.repeatBeats);
              if (isWhole(step.extentBeats)) next.extentMs = tempoSpanMs(tempo, step.extentBeats);
              if (run && !next.stepMs) next.stepMs = tempoSpanMs(tempo, 1);
            }
          }
          return next;
        });
        const end = out[out.length - 1];
        if (run && end && end.type === "end") {
          const endMs = Number(end.t) || 0;
          out.forEach((step) => {
            if (!step || step.type !== "gesture" || !(step.repeatMs > 0) || step.extentMs) return;
            let extent = Math.max(0, endMs - (Number(step.t) || 0));
            const track = Number(tempo?.duration) || 0;
            if (track > step.t && track - step.t < extent) extent = track - step.t;
            step.extentMs = extent;
          });
        }
        return out;
      };
      const out = { ...seq, steps: resolveBranch(seq.steps) };
      if (Array.isArray(seq.closeSteps)) out.closeSteps = resolveBranch(seq.closeSteps);
      return out;
    },

    /**
     * The beat rules on each step (src/seq_json.cpp parseStepBeats()): a beat
     * or a span needs a tempo, a step in a loop body carries no beat, a beat
     * is a whole 0..1200 and a span a whole 1..1200 on a step that has a
     * duration to set. `label` is the branch's key, for the field a refusal
     * names.
     */
    _validateBeats(steps, tempo, label = "steps") {
      const inLoop = loopBodyIndices(steps);
      for (let i = 0; i < steps.length; i++) {
        const step = steps[i] || {};
        if (step.beat !== undefined) {
          if (tempo === undefined) {
            return { ok: false, field: `${label}[${i}].beat`, error: "Set a tempo before putting a step on a beat" };
          }
          if (inLoop.has(i)) {
            return {
              ok: false,
              field: `${label}[${i}].beat`,
              error: "A step inside a repeat is timed from the repeat. Put the repeat on the beat instead.",
            };
          }
          if (!isWhole(step.beat) || step.beat < 0 || step.beat > TEMPO_BEAT_MAX) {
            return { ok: false, field: `${label}[${i}].beat`, error: "Pick a beat on the grid" };
          }
        }
        for (const key of ["stepBeats", "repeatBeats", "extentBeats"]) {
          if (step[key] === undefined) continue;
          if (step.type !== "gesture") {
            return { ok: false, field: `${label}[${i}].${key}`, error: "Only a gesture keeps its pace in beats" };
          }
          if (tempo === undefined) {
            return { ok: false, field: `${label}[${i}].${key}`, error: "Set a tempo before timing a gesture in beats" };
          }
          if (!isWhole(step[key]) || step[key] < 1 || step[key] > TEMPO_BEAT_MAX) {
            return { ok: false, field: `${label}[${i}].${key}`, error: `A gesture's beats are 1 to ${TEMPO_BEAT_MAX}` };
          }
        }
        if (step.spanBeats !== undefined) {
          if (tempo === undefined) {
            return { ok: false, field: `${label}[${i}].spanBeats`, error: "Set a tempo before timing a step in beats" };
          }
          if (!isWhole(step.spanBeats) || step.spanBeats < 1 || step.spanBeats > TEMPO_BEAT_MAX) {
            return { ok: false, field: `${label}[${i}].spanBeats`, error: `A span is 1 to ${TEMPO_BEAT_MAX} beats` };
          }
          const turns = step.type === "domeRotate" && Number(step.speedPct) !== 0;
          const flutters = step.type === "body" && step.shape === "flutter";
          if (!turns && !flutters) {
            return { ok: false, field: `${label}[${i}].spanBeats`, error: "Only a dome turn or a flutter lasts a number of beats" };
          }
        }
      }
      return { ok: true };
    },

    /**
     * Validate entire sequence.
     * @param {object} seq
     * @returns {{ok: boolean, field?: string, error?: string}}
     */
    validateSequence(seq) {
      if (!seq || typeof seq !== "object") {
        return { ok: false, error: "This sequence is missing its details" };
      }

      // The tempo and the beats first, as the droid parses them: every rule
      // below reads the steps at the milliseconds their beats resolve to.
      if (seq.tempo !== undefined) {
        const tempoVal = this.validateTempo(seq.tempo);
        if (!tempoVal.ok) return tempoVal;
      }
      if (Array.isArray(seq.steps)) {
        const written = this._validateWritten(seq.steps, seq.tempo, "steps");
        if (!written.ok) return written;
      }
      const { name, suppressMs, toggleGroup, steps, closeSteps } = this.resolveBeats(seq);

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
      const main = this._validateBranch(steps, "steps");
      if (!main.ok) return main;

      // The close half (protocolCheck(), src/protocol_check.cpp): a sequence
      // in an interrupt group is a toggle, which runs its steps to open and
      // its close half to close, so it needs one; any other must not carry
      // one. An empty list is no close half, as the wire reads it
      // (seqJsonParseVariant(), src/seq_json.cpp). It is a branch like the
      // steps, timed on the same tempo and held to the same rules.
      const isToggle = toggleGroup !== "none";
      const hasClose = Array.isArray(closeSteps) && closeSteps.length > 0;
      if (isToggle && !hasClose) {
        return { ok: false, field: "closeSteps", error: "A sequence in an interrupt group needs a close half: the steps that close what it opened" };
      }
      if (!isToggle && hasClose) {
        return { ok: false, field: "closeSteps", error: "Only a sequence in an interrupt group has a close half" };
      }
      if (hasClose) {
        const written = this._validateWritten(seq.closeSteps, seq.tempo, "closeSteps");
        if (!written.ok) return written;
        const close = this._validateBranch(closeSteps, "closeSteps");
        if (!close.ok) return close;
      }

      return { ok: true };
    },

    // What the wire's parser holds one branch to as it reads it
    // (parseBranch(), src/seq_json.cpp): the beat rules, no sequence inside a
    // loop, and Protocol Check's cap on sequences in one branch. `steps` is
    // the branch as written, before its beats are resolved, and `label` its
    // key: "steps" or "closeSteps".
    _validateWritten(steps, tempo, label) {
      const beatVal = this._validateBeats(steps, tempo, label);
      if (!beatVal.ok) return beatVal;
      // A sequence is spliced in where it sits, which a loop body cannot take.
      const inLoop = loopBodyIndices(steps);
      const looped = steps.findIndex((step, i) => step && step.type === "sequence" && inLoop.has(i));
      if (looped >= 0) {
        return { ok: false, field: `${label}[${looped}].type`, error: "A sequence cannot sit inside a repeat" };
      }
      if (steps.filter((step) => step && step.type === "sequence").length > 8) {
        return { ok: false, field: label, error: "A sequence can hold at most 8 others" };
      }
      return { ok: true };
    },

    // One branch at the milliseconds it runs at, by the rules the droid
    // applies to the steps and to the close half alike
    // (protocolCheckBranch(), src/protocol_check.cpp).
    _validateBranch(steps, label) {
      if (steps.length > this.MAX_STEPS) {
        return { ok: false, error: `A sequence can have at most ${this.MAX_STEPS} steps` };
      }

      // One Sequence End, and it is the last step: the droid stops reading a
      // branch there, so one anywhere else is refused before anything after
      // it is looked at.
      const early = steps.findIndex((step, i) => step && step.type === "end" && i !== steps.length - 1);
      if (early >= 0) {
        return { ok: false, field: `${label}[${early}].type`, error: "Sequence End must be the last step" };
      }
      const lastStep = steps[steps.length - 1];
      if (!lastStep || lastStep.type !== "end") {
        return { ok: false, error: "The sequence must finish with a Sequence End step" };
      }

      // Identify loop body step indices so we can skip outer non-decreasing time
      // check for them — body step times are relative to the loop iteration.
      const bodyStepIndices = loopBodyIndices(steps);

      // Validate each step individually
      let lastOuterT = -1;
      for (let i = 0; i < steps.length; i++) {
        const isBody = bodyStepIndices.has(i);
        // Pass isBranchRoot=true to suppress validateStep's built-in non-decreasing
        // check; outer-sequence ordering is enforced below instead.
        const stepVal = this.validateStep(steps[i], i, steps, true);
        if (!stepVal.ok) {
          return {
            ok: false,
            field: stepVal.field || `${label}[${i}]`,
            error: stepVal.error,
          };
        }
        // A Marcduino sequence trigger is refused among the steps a loop
        // repeats (":SE not allowed inside loops", protocolCheckBranch()).
        if (isBody && steps[i].type === "dome" && String(steps[i].cmd || "").startsWith(":SE")) {
          return { ok: false, field: `${label}[${i}].cmd`, error: "A Marcduino sequence (:SE) cannot sit inside a repeat" };
        }
        if (!isBody) {
          if (lastOuterT >= 0 && steps[i].t < lastOuterT) {
            return {
              ok: false,
              field: `${label}[${i}].t`,
              error: `This step must happen at or after the previous step (${lastOuterT}ms)`,
            };
          }
          lastOuterT = steps[i].t;
        }
      }

      // :OF cleanup is one list across the whole branch, in the order the
      // steps are stored, the steps a loop repeats among them: a flutter
      // inside a loop is cleaned by a close after the loop, and one before a
      // loop by a close inside it (protocolCheckBranch()'s pendingFlutter,
      // checked once at the branch's end).
      return this._checkBranchOfCleanup(steps);
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

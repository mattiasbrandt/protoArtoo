// =============================================================================
// data/dome_lights.js
//
// The dome controller's light vocabulary, with the words a builder reads: the
// tokens src/protocol_check.cpp validates and the label for each. Two surfaces
// read it - Sequences, to author a light step and to validate one
// (data/seq.js, data/seq_protocol_check.js), and Lights, to offer the same
// modes and colors as a live control (data/lights.js) - so it is a file of its
// own rather than a part of the validator Lights has no use for (#466).
// =============================================================================
(() => {
  "use strict";

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

  // The dome controller's own light vocabulary, with the words a builder reads.
  // The tokens are the ones src/protocol_check.cpp validates, above; the labels
  // are the ones a builder reads when authoring a step. Published because
  // Lights offers the same modes and colors as a live control (#410, ADR
  // 0067), and two surfaces naming one mode two things is the drift ADR 0045
  // exists to stop. This is the one copy: data/seq.js reads every label it
  // shows from here - a step's words and the Picked block tab (#441) - and
  // data/lights.js reads the logic and PSI groups.
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
  window.DomeLights = Object.freeze({
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
  });
})();

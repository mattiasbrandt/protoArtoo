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
  const STEP_TYPES = ["audio", "dome", "loop", "random", "audioCat", "end"];
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
            "Name must match DM:[A-Z0-9_]{1,18} (uppercase alphanumeric + underscore, 1-18 chars after DM:)",
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
          error: `suppressMs must be between ${SUPPRESS_MS_MIN} and ${SUPPRESS_MS_MAX}ms`,
        };
      }
      if (suppressMs < endT) {
        return {
          ok: false,
          error: `suppressMs (${suppressMs}ms) must be >= end time (${endT}ms)`,
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
        return { ok: false, error: "Step must be an object" };
      }

      const { t, type } = step;

      // Timing
      if (typeof t !== "number" || t < 0 || t > 120000) {
        return {
          ok: false,
          field: "t",
          error: "Step time must be 0–120000ms",
        };
      }

      // Non-decreasing check (outer sequence only; loop body steps use relative time)
      if (!isBranchRoot && stepIndex > 0) {
        const prevT = allSteps[stepIndex - 1]?.t || 0;
        if (t < prevT) {
          return {
            ok: false,
            field: "t",
            error: `Step time must be >= previous step (${prevT}ms)`,
          };
        }
      }

      // Type
      if (!STEP_TYPES.includes(type)) {
        return {
          ok: false,
          field: "type",
          error: `Type must be one of: ${STEP_TYPES.join(", ")}`,
        };
      }

      switch (type) {
        case "audio":    return this._validateAudioStep(step);
        case "dome":     return this._validateDomeStep(step);
        case "loop":     return this._validateLoopStep(step, allSteps);
        case "random":   return this._validateRandomStep(step);
        case "audioCat": return this._validateAudioCatStep(step);
        case "end":      return { ok: true };
        default:         return { ok: true };
      }
    },

    _validateAudioStep(step) {
      const { cmd } = step;
      if (!cmd || typeof cmd !== "string") {
        return { ok: false, field: "cmd", error: "Audio command required" };
      }
      if (!cmd.startsWith("$")) {
        return {
          ok: false,
          field: "cmd",
          error: "Audio command must start with $ (e.g., $H for Happy)",
        };
      }
      return { ok: true };
    },

    _validateDomeStep(step) {
      const { cmd } = step;
      if (!cmd || typeof cmd !== "string") {
        return { ok: false, field: "cmd", error: "Dome command required" };
      }

      // Explicit rejection with clear actionable message
      if (cmd.startsWith(":SM")) {
        return {
          ok: false,
          field: "cmd",
          error:
            ":SM is diagnostic/calibration only — use :OP/:CL/:OF panel intent commands in sequences",
        };
      }
      if (/^DM:/.test(cmd)) {
        return {
          ok: false,
          field: "cmd",
          error: "DM:* is a sequence trigger, not a step command",
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
              `Unknown DV: visual preset "${cmd.slice(3)}". ` +
              "Allowed: " + Array.from(DV_PRESETS).join(", "),
          };
        }
        return { ok: true };
      }

      // Panel intent: :OP<target>, :CL<target>, :OF<target>
      if (cmd.startsWith(":OP") || cmd.startsWith(":CL") || cmd.startsWith(":OF")) {
        const target = cmd.slice(3);
        if (!PANEL_INTENT_TARGETS.has(target)) {
          const numericAmbiguous = /^\d{2}$/.test(target);
          const hint = numericAmbiguous
            ? " (targets 08-10 and 12 are ambiguous pie/ring IDs — use explicit aliases P1-P6 for pie panels)"
            : "";
          return {
            ok: false,
            field: "cmd",
            error:
              `Panel target "${target}" not in allowed set${hint}. ` +
              "Use ring (01-04,07,11,13), pie (P1-P6), or group (00=all, 14=pie, 15=ring)",
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
            error: ":SE requires exactly 2 digits (e.g., :SE07)",
          };
        }
        return { ok: true };
      }

      return {
        ok: false,
        field: "cmd",
        error:
          "Dome command not recognized. Use :OP/:CL/:OF for panels, @... for logic/PSI, *... for holos, DV:<name> for dome visual presets, or :SE## for Marcduino sequences",
      };
    },

    _validateLoopStep(step, _allSteps) {
      const { body, periodMs, durationMs } = step;

      if (typeof body !== "number" || body < 1 || body > 96) {
        return {
          ok: false,
          field: "body",
          error: "Loop body must be 1–96 steps",
        };
      }

      if (typeof periodMs !== "number" || periodMs < 100 || periodMs > 60000) {
        return {
          ok: false,
          field: "periodMs",
          error: "Loop period must be 100–60000ms",
        };
      }

      if (typeof durationMs !== "number" || durationMs < 100 || durationMs > 120000) {
        return {
          ok: false,
          field: "durationMs",
          error: "Loop duration must be 100–120000ms",
        };
      }

      if (periodMs > durationMs) {
        return {
          ok: false,
          field: "periodMs",
          error: "Loop period must be <= duration",
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
            "Random pulse ranges are not supported — use mode (flutter/open/close) with a logical target set instead",
        };
      }

      if (!RANDOM_SETS.includes(set)) {
        return {
          ok: false,
          field: "set",
          error: `Random set must be one of: ${RANDOM_SETS.join(", ")}`,
        };
      }

      if (!RANDOM_MODES.includes(mode)) {
        return {
          ok: false,
          field: "mode",
          error: `Random mode must be one of: ${RANDOM_MODES.join(", ")}`,
        };
      }

      if (typeof moveMs !== "number" || moveMs < 0 || moveMs > 5000) {
        return {
          ok: false,
          field: "moveMs",
          error: "Move time must be 0–5000ms",
        };
      }

      if (typeof jitterMs !== "number" || jitterMs < 0 || jitterMs > 2000) {
        return {
          ok: false,
          field: "jitterMs",
          error: "Jitter must be 0–2000ms",
        };
      }

      if (typeof distinct !== "boolean") {
        return {
          ok: false,
          field: "distinct",
          error: "Distinct must be true or false",
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
          error: `Category must be one of: ${AUDIO_CATEGORIES.join(", ")}`,
        };
      }

      if (!fallback || typeof fallback !== "string") {
        return { ok: false, field: "fallback", error: "Fallback track required" };
      }

      if (!fallback.startsWith("$")) {
        return {
          ok: false,
          field: "fallback",
          error: "Fallback must be a named track (e.g., $H)",
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
          error: `Panel flutter without cleanup: ${targets} — each :OF step must be followed by a matching :CL in the same branch`,
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
        return { ok: false, error: "Sequence must be an object" };
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
          error: `Toggle group must be one of: ${TOGGLE_GROUPS.join(", ")}`,
        };
      }

      // Steps array
      if (!Array.isArray(steps)) {
        return { ok: false, field: "steps", error: "Steps must be an array" };
      }
      if (steps.length === 0) {
        return { ok: false, error: "Sequence must have at least one step" };
      }
      if (steps.length > 96) {
        return { ok: false, error: "Sequence cannot exceed 96 steps" };
      }

      // Must end with 'end' type
      const lastStep = steps[steps.length - 1];
      if (lastStep.type !== "end") {
        return { ok: false, error: "Sequence must end with an 'end' type step" };
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
              error: `Step time must be >= previous step (${lastOuterT}ms)`,
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

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
  const RANDOM_SETS = ["ring", "pie"];

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
     * @param {boolean} isBranchRoot - whether step is in a loop body
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

      // Non-decreasing check (outside loop bodies)
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

      // Type-specific validation
      switch (type) {
        case "audio":
          return this._validateAudioStep(step);
        case "dome":
          return this._validateDomeStep(step);
        case "loop":
          return this._validateLoopStep(step, allSteps);
        case "random":
          return this._validateRandomStep(step);
        case "audioCat":
          return this._validateAudioCatStep(step);
        case "end":
          return this._validateEndStep(step);
        default:
          return { ok: true };
      }
    },

    _validateAudioStep(step) {
      const { cmd } = step;
      if (!cmd || typeof cmd !== "string") {
        return { ok: false, field: "cmd", error: "Audio command required" };
      }
      // Basic check: starts with $ or is a named track reference
      if (!cmd.startsWith("$")) {
        return {
          ok: false,
          field: "cmd",
          error: "Audio command must start with $ (e.g., $H for Hello)",
        };
      }
      return { ok: true };
    },

    _validateDomeStep(step) {
      const { cmd } = step;
      if (!cmd || typeof cmd !== "string") {
        return { ok: false, field: "cmd", error: "Dome command required" };
      }

      // Recognized dome commands: :SM..., @, *, :SE##, :CL00
      const validPatterns = [
        /^:SM\d{1,2},\d{3,4},\d{1,4}$/, // :SM<slot>,<pulse>,<ms>
        /^@$/, // Sweep
        /^\*$/, // Random pulse
        /^:SE\d{2}$/, // Sound effect :SE##
        /^:CL00$/, // Center light reset
      ];

      const isValid = validPatterns.some((pattern) => pattern.test(cmd));
      if (!isValid) {
        return {
          ok: false,
          field: "cmd",
          error: "Dome command not recognized (valid: :SM<slot>,<pulse>,<ms>, @, *, :SE##, :CL00)",
        };
      }

      // If :SM, validate components
      if (cmd.startsWith(":SM")) {
        const match = cmd.match(/^:SM(\d{1,2}),(\d{3,4}),(\d{1,4})$/);
        if (match) {
          const slot = parseInt(match[1], 10);
          const pulse = parseInt(match[2], 10);
          const ms = parseInt(match[3], 10);

          if (slot < 0 || slot > 12) {
            return {
              ok: false,
              field: "cmd",
              error: "Dome slot must be 0–12",
            };
          }
          if (pulse < 800 || pulse > 2200) {
            return {
              ok: false,
              field: "cmd",
              error: "Dome pulse must be 800–2200",
            };
          }
          if (ms < 50 || ms > 5000) {
            return {
              ok: false,
              field: "cmd",
              error: "Dome move time must be 50–5000ms",
            };
          }
        }
      }

      return { ok: true };
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

      if (
        typeof periodMs !== "number" ||
        periodMs < 100 ||
        periodMs > 60000
      ) {
        return {
          ok: false,
          field: "periodMs",
          error: "Loop period must be 100–60000ms",
        };
      }

      if (
        typeof durationMs !== "number" ||
        durationMs < 100 ||
        durationMs > 120000
      ) {
        return {
          ok: false,
          field: "durationMs",
          error: "Loop duration must be 100–120000ms",
        };
      }

      // Period must be <= duration
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
      const { set, pulseMin, pulseMax, moveMs, jitterMs, distinct } = step;

      if (!RANDOM_SETS.includes(set)) {
        return {
          ok: false,
          field: "set",
          error: `Random set must be one of: ${RANDOM_SETS.join(", ")}`,
        };
      }

      if (
        typeof pulseMin !== "number" ||
        pulseMin < 800 ||
        pulseMin > 2200
      ) {
        return {
          ok: false,
          field: "pulseMin",
          error: "Min pulse must be 800–2200",
        };
      }

      if (
        typeof pulseMax !== "number" ||
        pulseMax < 800 ||
        pulseMax > 2200
      ) {
        return {
          ok: false,
          field: "pulseMax",
          error: "Max pulse must be 800–2200",
        };
      }

      if (pulseMin > pulseMax) {
        return {
          ok: false,
          field: "pulseMin",
          error: "Min pulse must be <= max pulse",
        };
      }

      if (typeof moveMs !== "number" || moveMs < 50 || moveMs > 5000) {
        return {
          ok: false,
          field: "moveMs",
          error: "Move time must be 50–5000ms",
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
        return {
          ok: false,
          field: "fallback",
          error: "Fallback track required",
        };
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

    _validateEndStep(_step) {
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

      // Suppress
      let endT = 0;
      if (steps && steps.length > 0) {
        const endStep = steps.find((s) => s.type === "end");
        if (endStep) endT = endStep.t || 0;
      }
      const suppressVal = this.validateSuppressMs(suppressMs, endT);
      if (!suppressVal.ok) {
        return {
          ok: false,
          field: "suppressMs",
          error: suppressVal.error,
        };
      }

      // Toggle group
      if (!TOGGLE_GROUPS.includes(toggleGroup)) {
        return {
          ok: false,
          field: "toggleGroup",
          error: `Toggle group must be one of: ${TOGGLE_GROUPS.join(", ")}`,
        };
      }

      // Steps
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

      // Identify loop body step indices so we can skip the non-decreasing time check
      // for them — body step t values are relative to the loop iteration, not absolute.
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

      // Validate each step. Non-decreasing time check is applied only to
      // outer-sequence steps; body steps have their own per-iteration timing.
      const warnings = [];
      let lastOuterT = -1;
      for (let i = 0; i < steps.length; i++) {
        const isBody = bodyStepIndices.has(i);
        // Pass isBranchRoot=true to suppress validateStep's built-in non-decreasing
        // check; we enforce it below for outer steps only.
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
        if (typeof step.t === "number") {
          maxT = Math.max(maxT, step.t);
        }
      });
      return maxT;
    },
  };

  window.SeqProtocolCheck = SeqProtocolCheck;
})();

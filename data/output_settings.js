// =============================================================================
// data/output_settings.js
//
// Which servo each Output carries, picked on Servos, on the Output's own row
// there (#399, operator 2026-09-28: "clean and nice"). These were
// Configuration's rows until the operator moved them where the question is
// asked (2026-09-18 on #369). Wiring's Output plates - a wired switch and a
// Servo / LED strip choice per Output - are gone: Wiring is part-first, and an
// Output with a Part on it is wired (operator, 2026-09-29 on #411, CONTEXT.md
// "Wiring"). What is on a Part's wire is chosen on that Part's row there
// (data/parts_mapping.js picker()), with this file's segmented control.
//
// THIS FILE DRAWS; data/outputs.js KNOWS. Which Outputs the droid has, what
// each is called, whether it is wired, what is on its wire and what it can
// save are data/outputs.js's answer (#415), and a plate is drawn per Output
// in the order it gives them. An Output is called by what its board prints
// (CONTEXT.md "Output Address"), never by a name this file could have made
// up, and never split into kinds.
//
// A pick is drawn at once, before the droid has taken it. A picked-but-unsaved
// answer is the only thing this file holds, and it is dropped the moment the
// droid answers. It writes only through data/outputs.js.
//
// WHEN A PICK BITES is its row Setting's timing, which the firmware declares
// and the Setting's entry mirrors (data/web_api.js, #432), said in the one
// timing vocabulary (data/apply_timing.js, #370): which servo an output
// carries lands on its Servo Output row and bounds the very next move
// (configCommitApplied()).
// =============================================================================
(() => {
  "use strict";

  const OUTPUTS = window.PAOutputs;
  const TIMING = window.PAApplyTiming;
  // A pick's timing is its row Setting's, read off the entry (data/web_api.js).
  const TYPE_TIMING = window.PAApi.rowTimingOf("component");

  const views = [];
  // What a builder has picked and the droid has not answered yet, by Output
  // Address: { type }.
  const picked = new Map();
  let saveTimer = null;

  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  // An Output as it is drawn: what the droid holds, with a pick not yet
  // saved laid over it.
  const shown = (output) => {
    const pick = picked.get(output.address);
    if (!pick) return output;
    const type = "type" in pick ? pick.type : output.type;
    return { ...output, ...pick, type, light: OUTPUTS.lightType(type), servo: OUTPUTS.servoModel(type) };
  };

  const setFeedback = (message, variant = "") => {
    views.forEach((view) => {
      view.feedback.textContent = message;
      view.feedback.className = variant ? `feedback ${variant}` : "feedback";
    });
  };

  const save = async () => {
    if (picked.size === 0) return;
    const sent = new Map(picked);
    setFeedback("Saving…");
    try {
      await OUTPUTS.saveAll(Object.fromEntries(sent));
      // When it takes effect is the timing line's to say, just above: this
      // line says only that the droid took it (operator, 2026-09-19 on #370).
      setFeedback(`Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[outputs] save failed:", error);
      setFeedback(window.PAApi.messageFor(error), "error");
    } finally {
      // Taken or refused, the droid's answer is what is drawn now. A pick made
      // while this one was on its way is a newer object and stays.
      sent.forEach((pick, address) => {
        if (picked.get(address) === pick) picked.delete(address);
      });
      renderAll();
    }
  };

  // Picking is applying: the change is drawn at once and saved after a short
  // settle, so a builder picking on three rows sends one request, not three.
  const change = (address, patch) => {
    picked.set(address, { ...picked.get(address), ...patch });
    renderAll();
    clearTimeout(saveTimer);
    saveTimer = setTimeout(save, 300);
  };

  // ---------------------------------------------------------------------------
  // Drawing
  // ---------------------------------------------------------------------------
  const segmented = (label, options, current, onPick) => {
    const row = element("div", "seg output-seg");
    row.setAttribute("role", "radiogroup");
    row.setAttribute("aria-label", label);
    options.forEach((option) => {
      const on = option.id === current;
      const button = element("button", on ? "active" : "", option.label);
      button.type = "button";
      button.dataset.value = option.id;
      button.setAttribute("role", "radio");
      button.setAttribute("aria-checked", on ? "true" : "false");
      button.addEventListener("click", () => {
        if (!on) onPick(option.id);
      });
      row.appendChild(button);
    });
    return row;
  };

  // Servos' pick: which servo the output carries, drawn into the slot the host
  // keeps for that Output on its own row, so the row's name, what it drives and
  // why it will not drive are the row's and not said twice. An Output set to
  // a light has no servo to pick (its row says what it carries), and one the
  // droid names no save fields for has nothing to pick with. The slot wears
  // is-on while the Output is wired, from the same answer Wiring draws, so a
  // Part put on it there shows here at once.
  const drawPick = (output, slot) => {
    slot.classList.toggle("is-on", output.wired === true);
    if (output.light || !output.switchable) {
      slot.replaceChildren();
      return;
    }
    const options = output.canLight ? [OUTPUTS.NO_SERVO, ...OUTPUTS.SERVO_MODELS] : OUTPUTS.SERVO_MODELS;
    slot.replaceChildren(segmented(`${output.name} servo`, options, output.type,
      (value) => change(output.address, { type: value })));
  };

  // When a pick bites, beside the outputs it asks about.
  const paintTiming = (host) => {
    TIMING.paint(host, TYPE_TIMING);
  };

  // Servos' view has no body of its own: its picks live in the host's rows,
  // which the host builds, so an Output with no row yet is simply not drawn
  // and the host calls this again once it has built them (mount()'s answer).
  // Until the rows have answered the host's own rows say so.
  const renderPicks = (view) => {
    if (!OUTPUTS.known().table) return;
    OUTPUTS.list().forEach((output) => {
      const slot = view.slot(output.address);
      if (slot) drawPick(shown(output), slot);
    });
    if (view.timing) paintTiming(view.timing);
  };

  const renderAll = () => views.forEach(renderPicks);

  OUTPUTS.onChange(renderAll);

  /**
   * Draw Servos' servo picks into its rows. The host reads the droid
   * (data/outputs.js load()); the picks are drawn again whenever that answer
   * changes.
   *
   * @param {"type"} kind - Servos' servo types, the one view left
   * @param {object} hosts
   * @param {function} hosts.slot - the element on an Output's row its pick
   *   goes in, by Output Address, or null while it has no row
   * @param {Element} [hosts.timing] - the line saying when a pick bites
   * @param {Element} hosts.feedback - the save line
   * @returns {function|undefined} draws the view again, for a host that has
   *   just rebuilt the rows its picks live in
   */
  const mount = (kind, hosts) => {
    if (kind !== "type" || !hosts?.feedback || typeof hosts.slot !== "function") return undefined;
    const view = { ...hosts, kind };
    views.push(view);
    renderPicks(view);
    return () => renderPicks(view);
  };

  // The segmented control a small set of peers is picked with, for a surface
  // that draws such a pick of its own: Wiring's servo or Light Type on a
  // Part's row (data/parts_mapping.js picker()).
  window.PAOutputSettings = Object.freeze({ mount, segmented });
})();

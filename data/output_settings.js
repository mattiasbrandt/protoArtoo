// =============================================================================
// data/output_settings.js
//
// The body controller's Outputs as a builder sets them up: which ones are
// wired and what each carries - a servo, or a light. These
// were Configuration's rows until the operator moved them where the question
// is asked (2026-09-18 on #369): the wired ticks and the Light Type choice to
// Wiring, beside where each wire plugs in, and the servo type to Servos, with
// the line it drives.
//
// THIS FILE DRAWS; data/outputs.js KNOWS. Which Outputs the droid has, what
// each is called, whether it is wired, what is on its wire and which fields
// save it are data/outputs.js's answer (#415), and a plate is drawn per Output
// in the order it gives them. An Output is called by what its board prints
// (CONTEXT.md "Output Address"), never by a name this file could have made
// up, and never split into kinds.
//
// ONE ANSWER, TWO VIEWS. Wiring and Servos each mount a view of the same
// answer, and a pick made on one is drawn on both at once, before the droid
// has taken it - the same rule the Component Picker keeps for its two homes.
// A picked-but-unsaved answer is the only thing this file holds, and it is
// dropped the moment the droid answers.
//
// Wiring's sheet itself stays a reference: data/wiring.js generates the
// document and writes nothing. These plates are the one thing on that surface
// that writes, and they write only through data/outputs.js.
//
// WHEN EACH VIEW'S ANSWER BITES differs, and each view says so in the one
// timing vocabulary (data/apply_timing.js, #370). Whether an output is wired,
// and which Outputs carry a light, are read once at start (ADR 0027,
// src/tasks/aux_led.cpp); which servo an output carries lands on its Servo
// Output row and bounds the very next move (configCommitApplied()).
// =============================================================================
(() => {
  "use strict";

  const OUTPUTS = window.PAOutputs;
  const TIMING = window.PAApplyTiming;
  const VIEW_TIMING = { wired: TIMING.AT_REBOOT, type: TIMING.IMMEDIATE };

  const views = [];
  // What a builder has picked and the droid has not answered yet, by Output
  // Address: { wired?, type? }. Drawn on every view at once.
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

  // A saved wired tick or Light Type the droid has not started with yet.
  const waitingOnStart = () =>
    OUTPUTS.list().some((output) => {
      if (!output.started) return false;
      const now = shown(output);
      return now.wired !== output.started.wired || (now.light ? now.light.id : null) !== output.started.light;
    });

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
  // settle, so a builder ticking three lines sends one request, not three.
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

  // The wire's color is picked by its place in the firmware's order, from the
  // numbered --wire-* palette in data/style.css, the same way data/wiring.js
  // picks it for the line on the sheet (CONTEXT.md "Status Color": it names
  // a wire, never a state). The plate carries only that place, as data-wire;
  // the stylesheet maps it to the color, so this file holds none.
  const WIRE_PALETTE = 8;
  const wireSlot = (place) => String((place % WIRE_PALETTE) + 1);

  const WIRED = "Wired";
  const NOT_WIRED = "Not wired";

  // Wiring's plate: the Output as it sits on the board - its printed name, the
  // three-pin header its wire plugs onto, whether it is wired - and, on an
  // Output that can carry one, whether it carries a servo or a light.
  // The whole head is the press. The plate wears its wire's color
  // (its data-wire, from wireSlot()), the same color that wire is drawn in on
  // the sheet above, so a plate and its line on the diagram are found by eye (operator, 2026-09-19 on #411: the Outputs
  // section "looks to basic and boring"). An Output the droid names no save
  // fields for has no switch, and says what it is.
  const wiredPlate = (output, place) => {
    const plate = element("div", "output-plate output-setting output-wire");
    plate.dataset.output = output.address;
    plate.dataset.wire = wireSlot(place);
    if (output.wired) plate.classList.add("is-on");
    const press = element(output.switchable ? "button" : "div", "output-setting-head output-wire-head");
    // Signal, power and ground, left to right, as a servo header is laid out.
    // Only the signal pin is this sheet's to light: power is the builder's.
    const header = element("span", "output-wire-header");
    header.setAttribute("aria-hidden", "true");
    ["signal", "power", "ground"].forEach((pin) => header.appendChild(element("span", `output-wire-pin is-${pin}`)));
    press.appendChild(header);
    press.appendChild(element("span", "toggle-label output-wire-name", output.name));
    press.appendChild(element("span", "toggle-status", output.wired ? WIRED : NOT_WIRED));
    plate.appendChild(press);
    if (!output.switchable) return plate;
    press.type = "button";
    press.setAttribute("aria-pressed", output.wired ? "true" : "false");
    press.addEventListener("click", () => change(output.address, { wired: !output.wired }));
    if (output.canLight) {
      // What is on this wire: a servo, or one of the Light Types. Which servo
      // MODEL is Servos' question; this asks only which of the two it is.
      const carries = output.light ? output.light.id : "servo";
      plate.appendChild(segmented(`${output.name} carries`, [{ id: "servo", label: "Servo" }, ...OUTPUTS.LIGHT_TYPES], carries,
        (value) => change(output.address, { type: OUTPUTS.lightType(value) ? value : OUTPUTS.NO_SERVO.id })));
    } else {
      plate.appendChild(element("p", "output-wire-carries", "Servo only"));
    }
    return plate;
  };

  // Servos' plate: which servo the output carries. An Output set to LED strip
  // has no servo, and says where that is answered.
  //
  // A host may say what is on the end of each Output's wire (`describe`,
  // Servos' Part names): it goes under the head, and an Output with nothing
  // assigned gets no line at all (operator, 2026-09-19 on #412: "if it has
  // one"). Read-only: the assignment is Parts'.
  const typePlate = (output, place, view) => {
    const plate = element("div", "output-plate output-setting");
    plate.dataset.output = output.address;
    if (output.wired) plate.classList.add("is-on");
    const head = element("div", "output-setting-head");
    head.appendChild(element("span", "toggle-label", output.name));
    head.appendChild(element("span", "toggle-status", output.wired ? WIRED : NOT_WIRED));
    plate.appendChild(head);
    const onIt = typeof view?.describe === "function" ? view.describe(output) : "";
    if (onIt) plate.appendChild(element("p", "output-parts", onIt));
    if (output.light) {
      plate.appendChild(element("p", "hint output-setting-note", `Carries the ${output.light.label}. Set on Wiring.`));
      return plate;
    }
    if (!output.switchable) return plate;
    const options = output.canLight ? [OUTPUTS.NO_SERVO, ...OUTPUTS.SERVO_MODELS] : OUTPUTS.SERVO_MODELS;
    plate.appendChild(segmented(`${output.name} servo`, options, output.type,
      (value) => change(output.address, { type: value })));
    return plate;
  };

  const render = (view) => {
    // A plate saves the config's answer, so it waits for that answer: a servo
    // table read alone would draw every Output as wired with no switch.
    if (!OUTPUTS.known().config) {
      view.body.replaceChildren(element("p", "hint", "Reading the outputs from the droid…"));
      return;
    }
    const outputs = OUTPUTS.list();
    if (outputs.length === 0) {
      view.body.replaceChildren(element("p", "hint", "The droid reports no outputs to wire."));
      return;
    }
    const plates = element("div", "output-plates");
    outputs.forEach((output, place) => plates.appendChild(view.plate(shown(output), place, view)));
    // When this view's answer bites, beside the outputs it asks about.
    const timing = element("p", "apply-timing");
    TIMING.paint(timing, VIEW_TIMING[view.kind], { pending: VIEW_TIMING[view.kind] === TIMING.AT_REBOOT && waitingOnStart() });
    view.body.replaceChildren(plates, timing);
  };

  const renderAll = () => views.forEach(render);

  OUTPUTS.onChange(renderAll);

  /**
   * Draw one view of the outputs into a host. The host reads the droid
   * (data/outputs.js load()); the view is drawn again whenever that answer
   * changes.
   *
   * @param {"wired"|"type"} kind - Wiring's wired ticks, or Servos' servo types
   * @param {object} hosts
   * @param {Element} hosts.body - where the plates go
   * @param {Element} hosts.feedback - the save line under them
   * @param {function} [hosts.describe] - what is on an Output's wire, or ""
   */
  const mount = (kind, hosts) => {
    if (!hosts?.body || !hosts?.feedback) return;
    const view = { ...hosts, kind: kind === "type" ? "type" : "wired", plate: kind === "type" ? typePlate : wiredPlate };
    views.push(view);
    render(view);
  };

  window.PAOutputSettings = Object.freeze({ mount });
})();

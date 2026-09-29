// =============================================================================
// data/output_settings.js
//
// The body controller's Outputs as a builder sets them up: which ones are
// wired and what each carries - a servo, or a light. These
// were Configuration's rows until the operator moved them where the question
// is asked (2026-09-18 on #369): the wired ticks and the Light Type choice to
// Wiring, beside where each wire plugs in, and the servo type to Servos, on
// the Output's own row there (#399, operator 2026-09-28: "clean and nice").
//
// THIS FILE DRAWS; data/outputs.js KNOWS. Which Outputs the droid has, what
// each is called, whether it is wired, what is on its wire and what it can
// save are data/outputs.js's answer (#415), and a plate is drawn per Output
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
// document and writes nothing. These plates write beside it, only through
// data/outputs.js, and so does the part-first picker under them
// (data/parts_mapping.js picker()).
//
// WHEN EACH VIEW'S ANSWER BITES is its row Setting's timing, which the
// firmware declares and the Setting's entry mirrors (data/web_api.js, #432),
// said in the one timing vocabulary (data/apply_timing.js, #370). Whether an
// output is wired is read once at start (ADR 0027); which servo an output
// carries lands on its Servo Output row and bounds the very next move
// (configCommitApplied()).
// =============================================================================
(() => {
  "use strict";

  const OUTPUTS = window.PAOutputs;
  const TIMING = window.PAApplyTiming;
  // Each view's timing is its row Setting's, read off the entry (data/web_api.js).
  const VIEW_TIMING = {
    wired: window.PAApi.rowTimingOf("wired"),
    type: window.PAApi.rowTimingOf("component"),
  };

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

  // Wiring's plate: the Output as it sits on the board - its printed name, the
  // three-pin header its wire plugs onto, whether it is wired - and, on an
  // Output that can carry one, whether it carries a servo or a light.
  // The whole head is the press, and it is drawn as what it is: a switch
  // labelled Wired, so a builder can see that pressing a plate marks the wire
  // and does not put a Part on it - that is the table under the plates
  // (operator, 2026-09-29 on #411: "clicking a output to actually activate is
  // not very clear or intuitive"). The plate wears its wire's color
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
    // An Output with no switch says it is wired in words; the switch is only
    // drawn where a press flips it.
    if (!output.switchable) {
      press.appendChild(element("span", "toggle-status", WIRED));
      plate.appendChild(press);
      return plate;
    }
    const state = element("span", "toggle-status output-wire-state");
    const track = element("span", "output-wire-switch");
    track.setAttribute("aria-hidden", "true");
    track.appendChild(element("span", "output-wire-knob"));
    state.appendChild(track);
    state.appendChild(element("span", "output-wire-state-word", WIRED));
    press.appendChild(state);
    plate.appendChild(press);
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

  // Servos' pick: which servo the output carries, drawn into the slot the host
  // keeps for that Output on its own row, so the row's name, what it drives and
  // why it will not drive are the row's and not said twice. An Output set to
  // a light has no servo to pick (its row says what it carries), and one the
  // droid names no save fields for has nothing to pick with. The slot wears
  // is-on while the Output is wired, from the same answer Wiring draws, so a
  // tick there shows here at once.
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

  // When this view's answer bites, beside the outputs it asks about.
  const paintTiming = (view, host) => {
    TIMING.paint(host, VIEW_TIMING[view.kind], { pending: VIEW_TIMING[view.kind] === TIMING.AT_REBOOT && waitingOnStart() });
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
    if (view.timing) paintTiming(view, view.timing);
  };

  const render = (view) => {
    if (view.kind === "type") {
      renderPicks(view);
      return;
    }
    // A plate saves what the Output's row says, so it waits for the rows: until
    // they have answered there is no Output to draw, and the line says so in
    // the one word every surface uses for it (data/outputs.js live()).
    if (!OUTPUTS.known().table) {
      view.body.replaceChildren(element("p", "hint waiting", window.PALiveReading.slotText(OUTPUTS.live(null).word)));
      return;
    }
    const outputs = OUTPUTS.list();
    if (outputs.length === 0) {
      view.body.replaceChildren(element("p", "hint", "The droid reports no outputs to wire."));
      return;
    }
    const plates = element("div", "output-plates");
    outputs.forEach((output, place) => plates.appendChild(wiredPlate(shown(output), place)));
    const timing = element("p", "apply-timing");
    paintTiming(view, timing);
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
   * @param {Element} [hosts.body] - Wiring's: where the plates go
   * @param {function} [hosts.slot] - Servos': the element on an Output's row
   *   its pick goes in, by Output Address, or null while it has no row
   * @param {Element} [hosts.timing] - Servos': the line saying when a pick bites
   * @param {Element} hosts.feedback - the save line
   * @returns {function|undefined} draws the view again, for a host that has
   *   just rebuilt the rows its picks live in
   */
  const mount = (kind, hosts) => {
    const type = kind === "type";
    if (!hosts?.feedback || (type ? typeof hosts.slot !== "function" : !hosts.body)) return undefined;
    const view = { ...hosts, kind: type ? "type" : "wired" };
    views.push(view);
    render(view);
    return () => render(view);
  };

  window.PAOutputSettings = Object.freeze({ mount });
})();

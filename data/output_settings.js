// =============================================================================
// data/output_settings.js
//
// The body controller's Outputs as a builder sets them up: which ones are
// wired, what servo is on each, and which one carries the LED strip. These
// were Configuration's rows until the operator moved them where the question
// is asked (2026-09-18 on #369): the wired ticks and the LED-strip choice to
// Wiring, beside where each lead plugs in, and the servo type to Servos, with
// the line it drives.
//
// THIS FILE KNOWS NO OUTPUT. Which Outputs the body controller has, what its
// board prints beside each, which one can carry the LED strip and which config
// fields save it all come from the running firmware, in GET /api/config: every
// components{} entry that carries an `address` is an Output, in the order the
// firmware lists them (include/board_outputs.h BOARD_OUTPUTS, docs/api.md).
// A plate is drawn per entry and saved under the fields that entry names. The
// operator's rule, 2026-09-19 on #411: "the outputs is supposed to be dynamic,
// thats the whole point of the wiring and mapping we have" - and an Output is
// called by what its board prints (CONTEXT.md "Output Address"), never by a
// name this file could have made up, and never split into kinds.
//
// WHAT IS ON THE LEAD IS ONE ANSWER IN TWO VOCABULARIES (CONTEXT.md "Output",
// ADR 0067): a servo's model where the lead drives a servo, a Light Type where
// it lights something. One stored field holds either, so `rgb` is not a servo
// type and never was - it is the LED strip Light Type, and naming it is what
// took it out of the servo list. A Part inherits the type of the lead it is
// on, and nothing here asks which Part that is.
//
// ONE STATE, TWO VIEWS. Both surfaces mount a view of the same answer, read
// from GET /api/config and written back by one save, so the two can never
// show different choices - the same rule the Component Picker keeps for its
// two homes. The save sends each Output's enabled and type fields, as named,
// and aux_led_pin, derived from which wired Output carries a Light Type. One
// lead can be lit today; per-lead light settings are their own change.
//
// LIGHTS NEVER ASKS WHICH LEAD. It lists the droid's lights and says how each
// is lit, reading the `light` fact below; which Output carries one is Wiring's
// question and is answered there alone (operator, 2026-09-20 on #410).
//
// Wiring's sheet itself stays a reference: data/wiring.js generates the
// document and writes nothing. This module is the one thing on that surface
// that writes, and it writes only these fields.
//
// WHEN EACH VIEW'S ANSWER BITES differs, and each view says so in the one
// timing vocabulary (data/apply_timing.js, #370). Whether an output is wired,
// and which Output the LED strip leaves on, are read once at start (ADR 0027,
// src/tasks/aux_led.cpp); which servo an output carries lands on its Servo
// Output row and bounds the very next move (configCommitApplied()).
// =============================================================================
(() => {
  "use strict";

  // The servo an output can carry. One that cannot carry a light always
  // carries a servo; one that can may carry nothing yet, and what lights it is
  // a Light Type rather than a servo model.
  const SERVO_TYPES = [
    { id: "mg996r", label: "MG996R" },
    { id: "mg90s", label: "MG90S" },
  ];
  const NO_SERVO = { id: "none", label: "None" };

  // The Light Types protoArtoo can put on one of its own leads (CONTEXT.md
  // "Light Type", ADR 0067). One today; the list is what grows when there are
  // more, and the stored token stays the one the firmware already saves.
  const LIGHT_TYPES = [
    { id: "rgb", label: "LED strip" },
  ];
  const lightTypeOf = (token) => LIGHT_TYPES.find((type) => type.id === token) || null;
  const LED_STRIP = LIGHT_TYPES[0].id;

  const TIMING = window.PAApplyTiming;
  const VIEW_TIMING = { wired: TIMING.AT_REBOOT, type: TIMING.IMMEDIATE };

  // The Outputs as the droid reported them, in its order. Each is
  //   id           the components{} key: the stored config key, never shown
  //   label        what the board prints beside it
  //   address      its Output Address, where no label reached us
  //   strip        the aux_led_pin value that routes the LED strip to it, 0 if
  //                it cannot carry the strip
  //   enabledField, typeField   the POST /api/config fields that save it
  //   wire         its place in that order, which picks its wire's colour
  let outputs = [];
  let state = null;  // { [id]: { enabled, type } }
  // What the droid started with, as first read: the wired ticks and the
  // strip's line, the two answers that wait for the next start.
  let started = null;
  const views = [];
  const listeners = new Set();
  let saveTimer = null;
  let saving = false;
  let saveAgain = false;

  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const text = (value) => (typeof value === "string" ? value : "");

  // The Outputs in a config payload. An entry the firmware names no save
  // fields for is one this page could not save, so it is not drawn as a
  // control at all.
  const reportedOutputs = (components) =>
    Object.keys(components)
      .filter((id) => {
        const entry = components[id];
        return text(entry?.address) && text(entry.enabledField) && text(entry.typeField);
      })
      .map((id, index) => {
        const entry = components[id];
        return {
          id,
          label: text(entry.label),
          address: entry.address,
          strip: Number(entry.ledStripPin) || 0,
          enabledField: entry.enabledField,
          typeField: entry.typeField,
          wire: index + 1,
        };
      });

  // The config payload's answer for each output, as Configuration read it.
  const adopt = (payload) => {
    const components = payload?.components || {};
    outputs = reportedOutputs(components);
    const next = {};
    outputs.forEach((output) => {
      const entry = components[output.id];
      next[output.id] = {
        enabled: Boolean(entry.enabled),
        type: String(entry.type || (output.strip ? "none" : SERVO_TYPES[0].id)),
      };
    });
    // The routed strip is the controller's answer; a line it routes is an LED
    // strip even if the type field says otherwise.
    const pin = Number(payload?.aux_led_pin || 0);
    const routed = outputs.find((output) => output.strip && output.strip === pin);
    if (routed) next[routed.id].type = LED_STRIP;
    state = next;
    if (!started) started = startedFrom(state);
    renderAll();
  };

  const startedFrom = (answer) => ({
    enabled: outputs.map((output) => answer[output.id].enabled).join(","),
    ledPin: ledPinOf(answer),
  });

  // A saved wired tick or strip line the droid has not started with yet.
  const waitingOnStart = () => {
    if (!started || !state) return false;
    const now = startedFrom(state);
    return now.enabled !== started.enabled || now.ledPin !== started.ledPin;
  };

  // The line the strip leaves the controller on: the wired Output set to LED
  // strip, or none (0) - what Configuration derived from its own rows.
  const ledPinOf = (answer) =>
    outputs.find((output) => output.strip && answer[output.id].enabled && answer[output.id].type === LED_STRIP)?.strip || 0;
  const ledPin = () => ledPinOf(state);

  const fields = () => {
    const out = {};
    outputs.forEach((output) => {
      out[output.enabledField] = state[output.id].enabled ? "true" : "false";
      out[output.typeField] = state[output.id].type;
    });
    out.aux_led_pin = String(ledPin());
    return out;
  };

  const setFeedback = (message, variant = "") => {
    views.forEach((view) => {
      view.feedback.textContent = message;
      view.feedback.className = variant ? `feedback ${variant}` : "feedback";
    });
  };

  const save = async () => {
    if (!window.PAApi || !state) return;
    if (saving) {
      saveAgain = true;
      return;
    }
    saving = true;
    setFeedback("Saving…");
    try {
      const result = await window.PAApi.postForm("/api/config", fields(), { timeoutMs: 5000 });
      adopt(result.data);
      // When it takes effect is the timing line's to say, just above: this
      // line says only that the droid took it (operator, 2026-09-19 on #370).
      setFeedback(`Saved at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[outputs] save failed:", error);
      setFeedback(window.PAApi.messageFor(error), "error");
      // What the droid holds, not the answer it refused.
      await load().catch((reloadError) => console.error("[outputs] reload failed:", reloadError));
    } finally {
      saving = false;
      if (saveAgain) {
        saveAgain = false;
        save();
      }
    }
  };

  // Picking is applying: the change is drawn at once and saved after a short
  // settle, so a builder ticking three lines sends one request, not three.
  const change = (id, patch) => {
    if (!state) return;
    Object.assign(state[id], patch);
    // One lit lead today: giving one Output a Light Type takes it off the
    // others, because the controller lights a single lead.
    if (lightTypeOf(patch.type)) {
      outputs.forEach((output) => {
        if (output.strip && output.id !== id && lightTypeOf(state[output.id].type)) {
          state[output.id].type = NO_SERVO.id;
        }
      });
    }
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

  // What a builder calls an Output: what the board prints beside its pin. An
  // Output the board labels nothing reads as its Output Address, which still
  // says where it plugs in - never a name this file made up.
  const nameOf = (output) => output.label || output.address;

  // The wire's colour is picked by its place in the firmware's order, from the
  // numbered --wire-* palette in data/style.css, the same way data/wiring.js
  // picks it for the line on the sheet (CONTEXT.md "Status Colour": it names
  // a wire, never a state). The plate carries only that place, as data-wire;
  // the stylesheet maps it to the colour, so this file holds none.
  const WIRE_PALETTE = 8;
  const wireSlot = (output) => String(((output.wire - 1) % WIRE_PALETTE) + 1);

  const WIRED = "Wired";
  const NOT_WIRED = "Not wired";

  // Wiring's plate: the Output as it sits on the board - its printed name, the
  // three-pin header its lead plugs onto, whether it is wired - and, on an
  // Output that can carry the LED strip, whether it carries a servo or the
  // strip. The whole head is the press. The plate wears its wire's colour
  // (its data-wire, from wireSlot()), the same colour that wire is drawn in on
  // the sheet above, so a plate and its line on the diagram are found by eye (operator, 2026-09-19 on #411: the Outputs
  // section "looks to basic and boring").
  const wiredPlate = (output) => {
    const answer = state[output.id];
    const name = nameOf(output);
    const plate = element("div", "output-plate output-setting output-wire");
    plate.dataset.output = output.id;
    plate.dataset.wire = wireSlot(output);
    if (answer.enabled) plate.classList.add("is-on");
    const press = element("button", "output-setting-head output-wire-head");
    press.type = "button";
    press.setAttribute("aria-pressed", answer.enabled ? "true" : "false");
    // Signal, power and ground, left to right, as a servo header is laid out.
    // Only the signal pin is this sheet's to light: power is the builder's.
    const header = element("span", "output-wire-header");
    header.setAttribute("aria-hidden", "true");
    ["signal", "power", "ground"].forEach((pin) => header.appendChild(element("span", `output-wire-pin is-${pin}`)));
    press.appendChild(header);
    press.appendChild(element("span", "toggle-label output-wire-name", name));
    press.appendChild(element("span", "toggle-status", answer.enabled ? WIRED : NOT_WIRED));
    press.addEventListener("click", () => change(output.id, { enabled: !answer.enabled }));
    plate.appendChild(press);
    if (output.strip) {
      // What is on this lead: a servo, or one of the Light Types. Which servo
      // MODEL is Servos' question; this asks only which of the two it is.
      const carries = lightTypeOf(answer.type)?.id || "servo";
      plate.appendChild(segmented(`${name} carries`, [{ id: "servo", label: "Servo" }, ...LIGHT_TYPES], carries,
        (value) => change(output.id, { type: lightTypeOf(value) ? value : NO_SERVO.id })));
    } else {
      plate.appendChild(element("p", "output-wire-carries", "Servo only"));
    }
    return plate;
  };

  // Servos' plate: which servo the output carries. An Output set to LED strip
  // has no servo, and says where that is answered.
  //
  // A host may say what is on the end of each Output's lead (`describe`,
  // Servos' Part names, read from GET /api/servo/outputs): it goes under the
  // head, and an Output with nothing assigned gets no line at all (operator,
  // 2026-09-19 on #412: "if it has one"). Read-only: the assignment is Parts'.
  const typePlate = (output, view) => {
    const answer = state[output.id];
    const name = nameOf(output);
    const plate = element("div", "output-plate output-setting");
    plate.dataset.output = output.id;
    if (answer.enabled) plate.classList.add("is-on");
    const head = element("div", "output-setting-head");
    head.appendChild(element("span", "toggle-label", name));
    head.appendChild(element("span", "toggle-status", answer.enabled ? WIRED : NOT_WIRED));
    plate.appendChild(head);
    const onIt = typeof view?.describe === "function" ? view.describe(output) : "";
    if (onIt) plate.appendChild(element("p", "output-parts", onIt));
    const light = output.strip ? lightTypeOf(answer.type) : null;
    if (light) {
      plate.appendChild(element("p", "hint output-setting-note", `Carries the ${light.label}. Set on Wiring.`));
      return plate;
    }
    const options = output.strip ? [NO_SERVO, ...SERVO_TYPES] : SERVO_TYPES;
    plate.appendChild(segmented(`${name} servo`, options, answer.type,
      (value) => change(output.id, { type: value })));
    return plate;
  };

  const render = (view) => {
    if (!state) {
      view.body.replaceChildren(element("p", "hint", "Reading the outputs from the droid…"));
      return;
    }
    if (outputs.length === 0) {
      view.body.replaceChildren(element("p", "hint", "The droid reports no outputs to wire."));
      return;
    }
    const plates = element("div", "output-plates");
    outputs.forEach((output) => plates.appendChild(view.plate(output, view)));
    // When this view's answer bites, beside the outputs it asks about.
    const timing = element("p", "apply-timing");
    TIMING.paint(timing, VIEW_TIMING[view.kind], { pending: VIEW_TIMING[view.kind] === TIMING.AT_REBOOT && waitingOnStart() });
    view.body.replaceChildren(plates, timing);
  };

  // A listener is handed the answer and the Outputs it is about - id, label,
  // address, strip - in the firmware's order, so a surface that draws its own
  // rows (Servos' controls) draws them from the same list and names each the
  // same way, and never keeps a list of its own. `light` is the lead's Light
  // Type or null, which is exactly what a Part on that lead inherits (ADR
  // 0067): a surface saying how a light is lit reads it here rather than
  // working the answer out a second way from the saved fields.
  const renderAll = () => {
    views.forEach(render);
    const facts = outputs.map((output) => ({
      ...output,
      name: nameOf(output),
      light: lightTypeOf(state[output.id].type),
    }));
    listeners.forEach((listener) => listener(state, facts));
  };

  let loading = null;
  const load = async () => {
    const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
    adopt(result.data);
    return true;
  };

  // Read once, by whichever surface mounts first; the other is drawn from
  // the same answer. A failed read says so where the builder is looking and
  // is tried again by the next mount.
  const ensureLoaded = () => {
    if (state || loading || !window.PAApi) return;
    loading = load()
      .catch((error) => {
        console.error("[outputs] read failed:", error);
        setFeedback(`Could not read the outputs: ${window.PAApi.messageFor(error)}`, "error");
      })
      .finally(() => {
        loading = null;
      });
  };

  /**
   * Draw one view of the outputs into a host.
   *
   * @param {"wired"|"type"} kind - Wiring's wired ticks, or Servos' servo types
   * @param {object} hosts
   * @param {Element} hosts.body - where the plates go
   * @param {Element} hosts.feedback - the save line under them
   * @param {function} [hosts.describe] - what is on an Output's lead, or ""
   */
  const mount = (kind, hosts) => {
    if (!hosts?.body || !hosts?.feedback) return;
    const view = { ...hosts, kind: kind === "type" ? "type" : "wired", plate: kind === "type" ? typePlate : wiredPlate };
    views.push(view);
    render(view);
    ensureLoaded();
  };

  const onChange = (listener) => {
    listeners.add(listener);
    return () => listeners.delete(listener);
  };

  // A surface that only LISTENS still needs the answer read once, and mounting
  // a view is what asks for it. Lights mounts none - it never draws an Output -
  // so it asks here.
  const ensure = () => ensureLoaded();

  // Draw every view again, for a host whose `describe` answer has changed.
  const redraw = () => {
    if (state) renderAll();
  };

  window.PAOutputSettings = { mount, onChange, ensure, redraw };
})();

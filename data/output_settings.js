// =============================================================================
// data/output_settings.js
//
// The body's spare outputs - Utility Arm 1 and 2, AUX 1 to 3 - as a builder
// sets them up: which ones are in use, what servo is on each, and which AUX
// line carries the LED strip. These were Configuration's rows until the
// operator moved them where the question is asked (2026-09-18 on #369): the
// in-use ticks and AUX's LED-strip choice to Wiring, beside where each lead
// plugs in, and the servo type to Servos, with the line it drives.
//
// ONE STATE, TWO VIEWS. Both surfaces mount a view of the same answer, read
// from GET /api/config and written back by one save, so the two can never
// show different choices - the same rule the Component Picker keeps for its
// two homes. The fields are the ones Configuration always sent: enableArm1..
// enableAux3, arm1Type..aux3Type, and aux_led_pin, derived from which in-use
// AUX line is set to LED strip. Only one can be (the controller routes the
// strip to one line).
//
// Wiring's sheet itself stays a reference: data/wiring.js generates the
// document and writes nothing. This module is the one thing on that surface
// that writes, and it writes only these fields.
// =============================================================================
(() => {
  "use strict";

  const OUTPUTS = [
    { id: "arm1", name: "Utility Arm 1", toggle: "enableArm1", type: "arm1Type", aux: false },
    { id: "arm2", name: "Utility Arm 2", toggle: "enableArm2", type: "arm2Type", aux: false },
    { id: "aux1", name: "AUX 1", toggle: "enableAux1", type: "aux1Type", aux: true, pin: 1 },
    { id: "aux2", name: "AUX 2", toggle: "enableAux2", type: "aux2Type", aux: true, pin: 2 },
    { id: "aux3", name: "AUX 3", toggle: "enableAux3", type: "aux3Type", aux: true, pin: 3 },
  ];

  // The servo an output can carry. An arm always carries one; an AUX line may
  // carry nothing yet, and its LED-strip answer is Wiring's, not a servo type.
  const SERVO_TYPES = [
    { id: "mg996r", label: "MG996R" },
    { id: "mg90s", label: "MG90S" },
  ];
  const AUX_NONE = { id: "none", label: "None" };
  const LED_STRIP = "rgb";

  let state = null;  // { [id]: { enabled, type } }
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

  // The config payload's answer for each output, as Configuration read it.
  const adopt = (payload) => {
    const components = payload?.components || {};
    const next = {};
    OUTPUTS.forEach((output) => {
      const entry = components[output.id] || {};
      next[output.id] = {
        enabled: Boolean(entry.enabled),
        type: String(entry.type || (output.aux ? "none" : SERVO_TYPES[0].id)),
      };
    });
    // The routed strip is the controller's answer; a line it routes is an LED
    // strip even if the type field says otherwise.
    const pin = Number(payload?.aux_led_pin || 0);
    const routed = OUTPUTS.find((output) => output.pin === pin);
    if (routed) next[routed.id].type = LED_STRIP;
    state = next;
    renderAll();
  };

  // The line the strip leaves the controller on: the in-use AUX line set to
  // LED strip, or none (0) - what Configuration derived from its own rows.
  const ledPin = () =>
    OUTPUTS.find((output) => output.aux && state[output.id].enabled && state[output.id].type === LED_STRIP)?.pin || 0;

  const fields = () => {
    const out = {};
    OUTPUTS.forEach((output) => {
      out[output.toggle] = state[output.id].enabled ? "true" : "false";
      out[output.type] = state[output.id].type;
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
      setFeedback(`Saved at ${new Date().toLocaleTimeString()}. The droid uses it after a restart.`, "success");
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
    // One LED strip: setting it on one AUX line takes it off the others.
    if (patch.type === LED_STRIP) {
      OUTPUTS.forEach((output) => {
        if (output.aux && output.id !== id && state[output.id].type === LED_STRIP) {
          state[output.id].type = AUX_NONE.id;
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

  // Wiring's plate: the output, whether it is in use, and on an AUX line
  // whether it carries a servo or the LED strip.
  const inUsePlate = (output) => {
    const answer = state[output.id];
    const plate = element("div", "output-plate output-setting");
    plate.dataset.output = output.id;
    if (answer.enabled) plate.classList.add("is-on");
    const press = element("button", "output-setting-head");
    press.type = "button";
    press.setAttribute("aria-pressed", answer.enabled ? "true" : "false");
    press.appendChild(element("span", "toggle-label", output.name));
    press.appendChild(element("span", "toggle-status", answer.enabled ? "In use" : "Not used"));
    press.addEventListener("click", () => change(output.id, { enabled: !answer.enabled }));
    plate.appendChild(press);
    if (output.aux) {
      const carries = answer.type === LED_STRIP ? LED_STRIP : "servo";
      plate.appendChild(segmented(`${output.name} carries`, [
        { id: "servo", label: "Servo" },
        { id: LED_STRIP, label: "LED strip" },
      ], carries, (value) => change(output.id, { type: value === LED_STRIP ? LED_STRIP : AUX_NONE.id })));
    }
    return plate;
  };

  // Servos' plate: which servo the output carries. An AUX line set to LED
  // strip has no servo, and says where that is answered.
  const typePlate = (output) => {
    const answer = state[output.id];
    const plate = element("div", "output-plate output-setting");
    plate.dataset.output = output.id;
    if (answer.enabled) plate.classList.add("is-on");
    const head = element("div", "output-setting-head");
    head.appendChild(element("span", "toggle-label", output.name));
    head.appendChild(element("span", "toggle-status", answer.enabled ? "In use" : "Not used"));
    plate.appendChild(head);
    if (output.aux && answer.type === LED_STRIP) {
      plate.appendChild(element("p", "hint output-setting-note", "Carries the LED strip. Set on Wiring."));
      return plate;
    }
    const options = output.aux ? [AUX_NONE, ...SERVO_TYPES] : SERVO_TYPES;
    plate.appendChild(segmented(`${output.name} servo`, options, answer.type,
      (value) => change(output.id, { type: value })));
    return plate;
  };

  const render = (view) => {
    if (!state) {
      view.body.replaceChildren(element("p", "hint", "Reading the outputs from the droid…"));
      return;
    }
    const plates = element("div", "output-plates");
    OUTPUTS.forEach((output) => plates.appendChild(view.plate(output)));
    view.body.replaceChildren(plates);
  };

  const renderAll = () => {
    views.forEach(render);
    listeners.forEach((listener) => listener(state));
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
   * @param {"in-use"|"type"} kind - Wiring's in-use ticks, or Servos' servo types
   * @param {object} hosts
   * @param {Element} hosts.body - where the plates go
   * @param {Element} hosts.feedback - the save line under them
   */
  const mount = (kind, hosts) => {
    if (!hosts?.body || !hosts?.feedback) return;
    const view = { ...hosts, plate: kind === "type" ? typePlate : inUsePlate };
    views.push(view);
    render(view);
    ensureLoaded();
  };

  const onChange = (listener) => {
    listeners.add(listener);
    return () => listeners.delete(listener);
  };

  window.PAOutputSettings = { mount, onChange };
})();

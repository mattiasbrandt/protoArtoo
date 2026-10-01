// =============================================================================
// data/output_settings.js
//
// The segmented control a small set of peers is picked with on a Part's row of
// Wiring's parts table (data/parts_mapping.js picker()): which Output the Part
// is on, and which servo, or which Light Type, is on its wire. The pick is
// saved through data/outputs.js, or sent as a move.
//
// This file used to draw the Outputs themselves: Wiring's plates, a wired
// switch and a Servo / LED strip choice per Output (#369), and Servos' servo
// pick on each Output's row (#399). Both went when Wiring became part-first:
// an Output with a Part on it is wired, and what is on its wire is chosen on
// the Part's row (operator, 2026-09-29 on #411, CONTEXT.md "Wiring",
// "Servos"). What is left is the one control both used, so the two cannot come
// to draw a choice differently.
// =============================================================================
(() => {
  "use strict";

  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  /**
   * A joined bar of radio buttons, one per option, the current one lit. A
   * press on another option is handed to `onPick`; a press on the one already
   * picked asks nothing, so it never sends a save that changes nothing.
   *
   * An option the builder cannot pick is `disabled`: it stays on the bar, so
   * the bar still shows every peer, and a press on it asks nothing. The
   * refusal is here as well as on the button, because the request it would
   * send is one the droid refuses.
   *
   * @param {string} label - what the group picks, for a screen reader
   * @param {object[]} options
   * @param {string} options[].id
   * @param {string} options[].label
   * @param {boolean} [options[].disabled] - shown, and cannot be picked
   * @param {string} [options[].className] - a mark the surface styles
   * @param {string} [options[].name] - what a screen reader calls the option,
   *   where the label alone does not say enough
   * @param {string|null} current - the id picked now, or null for none
   * @param {function} onPick - (id) a different option was pressed
   * @returns {Element}
   */
  const segmented = (label, options, current, onPick) => {
    const row = element("div", "seg output-seg");
    row.setAttribute("role", "radiogroup");
    row.setAttribute("aria-label", label);
    options.forEach((option) => {
      const on = option.id === current;
      const off = option.disabled === true;
      const button = element("button", [on ? "active" : "", option.className || ""].filter(Boolean).join(" "), option.label);
      button.type = "button";
      button.dataset.value = option.id;
      button.setAttribute("role", "radio");
      button.setAttribute("aria-checked", on ? "true" : "false");
      if (option.name) button.setAttribute("aria-label", option.name);
      if (off) {
        button.disabled = true;
        button.setAttribute("aria-disabled", "true");
      }
      button.addEventListener("click", () => {
        if (!on && !off) onPick(option.id);
      });
      row.appendChild(button);
    });
    return row;
  };

  window.PAOutputSettings = Object.freeze({ segmented });
})();

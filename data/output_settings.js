// =============================================================================
// data/output_settings.js
//
// The segmented control a small set of peers is picked with on an Output's
// behalf: which servo, or which Light Type, is on a Part's wire. Wiring draws
// it on each Part's row of its part-first table (data/parts_mapping.js
// picker()) and saves the pick through data/outputs.js.
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
   * @param {string} label - what the group picks, for a screen reader
   * @param {{id: string, label: string}[]} options
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

  window.PAOutputSettings = Object.freeze({ segmented });
})();

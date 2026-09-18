// =============================================================================
// data/apply_timing.js
//
// When an answer takes effect (#370): the one vocabulary every surface that
// saves uses to say so, beside the question the answer belongs to.
//
// TIMING IS DATA, THE SENTENCE IS COMPOSED HERE. A step or a row declares WHEN
// its answer bites - one of the values below - and never types the sentence
// itself. Every sentence a surface shows about it is built from that value at
// render, which is what lets the same fact read one way before a change and
// another once a change is waiting, and lets a restart the builder has to
// perform carry the route to it (r2d2-astromech-simulator v1.79.0, lint.js:116
// composes its findings the same way).
//
// Each value is what the firmware's Commit Step leaves behind for that key,
// not a guess about it:
//   immediate         the key is read live, so the droid changes as it is saved
//   at-reboot         saved at once, read once at start: the droid changes at
//                     its next start and there is nothing for the builder to do
//                     (ADR 0027 for the Component Toggles, ADR 0042 for a
//                     Component Member, ADR 0015 for the network)
//   restart-required  saved at once, read once at start, and the droid is
//                     driven on it: the builder restarts it to use the change
//   nothing           the step is shown, not asked, and writes nothing
//
// THERE IS NO DEFAULT. A value outside this list is a step nobody decided the
// timing of, and a surface refuses to draw it rather than falling back to a
// promise - a fallback here is exactly the blanket "applies straight away" line
// this vocabulary replaced, which was false for at least three steps.
//
// Colour follows the Status Colour rule (#327): amber only for a restart the
// builder must perform, and only once a change is actually waiting on it. A
// value that merely stages at the next start is information, never amber.
// =============================================================================
(() => {
  "use strict";

  const IMMEDIATE = "immediate";
  const AT_REBOOT = "at-reboot";
  const RESTART_REQUIRED = "restart-required";
  const NOTHING = "nothing";

  // Ordered by how far the builder is from the change: a save that carries
  // several keys bites as late as the latest of them.
  const ORDER = [NOTHING, IMMEDIATE, AT_REBOOT, RESTART_REQUIRED];

  // Where a builder restarts the droid (data/maintenance.html, #404).
  const RESTART_ROUTE = { href: "#maintenance", label: "Restart it on Maintenance" };

  // Before anything has changed: when an answer here would bite.
  const AHEAD = {
    [IMMEDIATE]: "Used the moment it is saved.",
    [AT_REBOOT]: "Saved at once. The droid uses it from its next start.",
    [RESTART_REQUIRED]: "Saved at once. Restart the droid to use it.",
  };

  // A change is saved and the droid has not caught up with it yet.
  const WAITING = {
    [AT_REBOOT]: "Saved. The droid runs the old setting until its next start.",
    [RESTART_REQUIRED]: "Saved. The droid runs the old setting until you restart it.",
  };

  // What a save just did, after "Saved at <time>."
  const AFTER_SAVE = {
    [IMMEDIATE]: "",
    [AT_REBOOT]: "The droid uses it from its next start.",
    [RESTART_REQUIRED]: "Restart the droid to use it.",
  };

  // The save pill's tail, after "Saved at <time>".
  const PILL = {
    [AT_REBOOT]: "next start",
    [RESTART_REQUIRED]: "restart required",
  };

  // A chosen card whose product is not the one running yet.
  const BADGE = {
    [AT_REBOOT]: "Next start",
    [RESTART_REQUIRED]: "Restart needed",
  };

  const isStated = (timing) => ORDER.includes(timing);

  // Refuse loudly, never quietly default: see the header.
  const assertStated = (timing, who) => {
    if (!isStated(timing)) {
      throw new TypeError(`${who || "an answer"} does not say when it takes effect (got ${String(timing)})`);
    }
    return timing;
  };

  const latest = (...timings) =>
    timings.reduce((a, b) => (ORDER.indexOf(assertStated(b)) > ORDER.indexOf(assertStated(a)) ? b : a), NOTHING);

  /**
   * The line a question carries about when its answer bites.
   *
   * @param {string} timing - one of the values above
   * @param {{pending?: boolean}} [state] - a saved change the droid has not
   *   caught up with yet
   * @returns {{text: string, tone: "info"|"act", route: ({href: string, label: string}|null)}|null}
   *   null for a step that writes nothing
   */
  const line = (timing, { pending = false } = {}) => {
    assertStated(timing);
    if (timing === NOTHING) return null;
    const waiting = pending && Object.hasOwn(WAITING, timing);
    const mustAct = waiting && timing === RESTART_REQUIRED;
    return {
      text: waiting ? WAITING[timing] : AHEAD[timing],
      tone: mustAct ? "act" : "info",
      route: mustAct ? RESTART_ROUTE : null,
    };
  };

  /**
   * Draw a question's timing line into its element: a .note, blue while it is
   * information, amber with the route to the restart when one is owed.
   */
  const paint = (element, timing, state) => {
    if (!element) return;
    const said = line(timing, state);
    if (!said) {
      element.textContent = "";
      element.classList.add("hidden");
      return;
    }
    element.classList.remove("hidden");
    element.classList.remove("note-info");
    element.classList.remove("note-act");
    element.classList.add("note");
    element.classList.add(said.tone === "act" ? "note-act" : "note-info");
    element.dataset.applies = timing;
    element.dataset.pending = state?.pending && timing !== IMMEDIATE ? "true" : "false";
    // The trailing space separates the route from the sentence without a
    // text node of its own.
    element.textContent = said.route ? `${said.text} ` : said.text;
    if (said.route) {
      const route = document.createElement("a");
      route.className = "setup-link";
      route.setAttribute("href", said.route.href);
      route.textContent = `${said.route.label}.`;
      element.appendChild(route);
    }
  };

  // "Saved at 12:04. The droid uses it from its next start."
  const saved = (timing, savedAt) => {
    assertStated(timing);
    const tail = AFTER_SAVE[timing] || "";
    return tail ? `Saved at ${savedAt}. ${tail}` : `Saved at ${savedAt}`;
  };

  // The save pill: its words and its state (ok, or warn for a restart owed).
  const pill = (timing, savedAt) => {
    assertStated(timing);
    const tail = PILL[timing];
    return {
      text: tail ? `Saved at ${savedAt} · ${tail}` : `Saved at ${savedAt}`,
      state: timing === RESTART_REQUIRED ? "warn" : "ok",
    };
  };

  // The badge on a chosen card that is not yet the product running, or "".
  const badge = (timing) => BADGE[assertStated(timing)] || "";

  window.PAApplyTiming = {
    IMMEDIATE,
    AT_REBOOT,
    RESTART_REQUIRED,
    NOTHING,
    isStated,
    latest,
    line,
    paint,
    saved,
    pill,
    badge,
  };
})();

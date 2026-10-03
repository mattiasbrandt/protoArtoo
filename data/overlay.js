// =============================================================================
// data/overlay.js
//
// The three things every surface asks with, answered once for the whole
// browser (#456): a question, the Escape key, and a receipt. Loaded by the
// Operator Shell's own chain (data/index.html), and named after /web_api.js
// in the data-scripts of every surface that calls it, as a surface names each
// module it uses; the loader runs it once. window.PAOverlay.
//
//   ask({ title, body, yes, yesIcon, no, danger, near })  -> Promise<boolean>
//     A styled question in place of the browser's confirm(): a call site goes
//     from `if (!confirm(x)) return;` to
//     `if (!(await window.PAOverlay.ask({...}))) return;` and keeps its exact
//     behavior on the answer that keeps things. The title is the question and
//     both buttons are verbs naming their outcome. There are NO default
//     labels: a call that forgets one throws rather than shipping "Cancel" or
//     "OK". Both buttons are acts that show their icon alone (#460, the act in
//     data/shell.js): `yesIcon` is the icon of the act `yes` names, and is as
//     required as the verb; `no` always keeps things, so it always wears
//     `close`. `no: null` is a message with nothing to decide, and has one
//     button. Escape answers it false, so nothing that awaits it can hang.
//
//   escGuard(isOpen, close)  -> { bind(), unbind() }
//     Every overlay's Escape. Bind it when the overlay opens, unbind it when it
//     closes. One Escape closes one layer: the key goes to the most recently
//     bound guard whose isOpen() is true, and nothing underneath it hears the
//     key. A guard nobody bound, or one whose isOpen() is false, is skipped,
//     and with no guard open the key is not touched at all.
//
//   receipt(text, kind)
//     The short line after an act whose own question has closed, or after a
//     download (CONTEXT.md "Receipt"): `<verb-ed> <object> - <consequence, or
//     what was not touched>`. It never replaces a feedback line
//     (docs/ui-copy-voice.md rule 18) and is never for anything the droid
//     streams. One per act, three at most, each gone after a few seconds or on
//     a click.
//
//   holdSurface(dialog)  -> release()
//     What makes a dialog cover its surface and never the chrome (ADR 0048).
//
// After r2d2-astromech-simulator v1.79.0 (src/js/core/dialog.js, toast.js,
// esc-guard.js; credited in README.md). Their rules are taken; the code is
// ours. Two departures on purpose: the question has no "Cancel"/"OK" defaults,
// and Escape is dispatched from one listener to the topmost open guard rather
// than one capture listener per overlay, where two bound at once would both
// hear the same key, the lower one first.
//
// What Escape never does: act on hardware. The calibration dial binds no guard
// (closing it lets an Output go, ADR 0043, ADR 0064), and neither do the Page
// Recovery View or the Dashboard's sleep overlay - one goes when the page is
// back, the other when the droid wakes.
// =============================================================================
(() => {
  // ---------------------------------------------------------------------------
  // A dialog covers its surface, never the chrome (ADR 0048, #359). A native
  // showModal() makes everything outside the dialog inert, the shell's STOP
  // included, so a dialog here is opened non-modally and what goes inert is
  // the surface: at each level from the dialog up to its .surface, every
  // sibling of the path. Inert is inherited, so the path itself stays live and
  // the dialog can be answered.
  // ---------------------------------------------------------------------------
  const holdSurface = (dialog) => {
    const held = [];
    // Never above the surface, and never outside the work area: with no
    // .surface around it the climb stops at #shell-content, and with neither
    // nothing is held. A dialog in <body> would otherwise make #shell-top and
    // #shell-status inert, and STOP with them.
    const bound = dialog.closest?.(".surface") || dialog.closest?.("#shell-content");
    if (!bound) {
      console.error("[overlay] a dialog outside the work area holds nothing inert:", dialog);
    }
    let node = dialog;
    while (bound && node && node !== bound && node.parentElement) {
      const parent = node.parentElement;
      [...parent.children].forEach((sibling) => {
        if (sibling !== node && !sibling.inert) {
          sibling.inert = true;
          held.push(sibling);
        }
      });
      node = parent;
    }
    return () => {
      held.forEach((sibling) => {
        sibling.inert = false;
      });
      held.length = 0;
    };
  };

  // ---------------------------------------------------------------------------
  // Escape
  // ---------------------------------------------------------------------------

  // Bound guards, oldest first: the last one is the layer on top.
  const guards = [];

  // Capture phase on the document, so a surface's own key handler never sees
  // an Escape a layer above it has taken.
  document.addEventListener(
    "keydown",
    (event) => {
      // A held key repeats: one press closes one layer, not one per repeat.
      if (event.key !== "Escape" || event.isComposing || event.repeat) return;
      for (let i = guards.length - 1; i >= 0; i -= 1) {
        const guard = guards[i];
        if (!guard.isOpen()) continue;
        event.preventDefault();
        event.stopPropagation();
        guard.close();
        return;
      }
    },
    true
  );

  /**
   * @param {function(): boolean} isOpen - true when THIS overlay should take
   *   the key. Fold in whatever the site needs, such as the overlay still
   *   being in the document.
   * @param {function(): void} close - called once isOpen() is true. Free to
   *   close a nested state instead of the overlay.
   */
  const escGuard = (isOpen, close) => {
    const guard = { isOpen, close };
    const unbind = () => {
      const at = guards.indexOf(guard);
      if (at !== -1) guards.splice(at, 1);
    };
    return {
      // Binding again moves the guard to the top: it is the layer just opened.
      bind() {
        unbind();
        guards.push(guard);
      },
      unbind,
    };
  };

  // ---------------------------------------------------------------------------
  // The question
  // ---------------------------------------------------------------------------

  let questionNodes = null;
  let asking = null;

  const questionDialog = () => {
    if (questionNodes) return questionNodes;
    const dialog = document.createElement("dialog");
    dialog.className = "question-dialog";
    dialog.id = "pa-question";
    // Deliberately no aria-modal: what is inert is the surface, not the chrome
    // with the Latching Estop on it (ADR 0048).
    dialog.setAttribute("role", "alertdialog");
    dialog.setAttribute("aria-labelledby", "pa-question-title");
    dialog.setAttribute("aria-describedby", "pa-question-body");
    const title = document.createElement("h4");
    title.id = "pa-question-title";
    const body = document.createElement("p");
    body.id = "pa-question-body";
    const row = document.createElement("div");
    row.className = "button-row";
    const no = document.createElement("button");
    no.type = "button";
    no.className = "btn question-no icon-act act-keeps-words";
    no.innerHTML = window.PAUi.actFace("close", "");
    const yes = document.createElement("button");
    yes.type = "button";
    yes.className = "btn accent question-yes icon-act act-keeps-words";
    yes.innerHTML = window.PAUi.actFace("check", "");
    row.append(no, yes);
    dialog.append(title, body, row);
    no.addEventListener("click", () => settle(false));
    yes.addEventListener("click", () => settle(true));
    questionNodes = {
      dialog,
      title,
      body,
      no,
      yes,
      guard: escGuard(
        // A question whose surface was navigated away from is not on top of
        // anything the builder can see; the next surface's Escape is its own.
        () => asking !== null && dialog.open && dialog.isConnected,
        () => settle(false)
      ),
    };
    return questionNodes;
  };

  function settle(answer) {
    if (!asking) return;
    const { resolve, release, returnFocus } = asking;
    asking = null;
    const { dialog, guard } = questionNodes;
    guard.unbind();
    if (dialog.open) dialog.close();
    release();
    if (returnFocus?.isConnected) returnFocus.focus?.();
    resolve(answer);
  }

  // The surface the question belongs to: the one the press came from, else the
  // one on screen. Exactly one .surface is in the document at a time
  // (data/shell.js mount()). There is no further fallback: a question
  // anywhere else would cover the chrome, or hold it inert (ADR 0048).
  const surfaceFor = (near) =>
    near?.closest?.(".surface") || document.querySelector("#shell-content > .surface");

  /**
   * @param {object} q
   * @param {string} q.title - the question itself
   * @param {string} [q.body] - the consequence, one or two sentences
   * @param {string} q.yes - the verb that does it
   * @param {string} q.yesIcon - the icon of that act (docs/icon-set-provenance.md)
   * @param {string|null} q.no - the verb that keeps things as they are, or
   *   null for a message with nothing to decide
   * @param {boolean} [q.danger] - the act cannot be taken back
   * @param {Element} [q.near] - the control asked from; focus returns to it
   * @returns {Promise<boolean>} true only for `yes`
   */
  const ask = ({ title, body = "", yes, yesIcon, no, danger = false, near = null } = {}) => {
    if (!title || !yes || !yesIcon || (no !== null && !no)) {
      throw new Error("PAOverlay.ask needs a title, both answers named as verbs (no: null for a message) and yes's icon");
    }
    const surface = surfaceFor(near);
    if (!surface) {
      // Answered as kept, so the act it guards does not happen.
      console.error("[overlay] no surface on screen to ask over; answered as kept:", title);
      return Promise.resolve(false);
    }
    // A second question replaces the first, which is answered as kept: the
    // surface is inert while one is open, so only code can get here.
    settle(false);
    const nodes = questionDialog();
    nodes.title.textContent = title;
    nodes.body.textContent = body;
    nodes.body.hidden = !body;
    window.PAUi.setAct(nodes.yes, yes, yesIcon);
    nodes.yes.classList.toggle("accent", !danger);
    nodes.yes.classList.toggle("danger", Boolean(danger));
    nodes.no.hidden = no === null;
    window.PAUi.setAct(nodes.no, no === null ? "" : no);
    surface.appendChild(nodes.dialog);
    return new Promise((resolve) => {
      const returnFocus = near || document.activeElement;
      nodes.dialog.show();
      asking = { resolve, release: holdSurface(nodes.dialog), returnFocus };
      nodes.guard.bind();
      // Focus on the answer that keeps things. Not a trap: Tab leaves the
      // dialog for the chrome, which is where STOP is.
      (no === null ? nodes.yes : nodes.no).focus?.();
    });
  };

  // ---------------------------------------------------------------------------
  // The receipt
  // ---------------------------------------------------------------------------

  const RECEIPTS_MAX = 3;
  const RECEIPT_MS = 3500;
  // Its kind is its edge, and follows Status Color: an act that went through
  // takes no color, one the builder can act on amber, one refused or failed
  // red. Never blue and never green (CONTEXT.md "Status Color", "Receipt").
  const RECEIPT_KINDS = { done: "", act: "receipt-act", refused: "receipt-refused" };

  /**
   * @param {string} text - one line: `<verb-ed> <object> - <what was not touched>`
   * @param {"done"|"act"|"refused"} [kind]
   */
  const receipt = (text, kind = "done") => {
    if (!Object.prototype.hasOwnProperty.call(RECEIPT_KINDS, kind)) {
      throw new Error(`PAOverlay.receipt kind must be done, act or refused, not ${kind}`);
    }
    // The host is the shell's (data/shell.js), so a receipt outlives the
    // surface that issued it.
    const host = document.getElementById("shell-receipts");
    if (!host) {
      console.warn("[overlay] no receipt host; the receipt was not shown:", text);
      return;
    }
    // A fourth pushes the oldest out at once.
    while (host.children.length >= RECEIPTS_MAX) host.firstElementChild.remove();
    const plate = document.createElement("div");
    plate.className = ["receipt", RECEIPT_KINDS[kind]].filter(Boolean).join(" ");
    plate.textContent = text;
    host.appendChild(plate);
    const timer = window.setTimeout(() => plate.remove(), RECEIPT_MS);
    plate.addEventListener("click", () => {
      window.clearTimeout(timer);
      plate.remove();
    });
  };

  window.PAOverlay = { ask, escGuard, receipt, holdSurface };
})();

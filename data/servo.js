// =============================================================================
// data/servo.js
//
// Servos (CONTEXT.md "Servos"): the body's Outputs as servos. One section of
// Outputs, one row each, named by what the board prints beside the pin and by
// the Part(s) on it. On each row a builder drives the servo (open, close, stop,
// or a typed width sent once), records its ends with the calibration dial, and
// takes the pulse off; Find by Moving and back to centre sit over the rows. It
// is the output-first side of the mapping Parts reads from the part's end, and
// everything here moved from Parts on the operator's word (2026-09-19 on #412:
// "move bascially all of the "Outputs" section pieces to the "Servos" page.
// They all seem related to servo calibration"). The behaviour each piece keeps
// is its ADR's; only the page moved (ADR 0050, ADR 0064, amended 2026-09-19).
//
// THIS FILE KNOWS NO OUTPUT. The rows are the Outputs GET /api/servo/outputs
// lists, as data/outputs.js joins them to GET /api/config by Output Address
// (#415): what each is called is what its board prints (ADR 0033 Amendment
// 2026-09-19), and that label is also the word POST /api/servo moves it by,
// sent exactly as the droid gave it (data/parts_mapping.js servoWord()).
// Whether an Output is wired and what it carries come with it.
//
// Five rules shape this file.
//
// The table is built once per set of Outputs and repainted in place. A row is
// where live controls open, and a repaint that rebuilt one under the builder's
// pointer would move a panel, not merely redraw it (r2d2-astromech-simulator
// v1.79.0, src/js/maestro/hw-table.js:171-173).
//
// Both position marks are COMMANDED (#318, #362). The bar is the width the
// controller has put on the pin and the tick is where the move ends; nothing
// on this droid reads a servo back.
//
// An Output is found by moving it (ADR 0050, #363): pick an unwired Part, and
// the droid nudges each spare Output a little, one at a time, until the
// builder presses "That one". The rules of that run are with the code.
//
// A Part is calibrated by driving it (#291, #364, ADR 0064), and the part
// KEEPS being driven while the builder looks and listens. The two bounds that
// end the hold are the firmware's, not this page's.
//
// The whole droid goes back to centre on one press (#318, #365), and THE DROID
// PACES IT: this page sends one request and holds no pace at all.
//
// How a servo moves is set on its row, beside the dial (ADR 0052, #414): time
// to full throw, time to get up to speed and the ease. Until the Output is
// calibrated it moves by none of the three - it jumps - so its row offers
// nothing to set, and says why.
// =============================================================================
(() => {
  const catalog = window.DroidParts;
  const P = window.PAParts;
  const OUTPUTS = window.PAOutputs;
  if (!P || !catalog || !OUTPUTS) {
    console.error("[servo] window.PAParts, window.DroidParts or window.PAOutputs is missing; the parts scripts did not load");
    return;
  }
  const {
    NOT_WIRED,
    groupParts,
    partLabel,
    listParts,
    servoWord,
    hasServoWord,
    isLightRow,
  } = P;

  // The bench feed (#318): one read of the outputs answer a second while
  // Servos is on screen, stopped by the shell when the operator leaves (#360).
  const POLL_MS = 1000;

  // #293's honesty tiers, as the counts the Outputs section is headed with
  // (#318). A tier is a count, never a place a row moves to: the rows stay in
  // the order the wires plug in.
  const TIERS = [
    { id: "driving", label: "Driving parts" },
    { id: "switched-off", label: "Wired but switched off" },
    { id: "no-part", label: "Output with no part" },
  ];
  // A firmware older than this page reports no position at all, and that is
  // not the same as an Output with no pulse, so it is never counted as off.
  const tierOf = (output) => {
    if (output.parts.length === 0) return "no-part";
    return output.reported && output.commandedUs === null ? "switched-off" : "driving";
  };

  const tiersNode = document.getElementById("outputs-tiers");
  const outputsRegion = document.getElementById("outputs-table");
  const outputsSection = document.getElementById("outputs-card");
  const feedback = document.getElementById("outputs-feedback");
  const dialog = document.getElementById("outputs-move-dialog");
  const findBar = document.getElementById("outputs-find");
  const findPick = document.getElementById("outputs-find-part");
  if (!outputsRegion || !outputsSection || !dialog || !findBar || !findPick) return;
  const findButton = findBar.querySelector(".parts-find");
  const centreBulk = outputsSection.querySelector(".outputs-bulk");
  const centreButton = centreBulk.querySelector(".outputs-centre");
  // A plain .feedback, found once and held: showFeedback() rewrites its whole
  // class list, so it cannot be looked up again by the class it was found by.
  const centreSaid = centreBulk.querySelector(".feedback");

  const esc = (value) => window.PAUtils.escapeHtml(String(value));
  const showFeedback = (text, level) => window.PAUtils.showFeedback(feedback, text, level);

  // The Outputs this page draws a row for: the ones the servo table lists, in
  // the order data/outputs.js gives them, each carrying whether it is wired
  // and what it carries.
  let outputs = null; // null until the droid has answered
  const tableOutputs = () => OUTPUTS.list().filter((output) => output.fromTable);
  let run = null; // the Find by Moving run in progress, at most one (below)
  let dial = null; // the Output being calibrated, at most one (below)
  let estopLatched = null; // null until the droid has said

  // ---------------------------------------------------------------------------
  // The table: built once per set of Outputs
  // ---------------------------------------------------------------------------
  // Every catalog Part, grouped as Parts groups them, so a builder finds a Part
  // the same way from either end.
  const addOptions = groupParts(catalog.parts)
    .map(
      (group) =>
        `<optgroup label="${esc(group.label)}">` +
        group.parts.map((part) => `<option value="${esc(part.id)}">${esc(partLabel(part.id))}</option>`).join("") +
        `</optgroup>`
    )
    .join("");

  // An Output nobody has named - an expander's row - shows its address as its
  // name, and a named one shows the address beside it. Every act starts
  // refused: none may run on a guess about the estop, and the droid has not
  // said yet.
  const outputRowHtml = (output) => {
    const label = output.name;
    const address = output.label ? `<span class="outputs-address">${esc(output.address)}</span>` : "";
    return (
      `<tr class="parts-row outputs-row" data-output="${esc(output.address)}">` +
      `<th scope="row"><span class="parts-name">${esc(label)}</span>${address}</th>` +
      `<td><span class="outputs-parts"></span><select class="parts-output outputs-add" ` +
      `aria-label="${esc(`Put a part on ${label}`)}"><option value="">Put a part on ${esc(label)}...</option>` +
      `${addOptions}</select></td>` +
      `<td><div class="outputs-bar" aria-hidden="true"><div class="outputs-now"></div><div class="outputs-tick"></div></div>` +
      `<span class="outputs-us"></span></td>` +
      // Driving it: the typed width goes out once and is saved nowhere; open
      // and close go to the ends recorded for it; stop drives it to centre and
      // does not hold it there.
      `<td class="outputs-drive"><span class="outputs-drive-note"></span>` +
      `<span class="outputs-drive-acts">` +
      `<input class="input-narrow outputs-width" type="number" min="500" max="2500" step="10" value="1500" ` +
      `aria-label="${esc(`Width to drive ${label} to, in microseconds`)}">` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="position" disabled aria-disabled="true">drive</button>` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="open" disabled aria-disabled="true">open</button>` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="close" disabled aria-disabled="true">close</button>` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="stop" disabled aria-disabled="true">stop</button>` +
      `</span></td>` +
      `<td class="outputs-release"></td>` +
      // How it moves (#414). The two numbers borrow the drive cell's compact
      // box (.outputs-drive-acts .outputs-width); the pill and the controls
      // each sit in a plain wrapper so `hidden` can take them off the row.
      `<td class="outputs-motion">` +
      `<div class="outputs-motion-off"><span class="status-pill pill-info">off until calibrated</span></div>` +
      `<div class="outputs-motion-set">` +
      `<div><span class="outputs-drive-acts"><input class="input-narrow outputs-width outputs-throw" type="number" step="10" ` +
      `aria-label="${esc(`Time to full throw for ${label}, in milliseconds`)}"> ms to full throw</span></div>` +
      `<div><span class="outputs-drive-acts"><input class="input-narrow outputs-width outputs-accel" type="number" step="10" ` +
      `aria-label="${esc(`Time to get up to speed for ${label}, in milliseconds`)}"> ms to get up to speed</span></div>` +
      `<div class="seg outputs-ease" role="radiogroup" aria-label="${esc(`How ${label} eases`)}">` +
      OUTPUTS.EASES.map((ease) =>
        `<button type="button" role="radio" aria-checked="false" data-ease="${esc(ease.id)}">${esc(ease.label)}</button>`
      ).join("") +
      `</div></div></td>` +
      `<td class="outputs-acts">` +
      `<button class="btn btn-sm outputs-calibrate" type="button" ` +
      `aria-label="${esc(`Calibrate ${label} by driving it`)}" disabled aria-disabled="true">calibrate</button>` +
      `<button class="btn btn-sm outputs-off" type="button" ` +
      `aria-label="${esc(`Take the pulse off ${label}`)}" disabled aria-disabled="true">pulses off</button>` +
      `</td></tr>`
    );
  };

  const outputRows = new Map();
  let outputAddresses = null;

  // The only rebuild, and only when the set of Outputs itself changes - which a
  // controller does across a reboot, not while this page is reading it (#318).
  const buildOutputs = (addresses) => {
    outputsRegion.innerHTML =
      `<table class="parts-table outputs-table"><thead><tr><th scope="col">Output</th><th scope="col">Drives</th>` +
      `<th scope="col">Commanded position</th><th scope="col">Drive it</th><th scope="col">Output Release</th>` +
      `<th scope="col">Motion</th><th scope="col">Calibrate</th></tr></thead><tbody>` +
      outputs.map(outputRowHtml).join("") +
      `</tbody></table>`;
    outputRows.clear();
    outputsRegion.querySelectorAll("[data-output]").forEach((node) => {
      outputRows.set(node.dataset.output, {
        node,
        parts: node.querySelector(".outputs-parts"),
        bar: node.querySelector(".outputs-bar"),
        now: node.querySelector(".outputs-now"),
        tick: node.querySelector(".outputs-tick"),
        us: node.querySelector(".outputs-us"),
        driveNote: node.querySelector(".outputs-drive-note"),
        driveActs: node.querySelector(".outputs-drive-acts"),
        // The drive cell's own box: the Motion cell's two borrow its class.
        width: node.querySelector(".outputs-drive").querySelector(".outputs-width"),
        go: Array.from(node.querySelectorAll(".outputs-go")),
        release: node.querySelector(".outputs-release"),
        motion: node.querySelector(".outputs-motion"),
        motionOff: node.querySelector(".outputs-motion-off"),
        motionSet: node.querySelector(".outputs-motion-set"),
        throwMs: node.querySelector(".outputs-throw"),
        accelMs: node.querySelector(".outputs-accel"),
        eases: Array.from(node.querySelectorAll("[data-ease]")),
        calibrate: node.querySelector(".outputs-calibrate"),
        off: node.querySelector(".outputs-off"),
      });
    });
    outputAddresses = addresses;
    // The buttons are built refused; this is what makes them live again on a
    // droid whose estop is clear.
    gateActs();
  };

  // Both marks against one span, so they cannot disagree about scale and the
  // gap between them is the move (r2d2-astromech-simulator v1.79.0,
  // src/js/maestro/hw-table.js:186). The span is the band the Output's widths
  // are clamped into, so a commanded width never falls off either end.
  const markAt = (us, output) => {
    const span = output.bandHiUs - output.bandLoUs;
    const fraction = span > 0 ? (us - output.bandLoUs) / span : 0;
    return `${(Math.min(1, Math.max(0, fraction)) * 100).toFixed(1)}%`;
  };

  // An Output a dial can drive: one with travel, and with a name the servo
  // route takes as its arm.
  const isDriveable = (output) => !isLightRow(output) && hasServoWord(output);

  // Why an Output offers no drive, said the way a builder needs it, or "" when
  // it does. Whether it is wired and what it carries are Wiring's and Servo
  // assignment's answer; the route is refused for a row no board labels. An
  // Output whose settings nobody can save has nothing set to refuse on.
  const driveRefusal = (output) => {
    if (!hasServoWord(output)) return "No name the servo route takes";
    if (isLightRow(output)) return "A light has no position";
    if (!output.switchable) return "";
    if (output.light) return `Carries the ${output.light.label}`;
    if (!output.wired) return "Not wired. Mark it on Wiring";
    if (!output.servo) return "No servo set above";
    return "";
  };

  // Why an Output has no pulse, said the way a builder needs it. The two
  // firmware bounds each get their own sentence.
  const LIMP_SAID = {
    "off": "Limp - no pulse",
    "pulses-off": "Limp - pulses off",
    "expiry": "Went limp - the dial stopped asking",
    "ceiling": "Went limp - ten minutes is the most a dial holds",
    "estop": "Limp - the estop let go",
    "sleep": "Limp - sleep mode let go",
  };

  // Only style, textContent and classList, on nodes that already exist: a
  // repaint never rebuilds a row, so the control under the builder's pointer
  // stays where it is (hw-table.js:171-174).
  const paintOutputRow = (output) => {
    const row = outputRows.get(output.address);
    if (!row) return;
    const light = isLightRow(output);
    const pulsing = output.commandedUs !== null;
    row.node.classList.toggle("is-wired", output.parts.length > 0);
    row.node.classList.toggle("partkind-light", light);
    row.bar.classList.toggle("is-off", !pulsing);
    // Whatever the droid has just answered is current, including after an
    // estop cut a nudge short.
    row.bar.classList.remove("is-stale");
    row.parts.textContent = output.parts.length ? listParts(output.parts) : NOT_WIRED;
    row.now.style.width = pulsing ? markAt(output.commandedUs, output) : "0%";
    row.tick.style.left = pulsing ? markAt(output.targetUs, output) : "0%";
    const refusal = driveRefusal(output);
    row.driveNote.textContent = refusal;
    row.driveActs.hidden = refusal !== "";
    const driveable = isDriveable(output);
    row.calibrate.hidden = !driveable;
    row.off.hidden = !driveable;
    paintMotion(row, output, driveable);
    row.node.classList.toggle("is-held", output.held);
    if (!output.reported) {
      row.us.textContent = "Not reported by this firmware";
      row.release.textContent = "Not reported by this firmware";
      return;
    }
    // An Output with no pulse says so: a blank cell cannot be told from a table
    // that has stopped updating.
    if (!pulsing) row.us.textContent = "— off";
    else if (light) row.us.textContent = "A light has no position";
    else if (output.targetUs === output.commandedUs) row.us.textContent = `${output.commandedUs} µs`;
    else row.us.textContent = `${output.commandedUs} → ${output.targetUs} µs`;
    if (light) row.release.textContent = "None - a light has nothing to let go of";
    else if (output.held) row.release.textContent = "The dial is holding it";
    else if (pulsing) row.release.textContent = "Holds where it stops";
    // An Output that has gone limp says WHICH of the ways it can happen this
    // was (#364): a bound the dial ran into is not the estop letting go.
    else row.release.textContent = LIMP_SAID[output.limp] || LIMP_SAID.off;
  };

  // ---------------------------------------------------------------------------
  // How it moves (ADR 0052, #414)
  //
  // Only an Output that is calibrated, drives a servo and whose three fields
  // the droid names offers them. An unmeasured Output jumps whatever its
  // profile says, so its row says so rather than taking numbers that would do
  // nothing; its stored profile waits on the row until it is calibrated.
  // ---------------------------------------------------------------------------
  const motionOpen = (output) => isDriveable(output) && output.motionSettable && output.calibrated;

  const paintMotion = (row, output, driveable) => {
    const shown = driveable && output.motionSettable;
    const open = shown && output.calibrated;
    row.motionOff.hidden = !shown || open;
    row.motionSet.hidden = !open;
    if (!open) return;
    // Never the box the builder is typing in.
    if (document.activeElement !== row.throwMs) row.throwMs.value = output.throwMs === null ? "" : String(output.throwMs);
    if (document.activeElement !== row.accelMs) row.accelMs.value = output.accelMs === null ? "" : String(output.accelMs);
    row.eases.forEach((button) => {
      const on = button.dataset.ease === output.ease;
      button.classList.toggle("active", on);
      button.setAttribute("aria-checked", on ? "true" : "false");
    });
  };

  // One field at a time, through the one module that saves an Output's
  // settings (data/outputs.js). The droid refuses a number outside what the
  // row takes and says which; the row then repaints to what it holds.
  const saveMotion = async (address, patch) => {
    const output = outputs?.find((each) => each.address === address);
    if (!output || !motionOpen(output)) return;
    try {
      await OUTPUTS.save(address, patch);
      showFeedback(`${output.name} saved. The next move uses it.`, "success");
    } catch (error) {
      // data/outputs.js has already put a refusal in the page's words.
      showFeedback(`Not saved: ${window.PAApi.messageFor(error)}.`, "error");
      const row = outputRows.get(address);
      const now = outputs?.find((each) => each.address === address);
      if (row && now) paintMotion(row, now, isDriveable(now));
    }
  };

  const paintOutputs = (addresses) => {
    if (outputAddresses !== addresses) buildOutputs(addresses);
    outputs.forEach(paintOutputRow);
    const counts = new Map(TIERS.map((tier) => [tier.id, 0]));
    outputs.forEach((output) => counts.set(tierOf(output), counts.get(tierOf(output)) + 1));
    if (tiersNode) {
      if (!tiersNode.querySelector(".outputs-tier")) {
        tiersNode.innerHTML = TIERS.map((tier) => `<span class="outputs-tier" data-tier="${tier.id}"></span>`).join("");
      }
      tiersNode.querySelectorAll("[data-tier]").forEach((node) => {
        const tier = TIERS.find((each) => each.id === node.dataset.tier);
        const count = counts.get(tier.id);
        node.textContent = `${tier.label} — ${count} ${count === 1 ? "output" : "outputs"}`;
      });
    }
  };

  // The Parts Find by Moving can look for: every Part no Output drives, grouped
  // as Parts groups them. Rebuilt only when that set changes, and never under
  // the builder's pointer.
  let findSet = null;
  const paintFindPick = () => {
    const unwired = catalog.parts.filter((part) => OUTPUTS.forPart(part.id, outputs) === null);
    const key = unwired.map((part) => part.id).join(",");
    if (key !== findSet && document.activeElement !== findPick) {
      const keep = findPick.value;
      findPick.innerHTML = unwired.length
        ? `<option value="">Pick a part nothing drives</option>` +
          groupParts(unwired)
            .map(
              (group) =>
                `<optgroup label="${esc(group.label)}">` +
                group.parts.map((part) => `<option value="${esc(part.id)}">${esc(partLabel(part.id))}</option>`).join("") +
                `</optgroup>`
            )
            .join("")
        : `<option value="">Every part is on an output</option>`;
      findPick.value = unwired.some((part) => part.id === keep) ? keep : "";
      findSet = key;
    }
    findPick.disabled = unwired.length === 0;
    findButton.hidden = run !== null;
  };

  // ---------------------------------------------------------------------------
  // Repainted in place
  // ---------------------------------------------------------------------------
  const paint = () => {
    if (outputs === null) return;
    const addresses = outputs.map((output) => output.address).join(",");
    paintOutputs(addresses);
    paintFindPick();
    // The droid has answered again, which is the only thing a run steps on.
    stepRun();
    paintDial();
  };

  // The bench feed reads the servo table alone; the section run on mount reads
  // the config with it, once, for what each Output carries.
  const loadOutputs = async ({ handle = null, withConfig = false } = {}) => {
    if (withConfig) await OUTPUTS.load({ handle });
    else await OUTPUTS.refresh({ handle });
    outputs = tableOutputs();
    paint();
  };

  // One read the dial and a capture both wait on, so what the panel shows after
  // an act is what the droid answered rather than what this page assumed.
  const refresh = () =>
    loadOutputs().catch((error) => {
      console.warn("[servo] reading the outputs failed:", error);
    });

  // ---------------------------------------------------------------------------
  // Putting a Part on an Output: the same request Parts makes, asked in the
  // same words (data/parts_mapping.js).
  // ---------------------------------------------------------------------------
  const mover = P.mover({
    dialog,
    say: showFeedback,
    reload: () => loadOutputs(),
    repaint: () => paint(),
  });

  outputsRegion.addEventListener("change", (event) => {
    const box = event.target;
    if (box?.classList?.contains("outputs-throw") || box?.classList?.contains("outputs-accel")) {
      const address = box.closest?.("[data-output]")?.dataset.output;
      if (!address) return;
      const ms = Math.round(Number(box.value));
      const key = box.classList.contains("outputs-throw") ? "throwMs" : "accelMs";
      if (box.value === "" || !Number.isFinite(ms)) {
        showFeedback("Type a time in milliseconds.", "warning");
        return;
      }
      started(saveMotion(address, { [key]: ms }));
      return;
    }
    const select = event.target;
    if (!select?.classList?.contains("outputs-add")) return;
    const address = select.closest?.("[data-output]")?.dataset.output;
    const id = select.value;
    if (!address || !id || outputs === null) return;
    // A pick is a request, not a state this control keeps: it goes back to its
    // prompt, and the row's Drives cell says what the droid answered.
    select.value = "";
    mover.request(P.moveFor(outputs, id, address), select);
  });

  // ---------------------------------------------------------------------------
  // Driving an Output from its row
  // ---------------------------------------------------------------------------
  const drive = async (address, action) => {
    const output = outputs?.find((each) => each.address === address);
    const row = outputRows.get(address);
    if (!output || !row) return;
    const label = output.name;
    const form = { arm: servoWord(output), action };
    if (action === "position") {
      const us = Math.round(Number(row.width.value));
      if (!Number.isFinite(us) || us < 500 || us > 2500) {
        showFeedback(`Type a width from 500 to 2500 µs for ${label}.`, "warning");
        return;
      }
      form.positionUs = String(us);
    }
    const said = action === "position" ? `${label} to ${form.positionUs} µs` : `${label} ${action}`;
    try {
      await window.PAApi.postForm("/api/servo", form, { timeoutMs: 4000 });
      showFeedback(`${said} sent.`, "success");
    } catch (error) {
      showFeedback(`${said} failed: ${window.PAApi.messageFor(error)}`, "error");
      return;
    }
    refresh();
  };

  // ---------------------------------------------------------------------------
  // Find by Moving (ADR 0050, #363)
  //
  // A builder who cannot remember which output the rear-left door is on picks
  // that door and watches the droid. The page steps through the spare Outputs
  // -- the ones driving no Part, with a pulse on them -- and asks the droid to
  // nudge each one a little, one at a time; the builder presses "That one"
  // when the Part twitches, and that is the same move a row's picker makes.
  // Nothing here holds an Output: a nudge is one firmware command that goes out
  // and comes back on its own (POST /api/servo action=nudge), so a browser that
  // dies mid-run leaves the droid where it was, and Stop sends nothing further
  // -- the nudge in flight finishes its own return.
  //
  // One Output at a time, and the droid says when. The page sends the next
  // nudge only when the previous one has ended, and it knows that from the
  // answer's nudgesDone count going up -- never from watching the Output move,
  // because a whole nudge can fall between two of the bench feed's reads.
  //
  // The estop ends a run. The firmware refuses and halts on its own; this side
  // stops asking and stops showing the nudged Output's last commanded mark as
  // if it were current, until the droid has answered again (after
  // r2d2-astromech-simulator v1.79.0, src/js/config/hardware.js:894). While the
  // estop is latched the button is refused -- disabled plus aria-disabled --
  // and the shell's own ignored-input notice names why. No Non-RC Control
  // consent is asked: that flag has never reached POST /api/servo (ADR 0064).
  // ---------------------------------------------------------------------------
  const runPanel = document.createElement("span");
  runPanel.className = "parts-find-run";
  runPanel.innerHTML =
    `<span class="parts-find-text" role="status" aria-live="polite"></span>` +
    `<button class="btn accent parts-find-that" type="button">That one</button>` +
    `<button class="btn parts-find-stop" type="button">Stop</button>`;
  const runText = runPanel.querySelector(".parts-find-text");

  // The Outputs a run steps through: nothing on them, a pulse on them (an
  // Output with none cannot twitch, and the firmware would refuse it), and a
  // name the servo route takes as its arm.
  const spareOutputs = () =>
    outputs.filter((output) => output.parts.length === 0 && output.commandedUs !== null && hasServoWord(output));

  const endRun = (text, level) => {
    if (run === null) return;
    run = null;
    runPanel.remove();
    if (outputs !== null) paintFindPick();
    if (text) showFeedback(text, level);
  };

  // The nudged Output's last commanded mark is not current any more: the
  // estop ended the nudge somewhere the run never read. Held back, not
  // guessed at, until the next answer repaints the row.
  const markNotCurrent = (address) => {
    const row = outputRows.get(address);
    if (!row) return;
    row.bar.classList.add("is-stale");
    row.us.textContent = "Stopped — finding out where it is";
  };

  // Ask the droid to nudge the next spare Output, or end the run when there
  // is none left. `before` is the count from the droid's latest answer, and a
  // later answer with a higher one is the only thing that moves the run on.
  const nudgeNext = async () => {
    const current = run;
    current.at += 1;
    const label = partLabel(current.partId);
    const count = current.candidates.length;
    if (current.at >= count) {
      endRun(
        `None of the ${count} spare ${count === 1 ? "output" : "outputs"} moved ${label} in one pass, so it stays ${NOT_WIRED}. ` +
          `Check the wire, or run it again.`,
        "warning"
      );
      return;
    }
    const address = current.candidates[current.at];
    const output = outputs.find((each) => each.address === address);
    if (!output || output.nudgesDone === null) {
      // The droid's answer changed shape under the run: a reboot, or a
      // different firmware. Nothing is asked of an Output the page cannot
      // tell has finished.
      endRun(`${address} is not in the droid's answer any more, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning");
      return;
    }
    current.address = address;
    current.before = output.nudgesDone;
    current.sending = true;
    runText.textContent =
      `Nudging ${output.name} (${current.at + 1} of ${count}). Watch the droid, and press That one when ${label} moves.`;
    try {
      await window.PAApi.postForm("/api/servo", { arm: servoWord(output), action: "nudge" }, { timeoutMs: 4000 });
    } catch (error) {
      if (run === current) {
        endRun(`The nudge did not reach the droid: ${window.PAApi.messageFor(error)}. ${label} stays ${NOT_WIRED}.`, "error");
      }
      return;
    }
    if (run === current) current.sending = false;
  };

  // Called with every answer from the droid. Steps on only when the Output
  // the run asked about says its nudge has ended.
  const stepRun = () => {
    if (run === null || run.sending || run.address === null) return;
    const label = partLabel(run.partId);
    const wiredTo = OUTPUTS.forPart(run.partId, outputs);
    if (wiredTo) {
      endRun(`${label} is on ${wiredTo.name} now, so the run stopped.`);
      return;
    }
    const output = outputs.find((each) => each.address === run.address);
    if (!output || output.nudgesDone === null) {
      endRun(`${run.address} is not in the droid's answer any more, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning");
      return;
    }
    // The Output being nudged has gone limp - pulses off from its row, one of
    // the calibration dial's bounds, or anything else that takes a pulse off a
    // pin (#364). A limp Output cannot twitch, so the run ENDS here rather than
    // stepping on: ending a nudge bumps nudgesDone, and without this the count
    // going up would read as "that one finished, try the next".
    if (output.commandedUs === null) {
      endRun(`${output.name} is limp, so the run stopped. ${label} stays ${NOT_WIRED}.`, "warning");
      return;
    }
    if (output.nudgesDone === run.before) return;
    nudgeNext();
  };

  const startRun = (partId) => {
    if (outputs === null || !partId) return;
    if (run !== null) {
      showFeedback(`One run at a time: ${partLabel(run.partId)} is being found. Stop that run first.`, "warning");
      return;
    }
    if (mover.pending() !== null) {
      showFeedback(`One move at a time: wait for ${partLabel(mover.pending())} to land.`, "warning");
      return;
    }
    if (dial !== null) {
      showFeedback("One at a time: an output is being calibrated. Press done first.", "warning");
      return;
    }
    const candidates = spareOutputs();
    if (!candidates.length) {
      showFeedback("Nothing to nudge. Every output with a pulse already drives a part.", "warning");
      return;
    }
    if (candidates.some((output) => output.nudgesDone === null)) {
      showFeedback(
        "This firmware does not say when a nudge has ended, so Find by moving cannot step through the outputs. Update the firmware.",
        "warning"
      );
      return;
    }
    run = { partId, candidates: candidates.map((output) => output.address), at: -1, address: null, before: null, sending: false };
    findBar.appendChild(runPanel);
    paintFindPick();
    nudgeNext();
  };

  // "That one": the same request a row's picker makes, so a spare Output takes
  // the Part with no question and an Output somebody wired meanwhile is asked
  // about in the same words.
  const pickThatOne = () => {
    if (run === null) return;
    const { partId, address } = run;
    endRun();
    mover.request(P.moveFor(outputs, partId, address), findPick);
  };

  const stopRun = () => {
    if (run === null) return;
    endRun(`Stopped. ${partLabel(run.partId)} stays ${NOT_WIRED}.`);
  };

  runPanel.querySelector(".parts-find-that").addEventListener("click", pickThatOne);
  runPanel.querySelector(".parts-find-stop").addEventListener("click", stopRun);

  findButton.addEventListener("click", () => {
    // A browser delivers no click to a disabled button; this is the rule
    // itself: a refused control asks the droid for nothing, however the click
    // arrived.
    if (findButton.disabled) return;
    if (!findPick.value) {
      showFeedback("Pick the part to find first.", "warning");
      return;
    }
    startRun(findPick.value);
  });

  // ---------------------------------------------------------------------------
  // The calibration dial (#291, #364, ADR 0064)
  //
  // A builder drives the part until it looks right, presses a button, and that
  // becomes the end. No typing microseconds, and no guessing whether the number
  // they typed is the one the servo is holding -- the dial is already standing
  // at a width the droid is driving, so a capture is one assignment
  // (r2d2-astromech-simulator v1.79.0, src/js/maestro/setup-hw-cal.js:610).
  //
  // THE PART KEEPS BEING DRIVEN while they look and listen. Output Release cuts
  // drive when a part arrives, which is exactly when the builder has stopped
  // moving it in order to look at it, and a servo fighting its linkage is only
  // audible while it is being driven. So the dial takes the Output and holds
  // it, and the firmware -- not this page -- bounds that hold two ways: it lets
  // go a few seconds after these commands stop arriving, and ten minutes after
  // it took the Output whatever keeps arriving. This page can refresh the
  // first and can move neither.
  //
  // REVERSE IS A SWAP, READ BACK FROM THE NUMBERS. Nothing here stores an
  // invert flag and nothing may: the droid swaps the two ends on the row and
  // this page shows what the row then says.
  // ---------------------------------------------------------------------------
  // How often the hold is refreshed while the dial is open: comfortably inside
  // the firmware's few-second expiry, far short of the ten-minute ceiling.
  const HOLD_KEEPALIVE_MS = 1000;
  // A drag fires input events far faster than the droid needs to hear about
  // them; this is short enough to feel immediate and long enough not to queue.
  const HOLD_CHANGE_MS = 50;
  // Long enough to watch a part reach an end and settle before it leaves again.
  const SWEEP_DWELL_MS = 900;
  // What `safe range` narrows to: the cautious band every component band
  // contains (SERVO_BAND_STD, include/servo_output_row.h).
  const SAFE_LO = 1000;
  const SAFE_HI = 2000;
  // One press of the fine buttons. A slider cannot be dragged to a single
  // microsecond at the bench, and a tablet has no arrow keys (ADR 0059).
  const FINE_US = 5;

  // What the band the dial opens at is, and why it is that one. The component
  // governs the clamp (ADR 0041), so the dial opens at the widest range the
  // firmware will actually drive this row -- never wider.
  // A Light Type recorded on the row gets the cautious range too, and is named
  // by data/outputs.js's word for it.
  const COMPONENT_SAID = {
    mg996r: "what an MG996R takes",
    mg90s: "what an MG90S takes, the full servo range",
    none: "the cautious range: nothing is recorded as fitted",
  };

  const bandSentence = (output) => {
    const light = OUTPUTS.lightType(output.component);
    const said = light
      ? `the cautious range: this is recorded as an ${light.label}`
      : COMPONENT_SAID[output.component] || COMPONENT_SAID.none;
    const unlockable = output.component === "none" || output.component === "mg996r";
    const unlock = unlockable ? " Record the part as an MG90S for the full 500-2500." : "";
    return `${output.bandLoUs}-${output.bandHiUs} µs — ${said}.${unlock}`;
  };

  const dialPanel = document.createElement("section");
  dialPanel.className = "cal-panel";
  dialPanel.hidden = true;
  dialPanel.innerHTML =
    `<h4 class="cal-title"></h4>` +
    `<p class="desc">Drive the part until it looks right, then press the end you are setting. The ` +
    `servo holds while you watch, and goes limp a few seconds after you leave or after ten minutes.</p>` +
    `<p class="cal-band"></p>` +
    `<div class="cal-drive">` +
    `<button class="btn cal-fine" type="button" data-step="-1" aria-label="Down 5 microseconds">−5 µs</button>` +
    `<input class="cal-slider" type="range" step="1" aria-label="Drive this output">` +
    `<button class="btn cal-fine" type="button" data-step="1" aria-label="Up 5 microseconds">+5 µs</button>` +
    `<span class="cal-readout"></span>` +
    `</div>` +
    `<div class="cal-acts">` +
    `<button class="btn accent cal-set" type="button" data-end="close">Set MIN</button>` +
    `<button class="btn accent cal-set" type="button" data-end="centre">Set CENTER</button>` +
    `<button class="btn accent cal-set" type="button" data-end="open">Set MAX</button>` +
    `</div>` +
    `<p class="cal-ends"></p>` +
    `<div class="cal-acts">` +
    `<button class="btn cal-reverse" type="button">reverse</button>` +
    `<span class="cal-hint">it swaps the two ends</span>` +
    `<button class="btn cal-safe" type="button">safe range</button>` +
    `<button class="btn cal-useends" type="button">use these ends</button>` +
    `<button class="btn cal-sweep" type="button">test sweep</button>` +
    `</div>` +
    `<div class="cal-acts">` +
    `<button class="btn cal-off" type="button">pulses off</button>` +
    `<button class="btn cal-resume" type="button">take it again</button>` +
    `<button class="btn cal-done" type="button">done</button>` +
    `</div>` +
    `<p class="cal-note" role="status" aria-live="polite"></p>`;
  (document.getElementById("outputs-dial") || outputsSection).appendChild(dialPanel);

  const dialTitle = dialPanel.querySelector(".cal-title");
  const dialBand = dialPanel.querySelector(".cal-band");
  const dialSlider = dialPanel.querySelector(".cal-slider");
  const dialReadout = dialPanel.querySelector(".cal-readout");
  const dialEnds = dialPanel.querySelector(".cal-ends");
  const dialNote = dialPanel.querySelector(".cal-note");
  const dialSafe = dialPanel.querySelector(".cal-safe");
  const dialUseEnds = dialPanel.querySelector(".cal-useends");
  const dialSweep = dialPanel.querySelector(".cal-sweep");
  const dialResume = dialPanel.querySelector(".cal-resume");

  const dialOutput = () =>
    (dial === null || outputs === null ? null : outputs.find((each) => each.address === dial.address) || null);

  const setNote = (text, level) => {
    dialNote.textContent = text;
    dialNote.className = level ? `cal-note ${level}` : "cal-note";
  };

  // Every act here is started from a click handler and finishes later, so
  // nothing awaits it. Each act catches its own REQUEST failures and says
  // which; this catches everything else and refuses to be silent about it.
  const started = (promise) =>
    promise?.catch?.((error) => {
      console.error("[servo] an act failed:", error);
      setNote(`Something went wrong on this page: ${error && error.message ? error.message : error}`, "error");
    });

  // The span the slider covers. The component band by default, narrowed by
  // `safe range` to the cautious band, or by `use these ends` to the travel the
  // builder has recorded.
  const dialRange = (output) => {
    if (dial !== null && dial.ends && output.calibrated) {
      return {
        lo: Math.min(output.openUs, output.closeUs),
        hi: Math.max(output.openUs, output.closeUs),
      };
    }
    if (dial !== null && dial.safe) {
      return { lo: Math.max(output.bandLoUs, SAFE_LO), hi: Math.min(output.bandHiUs, SAFE_HI) };
    }
    return { lo: output.bandLoUs, hi: output.bandHiUs };
  };

  // MIN and MAX name the two ends, not an ordering: after a reverse, MIN can be
  // the larger number. MIN is the row's `close` and MAX its `open`, and reverse
  // trades them, so nothing here has to sort a pair.
  const endsSentence = (output) => {
    if (!output.calibrated) {
      return "No ends recorded yet. Drive the part to one and press Set MIN or Set MAX.";
    }
    return `MIN ${output.closeUs} µs · CENTER ${output.centreUs} µs · MAX ${output.openUs} µs`;
  };

  const sendServo = async (action, extra = {}) => {
    const output = dialOutput();
    if (!output) return false;
    try {
      await window.PAApi.postForm(
        "/api/servo",
        { arm: servoWord(output), action, ...extra },
        { timeoutMs: 4000 }
      );
      return true;
    } catch (error) {
      setNote(`The droid did not take that: ${window.PAApi.messageFor(error)}`, "error");
      return false;
    }
  };

  // Every hold command carries the width the dial is standing at. The first one
  // takes the Output and starts both bounds; the rest refresh the short expiry.
  const sendHold = () => sendServo("hold", { positionUs: String(dial === null ? 0 : dial.us) });
  const sendHoldSoon = window.PAUtils.debounce(() => {
    if (dial !== null && !dial.sweeping) sendHold();
  }, HOLD_CHANGE_MS);

  let holdTimer = null;
  const stopKeepalive = () => {
    if (holdTimer === null) return;
    window.clearInterval(holdTimer);
    holdTimer = null;
  };
  // The keepalive refreshes the hold only while the droid still says it HAS the
  // Output. The moment one of the firmware's two bounds lets go, this stops
  // asking and waits for the builder to press: a page that kept asking would
  // take the Output afresh and restart the ceiling, holding a servo for as long
  // as the tab was open - the one thing ADR 0064 says it must not be able to do.
  const startKeepalive = () => {
    stopKeepalive();
    holdTimer = window.setInterval(() => {
      if (dial !== null && dial.holding && !dial.sweeping) sendHold();
    }, HOLD_KEEPALIVE_MS);
  };

  const closeDial = ({ release = true } = {}) => {
    if (dial === null) return;
    // Closing the dial ends the hold (ADR 0064). Leaving it to the expiry would
    // drive the part for three more seconds with nobody watching.
    if (release) sendServo("release");
    dial = null;
    stopKeepalive();
    dialPanel.hidden = true;
    paint();
  };

  const openDial = (address) => {
    if (outputs === null) return;
    const output = outputs.find((each) => each.address === address);
    if (!output) return;
    if (run !== null) {
      showFeedback(`One at a time: ${partLabel(run.partId)} is being found. Stop that run first.`, "warning");
      return;
    }
    if (dial !== null && dial.address !== address) closeDial();
    const band = { lo: output.bandLoUs, hi: output.bandHiUs };
    dial = {
      address,
      // Start from where the droid says the Output is standing, so the first
      // hold does not move the part at all. An Output with no pulse has no
      // position to start from, so the middle of its band is the honest guess.
      us: output.commandedUs === null ? Math.round((band.lo + band.hi) / 2) : output.commandedUs,
      safe: false,
      ends: false,
      sweeping: false,
      // The droid has the Output as far as this page knows. Cleared when an
      // answer says it let go, so the keepalive stops asking for it.
      holding: true,
    };
    dialPanel.hidden = false;
    setNote("");
    sendHold();
    startKeepalive();
    paint();
  };

  // A capture: the width the dial is standing at becomes one of this Output's
  // three recorded positions. The droid may drag the centre inside the travel
  // the capture just described; this page reads the centre back and says so.
  const capture = async (end) => {
    const output = dialOutput();
    if (!output) return;
    const centreBefore = output.centreUs;
    const label = end === "centre" ? "CENTER" : end === "open" ? "MAX" : "MIN";
    try {
      await window.PAApi.postForm(
        "/api/config",
        { captureOutput: output.address, captureEnd: end, captureUs: String(dial.us) },
        { timeoutMs: 4000 }
      );
    } catch (error) {
      setNote(`${label} was not recorded: ${window.PAApi.messageFor(error)}`, "error");
      return;
    }
    await refresh();
    const after = dialOutput();
    if (after && after.centreUs !== centreBefore) {
      setNote(
        `${label} is ${dial.us} µs. Centre moved to ${after.centreUs} µs — it was outside the travel you just captured.`,
        "warning"
      );
      return;
    }
    setNote(`${label} is ${dial.us} µs.`, "success");
  };

  const reverseEnds = async () => {
    const output = dialOutput();
    if (!output) return;
    try {
      await window.PAApi.postForm("/api/config", { reverseOutput: output.address }, { timeoutMs: 4000 });
    } catch (error) {
      setNote(`The ends were not swapped: ${window.PAApi.messageFor(error)}`, "error");
      return;
    }
    await refresh();
    const after = dialOutput();
    if (after) setNote(`Ends swapped — MIN is now ${after.closeUs} µs.`, "success");
  };

  // test sweep visits the ends the builder recorded and comes back to where the
  // dial was standing. It refuses on an Output with none (ADR 0052's shape).
  const testSweep = async () => {
    const output = dialOutput();
    if (!output) return;
    const startedAt = dial.us;
    dial.sweeping = true;
    paint();
    const legs = [output.closeUs, output.openUs, startedAt];
    for (const target of legs) {
      if (dial === null || !dial.sweeping) return;
      dial.us = target;
      paint();
      dial.holding = true;
      if (!(await sendHold())) break;
      await new Promise((resolve) => window.setTimeout(resolve, SWEEP_DWELL_MS));
    }
    if (dial === null) return;
    dial.sweeping = false;
    dial.us = startedAt;
    paint();
    setNote(`Swept ${output.closeUs} µs to ${output.openUs} µs and back.`, "success");
  };

  // Repaint in place: never innerHTML, and never the control the builder has
  // hold of.
  const paintDial = () => {
    gateActs();
    if (dial === null) {
      dialPanel.hidden = true;
      return;
    }
    const output = dialOutput();
    if (!output) {
      // The Output left the droid's answer under the dial - a reboot, or a
      // different firmware. Say so rather than driving something that is gone.
      setNote("That output is not in the droid's answer any more, so the dial closed.", "warning");
      closeDial({ release: false });
      return;
    }
    const range = dialRange(output);
    if (dial.us < range.lo) dial.us = range.lo;
    if (dial.us > range.hi) dial.us = range.hi;

    dialTitle.textContent = `Calibrating ${output.name}`;
    dialBand.textContent = bandSentence(output);
    dialEnds.textContent = endsSentence(output);
    dialReadout.textContent = `${dial.us} µs`;
    if (document.activeElement !== dialSlider) {
      dialSlider.min = String(range.lo);
      dialSlider.max = String(range.hi);
      dialSlider.value = String(dial.us);
    }

    dialSafe.classList.toggle("is-on", dial.safe);
    dialUseEnds.classList.toggle("is-on", dial.ends);
    // `safe range` is inert on a row whose band is already the cautious one,
    // and says so rather than pretending to narrow anything.
    const alreadySafe = output.bandLoUs >= SAFE_LO && output.bandHiUs <= SAFE_HI;
    dialSafe.textContent = alreadySafe ? "safe range (already)" : "safe range";
    dialUseEnds.disabled = !output.calibrated;
    dialUseEnds.setAttribute("aria-disabled", output.calibrated ? "false" : "true");
    dialSweep.disabled = !output.calibrated || dial.sweeping;
    dialSweep.setAttribute("aria-disabled", dialSweep.disabled ? "true" : "false");

    // The Output has gone limp under the dial: one of the firmware's two
    // bounds, or the estop. The panel says which, and one press takes it back.
    const limp = output.commandedUs === null;
    if (limp) dial.holding = false;
    dialResume.hidden = !limp;
    if (limp && !dial.sweeping) {
      setNote(`${LIMP_SAID[output.limp] || LIMP_SAID.off}. Press take it again to hold it once more.`, "warning");
    }
  };

  // Every act that asks the droid to move something is refused while the estop
  // is latched, and until the droid has said it is not; the shell's own notice
  // names why. The rows are rebuilt whenever the SET of Outputs changes, so
  // their buttons are re-gated after each build as well as on each frame.
  function gateActs() {
    const live = estopLatched === false;
    outputRows.forEach((row) => {
      window.PAApi.gateControls([row.calibrate, row.off, ...row.go], live);
    });
    window.PAApi.gateControls([centreButton, findButton], live);
    window.PAApi.gateControls(Array.from(dialPanel.querySelectorAll("button")), live);
    window.PAApi.gateControls([dialSlider], live);
  }

  dialSlider.addEventListener("input", () => {
    if (dial === null) return;
    dial.us = Number(dialSlider.value);
    dialReadout.textContent = `${dial.us} µs`;
    sendHoldSoon();
  });

  dialPanel.addEventListener("click", (event) => {
    const button = event.target?.closest?.("button");
    if (!button || button.disabled || dial === null) return;
    if (button.classList.contains("cal-fine")) {
      const output = dialOutput();
      if (!output) return;
      const range = dialRange(output);
      const next = dial.us + Number(button.dataset.step) * FINE_US;
      dial.us = Math.min(range.hi, Math.max(range.lo, next));
      paintDial();
      sendHoldSoon();
      return;
    }
    if (button.classList.contains("cal-set")) {
      started(capture(button.dataset.end));
      return;
    }
    if (button.classList.contains("cal-reverse")) {
      started(reverseEnds());
      return;
    }
    if (button === dialSafe) {
      const output = dialOutput();
      if (output && output.bandLoUs >= SAFE_LO && output.bandHiUs <= SAFE_HI) {
        setNote("This output is already on the cautious range, so safe range has nothing to narrow.", "warning");
        return;
      }
      dial.safe = !dial.safe;
      if (dial.safe) dial.ends = false;
      paintDial();
      return;
    }
    if (button === dialUseEnds) {
      dial.ends = !dial.ends;
      if (dial.ends) dial.safe = false;
      paintDial();
      return;
    }
    if (button === dialSweep) {
      const output = dialOutput();
      if (output && !output.calibrated) {
        setNote("Nothing to sweep between yet: record MIN and MAX first.", "warning");
        return;
      }
      started(testSweep());
      return;
    }
    if (button.classList.contains("cal-off")) {
      started(pulsesOff(dial.address));
      return;
    }
    if (button === dialResume) {
      // One press re-takes the Output, which restarts both firmware bounds.
      dial.holding = true;
      started(sendHold());
      setNote("Holding it again.", "success");
      return;
    }
    if (button.classList.contains("cal-done")) closeDial();
  });

  // ---------------------------------------------------------------------------
  // Back to centre (#318, #365)
  //
  // One press, one request, and the droid paces the sweep itself: the
  // controller expands the press into one Output at a time and holds the
  // Cadence Floor between them, because a safe pace this page held is one a
  // hand-edited or imported client could walk around. No timer here, no queue.
  // The line counts from the rows: a row whose every Part is a light has no
  // centre to go back to, and the controller skips it for the same reason.
  // ---------------------------------------------------------------------------
  const centreAll = async () => {
    if (outputs === null) return;
    const lights = outputs.filter(isLightRow).length;
    const going = outputs.length - lights;
    try {
      await window.PAApi.postForm("/api/servo/centre", {}, { timeoutMs: 4000 });
    } catch (error) {
      window.PAUtils.showFeedback(centreSaid, `Nothing is going back to centre: ${window.PAApi.messageFor(error)}`, "error");
      return;
    }
    const said =
      `${going} ${going === 1 ? "output is" : "outputs are"} going back to centre, one at a time.` +
      (lights ? ` ${lights} skipped — a light has no centre.` : "");
    window.PAUtils.showFeedback(centreSaid, said, "success");
    refresh();
  };

  centreButton.addEventListener("click", () => {
    if (centreButton.disabled) return;
    started(centreAll());
  });

  // ---------------------------------------------------------------------------
  // pulses off, from a row or from the dial
  //
  // The Output goes limp where it is, at once. Reachable from a row so that it
  // is reachable DURING a Find by Moving run (#363): the run is nudging that
  // Output, and taking the pulse off it ends the run's motion. The surface says
  // which of the two happened, because the builder has just caused both.
  // ---------------------------------------------------------------------------
  const pulsesOff = async (address) => {
    if (outputs === null) return;
    const output = outputs.find((each) => each.address === address);
    if (!output) return;
    const label = output.name;
    const findingPart = run !== null && run.address === address ? run.partId : null;
    if (findingPart !== null) {
      markNotCurrent(address);
      endRun();
    }
    try {
      await window.PAApi.postForm(
        "/api/servo",
        { arm: servoWord(output), action: "release" },
        { timeoutMs: 4000 }
      );
    } catch (error) {
      const said = `${label} did not let go: ${window.PAApi.messageFor(error)}`;
      if (dial !== null && dial.address === address) setNote(said, "error");
      else showFeedback(said, "error");
      return;
    }
    const said =
      findingPart !== null
        ? `${label} is limp, and that stopped the run finding ${partLabel(findingPart)}. ${partLabel(findingPart)} stays ${NOT_WIRED}.`
        : `${label} is limp — nothing is driving it, so it will sit wherever it is.`;
    if (dial !== null && dial.address === address) setNote(said, "success");
    showFeedback(said, findingPart !== null ? "warning" : "success");
    refresh();
  };

  outputsRegion.addEventListener("click", (event) => {
    const button = event.target?.closest?.("button");
    const address = button?.closest?.("[data-output]")?.dataset.output;
    // A browser delivers no click to a disabled button; this is the rule
    // itself: a refused control asks the droid for nothing.
    if (!address || button.disabled) return;
    if (button.dataset.ease) {
      const output = outputs?.find((each) => each.address === address);
      if (output && button.dataset.ease !== output.ease) started(saveMotion(address, { ease: button.dataset.ease }));
      return;
    }
    if (button.classList.contains("outputs-calibrate")) openDial(address);
    else if (button.classList.contains("outputs-off")) started(pulsesOff(address));
    else if (button.classList.contains("outputs-go")) started(drive(address, button.dataset.action));
  });

  // Leaving Servos ends a run and lets go of the dial: the bench feed stops
  // with the surface (#360), so nothing could step a run on, and a nudge sent
  // on the way back would be motion the builder did not press for. ADR 0064
  // ends a hold when the dial closes, and leaving the page is closing it.
  // Never a hold -- leaving is always allowed; this only hears it happening.
  window.PASurface?.holdUnmount(() => {
    if (run !== null) endRun(`The run stopped when you left Servos. ${partLabel(run.partId)} stays ${NOT_WIRED}.`);
    closeDial();
    return false;
  });

  // The estop, on the status stream rather than the bench feed. Subscribed
  // below every definition it calls, because the stream replays its last frame
  // to a new subscriber synchronously.
  window.PAStatusStream?.subscribe((eventType, payload) => {
    if (eventType !== "status" || !payload || typeof payload !== "object") return;
    estopLatched = payload.estop === true;
    gateActs();
    if (estopLatched && run !== null) {
      const { partId, address } = run;
      if (address !== null) markNotCurrent(address);
      endRun(`The estop stopped the run. ${partLabel(partId)} stays ${NOT_WIRED}.`, "error");
    }
    // A latched estop has released every enabled Output (ADR 0043), so a dial
    // that was holding one is no longer holding anything. The panel stays open
    // and says so.
    if (payload.estop === true && dial !== null) {
      dial.sweeping = false;
      setNote("The estop let go of every output. Clear it, then press take it again.", "error");
    }
    // And it ends a back-to-centre sweep wherever it had got to (#365): no
    // row's commanded mark is current any longer, so each is held back rather
    // than guessed at until the droid answers again.
    if (payload.estop === true && outputs !== null) {
      outputs.forEach((output) => markNotCurrent(output.address));
      window.PAUtils.showFeedback(
        centreSaid,
        "The estop let go of every output. Nothing is being driven, so nothing is going back to centre.",
        "error"
      );
    }
  });

  // ---------------------------------------------------------------------------
  // Servo assignment: which servo each Output carries, drawn by
  // data/output_settings.js and shared with Wiring's wired ticks. Its plates
  // name the Part(s) on each Output, and a save there - wired, and what each
  // carries - decides what each row's drive cell offers, so the rows are
  // repainted in place whenever data/outputs.js's answer changes.
  // ---------------------------------------------------------------------------
  window.PAOutputSettings?.mount("type", {
    body: document.getElementById("servo-types-body"),
    feedback: document.getElementById("servo-types-feedback"),
    describe: (output) => listParts(output.parts),
  });
  OUTPUTS.onChange(() => {
    if (outputs === null) return;
    outputs = tableOutputs();
    outputs.forEach(paintOutputRow);
  });

  // ---------------------------------------------------------------------------
  // Loading
  // ---------------------------------------------------------------------------
  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/droid_parts.js": "the parts catalog",
      "/droid_part_kind.js": "the parts catalog",
      "/parts_mapping.js": "the parts on each output",
      "/outputs.js": "the outputs",
      "/output_settings.js": "the outputs",
      "/servo.js": "servo control",
      "/footer.js": "page footer",
    });
    window.PABootstrap.registerSection("servo-outputs", (opts) => loadOutputs({ ...opts, withConfig: true }), {
      label: "the outputs and what they drive",
    });
  } else {
    loadOutputs({ withConfig: true }).catch((error) => console.warn("[servo] outputs unavailable:", error));
  }

  // Owned by this surface, so the shell stops it when the operator leaves
  // Servos and starts it on the way back (#360). A failed read is
  // PASurface.poll()'s to report: catching it here would hand the registry a
  // fulfilled promise and mark the surface current on a read that never landed
  // (#360).
  window.PASurface?.poll(() => loadOutputs(), { cadenceMs: POLL_MS, refreshOnReturn: true }).start();
})();

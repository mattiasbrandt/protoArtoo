// =============================================================================
// data/servo.js
//
// Servos (CONTEXT.md "Servos"): the body's Outputs as servos. One section of
// Outputs, one row each, named by what the board prints beside the pin and by
// the Part(s) on it. On each row a builder picks which servo it carries, drives
// it (open, close, stop, or a typed width sent once), records its ends with the
// calibration dial, and takes the pulse off; how it lets go, moves and powers
// up, and which Parts are on it, open under the row on demand. Find by Moving
// and back to centre sit over the rows. It
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
// An Output is found by moving it from Wiring, where a Part with no Output is
// listed (data/find_by_moving.js, #411): every row here has a Part already.
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
// nothing to set, and says why. How long it holds after a move arrives before
// it lets go - its Output Release (ADR 0043, #443) - is set beside them, on
// the same terms, and the row says what it will do whether or not it is set. What it does at power-up sits beside them and
// is offered whether or not it is calibrated: the two are separate decisions
// (ADR 0052), and calibrating never changes it.
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
    groupParts,
    partLabel,
    listParts,
    servoWord,
    hasServoWord,
    isLightRow,
  } = P;

  // #293's honesty tiers, as the counts the Outputs section is headed with
  // (#318). A tier is a count, never a place a row moves to: the rows stay in
  // the order the wires plug in. Every row has a Part (listed() below), so an
  // Output with none is no tier of this page's.
  const TIERS = [
    { id: "driving", label: "Moving a part" },
    { id: "switched-off", label: "Wired but switched off" },
  ];
  // A firmware older than this page reports no position at all, and that is
  // not the same as an Output with no pulse, so it is never counted as off.
  const tierOf = (output) => (OUTPUTS.live(output).state === "limp" ? "switched-off" : "driving");

  // The Outputs this page lists: only those with a Part on them, because an
  // Output with a Part on it is wired and one with none is free (operator,
  // 2026-09-29 on #411: "why is the servos page hardcoded to list out these
  // when I have no parts defined with wiring?!"; CONTEXT.md "Servos"). Read
  // from the Parts, never from the wired tick, as Wiring reads it.
  const listed = (outputs) => outputs.filter((output) => output.parts.length > 0);

  const tiersNode = document.getElementById("outputs-tiers");
  const outputsRegion = document.getElementById("outputs-table");
  const outputsSection = document.getElementById("outputs-card");
  const feedback = document.getElementById("outputs-feedback");
  const dialog = document.getElementById("outputs-move-dialog");
  if (!outputsRegion || !outputsSection || !dialog) return;
  const centreBulk = outputsSection.querySelector(".outputs-bulk");
  const centreButton = centreBulk.querySelector(".outputs-centre");
  // A plain .feedback, found once and held: showFeedback() rewrites its whole
  // class list, so it cannot be looked up again by the class it was found by.
  const centreSaid = centreBulk.querySelector(".feedback");

  const esc = (value) => window.PAUtils.escapeHtml(String(value));
  const showFeedback = (text, level) => window.PAUtils.showFeedback(feedback, text, level);

  // Until the table answers, the section and the part picker say so in the
  // one word for it (data/outputs.js live()); the page's markup carries none.
  if (tiersNode) tiersNode.textContent = OUTPUTS.live(null).word;

  // The Outputs this page draws a row for are data/outputs.js's list, in the
  // order it gives them, each carrying whether it is wired and what it
  // carries; this page keeps no copy of its own.
  const answered = () => OUTPUTS.known().table;
  let dial = null; // the Output being calibrated, at most one (below)
  // Whether anything may be asked to move: the Live Reading's answer
  // (data/live_reading.js), false until the droid has said its estop is clear
  // and whenever contact with it is lost.
  let moveActsLive = false;

  // ---------------------------------------------------------------------------
  // The table: built once per set of Outputs
  // ---------------------------------------------------------------------------
  // Parts as wrapping pills, grouped as Parts groups them, so a builder finds
  // a Part the same way from either end. Pills and not a drop-down: there are
  // dozens, and the house picker beyond about five choices is pills that wrap
  // (operator, 2026-09-28 on #399).
  const partPills = (parts) =>
    groupParts(parts)
      .map(
        (group) =>
          `<div class="part-pills-group"><span class="part-pills-name">${esc(group.label)}</span><span class="part-pills">` +
          group.parts
            .map((part) => `<button class="part-pill" type="button" data-part="${esc(part.id)}">${esc(partLabel(part.id))}</button>`)
            .join("") +
          `</span></div>`
      )
      .join("");
  // Every catalog Part, for each row's "put a part on".
  const addPills = partPills(catalog.parts);

  // A row is headed by the Part(s) on it and the pin the board prints beside
  // it (CONTEXT.md "Servos"); an Output nobody has named - an expander's row -
  // shows its address for the pin. The Parts are painted (paintOutputRow()),
  // since a Part moves without the rows being rebuilt. Which servo it carries
  // is picked on Wiring, on the Part's row (#411). Every act starts refused:
  // none may run on a guess about the estop, and the droid has not said yet.
  //
  // One Output is one <tbody> of two lines (#399, operator reviews 2026-09-28:
  // "too many simply ugly square boxes", then "clean and nice"). The first is
  // what the builder acts on: the Part(s) and the pin, a press to put another
  // Part on it, where it was told to go, and the acts on it. The second holds two
  // panels, each closed until its own press opens it: the settings (how it
  // lets go, how it moves, what it does at power-up) and the Parts to put on
  // it, which open from the Drives cell they change. The line shows while
  // either is open. The <tbody> carries data-output, so every lookup below
  // finds its cells in either line. The drive cell comes before the settings
  // line on purpose: .outputs-drive-note and .outputs-drive-acts are looked up
  // by their first match.
  const outputRowHtml = (output) => {
    const label = output.name;
    return (
      `<tbody class="parts-row outputs-row" data-output="${esc(output.address)}">` +
      `<tr class="outputs-main">` +
      `<th scope="row"><span class="parts-name outputs-parts"></span>` +
      `<span class="outputs-address">${esc(label)}</span>` +
      `<div class="hint outputs-narrowed" hidden></div></th>` +
      `<td class="outputs-drives">` +
      `<button class="btn btn-sm btn-quiet outputs-add-open" type="button" aria-expanded="false" ` +
      `aria-label="${esc(`Put a part on ${label}`)}">+ part</button></td>` +
      `<td class="outputs-position"><div class="outputs-bar" aria-hidden="true"><div class="outputs-now"></div><div class="outputs-tick"></div></div>` +
      `<span class="outputs-us"></span></td>` +
      // Driving it: the typed width goes out once and is saved nowhere; open
      // and close go to the ends recorded for it; stop drives it to centre and
      // does not hold it there.
      `<td class="outputs-drive"><span class="outputs-drive-note"></span>` +
      `<span class="outputs-drive-acts">` +
      `<input class="number-cell outputs-width" type="number" step="10" value="1500" ` +
      `aria-label="${esc(`Width to move ${label} to, in microseconds`)}">` +
      `<span class="outputs-go-group">` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="position" disabled aria-disabled="true">move</button>` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="open" disabled aria-disabled="true">open</button>` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="close" disabled aria-disabled="true">close</button>` +
      `<button class="btn btn-sm outputs-go" type="button" data-action="stop" disabled aria-disabled="true">stop</button>` +
      `</span></span></td>` +
      `<td class="outputs-acts">` +
      `<button class="btn btn-sm btn-quiet outputs-calibrate" type="button" ` +
      `aria-label="${esc(`Calibrate ${label} by moving it`)}" disabled aria-disabled="true">calibrate</button>` +
      `<button class="btn btn-sm btn-quiet outputs-off" type="button" ` +
      `aria-label="${esc(`Take the pulse off ${label}`)}" disabled aria-disabled="true">pulses off</button>` +
      `<button class="btn btn-sm btn-quiet outputs-more" type="button" aria-expanded="false" ` +
      `aria-label="${esc(`Settings for ${label}`)}">settings` +
      `<svg class="i chev" aria-hidden="true" focusable="false"><use href="#i-chevron-right"/></svg></button>` +
      `</td></tr>` +
      `<tr class="outputs-sub" hidden><td colspan="5"><div class="outputs-settings" hidden>` +
      // How it lets go (#443): never, or a time after each move arrives,
      // offered where how it moves is. The state beside it says what the
      // Output will do, or why it is limp.
      `<div class="outputs-setting"><span class="outputs-setting-name">release</span>` +
      `<span class="outputs-motion-field"><span class="outputs-release-set">` +
      `<div class="seg outputs-release-seg" role="radiogroup" aria-label="${esc(`When ${label} lets go`)}">` +
      `<button type="button" role="radio" aria-checked="false" data-release="never">never</button>` +
      `<button type="button" role="radio" aria-checked="false" data-release="after">after</button>` +
      `</div> <label class="outputs-release-after"><input class="number-cell outputs-release-s" type="number" ` +
      `min="0" max="60" step="0.5" aria-label="${esc(`Seconds ${label} holds after it arrives`)}"> ` +
      `s after it arrives</label></span>` +
      `<span class="outputs-release"></span></span></div>` +
      // How it moves (#414). The pill and the controls each sit in a plain
      // wrapper so `hidden` can take them off the line.
      `<div class="outputs-motion">` +
      `<div class="outputs-setting outputs-motion-off"><span class="outputs-setting-name">motion</span>` +
      `<span class="outputs-motion-word">off until calibrated</span></div>` +
      `<div class="outputs-setting outputs-motion-set"><span class="outputs-setting-name">motion</span>` +
      `<label class="outputs-motion-field"><input class="number-cell outputs-throw" type="number" step="10" ` +
      `aria-label="${esc(`Time to full throw for ${label}, in milliseconds`)}"> ms to full throw</label>` +
      `<label class="outputs-motion-field"><input class="number-cell outputs-accel" type="number" step="10" ` +
      `aria-label="${esc(`Time to get up to speed for ${label}, in milliseconds`)}"> ms to get up to speed</label>` +
      `<div class="seg outputs-ease" role="radiogroup" aria-label="${esc(`How ${label} eases`)}">` +
      OUTPUTS.EASES.map((ease) =>
        `<button type="button" role="radio" aria-checked="false" data-ease="${esc(ease.id)}">${esc(ease.label)}</button>`
      ).join("") +
      `</div></div></div>` +
      // At power-up (#414): limp, the default, or home. Hold keeps the drive
      // on, and its one-sentence risk shows only while hold is picked.
      `<div class="outputs-setting outputs-boot"><span class="outputs-setting-name">at power-up</span>` +
      `<div class="seg outputs-boot-seg" role="radiogroup" aria-label="${esc(`What ${label} does at power-up`)}">` +
      OUTPUTS.BOOTS.map((boot) =>
        `<button type="button" role="radio" aria-checked="false" data-boot="${esc(boot.id)}">${esc(boot.label)}</button>`
      ).join("") +
      `</div><span class="hint outputs-boot-risk">Hold keeps the pulse on, so a blocked part grinds.</span></div>` +
      `</div>` +
      // Putting a Part on it: a press is a request, not a state this control
      // keeps, and the row's Drives cell says what the droid answered.
      `<div class="outputs-add" role="group" aria-label="${esc(`Put a part on ${label}`)}" hidden>${addPills}</div>` +
      `</td></tr></tbody>`
    );
  };

  const outputRows = new Map();
  let outputAddresses = null;

  // The only rebuild, and only when the set of Outputs itself changes - which a
  // controller does across a reboot, not while this page is reading it (#318).
  const buildOutputs = (outputs, addresses) => {
    outputsRegion.innerHTML =
      `<table class="parts-table outputs-table"><thead><tr><th scope="col">Part</th>` +
      `<th scope="col" aria-label="Put another part on it"></th>` +
      `<th scope="col">Commanded position</th><th scope="col">Move it</th>` +
      `<th scope="col" aria-label="Calibrate and settings"></th>` +
      `</tr></thead>` +
      outputs.map(outputRowHtml).join("") +
      `</table>`;
    outputRows.clear();
    outputsRegion.querySelectorAll("[data-output]").forEach((node) => {
      outputRows.set(node.dataset.output, {
        node,
        parts: node.querySelector(".outputs-parts"),
        bar: node.querySelector(".outputs-bar"),
        now: node.querySelector(".outputs-now"),
        tick: node.querySelector(".outputs-tick"),
        us: node.querySelector(".outputs-us"),
        narrowed: node.querySelector(".outputs-narrowed"),
        driveNote: node.querySelector(".outputs-drive-note"),
        driveActs: node.querySelector(".outputs-drive-acts"),
        // The drive cell's own box, by its cell: the motion line's two boxes
        // share its .number-cell look and not its lookup.
        width: node.querySelector(".outputs-drive").querySelector(".outputs-width"),
        go: Array.from(node.querySelectorAll(".outputs-go")),
        release: node.querySelector(".outputs-release"),
        releaseSet: node.querySelector(".outputs-release-set"),
        releaseChoices: Array.from(node.querySelectorAll("[data-release]")),
        releaseAfter: node.querySelector(".outputs-release-after"),
        releaseSeconds: node.querySelector(".outputs-release-s"),
        motion: node.querySelector(".outputs-motion"),
        motionOff: node.querySelector(".outputs-motion-off"),
        motionSet: node.querySelector(".outputs-motion-set"),
        throwMs: node.querySelector(".outputs-throw"),
        accelMs: node.querySelector(".outputs-accel"),
        eases: Array.from(node.querySelectorAll("[data-ease]")),
        boot: node.querySelector(".outputs-boot"),
        boots: Array.from(node.querySelectorAll("[data-boot]")),
        bootRisk: node.querySelector(".outputs-boot-risk"),
        calibrate: node.querySelector(".outputs-calibrate"),
        off: node.querySelector(".outputs-off"),
        more: node.querySelector(".outputs-more"),
        addOpen: node.querySelector(".outputs-add-open"),
        sub: node.querySelector(".outputs-sub"),
        settings: node.querySelector(".outputs-settings"),
        add: node.querySelector(".outputs-add"),
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
  // One the droid drives since it started (data/outputs.js `driven`): its acts
  // are offered. Any other is refused by the route whatever is pressed, so its
  // row offers none (#364).
  const isActable = (output) => isDriveable(output) && output.driven !== false;

  // Why an Output offers no drive, said the way a builder needs it, or "" when
  // it does. What it carries is Wiring's answer, on the Part's row there; the
  // route is refused for a row no board labels. An Output whose settings
  // nobody can save has nothing set to refuse on. Every row has a Part, so it
  // is wired (the tick follows the Parts, #411).
  const driveRefusal = (output) => {
    if (!hasServoWord(output)) return "No name the servo route takes";
    if (isLightRow(output)) return "A light has no position";
    if (!output.switchable) return "";
    if (output.light) return `Carries the ${output.light.label}`;
    if (!output.servo) return "Pick its servo on Wiring";
    // Wired since the droid started: the tick is read at start (#364).
    if (output.driven === false) return "Restart the droid to use it";
    return "";
  };

  // Only style, textContent and classList, on nodes that already exist: a
  // repaint never rebuilds a row, so the control under the builder's pointer
  // stays where it is (hw-table.js:171-174). What the Output is doing, and
  // the word for it when there is no position to draw, are data/outputs.js's.
  const paintOutputRow = (output) => {
    const row = outputRows.get(output.address);
    if (!row) return;
    const light = isLightRow(output);
    const live = OUTPUTS.live(output);
    const pulsing = live.state === "pulsing";
    row.node.classList.toggle("is-wired", output.parts.length > 0);
    row.node.classList.toggle("partkind-light", light);
    row.bar.classList.toggle("is-off", !pulsing);
    // Whatever the droid has just answered is current, including after an
    // estop cut a nudge short.
    row.bar.classList.remove("is-stale");
    row.parts.textContent = listParts(output.parts);
    // The ends the upgrade moved into this part's range, and what they were,
    // until the builder saves this Output (#417).
    const was = output.narrowedFrom;
    row.narrowed.hidden = was === null;
    row.narrowed.textContent = was === null ? "" : `Ends moved to fit: were open ${was.openUs}, close ${was.closeUs} µs`;
    row.now.style.width = pulsing ? markAt(output.commandedUs, output) : "0%";
    row.tick.style.left = pulsing ? markAt(output.targetUs, output) : "0%";
    const refusal = driveRefusal(output);
    row.driveNote.textContent = refusal;
    row.driveActs.hidden = refusal !== "";
    const driveable = isDriveable(output);
    const actable = isActable(output);
    row.calibrate.hidden = !actable;
    row.off.hidden = !actable;
    paintMotion(row, output, driveable);
    row.node.classList.toggle("is-held", output.held);
    // The droid does not send this Output's pulse at all: the word for a field
    // that never arrives, not the one for one still coming.
    if (live.state === "unknown") {
      row.us.textContent = live.word;
      row.release.textContent = live.word;
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
    // What a pulsing Output will do. Where the control is open it already
    // says so, and the line stays quiet rather than saying it twice.
    else if (pulsing) row.release.textContent = row.releaseSet.hidden ? releaseSaid(output) : "";
    // An Output that has gone limp says WHICH of the ways it can happen this
    // was (#364): a bound the dial ran into is not the estop letting go.
    else row.release.textContent = live.word;
  };

  // A release time in the seconds a builder types, from the ms the row holds.
  const seconds = (ms) => String(Number((ms / 1000).toFixed(3)));
  const releaseSet = (output) => typeof output.release === "number" && output.release > 0;
  // What a pulsing servo Output will do once a move arrives (#443). Never
  // "holds where it stops" for one that will let go.
  const releaseSaid = (output) =>
    releaseSet(output) ? `Lets go ${seconds(output.release)} s after it arrives` : "Holds where it stops";

  // ---------------------------------------------------------------------------
  // How it moves (ADR 0052, #414)
  //
  // Only an Output that is calibrated, drives a servo and whose three fields
  // the droid names offers them. An unmeasured Output jumps whatever its
  // profile says, so its row says so rather than taking numbers that would do
  // nothing; its stored profile waits on the row until it is calibrated.
  // ---------------------------------------------------------------------------
  const motionOpen = (output) => isDriveable(output) && output.motionSettable && output.calibrated;
  // Power-up is not fenced by calibration: a Part sent home goes to the centre
  // the row holds, measured or not, exactly as back to centre does.
  const bootOpen = (output) => isDriveable(output) && output.bootSettable;

  const paintMotion = (row, output, driveable) => {
    const bootShown = driveable && output.bootSettable;
    row.boot.hidden = !bootShown;
    if (bootShown) {
      row.boots.forEach((button) => {
        const on = button.dataset.boot === output.boot;
        button.classList.toggle("active", on);
        button.setAttribute("aria-checked", on ? "true" : "false");
      });
      row.bootRisk.hidden = output.boot !== "home-hold";
      // Home and hold with a release time lets go after it gets home, like
      // any arrival (operator, 2026-09-30 on #443): the grind risk is only
      // true of a hold with none.
      row.bootRisk.textContent = releaseSet(output)
        ? `Lets go ${seconds(output.release)} s after it gets home.`
        : "Hold keeps the pulse on, so a blocked part grinds.";
    }
    const shown = driveable && output.motionSettable;
    const open = shown && output.calibrated;
    row.motionOff.hidden = !shown || open;
    row.motionSet.hidden = !open;
    paintRelease(row, output, open && output.releaseSettable);
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

  // The release control (#443): never, or a time after each move arrives.
  // "after" on an Output that holds is the builder about to type a time: the
  // box opens and nothing is asked of the droid until they do.
  const releaseAsked = new Set();
  const paintRelease = (row, output, open) => {
    row.releaseSet.hidden = !open;
    if (!open) return;
    if (releaseSet(output)) releaseAsked.delete(output.address);
    const after = releaseSet(output) || releaseAsked.has(output.address);
    row.releaseChoices.forEach((button) => {
      const on = (button.dataset.release === "after") === after;
      button.classList.toggle("active", on);
      button.setAttribute("aria-checked", on ? "true" : "false");
    });
    row.releaseAfter.hidden = !after;
    // Never the box the builder is typing in.
    if (document.activeElement !== row.releaseSeconds) {
      row.releaseSeconds.value = releaseSet(output) ? seconds(output.release) : "";
    }
  };

  // One field at a time, through the one module that saves an Output's
  // settings (data/outputs.js). The droid refuses a number outside what the
  // row takes and says which; the row then repaints to what it holds.
  const saveMotion = async (address, patch) => {
    const output = OUTPUTS.at(address);
    const power = "boot" in patch;
    if (!output || !(power ? bootOpen(output) : motionOpen(output))) return;
    try {
      await OUTPUTS.save(address, patch);
      showFeedback(`${output.name} saved. ${power ? "The next power-up uses it." : "The next move uses it."}`, "success");
    } catch (error) {
      // data/outputs.js has already put a refusal in the page's words.
      showFeedback(`Not saved: ${window.PAApi.messageFor(error)}.`, "error");
      const row = outputRows.get(address);
      const now = OUTPUTS.at(address);
      if (row && now) paintMotion(row, now, isDriveable(now));
    }
  };

  // With no Part on any Output there is no row to draw, and one line sends
  // the builder to where Parts are put on Outputs.
  const NONE_LISTED = `No part is on an output yet. <a class="link-btn" href="#wiring">Put parts on outputs on Wiring</a>.`;

  const paintOutputs = (outputs, addresses) => {
    if (outputs.length === 0) {
      if (outputAddresses !== "") {
        outputsRegion.innerHTML = `<p class="hint outputs-none">${NONE_LISTED}</p>`;
        outputRows.clear();
        outputAddresses = "";
      }
    } else if (outputAddresses !== addresses) {
      buildOutputs(outputs, addresses);
    }
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

  // ---------------------------------------------------------------------------
  // Repainted in place
  // ---------------------------------------------------------------------------
  const paint = () => {
    if (!answered()) return;
    const outputs = listed(OUTPUTS.list());
    const addresses = outputs.map((output) => output.address).join(",");
    paintOutputs(outputs, addresses);
    paintDial();
  };

  // A read after an act reads the servo table alone; the section run on mount
  // reads the config with it, once, for what each Output carries. Either
  // publishes, and the page paints from that, once (below).
  const loadOutputs = async ({ handle = null, withConfig = false } = {}) => {
    if (withConfig) await OUTPUTS.load({ handle });
    else await OUTPUTS.refresh({ handle });
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

  // A release time is typed in seconds and saved as the ms the droid counts:
  // 0 to 60 s, where 0 is never.
  outputsRegion.addEventListener("change", (event) => {
    const box = event.target;
    if (!box?.classList?.contains("outputs-release-s")) return;
    const address = box.closest?.("[data-output]")?.dataset.output;
    if (!address) return;
    const s = Number(box.value);
    if (box.value === "" || !Number.isFinite(s) || s < 0 || s > 60) {
      showFeedback("Type a time from 0 to 60 seconds.", "warning");
      return;
    }
    started(saveMotion(address, { release: Math.round(s * 1000) }));
  });

  outputsRegion.addEventListener("change", (event) => {
    const box = event.target;
    if (!box?.classList?.contains("outputs-throw") && !box?.classList?.contains("outputs-accel")) return;
    const address = box.closest?.("[data-output]")?.dataset.output;
    if (!address) return;
    const ms = Math.round(Number(box.value));
    const key = box.classList.contains("outputs-throw") ? "throwMs" : "accelMs";
    if (box.value === "" || !Number.isFinite(ms)) {
      showFeedback("Type a time in milliseconds.", "warning");
      return;
    }
    started(saveMotion(address, { [key]: ms }));
  });

  // ---------------------------------------------------------------------------
  // Driving an Output from its row
  // ---------------------------------------------------------------------------
  const drive = async (address, action) => {
    const output = OUTPUTS.at(address);
    const row = outputRows.get(address);
    if (!output || !row) return;
    const label = output.name;
    const form = { arm: servoWord(output), action };
    if (action === "position") {
      // Sent as typed: the droid holds the range, and a width it will not
      // take comes back as a refusal messageFor() words.
      form.positionUs = String(row.width.value).trim();
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

  // A row's last commanded mark is not current any more: the estop let go of
  // every Output somewhere the page never read. Held back, not guessed at,
  // until the next answer repaints the row - which is the table not having
  // answered yet, and says so in its word.
  const markNotCurrent = (address) => {
    const row = outputRows.get(address);
    if (!row) return;
    row.bar.classList.add("is-stale");
    row.us.textContent = OUTPUTS.live(null).word;
  };

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
  // first and can move neither. Only a press takes the Output: opening the
  // dial, take it again, a test sweep. Everything else this page sends is a
  // refresh, which the firmware drops once the hold has gone (#417).
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
  // Grouped by what the builder does in turn: drive the part, record an end,
  // then the tools that change the range or test it, and the way out. The
  // three captures are one joined control, because they are three answers to
  // "which end is this?" (#399, operator review 2026-09-28).
  dialPanel.innerHTML =
    `<div class="cal-head"><h4 class="cal-title"></h4><p class="cal-band"></p></div>` +
    `<p class="hint cal-desc">Move the part until it looks right, then press the end you are setting. The ` +
    `servo holds while you watch, and goes limp a few seconds after you leave or after ten minutes.</p>` +
    `<div class="cal-drive">` +
    `<button class="btn btn-sm btn-quiet cal-fine" type="button" data-step="-1" aria-label="Down 5 microseconds">−5 µs</button>` +
    `<input class="cal-slider fader" type="range" step="1" aria-label="Move this output">` +
    `<button class="btn btn-sm btn-quiet cal-fine" type="button" data-step="1" aria-label="Up 5 microseconds">+5 µs</button>` +
    `<span class="cal-readout"></span>` +
    `</div>` +
    `<div class="cal-record">` +
    `<div class="seg cal-sets" role="group" aria-label="Record this width as an end">` +
    `<button class="cal-set" type="button" data-end="close">Set MIN</button>` +
    `<button class="cal-set" type="button" data-end="centre">Set CENTER</button>` +
    `<button class="cal-set" type="button" data-end="open">Set MAX</button>` +
    `</div>` +
    `<p class="cal-ends"></p>` +
    `</div>` +
    `<div class="cal-tools">` +
    `<div class="cal-acts">` +
    `<button class="btn btn-sm btn-quiet cal-reverse" type="button">reverse</button>` +
    `<span class="cal-hint">it swaps the two ends</span>` +
    `<button class="btn btn-sm btn-quiet cal-safe" type="button">safe range</button>` +
    `<button class="btn btn-sm btn-quiet cal-useends" type="button">use these ends</button>` +
    `<button class="btn btn-sm btn-quiet cal-sweep" type="button">test sweep</button>` +
    `</div>` +
    `<div class="cal-acts">` +
    `<button class="btn btn-sm btn-quiet cal-off" type="button">pulses off</button>` +
    `<button class="btn btn-sm accent cal-resume" type="button">take it again</button>` +
    `<button class="btn btn-sm cal-done" type="button">done</button>` +
    `</div>` +
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

  const dialOutput = () => (dial === null ? null : OUTPUTS.at(dial.address));

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
      return "No ends recorded yet. Move the part to one and press Set MIN or Set MAX.";
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

  // Every hold command carries the width the dial is standing at. A press takes
  // the Output and starts both bounds; a refresh - the keepalive, and every
  // move of the dial - keeps a standing hold and takes nothing (#417).
  const sendHold = ({ refresh }) =>
    sendServo("hold", {
      positionUs: String(dial === null ? 0 : dial.us),
      ...(refresh ? { refresh: "1" } : {}),
    });
  const sendHoldSoon = window.PAUtils.debounce(() => {
    if (dial !== null && !dial.sweeping) sendHold({ refresh: true });
  }, HOLD_CHANGE_MS);

  let holdTimer = null;
  const stopKeepalive = () => {
    if (holdTimer === null) return;
    window.clearInterval(holdTimer);
    holdTimer = null;
  };
  // The keepalive only refreshes, so however late this page learns that a
  // bound, the estop or pulses off let go, what it sends in the meantime takes
  // nothing: the firmware drops a refresh with no hold standing, and only the
  // builder's press takes the Output back (ADR 0064). It stops on the estop,
  // and while the tab is hidden, where nobody is watching the part and the
  // few-second expiry is what ends the hold.
  const startKeepalive = () => {
    stopKeepalive();
    if (document.visibilityState === "hidden") return;
    holdTimer = window.setInterval(() => {
      if (dial !== null && dial.holding && !dial.sweeping) sendHold({ refresh: true });
    }, HOLD_KEEPALIVE_MS);
  };
  // A press: take the Output, and keep asking for it from here on.
  //
  // `take` is this press's own record: `after` stays null until the droid has
  // answered it, then holds the table-read mark from that moment, which is
  // the line paintDial() judges every limp reading against. A fresh object
  // per press, so a late answer to an earlier press - two quick presses, a
  // sweep's legs - can never stamp its mark on a later one. The droid refusing
  // the take is an answer too: nothing is standing, and the next reading's
  // limp is the truth.
  const takeHold = () => {
    const take = { after: null };
    dial.take = take;
    dial.holding = true;
    startKeepalive();
    const taken = sendHold({ refresh: false });
    taken.then(() => {
      take.after = OUTPUTS.readMark();
    });
    return taken;
  };
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden") stopKeepalive();
    else if (dial !== null && dial.holding) startKeepalive();
  });

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
    const output = OUTPUTS.at(address);
    if (!output) return;
    if (dial !== null && dial.address !== address) closeDial();
    const band = { lo: output.bandLoUs, hi: output.bandHiUs };
    dial = {
      address,
      // Start from where the droid says the Output is standing, so the first
      // hold does not move the part at all. An Output with no pulse has no
      // position to start from, so the middle of its band is the honest guess.
      us: OUTPUTS.live(output).state === "pulsing" ? output.commandedUs : Math.round((band.lo + band.hi) / 2),
      safe: false,
      ends: false,
      sweeping: false,
      // The droid has the Output as far as this page knows. Cleared when an
      // answer says it let go, so the keepalive stops asking for it.
      holding: true,
      // The last press, set by takeHold() below before anything paints.
      take: null,
    };
    dialPanel.hidden = false;
    setNote("");
    takeHold();
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
    const form = { captureOutput: output.address, captureEnd: end, captureUs: String(dial.us) };
    try {
      await window.PAApi.postForm("/api/config", form, { timeoutMs: 4000 });
    } catch (error) {
      setNote(`${label} was not recorded: ${window.PAApi.messageFor(error, form)}`, "error");
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
    const form = { reverseOutput: output.address };
    try {
      await window.PAApi.postForm("/api/config", form, { timeoutMs: 4000 });
    } catch (error) {
      setNote(`The ends were not swapped: ${window.PAApi.messageFor(error, form)}`, "error");
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
      if (!(await takeHold())) break;
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
    //
    // Only a reading the droid was asked for AFTER it answered the take can
    // say that. The reading the dial opens on, and one already in flight when
    // the take went out, describe the Output before the take, and whatever
    // reason they carry is the reason it went limp THEN: "off" from boot
    // (#355 finding 9), "estop" from an estop latched and cleared before the
    // dial opened (#417). Believing either ended the fresh take at once, so
    // the keepalive never ran. The reason is not the test and must not become
    // one again - a list of stale reasons is only ever one reason short. The
    // order of the reads is the test (data/outputs.js readSince()).
    //
    // The droid answers a take once it is queued, and ServoTask drains that
    // queue every 20 ms tick (src/tasks/servo_task.cpp), so a read issued
    // after the answer - a whole browser round trip later - finds it applied.
    // One that beat the tick would put take it again up one press early; it
    // can never keep asking for a pin the droid has let go.
    //
    // An estop latched after the take does not wait for any of this: the
    // status stream ends the hold the moment it is heard (below).
    const live = OUTPUTS.live(output);
    const take = dial.take;
    const limp = live.state === "limp" && take.after !== null && OUTPUTS.readSince(take.after);
    if (limp) dial.holding = false;
    dialResume.hidden = !limp;
    if (limp && !dial.sweeping) {
      setNote(`${live.word}. Press take it again to hold it once more.`, "warning");
    }
  };

  // Every act that asks the droid to move something is refused while the estop
  // is latched, until the droid has said it is not, and while contact with it
  // is lost; the shell's own notice names why. The rows are rebuilt whenever
  // the SET of Outputs changes, so their buttons are re-gated after each build
  // as well as on each reading.
  function gateActs() {
    const live = moveActsLive;
    outputRows.forEach((row) => {
      window.PAApi.gateControls([row.calibrate, row.off, ...row.go], live);
    });
    window.PAApi.gateControls([centreButton], live);
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
      started(takeHold());
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
    if (!answered()) return;
    const outputs = OUTPUTS.list();
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
  // The Output goes limp where it is, at once.
  // ---------------------------------------------------------------------------
  const pulsesOff = async (address) => {
    const output = OUTPUTS.at(address);
    if (!output) return;
    const label = output.name;
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
    const said = `${label} is limp — no pulse holds it, so it will sit wherever it is.`;
    if (dial !== null && dial.address === address) setNote(said, "success");
    showFeedback(said, "success");
    refresh();
  };

  outputsRegion.addEventListener("click", (event) => {
    const button = event.target?.closest?.("button");
    const address = button?.closest?.("[data-output]")?.dataset.output;
    // A browser delivers no click to a disabled button; this is the rule
    // itself: a refused control asks the droid for nothing.
    if (!address || button.disabled) return;
    // Opening or closing one of the row's two panels: the page's own view,
    // and nothing is asked of the droid.
    const row = outputRows.get(address);
    if (row && (button === row.more || button === row.addOpen)) {
      const panel = button === row.more ? row.settings : row.add;
      const open = panel.hidden;
      panel.hidden = !open;
      button.setAttribute("aria-expanded", open ? "true" : "false");
      row.sub.hidden = row.settings.hidden && row.add.hidden;
      return;
    }
    // A Part put on this Output: the same request Parts makes (above).
    if (button.dataset.part) {
      if (answered()) mover.request(P.moveFor(OUTPUTS.list(), button.dataset.part, address), button);
      return;
    }
    if (button.dataset.ease) {
      const output = OUTPUTS.at(address);
      if (output && button.dataset.ease !== output.ease) started(saveMotion(address, { ease: button.dataset.ease }));
      return;
    }
    if (button.dataset.release) {
      const output = OUTPUTS.at(address);
      if (!output) return;
      if (button.dataset.release === "never") {
        releaseAsked.delete(address);
        if (releaseSet(output)) started(saveMotion(address, { release: 0 }));
        else paint();
        return;
      }
      releaseAsked.add(address);
      paint();
      outputRows.get(address)?.releaseSeconds.focus?.();
      return;
    }
    if (button.dataset.boot) {
      const output = OUTPUTS.at(address);
      if (output && button.dataset.boot !== output.boot) started(saveMotion(address, { boot: button.dataset.boot }));
      return;
    }
    if (button.classList.contains("outputs-calibrate")) openDial(address);
    else if (button.classList.contains("outputs-off")) started(pulsesOff(address));
    else if (button.classList.contains("outputs-go")) started(drive(address, button.dataset.action));
  });

  // Leaving Servos lets go of the dial: ADR 0064 ends a hold when the dial
  // closes, and leaving the page is closing it. Never a hold -- leaving is
  // always allowed; this only hears it happening.
  window.PASurface?.holdUnmount(() => {
    closeDial();
    return false;
  });

  // The estop, from the Live Reading rather than the bench feed. Subscribed
  // below every definition it calls, because the Live Reading hands a new
  // subscriber the current reading synchronously.
  window.PALiveReading.subscribe((reading) => {
    moveActsLive = reading.moveActsLive;
    gateActs();
    const latched = reading.estopLatched;
    // A latched estop has released every enabled Output (ADR 0043), so a dial
    // that was holding one is no longer holding anything, and stops asking.
    // The panel stays open and says so; take it again is the way back.
    if (latched && dial !== null) {
      dial.sweeping = false;
      dial.holding = false;
      stopKeepalive();
      setNote("The estop let go of every output. Clear it, then press take it again.", "error");
    }
    // And it ends a back-to-centre sweep wherever it had got to (#365): no
    // row's commanded mark is current any longer, so each is held back rather
    // than guessed at until the droid answers again.
    if (latched && answered()) {
      OUTPUTS.list().forEach((output) => markNotCurrent(output.address));
      window.PAUtils.showFeedback(
        centreSaid,
        "The estop let go of every output. Every one is limp, so nothing is going back to centre.",
        "error"
      );
    }
  });

  // Every read of the Outputs - the follow's, an act's, a save's answer -
  // publishes once, and this is the one place the page paints from it, so a
  // read paints each row once (#421).
  OUTPUTS.onChange(() => paint());

  // ---------------------------------------------------------------------------
  // Loading
  // ---------------------------------------------------------------------------
  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/live_reading.js": "live updates",
      "/shell.js": "page layout",
      "/droid_parts.js": "the parts catalog",
      "/droid_part_kind.js": "the parts catalog",
      "/parts_mapping.js": "the parts on each output",
      "/outputs.js": "the outputs",
      "/servo.js": "servo control",
      "/footer.js": "page footer",
    });
    window.PABootstrap.registerSection("servo-outputs", (opts) => loadOutputs({ ...opts, withConfig: true }), {
      label: "the outputs and their parts",
    });
  } else {
    loadOutputs({ withConfig: true }).catch((error) => console.warn("[servo] outputs unavailable:", error));
  }

  // The bench feed (#318): data/outputs.js's follow of the table. It is this
  // surface's, so the shell stops it when the operator leaves Servos and
  // starts it on the way back (#360).
  OUTPUTS.follow().start();
})();

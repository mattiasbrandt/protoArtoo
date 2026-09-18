// =============================================================================
// data/wiring.js
//
// Wiring (CONTEXT.md "Wiring"): the destination that answers the one question
// no other screen can -- "I am holding a servo lead: which output does it go
// to, and which part will it move?" It is a reference, not a control surface:
// it writes nothing, and no act on it reaches the droid.
//
// EVERYTHING ON IT IS GENERATED. The loom comes from the Board Lanes the
// running firmware reports (GET /api/identity), the rows from this droid's own
// Servo Output table (GET /api/servo/outputs) joined to the Droid Parts
// Catalog, and the switches from the Component Toggles (GET /api/config).
// Nothing here keeps a copy of one board's pin numbers, which is the defect a
// Board Lane exists to close (include/board_lanes.inc).
//
// ONE GENERATOR, TWO CALLERS. wiringDocument() returns the whole sheet as
// markup and nothing else -- no DOM, no fetches, no stylesheet. The screen
// caller at the foot of this file mounts those strings into the surface's
// plates; the printable bench copy (#366) wraps the same strings in a file of
// its own, wiringSheetFile(). That is why the sheet is built as strings rather
// than nodes: the two copies cannot disagree if there is only one thing that
// makes them, and CONTEXT.md "Wiring" is explicit that they are "the same
// document from one generator". A second generator is the one thing this group
// can get wrong that cannot be fixed cheaply later.
//
// COLOUR AND SIZE ARE THE STYLESHEET'S, AND THE PICTURES CARRY PAPER BENEATH IT.
// Every element this file emits carries a class, so on screen the tokens in
// data/style.css decide what it looks like -- and @media print re-points those
// tokens at paper (data/style.css ":root" and its print block). The saved bench
// copy ships with no stylesheet at all, though, and a picture is the part of it
// that gets cropped out and pasted somewhere else, so each picture also carries
// its own paint as SVG presentation attributes. Those are the lowest-priority
// paint there is -- any stylesheet rule beats them -- so on screen they change
// nothing, and in the saved file they are all there is. Every colour among them
// is a `var(--token,#fallback)` pair naming the token the stylesheet paints the
// same element with, and the fallback is the paper value @media print gives
// that token: the saved file and a printed screen are the same ink on the same
// paper. This is the form the project this view learns from shipped for the
// same reason (r2d2-astromech-simulator v1.79.0, src/js/app/wiring.js:50), and
// the one colour literal a standalone export is allowed (#366).
// =============================================================================
(() => {
  "use strict";

  // ---------------------------------------------------------------------------
  // The three sentences this view is not free to reword
  //
  // The bounded promise and the scope statement are the whole reason a
  // generated sheet is safe to take to a bench: they say what the sheet is
  // about before a builder reads a single row. They sit on the surface's face
  // AND on every picture, because a page gets cropped and a screenshot travels
  // (#293).
  // ---------------------------------------------------------------------------
  const PROMISE = "what this image will actually drive";
  const SCOPE = "signal + ground only, power distribution is your build's business";

  // The pace the droid holds between outputs it starts itself. Typed here, and
  // that is worth saying out loud: SEQ_CADENCE_FLOOR_MS lives in
  // include/sequence_bulk_centre.h and NOTHING reports it, so a page that wants
  // to say it has to carry it. Whoever takes the body's own measurement (#355)
  // changes it in both places.
  const CADENCE = "~450 ms (one servo at a time)";

  // The three plate headings. They are the generator's for the same reason
  // the promise is: the screen writes them into its plates and the saved file
  // prints them, so a heading reworded here is reworded in both, and a heading
  // typed into data/wiring.html as well would be the second copy this whole
  // view exists to abolish.
  const PLATES = Object.freeze({
    loom: "The loom",
    tiers: "What this image will drive",
    rail: "The shared rail",
  });

  // ---------------------------------------------------------------------------
  // The four honesty tiers
  //
  // Disjoint by construction: a row is built into exactly one of them, and the
  // tier travels ON the row as a token rather than as a predicate a reader has
  // to re-derive from three fields. Two of the four tokens are Availability
  // Reasons already (CONTEXT.md "Availability Reason"), which is deliberate --
  // the vocabulary for "a Part no Output claims" and "switched off" exists, and
  // a fifth spelling of either would be a second thing to keep in step. The
  // other two name states that are not a "no" at all: a driven row and an
  // output waiting for a lead.
  //
  // `heading` is verbatim from #293 and is not this file's to reword. The count
  // rides in the section head's subtitle beside it, which is where the anatomy
  // puts a count (docs/ui-copy-voice.md rule 15) -- so the heading stays the
  // four words it is required to be and the number is still in the head.
  //
  // `footnote` is what the generated footnote says about this tier. It is
  // written here, beside the predicate it explains, so the foot of the sheet
  // cannot define a tier the sheet no longer has -- the fault the reference's
  // six-line literal footer shipped, where "planned" and "unmapped" share one
  // definition zone and nothing regenerates either.
  // ---------------------------------------------------------------------------
  const TIERS = [
    {
      id: "driven",
      heading: "Driven",
      subject: "part",
      noun: ["part", "parts"],
      footnote:
        "A <b>Driven</b> row is a part an output records and whose output is switched on: " +
        "this image puts a pulse on that lead.",
    },
    {
      id: "component-disabled",
      heading: "Wired, switched off",
      subject: "part",
      noun: ["part", "parts"],
      footnote:
        "<b>Wired, switched off</b> means the lead is recorded and the output it is on is " +
        "switched off, so the wiring is right and nothing moves until you switch it on.",
    },
    {
      id: "part-not-assigned",
      heading: "Nothing drives it",
      subject: "part",
      noun: ["part", "parts"],
      footnote:
        "<b>Nothing drives it</b> means no output on this droid records that part. It is still " +
        "a part you can author a move for; it starts moving the day an output claims it.",
    },
    {
      id: "output-no-part",
      heading: "Output with no part",
      subject: "output",
      noun: ["output", "outputs"],
      footnote:
        "An <b>Output with no part</b> is a spare: the droid drives it, and nothing on the droid " +
        "is recorded as being on the end of it.",
    },
  ];

  const tierById = new Map(TIERS.map((tier) => [tier.id, tier]));

  // ---------------------------------------------------------------------------
  // What the operator calls a Board Lane
  //
  // The lane key IS the Component Toggle key: include/board_lanes.inc names the
  // signal and src/web/api_config.cpp reports that signal's toggle under the
  // same word, so the join between "where it is routed" and "is it switched on"
  // is the key itself and needs no table. The only thing neither payload
  // carries is the builder's word for the lane, which is this map. A lane with
  // no entry still draws -- adding a lane is adding a row, and a row nobody
  // named must not vanish because of it.
  // ---------------------------------------------------------------------------
  const LANE_NAMES = {
    drive: "Foot Drive",
    audio: "Sound",
    // The Status Plate's own word for this link (data/shell.js, the DOME LINK
    // chip), so the sheet and the chrome above it name one thing once.
    protor2link: "Dome link",
  };

  const esc = (value) => window.PAUtils?.escapeHtml?.(value) ?? String(value ?? "");
  const escAttr = (value) => window.PAUtils?.escapeAttr?.(value) ?? String(value ?? "");

  const plural = (count, [one, many]) => `${count} ${count === 1 ? one : many}`;

  // Where a builder changes a Component Toggle today. The shell owns the one
  // spelling of that route (data/shell.js, window.PAUi.setupActionHtml), so
  // every "switched off" answer on this sheet names the same destination as
  // every other surface's -- which is the half of "every no names the builder's
  // next move" that a wrong link quietly breaks (CONTEXT.md "Availability
  // Family": 16 strings once named a destination a builder could not reach).
  const setupRouteHtml = (action) =>
    window.PAUi?.setupActionHtml?.(action) ?? `${esc(action)} in Setup`;

  // Where a Part is put on an Output. Parts is the surface that owns that act,
  // and it is a destination that exists.
  const PARTS_ROUTE = '<a href="/parts.html">Parts</a>';

  // ---------------------------------------------------------------------------
  // The rows
  // ---------------------------------------------------------------------------

  // What this image could drive at all, which is what bounds the promise. A
  // Part whose control path is the dome link is the Dome Controller's to move,
  // not this image's, and a Part that declares no control path (the two dome
  // orientation fixtures) is not driven by anything. Both are named and counted
  // on the sheet rather than dropped -- see outOfScopeHtml() -- because a bound
  // a builder cannot see is indistinguishable from a row that went missing.
  //
  // This is the first reader of the catalog's `control` field in the browser,
  // and it is reading it for the question the field answers: what path a Part
  // CAN be driven through on the design. What drives it on THIS droid is still
  // the Servo Output rows and never this field (#375), which is the join below.
  const drivableHere = (part) =>
    part.control !== null && part.control !== undefined && part.control !== "dome-link";

  const outOfScope = (part) => !drivableHere(part);

  // ---------------------------------------------------------------------------
  // Finding a signal's Component Toggle
  //
  // GET /api/config keys its toggles by the signal's own name, and a Board Lane
  // names the same signal -- but not always with the same capitals.
  // include/board_lanes.inc declares `protor2link`; src/web/api_config.cpp
  // reports it as `protoR2link`. Joining on the name as written therefore found
  // no toggle for the dome link, and an absent toggle reads as "nothing has
  // switched this off" -- so the dome link's lane drew as live on every droid,
  // including one with the dome link switched off. Measured while building this
  // sheet, 2026-09-17.
  //
  // So the join folds the name to lower case, which also lets an Output's board
  // legend (ARM1) reach its own toggle (arm1) through the same door. Folding
  // rather than a rename table is what keeps a lane added later joining with no
  // edit here -- which is the whole point of a manifest where adding a lane is
  // adding a row.
  // ---------------------------------------------------------------------------
  const componentIndex = (components) => {
    const index = new Map();
    Object.keys(components || {}).forEach((key) => {
      const entry = components[key];
      if (entry && typeof entry === "object") index.set(key.toLowerCase(), entry);
    });
    return index;
  };

  // A signal the config carries no toggle for -- an expander row, which has no
  // name either -- is not one anybody has switched off, so it reads as on
  // rather than inventing a switch for it.
  const switchedOn = (toggles, name) => {
    const entry = toggles.get(String(name || "").toLowerCase());
    return entry ? entry.enabled === true : true;
  };

  // The Board Component Label: the silkscreen text this board prints beside the
  // thing (ADR 0033). Absent where a board has no established label, which is
  // every row on firebeetle2 today -- so a caller shows the address instead
  // rather than a legend that is printed nowhere.
  const labelOf = (toggles, name) => {
    const entry = toggles.get(String(name || "").toLowerCase());
    const label = entry ? entry.label : null;
    return typeof label === "string" && label !== "" ? label : "";
  };

  // An Output as a builder reads it: the legend on the board where it has one,
  // and the Output Address otherwise.
  const outputLabel = (output) => output.name || output.address;

  // The one place a Part is joined to the Output that drives it. A Part is on at
  // most one Output (CONTEXT.md "Part"), so the first hit is the answer.
  const outputForPart = (outputs, partId) =>
    outputs.find((output) => output.parts.includes(partId)) || null;

  const wiringRows = ({ parts = [], outputs = [], components = {} } = {}) => {
    const toggles = componentIndex(components);
    const partRows = parts.filter(drivableHere).map((part) => {
      const output = outputForPart(outputs, part.id);
      if (!output) {
        return {
          tier: "part-not-assigned",
          part,
          output: null,
          why:
            `No output on this droid records ${esc(part.name)}, so nothing drives it. ` +
            `Put it on an output in ${PARTS_ROUTE}.`,
        };
      }
      if (!switchedOn(toggles, output.name)) {
        return {
          tier: "component-disabled",
          part,
          output,
          why:
            `${esc(outputLabel(output))} is switched off, so the lead is right and nothing ` +
            `moves. ${setupRouteHtml("Switch it on")}.`,
        };
      }
      return { tier: "driven", part, output, why: "" };
    });

    const outputRows = outputs
      .filter((output) => output.parts.length === 0)
      .map((output) => ({
        tier: "output-no-part",
        part: null,
        output,
        why:
          `Nothing on this droid is recorded on ${esc(outputLabel(output))}, so a lead here ` +
          `moves nothing yet. Claim it from ${PARTS_ROUTE}.`,
      }));

    return partRows.concat(outputRows);
  };

  // ---------------------------------------------------------------------------
  // The loom
  // ---------------------------------------------------------------------------
  const loomRows = ({ lanes = {}, components = {}, capabilities = {} } = {}) => {
    const toggles = componentIndex(components);
    return Object.keys(lanes).map((key) => {
      const lane = lanes[key] || {};
      const on = switchedOn(toggles, key);
      const name = LANE_NAMES[key] || key;
      // The audio lane and PA_CAP_DEDICATED_AUDIO_UART have to be read
      // together: where the capability is false, audio shares the dome link's
      // UART controller and only its RX rides it, because that controller's TX
      // is committed to the dome (include/board_lanes.inc, docs/api.md).
      const shared = key === "audio" && capabilities.PA_CAP_DEDICATED_AUDIO_UART === false;
      return {
        key,
        name,
        uart: lane.uart,
        tx: lane.tx,
        rx: lane.rx,
        label: labelOf(toggles, key),
        shared,
        on,
        why: on
          ? ""
          : `${esc(name)} is switched off, so nothing rides this lane. ` +
            `${setupRouteHtml("Switch it on")}.`,
      };
    });
  };

  // ---------------------------------------------------------------------------
  // The pictures
  //
  // Both carry the scope statement along their own foot. A picture that leaves
  // the page -- cropped, printed, pasted into a build thread -- has to say what
  // it is not showing, because the header that said it is no longer attached
  // (r2d2-astromech-simulator v1.79.0, src/js/app/wiring.js:481, whose per-board
  // line does the same two jobs).
  // ---------------------------------------------------------------------------
  const ROW_H = 46;
  const DIAGRAM_W = 960;
  const BOARD_X = 24;
  const BOARD_W = 190;
  const BOX_X = 520;
  const BOX_W = 416;
  const TOP = 46;
  // The foot carries two lines, the promise and the scope statement.
  const FOOT_H = 48;
  const BADGE_W = 46;
  const BADGE_H = 16;
  const RIGHT_X = DIAGRAM_W - BOARD_X;

  // ---------------------------------------------------------------------------
  // The paper beneath the stylesheet
  //
  // Each pair names the token data/style.css paints that element with, and
  // falls back to what that token becomes under @media print: --text,
  // --text-dim and --text-faint all print as --paper-ink, the seams as
  // --paper-line, and every ground as --paper. Written out rather than read
  // from the stylesheet because the saved file is precisely the copy that has
  // no stylesheet to read. If --paper-ink or --paper-line ever moves, these
  // move with it.
  //
  // These are the only colour literals this surface ships, and they appear
  // nowhere but inside a var() pair (#366; D2's checker, #353, accepts exactly
  // this form and nothing looser).
  // ---------------------------------------------------------------------------
  const INK = "var(--text,#111111)";
  const INK_DIM = "var(--text-dim,#111111)";
  const INK_FAINT = "var(--text-faint,#111111)";
  const SEAM = "var(--border,#999999)";
  const SEAM_STRONG = "var(--border-strong,#999999)";
  const PLATE = "var(--surface,#ffffff)";
  const PLATE_RAISED = "var(--surface-alt,#ffffff)";

  // Sizes in the picture's own units, matching the type tokens the stylesheet
  // gives the same classes (--fs-hint 10, --fs-sect 11, --fs-cell 13).
  const SMALL = `fill="${INK_FAINT}" font-size="10"`;

  // ---------------------------------------------------------------------------
  // When the sheet was made
  //
  // ONE function, and both the saved file's name and the stamp drawn inside
  // every picture are cut from the string it returns, so the file on the
  // printer and the page in your hand cannot name two different minutes -- and
  // cannot have read the clock twice, one in UTC and one in local time.
  // Local time, to the minute, because it records the moment a person pressed
  // the button (r2d2-astromech-simulator v1.79.0, src/js/core/util.js:36).
  // ---------------------------------------------------------------------------
  const sheetStamp = (when) => {
    const time = when instanceof Date ? when : new Date();
    const pad = (n) => String(n).padStart(2, "0");
    return (
      `${time.getFullYear()}-${pad(time.getMonth() + 1)}-${pad(time.getDate())}` +
      `-${pad(time.getHours())}${pad(time.getMinutes())}`
    );
  };

  // "2026-09-18-1405" as a person writes it: "2026-09-18 14:05".
  const stampText = (stamp) => `${stamp.slice(0, 10)} ${stamp.slice(11, 13)}:${stamp.slice(13, 15)}`;

  // The droid name the firmware allows is lower-case letters, digits and
  // hyphens (docs/api.md, POST /api/identity), which is already safe in a file
  // name; anything else is not trusted into one.
  const sheetFileName = (droidName, stamp) =>
    `wiring-${/^[a-z0-9-]{1,32}$/.test(droidName || "") ? droidName : "droid"}-${stamp}.html`;

  // ---------------------------------------------------------------------------
  // What travels with every picture
  //
  // The badge, the droid and the minute in the head; the bounded promise and
  // the scope statement along the foot. A picture that leaves the page -- a
  // crop, a photo of the printout, a paste into a build thread -- has lost
  // the header that said all four, and a printed page loses its header first
  // (#293). The promise carries where it came from in the same line: this
  // image reported it, and it was not read out of docs/pin_map.md, which is not
  // on the droid.
  //
  // The badge is drawn in ink rather than the reference's amber. Amber here is
  // a Status Colour -- "degraded, and you can do something about it" -- and a
  // sheet being new is not a state of the droid (CONTEXT.md "Status Colour").
  // What BETA means rides in the badge's <title>, which is what a hover and a
  // screen reader get.
  // ---------------------------------------------------------------------------
  const svgBadge = () =>
    `<g class="wd-beta"><title>New sheet. Check it against the droid before you cut a ` +
    `wire.</title>` +
    `<rect class="wd-beta-edge" x="${RIGHT_X - BADGE_W}" y="11" width="${BADGE_W}" ` +
    `height="${BADGE_H}" rx="3" fill="none" stroke="${INK}" stroke-width="1.4"/>` +
    `<text class="wd-beta-mark" x="${RIGHT_X - BADGE_W / 2}" y="22.5" fill="${INK}" ` +
    `font-size="9.5" font-weight="700" letter-spacing=".12em" text-anchor="middle">BETA</text></g>`;

  const svgStamp = (droidName, stamp) =>
    `<text class="wd-stamp" x="${RIGHT_X - BADGE_W - 10}" y="23" ${SMALL} text-anchor="end">` +
    `${droidName ? `${esc(droidName)} · ` : ""}made ${esc(stampText(stamp))}</text>`;

  const svgFoot = (height) =>
    `<text class="wd-scope wd-promise" x="${BOARD_X}" y="${height - 26}" ${SMALL}>` +
    `${esc(PROMISE)} - as the firmware reports it, not a pin map</text>` +
    `<text class="wd-scope" x="${BOARD_X}" y="${height - 12}" ${SMALL}>${esc(SCOPE)}</text>`;

  const svgOpen = (title, height) =>
    `<svg class="wd" viewBox="0 0 ${DIAGRAM_W} ${height}" role="img" ` +
    `aria-label="${escAttr(title)}" xmlns="http://www.w3.org/2000/svg" ` +
    `font-family="monospace">`;

  const svgHead = (text, { droidName, stamp }) =>
    `<text class="wd-title" x="${BOARD_X}" y="24" fill="${INK}" font-size="13" ` +
    `font-weight="600">${esc(text)}</text>` +
    svgStamp(droidName, stamp) +
    svgBadge();

  // SVG text does not wrap: an over-long label runs out past its box and over
  // whatever is beside it. So a label that will not fit is cut and the whole of
  // it travels in a <title>, which is what a hover and a screen reader get.
  // Four parts ganged to one lead is what reaches this -- rare, and exactly the
  // kind of build a bench sheet is drawn for.
  const clip = (text, max) => {
    const whole = String(text ?? "");
    return whole.length > max ? `${whole.slice(0, max - 1)}...` : whole;
  };

  // `paint` is the element's presentation attributes: its paper, for the copy
  // that has no stylesheet (see "The paper beneath the stylesheet").
  const svgText = (className, x, y, text, max, paint) => {
    const whole = String(text ?? "");
    const shown = clip(whole, max);
    const title = shown === whole ? "" : `<title>${esc(whole)}</title>`;
    return `<text class="${className}" x="${x}" y="${y}" ${paint}>${esc(shown)}${title}</text>`;
  };

  // One row: the Body Controller's edge, out to the thing on the end of it. A
  // line that is not live is dashed AND says why on the row, because a dashed
  // line on its own is a convention a builder has to be taught. The paper
  // carries the same difference the .is-idle rules draw on screen.
  const svgLink = (index, { live, busText, name, note }) => {
    const y = TOP + index * ROW_H + ROW_H / 2;
    const state = live ? "is-live" : "is-idle";
    const line = live
      ? `stroke="${INK_DIM}" stroke-width="1.8"`
      : `stroke="${INK_FAINT}" stroke-width="1.3" stroke-dasharray="5 4"`;
    return (
      `<g class="wd-link ${state}">` +
      `<path class="wd-line" d="M${BOARD_X + BOARD_W} ${y} H ${BOX_X}" fill="none" ${line}/>` +
      `<path class="wd-arrow" d="M${BOX_X - 12} ${y - 4} L${BOX_X - 3} ${y} L${BOX_X - 12} ${y + 4} Z" ` +
      `fill="${live ? INK_DIM : INK_FAINT}"/>` +
      svgText("wd-bus", BOARD_X + BOARD_W + 12, y - 7, busText, 48, SMALL) +
      `<rect class="wd-box" x="${BOX_X}" y="${y - 17}" width="${BOX_W}" height="34" rx="2" ` +
      `fill="${PLATE}" stroke="${live ? SEAM_STRONG : SEAM}"/>` +
      svgText("wd-name", BOX_X + 10, y - 3, name, 58,
        `fill="${live ? INK : INK_DIM}" font-size="11" font-weight="600"`) +
      svgText("wd-note", BOX_X + 10, y + 11, note, 64, SMALL) +
      `</g>`
    );
  };

  // The caption says what the box is driving rather than repeating its own
  // name, which is the one fact a reader cannot get from the box itself.
  const svgBoard = (rowCount, caption) => {
    const height = Math.max(90, rowCount * ROW_H - 22);
    const y = TOP + 6;
    return (
      `<g class="wd-board">` +
      `<rect class="wd-board-face" x="${BOARD_X}" y="${y}" width="${BOARD_W}" height="${height}" rx="2" ` +
      `fill="${PLATE_RAISED}" stroke="${SEAM_STRONG}" stroke-width="1.5"/>` +
      `<text class="wd-board-name" x="${BOARD_X + BOARD_W / 2}" y="${y + height / 2 - 4}" ` +
      `fill="${INK}" font-size="11" text-anchor="middle">Body Controller</text>` +
      `<text class="wd-board-sub" x="${BOARD_X + BOARD_W / 2}" y="${y + height / 2 + 12}" ` +
      `${SMALL} text-anchor="middle">${esc(caption)}</text>` +
      `</g>`
    );
  };

  const loomDiagramHtml = (lanes, boardName, made) => {
    if (lanes.length === 0) return "";
    const height = TOP + lanes.length * ROW_H + FOOT_H;
    const title = `The loom: ${lanes.length} Board Lane${lanes.length === 1 ? "" : "s"} this image routes`;
    return (
      svgOpen(title, height) +
      svgHead(`Control signals - ${boardName}`, made) +
      svgBoard(lanes.length, plural(lanes.length, ["lane", "lanes"])) +
      lanes
        .map((lane, index) =>
          svgLink(index, {
            live: lane.on,
            busText: laneBusText(lane),
            name: lane.name,
            note: lane.on ? laneWhere(lane) : "switched off - nothing rides this lane",
          })
        )
        .join("") +
      svgFoot(height) +
      `</svg>`
    );
  };

  const laneBusText = (lane) => `UART ${lane.uart} - TX ${lane.tx} / RX ${lane.rx}`;

  const laneWhere = (lane) => {
    const where = lane.label ? `${lane.label} on the board` : "no legend printed on this board";
    return lane.shared ? `${where} - shares this UART with the dome link, RX only` : where;
  };

  // The driven signals: one pin stub per output this image is putting a pulse
  // on, out to the part it moves. Ground is drawn because a servo lead has
  // three wires and only two of them are this sheet's; the rail itself is
  // described in its own plate and never drawn (CONTEXT.md "Wiring").
  const signalDiagramHtml = (driven, boardName, made) => {
    if (driven.length === 0) return "";
    const height = TOP + driven.length * ROW_H + FOOT_H;
    const title = `Signal and ground for ${driven.length} driven output${driven.length === 1 ? "" : "s"}`;
    return (
      svgOpen(title, height) +
      svgHead(`Signal and ground - ${boardName}`, made) +
      svgBoard(driven.length, plural(driven.length, ["driven output", "driven outputs"])) +
      driven
        .map((group, index) =>
          svgLink(index, {
            live: true,
            busText: `${group.label} - ${group.address}`,
            name: group.parts.join(" + "),
            note: "signal on the pin, ground to the board's own ground",
          })
        )
        .join("") +
      svgFoot(height) +
      `</svg>`
    );
  };

  // One box per Output rather than one per Part, because a ganged lead is one
  // wire and drawing it twice would put a channel number on the sheet twice --
  // the fault the reference fixed by giving each board its own box
  // (r2d2-astromech-simulator v1.79.0, src/js/app/wiring.js:423-436).
  const drivenGroups = (rows) => {
    const groups = [];
    rows
      .filter((row) => row.tier === "driven")
      .forEach((row) => {
        const address = row.output.address;
        let group = groups.find((each) => each.address === address);
        if (!group) {
          group = { address, label: outputLabel(row.output), parts: [] };
          groups.push(group);
        }
        group.parts.push(row.part.name);
      });
    return groups;
  };

  // ---------------------------------------------------------------------------
  // The tables
  // ---------------------------------------------------------------------------

  // A part's design name, and the three things that field can say. `cadName`
  // absent is a name nobody has read out of the design files yet; `cadName`
  // null is a part the design does not carry at all, which is what marks a
  // Common Addition (data/droid_parts.js). Wiring is the declared bridge
  // between the two naming systems (CONTEXT.md "Part"), so neither case prints
  // as a blank cell a builder would read as a missing row.
  const designNameHtml = (part) => {
    if (typeof part.cadName === "string" && part.cadName !== "") {
      return `<span class="wiring-cad">${esc(part.cadName)}</span>`;
    }
    if (part.cadName === null) {
      return '<span class="wiring-dim">a common addition, no design name</span>';
    }
    return '<span class="wiring-dim">not read out of the design files yet</span>';
  };

  const whereHtml = (part) => {
    const position = part.position ? esc(part.position) : "";
    const bearing = typeof part.bearingDeg === "number" ? `${part.bearingDeg}&deg;` : "";
    if (position && bearing) return `${position} · ${bearing}`;
    return position || bearing || '<span class="wiring-dim">wherever you wired it</span>';
  };

  const partNameHtml = (part) => {
    const shorthand = part.shorthand
      ? ` <span class="wiring-shorthand">${esc(part.shorthand)}</span>`
      : "";
    const kind = part.kind ? ` <span class="wiring-kind">${esc(part.kind)}</span>` : "";
    return `${esc(part.name)}${shorthand}${kind}`;
  };

  // "- not wired -" is the project's own words for a Part no Output claims, and
  // it is what the part-first table on Parts puts in the same cell
  // (data/parts.js, NOT_WIRED). It is spelt out here rather than shared,
  // because data/parts.js is fenced to another slice this wave; the two
  // spellings must not drift, so this comment is where that is written down.
  const NOT_WIRED = "\u2013 not wired \u2013";

  const outputCellHtml = (output) => {
    if (!output) return `<span class="wiring-dim">${esc(NOT_WIRED)}</span>`;
    return (
      `${esc(outputLabel(output))} <span class="wiring-address">${esc(output.address)}</span>`
    );
  };

  const whyCellHtml = (why) => (why ? `<span class="wiring-why">${why}</span>` : "");

  const partTableHtml = (rows) =>
    '<table class="wiring-table"><thead><tr>' +
    "<th>Part</th><th>Design name</th><th>Where</th><th>Output</th><th>Why</th>" +
    "</tr></thead><tbody>" +
    rows
      .map(
        (row) =>
          `<tr class="wiring-row" data-tier="${row.tier}" data-part="${
            escAttr(row.part.id)
          }">` +
          `<th scope="row">${partNameHtml(row.part)}</th>` +
          `<td class="wiring-design">${designNameHtml(row.part)}</td>` +
          `<td>${whereHtml(row.part)}</td>` +
          `<td class="wiring-output">${outputCellHtml(row.output)}</td>` +
          `<td class="wiring-reason">${whyCellHtml(row.why)}</td></tr>`
      )
      .join("") +
    "</tbody></table>";

  // A row whose subject is an Output carries different columns, because kind
  // decides which columns a row carries at all (CONTEXT.md "Output"): there is
  // no part on it, so there is no design name and no place on the droid.
  const outputTableHtml = (rows) =>
    '<table class="wiring-table"><thead><tr>' +
    "<th>Output</th><th>On the board</th><th>What is fitted</th><th>Why</th>" +
    "</tr></thead><tbody>" +
    rows
      .map(
        (row) =>
          `<tr class="wiring-row" data-tier="${row.tier}" data-output="${
            escAttr(row.output.address)
          }">` +
          `<th scope="row">${esc(outputLabel(row.output))}</th>` +
          `<td class="wiring-address">${esc(row.output.address)}</td>` +
          `<td>${fittedHtml(row.output)}</td>` +
          `<td class="wiring-reason">${whyCellHtml(row.why)}</td></tr>`
      )
      .join("") +
    "</tbody></table>";

  // "nothing recorded as fitted" and "an MG996R" are the same band of pulse
  // widths and two different sentences, which is why the answer reports the
  // component rather than the numbers (docs/api.md, GET /api/servo/outputs).
  const fittedHtml = (output) =>
    output.component && output.component !== "none"
      ? esc(output.component)
      : '<span class="wiring-dim">nothing recorded as fitted</span>';

  const loomTableHtml = (lanes) =>
    '<table class="wiring-table wiring-loom-table"><thead><tr>' +
    "<th>Signal</th><th>On the board</th><th>Routed over</th><th>Why</th>" +
    "</tr></thead><tbody>" +
    lanes
      .map(
        (lane) =>
          `<tr class="wiring-row" data-lane="${escAttr(lane.key)}" data-live="${
            lane.on ? "yes" : "no"
          }">` +
          `<th scope="row">${esc(lane.name)}</th>` +
          `<td>${
            lane.label
              ? esc(lane.label)
              : '<span class="wiring-dim">no legend printed on this board</span>'
          }</td>` +
          `<td class="wiring-address">${esc(laneBusText(lane))}${
            lane.shared
              ? ' <span class="wiring-dim">shared with the dome link, RX only</span>'
              : ""
          }</td>` +
          `<td class="wiring-reason">${whyCellHtml(lane.why)}</td></tr>`
      )
      .join("") +
    "</tbody></table>";

  // ---------------------------------------------------------------------------
  // The bound, said out loud
  //
  // The parts this image does not drive are named and counted rather than
  // filtered away in silence. No row is hidden (#296): a row that is outside
  // the promise is outside it for a reason the sheet states, and this one
  // carries no link and no destination because there is nothing for a builder
  // to do about it -- a settled no (CONTEXT.md "Availability Family").
  // ---------------------------------------------------------------------------
  const outOfScopeHtml = (parts) => {
    const outside = parts.filter(outOfScope);
    if (outside.length === 0) return "";
    const domeLink = outside.filter((part) => part.control === "dome-link").length;
    const noPath = outside.length - domeLink;
    const clauses = [];
    if (domeLink > 0) {
      clauses.push(
        `${plural(domeLink, ["part", "parts"])} the Dome Controller drives over the dome link`
      );
    }
    if (noPath > 0) {
      clauses.push(`${plural(noPath, ["part", "parts"])} nothing on this droid drives at all`);
    }
    return (
      `<p class="hint wiring-bound">Outside this sheet: ${clauses.join(" and ")}. ` +
      `They are on the droid and they are not on this sheet, because this sheet says ` +
      `${esc(PROMISE)}.</p>`
    );
  };

  // ---------------------------------------------------------------------------
  // The two pieces that do not depend on what the droid answered
  //
  // They are generated anyway, and that is the point: the bounded promise, the
  // scope statement and the cadence have exactly one home each, so the screen
  // header and the bench copy's header cannot come to say different things.
  // Putting either sentence in data/wiring.html as well would be the second
  // copy this whole view exists to abolish.
  // ---------------------------------------------------------------------------
  const promiseHtml = () =>
    `Everything below says <b>${esc(PROMISE)}</b>, read from the Body Controller that ` +
    `answered this page - not from a document anyone keeps by hand. It draws ` +
    `<b>${esc(SCOPE)}</b>.`;

  // The shared rail is described and never drawn (CONTEXT.md "Wiring"), and this
  // is the one place the droid's own pacing is stated, because the reason it
  // paces itself is the rail every servo shares.
  //
  // The cadence carries its provenance in the same breath, and that is not
  // padding. CONTEXT.md "Cadence Floor" puts "the ~450 ms cadence (that figure
  // is the dome's)" in its _Avoid_ list, and include/sequence_bulk_centre.h
  // says why both are true: 450 ms is the DOME's measured figure, adopted
  // deliberately and explicitly as the body's stand-in until the body's own is
  // taken (#355), with "do not quietly let it become one". Stating it attributed
  // is the opposite of adopting it quietly.
  const railHtml = () =>
    `<p class="prose">Every servo on this droid draws from one supply, and several of them ` +
    `starting at once is what pulls that supply down - which drops every part the droid was ` +
    `holding and loses its place in whatever it was doing. So the droid paces itself: when it ` +
    `expands a move of its own, such as putting every output back to centre, it starts outputs ` +
    `<b>${esc(CADENCE)}</b> apart, and an output that takes longer than that to travel holds ` +
    `the next one off for as long as it is still moving.</p>` +
    `<p class="hint">That figure was measured on the Dome Controller, where seven ring servos ` +
    `share the dome supply. Nobody has measured this body's, so the droid stands in the dome's ` +
    `number until somebody does.</p>` +
    `<div class="note note-info"><b>How you fuse and distribute that supply is your build's, ` +
    `and no picture on this page is a claim about it.</b> Work out the stall current of ` +
    `everything on a rail rather than the idle draw: a servo fighting a linkage pulls several ` +
    `times what it pulls sitting still, and that is the moment a shared rail gives out.</div>`;

  // ---------------------------------------------------------------------------
  // The footnote
  //
  // Generated from the tiers actually on the sheet, in the sheet's own order,
  // and from nothing else. A tier with no rows contributes no sentence, so the
  // foot of the sheet can never define a section a builder cannot find.
  // ---------------------------------------------------------------------------
  const footnoteHtml = (present, hasDesignNames) => {
    const lines = present.map((section) => `<li>${tierById.get(section.id).footnote}</li>`);
    if (hasDesignNames) {
      lines.push(
        "<li><b>Design name</b> is what the part is called in the design files you printed it " +
          "from, so the name on this sheet and the name on your slicer agree. Label the loom " +
          "with it.</li>"
      );
    }
    if (lines.length === 0) return "";
    return `<ul class="wiring-footnote">${lines.join("")}</ul>`;
  };

  // ---------------------------------------------------------------------------
  // wiringDocument()
  // The whole sheet, as markup, from one read of the droid. Pure: the minute it
  // is stamped with arrives in the model as a sheetStamp() string, so the
  // caller that names a file after that minute holds the very string the
  // pictures print.
  // ---------------------------------------------------------------------------
  const wiringDocument = (model = {}) => {
    const parts = model.parts || [];
    const outputs = model.outputs || [];
    const boardName = model.boardName || "Body Controller";
    const droidName = typeof model.droidName === "string" ? model.droidName : "";
    const stamp = typeof model.stamp === "string" ? model.stamp : sheetStamp();
    const made = { droidName, stamp };
    const rows = wiringRows(model);
    const lanes = loomRows(model);

    const present = TIERS.map((tier) => {
      const tierRows = rows.filter((row) => row.tier === tier.id);
      if (tierRows.length === 0) return null;
      return {
        id: tier.id,
        heading: tier.heading,
        count: tierRows.length,
        countText: plural(tierRows.length, tier.noun),
        rows: tierRows,
        tableHtml:
          tier.subject === "output" ? outputTableHtml(tierRows) : partTableHtml(tierRows),
      };
    }).filter(Boolean);

    const driven = drivenGroups(rows);
    const hasDesignNames = rows.some(
      (row) => row.part && typeof row.part.cadName === "string" && row.part.cadName !== ""
    );

    const drivenCount = rows.filter((row) => row.tier === "driven").length;
    const summary =
      `${plural(rows.length, ["row", "rows"])} · ${drivenCount} driven · ` +
      `${rows.length - drivenCount} not`;

    const lanesOn = lanes.filter((lane) => lane.on).length;
    const loomSummary =
      `${plural(lanes.length, ["lane", "lanes"])} · ${lanesOn} switched on`;

    return {
      promise: PROMISE,
      scope: SCOPE,
      cadence: CADENCE,
      promiseHtml: promiseHtml(),
      railHtml: railHtml(),
      plates: PLATES,
      boardName,
      droidName,
      stamp,
      fileName: sheetFileName(droidName, stamp),
      rows,
      lanes,
      tiers: present,
      summary,
      loomSummary,
      loomHtml: lanes.length
        ? loomTableHtml(lanes) + loomDiagramHtml(lanes, boardName, made)
        : '<p class="hint">This image reports no Board Lane at all, so there is no loom to draw.</p>',
      // Every tier that is present becomes a section; a tier that is not simply
      // is not here. The driven picture rides inside the Driven section, where
      // the rows it draws are.
      tiersHtml:
        present
          .map(
            (section) =>
              `<section class="wiring-tier" data-tier="${section.id}">` +
              `<div class="sect"><h3>${esc(section.heading)}</h3>` +
              `<span class="sub">${section.countText}</span></div>` +
              section.tableHtml +
              (section.id === "driven" ? signalDiagramHtml(driven, boardName, made) : "") +
              `</section>`
          )
          .join("") + outOfScopeHtml(parts),
      footnoteHtml: footnoteHtml(present, hasDesignNames),
    };
  };

  // ---------------------------------------------------------------------------
  // wiringSheetFile()
  // The bench copy: what wiringDocument() made, in a file that stands alone.
  //
  // It is a WRAPPER and nothing more. Every heading, tier, row, count,
  // picture and sentence in it is a string the generator returned; this adds
  // only the frame a file needs to be a page.
  //
  // STANDALONE, WHICH IS FOUR DECISIONS. No stylesheet, no <script> and no
  // <img>: it fetches nothing when it is opened, so it opens on a laptop at the
  // bench with no droid in reach. No <style> either: the pictures carry their
  // own paper (see "The paper beneath the stylesheet"), and the tables and
  // prose are plain HTML a browser prints legibly on its own. The one <link> is
  // an icon that is an empty data: URL, because without one a browser goes
  // looking for a favicon beside the file. And <base> points back at the droid
  // that made it, so the "Put it on an output in Parts" links still reach Parts
  // when this is opened from a download folder rather than resolving against
  // the disk.
  // ---------------------------------------------------------------------------
  const wiringSheetFile = (sheet, origin = "") => {
    const madeAt = stampText(sheet.stamp);
    const about = sheet.droidName ? `${sheet.droidName} - ${madeAt}` : madeAt;
    return (
      "<!doctype html>\n" +
      '<html lang="en"><head><meta charset="utf-8">' +
      `<title>Wiring - ${esc(about)}</title>` +
      '<link rel="icon" href="data:,">' +
      (origin ? `<base href="${escAttr(origin)}/">` : "") +
      "</head><body>" +
      `<h1>Wiring</h1>` +
      `<p>${sheet.droidName ? `${esc(sheet.droidName)} - ` : ""}made ${esc(madeAt)}</p>` +
      `<p>${sheet.promiseHtml}</p>` +
      `<h2>${esc(sheet.plates.loom)}</h2><p>${sheet.loomSummary}</p>${sheet.loomHtml}` +
      `<h2>${esc(sheet.plates.tiers)}</h2><p>${sheet.summary}</p>` +
      `${sheet.tiersHtml}${sheet.footnoteHtml}` +
      `<h2>${esc(sheet.plates.rail)}</h2>${sheet.railHtml}` +
      "</body></html>\n"
    );
  };

  window.PAWiring = Object.freeze({
    PROMISE,
    SCOPE,
    CADENCE,
    TIERS,
    PLATES,
    wiringRows,
    loomRows,
    promiseHtml,
    railHtml,
    sheetStamp,
    wiringDocument,
    wiringSheetFile,
  });

  // ===========================================================================
  // The screen caller
  //
  // Mounts what wiringDocument() made. It knows which plate each piece goes on
  // and nothing else about the sheet -- every string above is the generator's,
  // so the bench copy (#366) puts the same strings in a file without
  // re-deciding one of them.
  // ===========================================================================
  const write = (id, html) => {
    const node = document.getElementById(id);
    if (node) node.innerHTML = html;
  };

  let identity = window.PAIdentity || null;
  let outputs = [];
  let components = {};
  let answered = false;

  const model = () => ({
    parts: window.DroidParts?.parts || [],
    outputs,
    components,
    lanes: identity?.board_lanes || {},
    capabilities: identity?.board_capabilities || {},
    boardName: "Body Controller",
    droidName: typeof identity?.droidName === "string" ? identity.droidName : "",
  });

  // The pieces that do not wait for the droid: the two sentences and the
  // plate headings. They go up at mount rather than on the first answer,
  // because the promise is what tells a builder whether to read the rest at
  // all, and a header that is blank until a fetch lands has nothing to say in
  // exactly the moment it matters.
  write("wiring-promise", promiseHtml());
  write("wiring-rail", railHtml());
  write("wiring-loom-heading", esc(PLATES.loom));
  write("wiring-tiers-heading", esc(PLATES.tiers));
  write("wiring-rail-heading", esc(PLATES.rail));

  // The pictures on screen carry the minute they were drawn - when this
  // surface last read the droid, or when its sheet was last saved - so a
  // screenshot of them says when it was true, the same as the saved copy does.
  const paint = (stamp = sheetStamp()) => {
    const sheet = wiringDocument({ ...model(), stamp });
    write("wiring-loom-summary", sheet.loomSummary);
    write("wiring-summary", sheet.summary);
    write("wiring-loom", sheet.loomHtml);
    write("wiring-tiers", sheet.tiersHtml);
    write("wiring-footnote", sheet.footnoteHtml);
    return sheet;
  };

  // ---------------------------------------------------------------------------
  // The bench copy's caller
  //
  // The one act on this surface, and it writes nothing to the droid: it saves
  // a file on the computer in front of you. The sheet it saves is made from the
  // same read the screen is showing, and the screen is repainted with that
  // very sheet in the same moment, so the page you are looking at and the file
  // you just saved carry the same minute, the same tiers and the same counts.
  //
  // It is a real link rather than a button that fakes one: the file is put on
  // the link as the press is handled, and the browser's own download does the
  // rest, which is also what a keyboard's Enter reaches.
  // ---------------------------------------------------------------------------
  const saveLink = document.getElementById("wiring-save");
  let savedUrl = "";

  const saveFeedback = (text, level = "") =>
    window.PAUtils?.showFeedback?.(document.getElementById("wiring-save-feedback"), text, level);

  const saveSheet = (event) => {
    if (!answered) {
      event.preventDefault?.();
      saveFeedback("No answer from the droid yet. Nothing to save.");
      return;
    }
    try {
      const sheet = paint(sheetStamp(new Date()));
      const file = wiringSheetFile(sheet, window.location?.origin || "");
      if (savedUrl) URL.revokeObjectURL(savedUrl);
      savedUrl = URL.createObjectURL(new Blob([file], { type: "text/html" }));
      saveLink.setAttribute("href", savedUrl);
      saveLink.setAttribute("download", sheet.fileName);
      // The browser does the saving and does not say when it has, so this
      // says what was handed over rather than claiming it landed.
      saveFeedback(`Saving ${sheet.fileName}. Print it anywhere, no droid needed.`);
    } catch (error) {
      // The link still holds the last file it was given, or none, so the
      // press must not follow it: a stale sheet saved under a fresh name is
      // the one outcome worse than no sheet.
      event.preventDefault?.();
      saveFeedback(`Not saved: ${error.message}`, "error");
    }
  };

  saveLink?.addEventListener("click", saveSheet);

  // One section for one answer. The sheet is a join of three reads and a half
  // answer is not a sheet -- a table painted from outputs the droid reported
  // and toggles it did not would say a lead is live when it is switched off --
  // so both fetches are in the one section run and either one failing is the
  // section failing, which is what the Page Recovery View is for.
  const loadSheet = async ({ handle = null } = {}) => {
    const api = handle || window.PAApi;
    if (!api) throw new Error("no way to reach the Body Controller");
    const answer = await api.get("/api/servo/outputs");
    const table = answer?.data?.outputs;
    if (!Array.isArray(table)) throw new Error("the droid's outputs answer carries no table");
    const config = await api.get("/api/config");
    outputs = table.map((output) => ({
      address: String(output.address),
      name: typeof output.name === "string" ? output.name : "",
      parts: Array.isArray(output.parts) ? output.parts.map(String) : [],
      component: typeof output.component === "string" ? output.component : "",
    }));
    components =
      config?.data?.components && typeof config.data.components === "object"
        ? config.data.components
        : {};
    answered = true;
    paint();
    saveLink?.setAttribute("aria-disabled", "false");
  };

  // Identity is fetched once at boot by the shell, so a surface mounted later
  // reads the cache and also listens: the shell replays the outcome to a late
  // surface, and POST /api/identity republishes it (data/shell.js).
  //
  // It repaints only once the droid's own table has answered. The lanes alone
  // would draw a sheet with every part under "Nothing drives it" and no outputs
  // at all - which is the right answer for a fresh droid and a wrong one for
  // every other, so it is not a sheet worth flashing up on the way to the real
  // one.
  window.addEventListener("pa:identity-available", (event) => {
    identity = event.detail;
    if (answered) paint();
  });

  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/droid_parts.js": "parts list",
      "/wiring.js": "the wiring sheet",
    });
    window.PABootstrap.registerSection("wiring-sheet", loadSheet, {
      label: "what this image drives",
    });
  } else {
    loadSheet().catch((error) => console.warn("[wiring] sheet unavailable:", error));
  }

  // Owned by this surface, so the shell stops it when the operator leaves and
  // starts it again on the way back (#360). There is no cadence: a reference
  // surface has no live reading to keep up with, and the one thing that must
  // not go stale is the sheet after a Part was moved on Parts - which is a
  // return, not a tick.
  //
  // A return is `runOnStart`, because the shell restarts a surface's polls on
  // the way in (syncSurfacePoll, data/page_bootstrap.js); `refreshOnReturn` is
  // the browser tab coming back. Both are wanted, and both would also fire at
  // mount, where the section run above is already reading - so the first start
  // is the one the section covers. Fulfilling there is correct rather than
  // convenient: a surface at mount has never been left, so there is no stale
  // mark this could wrongly take down (#360).
  let firstStart = true;
  window.PASurface?.poll(
    () => {
      if (firstStart) {
        firstStart = false;
        return Promise.resolve();
      }
      return loadSheet();
    },
    { runOnStart: true, refreshOnReturn: true }
  ).start();
})();

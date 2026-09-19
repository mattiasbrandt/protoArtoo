// =============================================================================
// data/wiring.js
//
// Wiring (CONTEXT.md "Wiring"): the destination that answers the one question
// no other screen can -- "I am holding a servo lead: which output does it go
// to, and which part will it move?" It is a reference, not a control surface:
// it writes nothing, and no act on it reaches the droid.
//
// EVERYTHING ON IT IS GENERATED. The wires come from the Board Lanes the
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
  const PROMISE = "every signal this image puts on a wire";
  const SCOPE = "signal + ground only, power wiring is up to you";

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
  //
  // `tiers` is a proposal the operator picks from (#411): the review asked for
  // "some thing simpler and concise" in place of "What this image will drive".
  const PLATES = Object.freeze({
    wires: "The wires",
    tiers: "What moves",
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
  // other two name states that are not a "no" at all: a wired row and an
  // output waiting for a lead. The ids are the tokens; what a builder reads is
  // `heading`, and no id reaches the screen.
  //
  // `heading` is the operator's words from the Wiring design review (#411),
  // which renamed #293's, and is not this file's to reword. The count rides in
  // the section head's subtitle beside it, which is where the anatomy puts a
  // count (docs/ui-copy-voice.md rule 15).
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
      heading: "Wired",
      subject: "part",
      noun: ["part", "parts"],
      footnote:
        "<b>Wired</b>: the output is marked wired, so this image puts a pulse on that lead.",
    },
    {
      id: "component-disabled",
      heading: "Output not wired",
      subject: "part",
      noun: ["part", "parts"],
      footnote:
        "<b>Output not wired</b>: the part is on an output you have not marked wired. " +
        "Nothing moves until you do.",
    },
    {
      id: "output-no-part",
      heading: "Output with no part",
      subject: "output",
      noun: ["output", "outputs"],
      footnote:
        "<b>Output with no part</b>: a spare. It gets a pulse, and nothing is recorded on " +
        "the end.",
    },
    // Last on the sheet (operator, 2026-09-19 on #411): a part nothing claims
    // is the least of what a builder holding a lead needs to read.
    {
      id: "part-not-assigned",
      heading: "Unused",
      subject: "part",
      noun: ["part", "parts"],
      footnote:
        "<b>Unused</b>: no output claims the part. Moves you author for it wait until " +
        "one does.",
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
    window.PAUi?.setupActionHtml?.(action) ?? `${esc(action)} in Configuration`;

  // Where an Output is marked wired: on this surface, under the sheet
  // (data/output_settings.js, #369). Named in words rather than linked,
  // because the saved bench copy carries this sentence too and has no page
  // under it.
  const OUTPUTS_PLATE = "Outputs";

  // Where a Part is put on an Output. Parts is the surface that owns that act,
  // and it is a destination that exists.
  const PARTS_ROUTE = '<a href="/parts.html">Parts</a>';

  // ---------------------------------------------------------------------------
  // The rows
  // ---------------------------------------------------------------------------

  // What this image could move at all, which is what bounds the promise. A
  // Part whose control path is the dome link is the Dome Controller's to move,
  // not this image's, and a Part that declares no control path (the two dome
  // orientation fixtures) is not moved by anything. Both are named and counted
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
  // So the join folds the name to lower case. Folding rather than a rename
  // table is what keeps a lane added later joining with no edit here -- which
  // is the whole point of a manifest where adding a lane is adding a row. An
  // Output does not join here at all: it joins on its address (configOutputs()).
  // ---------------------------------------------------------------------------
  const componentIndex = (components) => {
    const index = new Map();
    Object.keys(components || {}).forEach((key) => {
      const entry = components[key];
      if (entry && typeof entry === "object") index.set(key.toLowerCase(), entry);
    });
    return index;
  };

  // A signal the config carries no toggle for is not one anybody has switched
  // off, so it reads as on rather than inventing a switch for it.
  const switchedOn = (toggles, name) => {
    const entry = toggles.get(String(name || "").toLowerCase());
    return entry ? entry.enabled === true : true;
  };

  // The Board Component Label: the silkscreen text this board prints beside the
  // thing (ADR 0033). The firmware reports it under components.<id>.label
  // (include/component_labels.inc), and it is the ONLY place a board's printed
  // words come from: this file keeps no copy of any board's legend.
  const labelOf = (toggles, name) => {
    const entry = toggles.get(String(name || "").toLowerCase());
    const label = entry ? entry.label : null;
    return typeof label === "string" && label !== "" ? label : "";
  };

  // The Outputs as GET /api/config reports them: every components{} entry
  // that carries an `address`, in the firmware's order (src/web/api_config.cpp
  // CONFIG_OUTPUTS, docs/api.md). This file knows no Output of its own - which
  // exist, and what each is called, is the running firmware's answer
  // (operator, 2026-09-19 on #411) - so this is the one door an Output's
  // label and its wired flag come in by, keyed by the Output Address that
  // also names its GET /api/servo/outputs row.
  const configOutputs = (components) => {
    const byAddress = new Map();
    Object.keys(components || {}).forEach((key) => {
      const entry = components[key];
      if (entry && typeof entry.address === "string" && entry.address !== "") {
        byAddress.set(entry.address, entry);
      }
    });
    return byAddress;
  };

  // An Output as a builder reads it: what the board prints beside its pin,
  // ARM3 on the Artoo PCB and GPIO 4 on the FireBeetle 2 (CONTEXT.md "Output
  // Address"). The name GET /api/servo/outputs puts on a row is protoArtoo's
  // word, the same on every board, and never reaches the sheet. An Output the
  // board labels nothing - an expander channel - reads as its Output Address,
  // which still says where it plugs in.
  const outputLabel = (output) => output.label || output.address;

  // Each Output of the table, carrying its board label and whether it is
  // marked wired. Joined once, here, so every row, table cell and picture
  // reads the same name. A row the config reports nothing for - an expander
  // channel - has no switch anybody could have turned off, so it reads as
  // wired rather than inventing one.
  const boardOutputs = (outputs, components) => {
    const reported = configOutputs(components);
    return outputs.map((output) => {
      const entry = reported.get(output.address);
      const label = entry && typeof entry.label === "string" ? entry.label : "";
      return { ...output, label, wired: entry ? entry.enabled === true : true };
    });
  };

  // The one place a Part is joined to the Output that moves it. A Part is on at
  // most one Output (CONTEXT.md "Part"), so the first hit is the answer.
  const outputForPart = (outputs, partId) =>
    outputs.find((output) => output.parts.includes(partId)) || null;

  const wiringRows = ({ parts = [], outputs: reported = [], components = {} } = {}) => {
    const outputs = boardOutputs(reported, components);
    const partRows = parts.filter(drivableHere).map((part) => {
      const output = outputForPart(outputs, part.id);
      if (!output) {
        return {
          tier: "part-not-assigned",
          part,
          output: null,
          why:
            `No output claims ${esc(part.name)}. Put it on one in ${PARTS_ROUTE}.`,
        };
      }
      if (!output.wired) {
        return {
          tier: "component-disabled",
          part,
          output,
          why:
            `${esc(outputLabel(output))} is not marked wired, so nothing moves. ` +
            `Mark it under ${OUTPUTS_PLATE}.`,
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
          `Nothing is recorded on ${esc(outputLabel(output))}, so a lead here moves nothing. ` +
          `Claim it from ${PARTS_ROUTE}.`,
      }));

    return partRows.concat(outputRows);
  };

  // ---------------------------------------------------------------------------
  // The wires: one per Board Lane
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
  //
  // The layout is the one the operator pointed at in the Wiring design review
  // (2026-09-19, #411): the board a block on the left, and each wire leaving it
  // as its own horizontal line to a box on the right, its name over the line
  // and a dim note under it.
  // ---------------------------------------------------------------------------
  const ROW_H = 64;
  const DIAGRAM_W = 960;
  const BOARD_X = 24;
  const BOARD_W = 200;
  const BOX_X = 648;
  const BOX_W = 288;
  const BOX_H = 40;
  const TOP = 46;
  // The foot carries two lines, the promise and the scope statement.
  const FOOT_H = 48;
  const RIGHT_X = DIAGRAM_W - BOARD_X;
  // Where a wire's words start, clear of the board's edge.
  const WIRE_TEXT_X = BOARD_X + BOARD_W + 22;

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

  // ---------------------------------------------------------------------------
  // Each wire's own colour
  //
  // A wire is told apart from its neighbours the way a real loom's are: by its
  // own colour (CONTEXT.md "Status Colour", the Wiring exception, operator
  // 2026-09-19 on #411). The colour NAMES a wire and carries no state; a wire
  // to something not wired takes the one grey instead, and is dashed.
  //
  // Which colour is picked by the wire's place in one order - the Outputs as
  // GET /api/config lists them, then the Board Lanes as the identity lists
  // them, then any Output only the servo table knows (an expander's) - from
  // the numbered palette --wire-1..--wire-8 in data/style.css, round again
  // past eight. Nothing here knows which wires a board has: the order is the
  // firmware's answer, and data/output_settings.js picks an Output plate's
  // colour from the same answer the same way, so a plate and its line match.
  //
  // The colours live in the stylesheet only. This file writes a token's name
  // and never a value, painted as an inline style so it beats nothing and
  // nothing beats it; the bench copy, which has no stylesheet, has each token
  // resolved into it as it is saved (inkedForFile()).
  // ---------------------------------------------------------------------------
  const WIRE_PALETTE = 8;
  const WIRE_OFF = "var(--wire-off)";

  const wireOrder = ({ components = {}, lanes = {}, outputs = [] } = {}) => {
    const order = [...configOutputs(components).keys(), ...Object.keys(lanes).map((key) => `lane:${key}`)];
    outputs.forEach((output) => {
      if (!order.includes(output.address)) order.push(output.address);
    });
    return order;
  };

  const wireInk = (order, key) => {
    const at = order.indexOf(key);
    return `var(--wire-${((at < 0 ? order.length : at) % WIRE_PALETTE) + 1})`;
  };

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
  // The droid and the minute in the head; the bounded promise and the scope
  // statement along the foot. A picture that leaves the page -- a crop, a
  // photo of the printout, a paste into a build thread -- has lost the header
  // that said all of them, and a printed page loses its header first (#293).
  // ---------------------------------------------------------------------------
  const svgStamp = (droidName, stamp) =>
    `<text class="wd-stamp" x="${RIGHT_X}" y="23" ${SMALL} text-anchor="end">` +
    `${droidName ? `${esc(droidName)} · ` : ""}made ${esc(stampText(stamp))}</text>`;

  const svgFoot = (height) =>
    `<text class="wd-scope wd-promise" x="${BOARD_X}" y="${height - 26}" ${SMALL}>` +
    `${esc(PROMISE)}</text>` +
    `<text class="wd-scope" x="${BOARD_X}" y="${height - 12}" ${SMALL}>${esc(SCOPE)}</text>`;

  const svgOpen = (title, height) =>
    `<svg class="wd" viewBox="0 0 ${DIAGRAM_W} ${height}" role="img" ` +
    `aria-label="${escAttr(title)}" xmlns="http://www.w3.org/2000/svg" ` +
    `font-family="monospace">`;

  const svgHead = (text, { droidName, stamp }) =>
    `<text class="wd-title" x="${BOARD_X}" y="24" fill="${INK}" font-size="13" ` +
    `font-weight="600">${esc(text)}</text>` +
    svgStamp(droidName, stamp);

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

  // The words over a wire. What the board prints beside the pin comes first,
  // in the wire's own colour, and what this sheet has always said about the
  // wire follows it (operator, 2026-09-19 on #411: "the wire lines in the
  // drawing should initally say what pcb silkscreen label and then what we
  // have now"). A wire the board prints nothing beside has only the second.
  const svgWireName = (y, { silk, detail, ink }) => {
    const room = 52 - (silk ? silk.length + 3 : 0);
    const whole = String(detail ?? "");
    const shown = clip(whole, room);
    const title = shown === whole ? "" : `<title>${esc(whole)}</title>`;
    const first = silk
      ? `<tspan class="wd-silk" style="fill:${ink}" font-weight="700">${esc(silk)}</tspan>` +
        (shown ? " · " : "")
      : "";
    return (
      `<text class="wd-bus" x="${WIRE_TEXT_X}" y="${y - 9}" fill="${INK_DIM}" font-size="11">` +
      `${first}${esc(shown)}${title}</text>`
    );
  };

  // One wire: out of the board's edge to the thing on the end of it, in its
  // own colour. A wire that is not wired is grey and dashed AND says why on
  // its row, because a dashed line on its own is a convention a builder has to
  // be taught.
  //
  // The line type says what the wire carries. A Board Lane is a serial link,
  // TX and RX - two conductors and a signal both ways - so it is drawn as a
  // pair with an arrow at each end. A servo lead's signal is one conductor
  // running out to the part, so it is one line with one arrow.
  const svgLink = (index, { live, wire, pair, silk, detail, name, role, note }) => {
    const y = TOP + index * ROW_H + ROW_H / 2;
    const ink = live ? wire : WIRE_OFF;
    const from = BOARD_X + BOARD_W;
    const to = BOX_X;
    const dash = live ? "" : ' stroke-dasharray="6 4"';
    const stroke = `fill="none" style="stroke:${ink}" stroke-width="${pair ? 1.6 : 2.2}"${dash}`;
    const line = pair
      ? `<path class="wd-line" d="M${from + 10} ${y - 2.5} H ${to - 10} ` +
        `M${from + 10} ${y + 2.5} H ${to - 10}" ${stroke}/>`
      : `<path class="wd-line" d="M${from} ${y} H ${to - 10}" ${stroke}/>`;
    // An arrowhead with its tip at `tip`, pointing along `dir` (+1 right).
    const arrow = (tip, dir) =>
      `<path class="wd-arrow" d="M${tip - dir * 11} ${y - 5} L${tip} ${y} ` +
      `L${tip - dir * 11} ${y + 5} Z" style="fill:${ink}"/>`;
    return (
      `<g class="wd-link ${live ? "is-live" : "is-idle"}">` +
      line +
      arrow(to - 1, 1) +
      (pair ? arrow(from + 1, -1) : "") +
      svgWireName(y, { silk, detail, ink }) +
      svgText("wd-note", WIRE_TEXT_X, y + 18, note, 60, SMALL) +
      `<rect class="wd-box" x="${BOX_X}" y="${y - BOX_H / 2}" width="${BOX_W}" ` +
      `height="${BOX_H}" rx="2" fill="${PLATE}" ` +
      (live ? `style="stroke:${ink}"` : `stroke="${SEAM}"`) +
      ` stroke-width="1.2"/>` +
      svgText("wd-name", BOX_X + 12, y - 3, name, 36,
        `fill="${live ? INK : INK_DIM}" font-size="12" font-weight="600"`) +
      svgText("wd-role", BOX_X + 12, y + 12, role, 42, SMALL) +
      `</g>`
    );
  };

  // ---------------------------------------------------------------------------
  // The board
  //
  // The builder's own Body Controller, drawn as its product picture rather
  // than named (operator, 2026-09-19 on #411: "instead of the text 'Body
  // Controller' we should use our existing selected actual body controller
  // product image"). The picture is not this file's to draw: the generator
  // leaves a slot, and the screen caller fills it with the Component Picker's
  // own frame for the board this image runs on (fillBoardArt() below), so a
  // product card and this block cannot come to show two different boards. The
  // bench copy carries the same picture, made standalone (boardArtForFile()).
  //
  // The caption says how many wires leave the board, which is the one fact a
  // reader cannot get from the picture itself.
  // ---------------------------------------------------------------------------
  const BOARD_SLOT = '<div xmlns="http://www.w3.org/1999/xhtml" class="wd-board-slot"></div>';

  // The board is never shorter than its picture and caption need, so a sheet
  // with one or two wires is as tall as the board rather than as its wires.
  const BOARD_MIN_H = 172;
  const boardHeight = (rowCount) => Math.max(BOARD_MIN_H, rowCount * ROW_H - 16);
  const diagramHeight = (rowCount) => TOP + 8 + boardHeight(rowCount) + 8 + FOOT_H;

  const svgBoard = (rowCount, caption) => {
    const height = boardHeight(rowCount);
    const y = TOP + 8;
    const artW = BOARD_W - 24;
    const artH = Math.round((artW * 3) / 4);
    const artY = Math.round(y + Math.max(12, (height - artH - 24) / 2));
    return (
      `<g class="wd-board">` +
      `<rect class="wd-board-face" x="${BOARD_X}" y="${y}" width="${BOARD_W}" height="${height}" ` +
      `rx="2" fill="${PLATE_RAISED}" stroke="${SEAM_STRONG}" stroke-width="1.5"/>` +
      `<foreignObject class="wd-board-art" x="${BOARD_X + 12}" y="${artY}" width="${artW}" ` +
      `height="${artH}">${BOARD_SLOT}</foreignObject>` +
      `<text class="wd-board-sub" x="${BOARD_X + BOARD_W / 2}" y="${y + height - 12}" ` +
      `${SMALL} text-anchor="middle">${esc(caption)}</text>` +
      `</g>`
    );
  };

  const loomDiagramHtml = (lanes, made, order) => {
    if (lanes.length === 0) return "";
    const height = diagramHeight(lanes.length);
    const title = `The wires: ${lanes.length} Board Lane${lanes.length === 1 ? "" : "s"} this image routes`;
    return (
      svgOpen(title, height) +
      svgHead("Control signals", made) +
      svgBoard(lanes.length, plural(lanes.length, ["wire", "wires"])) +
      lanes
        .map((lane, index) =>
          svgLink(index, {
            live: lane.on,
            wire: wireInk(order, `lane:${lane.key}`),
            pair: true,
            silk: lane.label,
            detail: laneBusText(lane),
            name: lane.name,
            role: lane.on ? "serial, both ways" : "switched off",
            note: lane.on ? laneNote(lane) : "switched off - nothing rides this wire",
          })
        )
        .join("") +
      svgFoot(height) +
      `</svg>`
    );
  };

  const laneBusText = (lane) => `UART ${lane.uart} - TX ${lane.tx} / RX ${lane.rx}`;

  // A UART is crossed: this board's TX lands on the far end's RX (docs/pin_map.md,
  // "Dome Control slip ring wiring").
  const laneNote = (lane) =>
    lane.shared
      ? "shares its UART with the dome link, RX only"
      : "TX to the far end's RX, RX to its TX, and ground";

  // The wired signals: one wire per output this image is putting a pulse on,
  // out to the part it moves. Ground is named because a servo lead has three
  // wires and only two of them are this sheet's; the rail itself is described
  // in its own plate and never drawn (CONTEXT.md "Wiring").
  const signalDiagramHtml = (groups, made, order) => {
    if (groups.length === 0) return "";
    const height = diagramHeight(groups.length);
    const title = `Signal and ground for ${groups.length} wired output${groups.length === 1 ? "" : "s"}`;
    return (
      svgOpen(title, height) +
      svgHead("Signal and ground", made) +
      svgBoard(groups.length, plural(groups.length, ["wired output", "wired outputs"])) +
      groups
        .map((group, index) =>
          svgLink(index, {
            live: true,
            wire: wireInk(order, group.address),
            pair: false,
            silk: group.silk,
            detail: group.address,
            name: group.parts.join(" + "),
            role: group.component && group.component !== "none" ? group.component : "a servo",
            note: "signal on the pin, ground to the board's own ground",
          })
        )
        .join("") +
      svgFoot(height) +
      `</svg>`
    );
  };

  // One wire per Output rather than one per Part, because a ganged lead is one
  // wire and drawing it twice would put a channel number on the sheet twice --
  // the fault the reference fixed by giving each board its own box
  // (r2d2-astromech-simulator v1.79.0, src/js/app/wiring.js:423-436).
  const wiredGroups = (rows) => {
    const groups = [];
    rows
      .filter((row) => row.tier === "driven")
      .forEach((row) => {
        const address = row.output.address;
        let group = groups.find((each) => each.address === address);
        if (!group) {
          group = {
            address,
            silk: row.output.label,
            component: row.output.component,
            parts: [],
          };
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
    if (!output.label) return `<span class="wiring-address">${esc(output.address)}</span>`;
    return `${esc(output.label)} <span class="wiring-address">${esc(output.address)}</span>`;
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
    "<th>Output</th><th>Address</th><th>What is fitted</th><th>Why</th>" +
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
  // The parts this image does not move are named and counted rather than
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
        `${plural(domeLink, ["part", "parts"])} the Dome Controller moves over the dome link`
      );
    }
    if (noPath > 0) {
      clauses.push(`${plural(noPath, ["part", "parts"])} nothing on this droid moves at all`);
    }
    return (
      `<p class="hint wiring-bound">Outside this sheet: ${clauses.join(" and ")}. ` +
      `This image sends them no signal.</p>`
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
    `Below is <b>${esc(PROMISE)}</b>, read off this Body Controller. It draws ` +
    `<b>${esc(SCOPE)}</b>.`;

  // The shared rail is described and never drawn (CONTEXT.md "Wiring"), and this
  // is the one place the droid's own pacing is stated, because the reason it
  // paces itself is the rail every servo shares.
  //
  // The cadence carries its provenance in the line after it, and that is not
  // padding. CONTEXT.md "Cadence Floor" puts "the ~450 ms cadence (that figure
  // is the dome's)" in its _Avoid_ list, and include/sequence_bulk_centre.h
  // says why both are true: 450 ms is the DOME's measured figure, adopted
  // deliberately and explicitly as the body's stand-in until the body's own is
  // taken (#355), with "do not quietly let it become one". Stating it attributed
  // is the opposite of adopting it quietly.
  const railHtml = () =>
    `<p class="hint">Every servo shares one supply, and too many starting at once sag it. ` +
    `So the droid starts its own moves, like centring every output, <b>${esc(CADENCE)}</b> ` +
    `apart.</p>` +
    `<p class="hint">That figure is the dome's, from its seven ring servos. Nobody has ` +
    `measured the body's yet.</p>` +
    `<div class="note note-info"><b>Fusing and power are your build's; nothing here draws ` +
    `them.</b> Size the rail for stall current: a servo fighting a linkage pulls several ` +
    `times its idle draw.</div>`;

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
        "<li><b>Design name</b> is the part's name in the files you printed it from, the " +
          "same one your slicer shows. Label the wire with it.</li>"
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
    const droidName = typeof model.droidName === "string" ? model.droidName : "";
    const stamp = typeof model.stamp === "string" ? model.stamp : sheetStamp();
    const made = { droidName, stamp };
    const rows = wiringRows(model);
    const lanes = loomRows(model);
    const order = wireOrder(model);

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

    const wired = wiredGroups(rows);
    const hasDesignNames = rows.some(
      (row) => row.part && typeof row.part.cadName === "string" && row.part.cadName !== ""
    );

    const wiredCount = rows.filter((row) => row.tier === "driven").length;
    const summary =
      `${plural(rows.length, ["row", "rows"])} · ${wiredCount} wired · ` +
      `${rows.length - wiredCount} not`;

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
      droidName,
      stamp,
      fileName: sheetFileName(droidName, stamp),
      rows,
      lanes,
      tiers: present,
      summary,
      loomSummary,
      loomHtml: lanes.length
        ? loomTableHtml(lanes) + loomDiagramHtml(lanes, made, order)
        : '<p class="hint">This image reports no Board Lane. No wires to draw.</p>',
      // Every tier that is present becomes a section; a tier that is not simply
      // is not here. The wired picture rides inside the Wired section, where
      // the rows it draws are.
      tiersHtml:
        present
          .map(
            (section) =>
              `<section class="wiring-tier" data-tier="${section.id}">` +
              `<div class="sect"><h3>${esc(section.heading)}</h3>` +
              `<span class="sub">${section.countText}</span></div>` +
              section.tableHtml +
              (section.id === "driven" ? signalDiagramHtml(wired, made, order) : "") +
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
  //
  // `boardArt` is the one thing the file is handed besides the sheet: the
  // board's picture, already made standalone by the caller (boardArtForFile()),
  // put into every board slot the generator left. Without it the slots stay
  // empty and the board is a plain block, which is still a true sheet.
  // ---------------------------------------------------------------------------
  const wiringSheetFile = (sheet, origin = "", boardArt = "") => {
    const madeAt = stampText(sheet.stamp);
    const about = sheet.droidName ? `${sheet.droidName} - ${madeAt}` : madeAt;
    const pictured = (html) =>
      boardArt ? html.split(BOARD_SLOT).join(BOARD_SLOT.replace("></div>", `>${boardArt}</div>`)) : html;
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
      `<h2>${esc(sheet.plates.wires)}</h2><p>${sheet.loomSummary}</p>${pictured(sheet.loomHtml)}` +
      `<h2>${esc(sheet.plates.tiers)}</h2><p>${sheet.summary}</p>` +
      `${pictured(sheet.tiersHtml)}${sheet.footnoteHtml}` +
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
    droidName: typeof identity?.droidName === "string" ? identity.droidName : "",
  });

  // The pieces that do not wait for the droid: the two sentences and the
  // plate headings. They go up at mount rather than on the first answer,
  // because the promise is what tells a builder whether to read the rest at
  // all, and a header that is blank until a fetch lands has nothing to say in
  // exactly the moment it matters.
  write("wiring-promise", promiseHtml());
  write("wiring-rail", railHtml());
  write("wiring-loom-heading", esc(PLATES.wires));
  write("wiring-tiers-heading", esc(PLATES.tiers));
  write("wiring-rail-heading", esc(PLATES.rail));

  // ---------------------------------------------------------------------------
  // The board's picture
  //
  // Found the way the Component Picker finds it, with its own lookup and its
  // own frame (data/component_picker.js artIdFor, data/product_art.js), and no
  // board-to-picture map of this surface's. The board's GPIO outputs are a
  // product of their own, "Body controller board GPIO", and #369 settled that
  // it is pictured by whichever Body Controller this image runs on - which is
  // exactly the board this sheet draws. Until the lineup has answered there is
  // no board to name, and the frame stays empty rather than guessing one.
  // ---------------------------------------------------------------------------
  const BOARD_GPIO_PRODUCT = "esp32_gpio_ledc";
  const boardArtId = () => window.ComponentPicker?.artIdFor?.(BOARD_GPIO_PRODUCT) || null;

  const fillBoardArt = () => {
    const frame = window.PAProductArt?.frame;
    if (!frame) return;
    const id = boardArtId();
    document.querySelectorAll(".wd-board-slot").forEach((slot) => slot.replaceChildren(frame(id)));
  };

  // The same picture for the bench copy, which fetches nothing when it opens:
  // the drawing's own symbol copied in where the page has one, else the
  // photograph the frame is showing, painted into the file as data. A
  // photograph that has not arrived yet leaves the file's board plain.
  const PICTURE_BOX = 'viewBox="0 0 400 300" width="100%" height="100%" xmlns="http://www.w3.org/2000/svg"';

  const boardArtForFile = () => {
    const id = boardArtId();
    if (!id) return "";
    const symbol = document.getElementById(`art-${id}`);
    if (symbol) return `<svg ${PICTURE_BOX}>${symbol.innerHTML}</svg>`;
    const photo = document.querySelector(".wd-board-slot img");
    if (!photo || !photo.complete || !photo.naturalWidth) return "";
    const scale = Math.min(400 / photo.naturalWidth, 300 / photo.naturalHeight, 1);
    const canvas = document.createElement("canvas");
    canvas.width = Math.round(photo.naturalWidth * scale);
    canvas.height = Math.round(photo.naturalHeight * scale);
    canvas.getContext("2d").drawImage(photo, 0, 0, canvas.width, canvas.height);
    return (
      `<svg ${PICTURE_BOX}><image href="${canvas.toDataURL("image/png")}" x="0" y="0" ` +
      `width="400" height="300" preserveAspectRatio="xMidYMid meet"/></svg>`
    );
  };

  // The wire colours as the bench copy needs them. The file carries no
  // stylesheet, so each --wire-* token the pictures name is resolved to the
  // value the stylesheet gives it right now, and the file is inked with that.
  // The stylesheet stays the one place a wire colour is written down.
  const inkedForFile = (file) => {
    const style = window.getComputedStyle?.(document.documentElement);
    if (!style) return file;
    return file.replace(/var\((--wire-(?:\d+|off))\)/g, (whole, token) => style.getPropertyValue(token).trim() || whole);
  };

  // The lineup can answer after the sheet is up; the board's picture follows it.
  window.ComponentPicker?.onChange?.(() => {
    if (answered) fillBoardArt();
  });

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
    fillBoardArt();
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
      const file = inkedForFile(wiringSheetFile(sheet, window.location?.origin || "", boardArtForFile()));
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
  // would draw a sheet with every part under "Unused" and no outputs
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
      label: "the wiring sheet",
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

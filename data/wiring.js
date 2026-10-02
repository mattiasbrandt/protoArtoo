// =============================================================================
// data/wiring.js
//
// Wiring (CONTEXT.md "Wiring"): the destination that answers the one question
// no other screen can -- "I am holding a servo wire: which output does it go
// to, and which part will it move?" The sheet is a reference, not a control
// surface: it writes nothing, and no act on it reaches the droid. What writes
// is the parts wiring table, mounted by the screen caller below: it puts a
// Part on an Output and says what is on its wire, with its move question
// (data/parts_mapping.js picker(); operator, 2026-09-28 and 2026-09-29 on
// #411). The sheet carries that same table as plain text, and none of its
// controls (operator, 2026-10-01 on #463).
//
// EVERYTHING ON IT IS GENERATED. The wires come from the Board Lanes the
// running firmware reports (GET /api/identity), the Outputs from
// data/outputs.js - this droid's Servo Output table joined to its config by
// Output Address (#415) - with the Droid Parts Catalog naming what is on each,
// and the lanes' switches from the Component Toggles (GET /api/config).
// Nothing here keeps a copy of one board's pin numbers, which is the defect a
// Board Lane exists to close (include/board_lanes.inc).
// The product wiring cards are generated too, at build time: the wiring cards
// the spec sheets carry, where the image holds them (#458). On the screen
// each opens on its product's row of the table; on paper they follow Power
// wiring.
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
// COLOR AND SIZE ARE THE STYLESHEET'S, AND THE PICTURES CARRY PAPER BENEATH IT.
// Every element this file emits carries a class, so on screen the tokens in
// data/style.css decide what it looks like -- and @media print re-points those
// tokens at paper (data/style.css ":root" and its print block). The saved bench
// copy ships with no stylesheet at all, though, and a picture is the part of it
// that gets cropped out and pasted somewhere else, so each picture also carries
// its own paint as SVG presentation attributes. Those are the lowest-priority
// paint there is -- any stylesheet rule beats them -- so on screen they change
// nothing, and in the saved file they are all there is. Every color among them
// is a `var(--token,#fallback)` pair naming the token the stylesheet paints the
// same element with, and the fallback is the paper value @media print gives
// that token: the saved file and a printed screen are the same ink on the same
// paper. This is the form the project this view learns from shipped for the
// same reason (r2d2-astromech-simulator v1.79.0, src/js/app/wiring.js:50), and
// the one color literal a standalone export is allowed (#366).
// =============================================================================
(() => {
  "use strict";

  // ---------------------------------------------------------------------------
  // The three sentences this view is not free to reword
  //
  // The bounded promise and the scope statement are the whole reason a
  // generated sheet is safe to take to a bench: they say what the sheet is
  // about before a builder reads a single row. They ride along the foot of the
  // picture, because a picture gets cropped and a screenshot travels (#293).
  //
  // The page's own subtitle is SUBTITLE: one plain line saying what the page
  // covers (operator, 2026-09-19 on #411: the old two sentences were "abit too
  // poetic and descriptive"). Power has its own section, so it is not here.
  // ---------------------------------------------------------------------------
  const SUBTITLE = "Where each wire goes on this board.";
  const PROMISE = "every signal this image puts on a wire";
  const SCOPE = "signal + ground only, power wiring is up to you";

  // The pace the droid holds between outputs it starts itself is the droid's
  // to say: GET /api/config reports it (`servo.cadenceFloorMs`) and whose
  // figure it is (`servo.cadenceFloorSource`), and cadenceOf() below reads both
  // (#453). No number is typed here.

  // The plate headings. They are the generator's for the same reason
  // the promise is: the screen writes them into its plates and the saved file
  // prints them, so a heading reworded here is reworded in both, and a heading
  // typed into data/wiring.html as well would be the second copy this whole
  // view exists to abolish.
  //
  // The operator's words from the Wiring design review (#411): "The wires"
  // for the loom, "Power wiring" for the shared rail. The section the sheet
  // used to call "What this image will drive" is gone (operator, 2026-09-19),
  // and its last list, the Parts no Output claims, went to Parts (operator,
  // 2026-09-28: it "makes more sense to have in the parts page").
  const PLATES = Object.freeze({
    wires: "The wires",
    parts: "Parts on outputs",
    rail: "Power wiring",
    // Only an image that carries the product wiring cards has this plate.
    products: "Product wiring",
  });

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
  // Output does not join here at all: data/outputs.js joins it on its address.
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

  // ---------------------------------------------------------------------------
  // The Board Lanes: the serial links, one wire each
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
  // These are the only color literals this surface ships, and they appear
  // nowhere but inside a var() pair (#366; D2's checker, #353, accepts exactly
  // this form and nothing looser).
  // ---------------------------------------------------------------------------
  const INK = "var(--text,#111111)";
  const INK_DIM = "var(--text-dim,#111111)";
  const INK_FAINT = "var(--text-faint,#111111)";
  const SEAM_STRONG = "var(--border-strong,#999999)";
  const PLATE = "var(--surface,#ffffff)";
  const PLATE_RAISED = "var(--surface-alt,#ffffff)";

  // ---------------------------------------------------------------------------
  // Each wire's own color
  //
  // A wire is told apart from its neighbours the way a real loom's are: by its
  // own color (CONTEXT.md "Status Color", the Wiring exception, operator
  // 2026-09-19 on #411). The color NAMES a wire and carries no state. Only the
  // wires a builder has run are drawn at all (sheetWires()).
  //
  // Which color is picked by the wire's place in one order - EVERY Output in
  // data/outputs.js's order, drawn or not, then EVERY Board Lane as the
  // identity lists them - from the numbered palette --wire-1..--wire-8 in
  // data/style.css (WIRE_INKS), round again past the last. Nothing here knows
  // which wires a board has: the order is the firmware's answer. A wire left
  // off the drawing still holds its place, so a wire keeps its color as others
  // come and go: putting a Part on ARM2 must not recolor ARM3.
  //
  // The colors live in the stylesheet only. This file writes a token's name
  // and never a value, painted as an inline style so it beats nothing and
  // nothing beats it; the bench copy, which has no stylesheet, has each token
  // resolved into it as it is saved (inkedForFile()). Each token is named
  // here in full, one by one: this file is the palette's only reader, and a
  // name put together from a number is one nothing can find by searching for
  // it (test/test_web/test_style_token_layer.js, "no token in :root is
  // orphaned").
  // ---------------------------------------------------------------------------
  const WIRE_INKS = Object.freeze([
    "var(--wire-1)",
    "var(--wire-2)",
    "var(--wire-3)",
    "var(--wire-4)",
    "var(--wire-5)",
    "var(--wire-6)",
    "var(--wire-7)",
    "var(--wire-8)",
  ]);

  const wireOrder = ({ lanes = {}, outputs = [] } = {}) => [
    ...outputs.map((output) => output.address),
    ...Object.keys(lanes).map((key) => `lane:${key}`),
  ];

  // The ink a wire takes, by its place in the order.
  const wireInk = (order, key) => {
    const at = order.indexOf(key);
    return WIRE_INKS[(at < 0 ? order.length : at) % WIRE_INKS.length];
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
  // Four parts ganged to one wire is what reaches this -- rare, and exactly the
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
  // in the wire's own color, and what this sheet has always said about the
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
  // own color.
  //
  // The line type says what the wire carries. A Board Lane is a serial link,
  // TX and RX - two conductors and a signal both ways - so it is drawn as a
  // pair with an arrow at each end. A servo wire's signal is one conductor
  // running out to the part, so it is one line with one arrow.
  const svgLink = (index, { key, ink, pair, silk, detail, name, role, note }) => {
    const y = TOP + index * ROW_H + ROW_H / 2;
    const from = BOARD_X + BOARD_W;
    const to = BOX_X;
    const stroke = `fill="none" style="stroke:${ink}" stroke-width="${pair ? 1.6 : 2.2}"`;
    const line = pair
      ? `<path class="wd-line" d="M${from + 10} ${y - 2.5} H ${to - 10} ` +
        `M${from + 10} ${y + 2.5} H ${to - 10}" ${stroke}/>`
      : `<path class="wd-line" d="M${from} ${y} H ${to - 10}" ${stroke}/>`;
    // An arrowhead with its tip at `tip`, pointing along `dir` (+1 right).
    const arrow = (tip, dir) =>
      `<path class="wd-arrow" d="M${tip - dir * 11} ${y - 5} L${tip} ${y} ` +
      `L${tip - dir * 11} ${y + 5} Z" style="fill:${ink}"/>`;
    return (
      `<g class="wd-link" data-wire="${escAttr(key)}">` +
      line +
      arrow(to - 1, 1) +
      (pair ? arrow(from + 1, -1) : "") +
      svgWireName(y, { silk, detail, ink }) +
      svgText("wd-note", WIRE_TEXT_X, y + 18, note, 60, SMALL) +
      `<rect class="wd-box" x="${BOX_X}" y="${y - BOX_H / 2}" width="${BOX_W}" ` +
      `height="${BOX_H}" rx="2" fill="${PLATE}" style="stroke:${ink}" stroke-width="1.2"/>` +
      svgText("wd-name", BOX_X + 12, y - 3, name, 36, `fill="${INK}" font-size="12" font-weight="600"`) +
      svgText("wd-role", BOX_X + 12, y + 12, role, 42, SMALL) +
      svgTick(y) +
      `</g>`
    );
  };

  // One empty box per wire, in the margin past the thing it runs to, for the
  // builder at the bench to tick once that wire is run (operator, 2026-09-29
  // on #411, "Ship it."). It is the printed sheet's and not the screen's: the
  // stylesheet hides it on screen and shows it under @media print, and the
  // saved bench copy, which has no stylesheet, always shows it - that copy is
  // the one that goes to the bench. Its paper is its own presentation
  // attributes, as every picture's is ("The paper beneath the stylesheet").
  const TICK = 14;
  const svgTick = (y) =>
    `<rect class="wd-tick" x="${RIGHT_X + 4}" y="${y - TICK / 2}" width="${TICK}" height="${TICK}" ` +
    `rx="1" fill="${PLATE}" stroke="${INK}" stroke-width="1.2"/>`;

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

  // ---------------------------------------------------------------------------
  // Every wire, in one picture
  //
  // One board and every wire that leaves it, as the operator's reference draws
  // a loom (2026-09-19 on #411: "One diagram, every wire"): the firmware's
  // Outputs in data/outputs.js's order, then its Board Lanes - the same order
  // the colors are picked in (wireOrder()). What a lane table used to
  // say beside the picture - which UART, which TX and RX pin - is on the
  // lane's own wire now, after the board's label.
  //
  // Which part an Output moves is on its box, because that is what is on the
  // end of the wire. Which Output a Part is on is set in the parts wiring
  // table under the drawing, and never on the drawing.
  // ---------------------------------------------------------------------------
  const partNames = (parts, ids) => {
    const byId = new Map(parts.map((part) => [part.id, part.name]));
    return ids.map((id) => byId.get(id) || id);
  };

  // What an Output carries, as the droid reported it. What is on a wire is one
  // answer in two vocabularies (ADR 0067): a Light Type where it lights
  // something, a servo's model where it drives a servo. The stored token is the
  // same field either way, and data/outputs.js names it (#413).
  const outputRole = (output) => {
    if (output.light) return `the ${output.light.label}`;
    return output.component && output.component !== "none" ? output.component : "a servo";
  };

  // An Output's wire is named first by what the board prints beside its pin,
  // ARM3 on the Artoo PCB and GPIO 4 on the FireBeetle 2 (CONTEXT.md "Output
  // Address"), then by its address. Only an Output with a Part on it gets one
  // (sheetWires()), so the box names what is on the end.
  const outputWire = (output, { parts, order }) => ({
    key: output.address,
    ink: wireInk(order, output.address),
    pair: false,
    silk: output.label,
    detail: output.address,
    name: partNames(parts, output.parts).join(" + "),
    role: outputRole(output),
    note: "signal on the pin, ground to the board's own ground",
  });

  // Where a Board Lane is routed, as the firmware reports it: on its wire in
  // the drawing, and beside its product's wiring card.
  const laneDetail = (lane) => `UART ${lane.uart} - TX ${lane.tx} / RX ${lane.rx}`;

  const laneWire = (lane, order) => ({
    key: lane.key,
    ink: wireInk(order, `lane:${lane.key}`),
    pair: true,
    silk: lane.label,
    detail: laneDetail(lane),
    name: lane.name,
    role: "serial, both ways",
    note: laneNote(lane),
  });

  // A UART is crossed: this board's TX lands on the far end's RX (docs/pin_map.md,
  // "Dome Control slip ring wiring").
  const LANE_SHARED = "shares its UART with the dome link, RX only";
  const laneNote = (lane) => (lane.shared ? LANE_SHARED : "TX to the far end's RX, RX to its TX, and ground");

  // Only the wires a builder has run are drawn (operator, 2026-09-29 on #411:
  // "the drawing should only draw the actaul lines (wires) currently
  // assigned/wired in"). An Output is wired when a Part is on it and free when
  // none is (CONTEXT.md "Wiring"), whatever drives it - a board pin or an
  // expander's channel alike. A serial link is wired while its component is
  // switched on in Configuration; one Not fitted or switched off rides no wire
  // (operator, 2026-09-29: "foot drive is now set to "not fitted" so why is
  // then wiring drawing still listing it as wired?"). The printed sheet, from
  // this same list, leaves both out too.
  const hasPart = (output) => output.parts.length > 0;

  const sheetWires = (model = {}) => {
    const { parts = [], outputs = [] } = model;
    const order = wireOrder(model);
    return [
      ...outputs.filter(hasPart).map((output) => outputWire(output, { parts, order })),
      ...loomRows(model).filter((lane) => lane.on).map((lane) => laneWire(lane, order)),
    ];
  };

  // An Output with no Part on it, in the one word for it (CONTEXT.md "Wiring"),
  // which is the parts wiring table's own (data/parts_mapping.js). The word is
  // kept here too for a page where that script did not load: the drawing and
  // its count stand without the table, and say so in the same word.
  const FREE = window.PAParts?.FREE ?? "free";

  // Titled with the board it draws, by the product name the lineup gives it
  // (operator, 2026-09-19 on #411: "it would make more sense for it to name and
  // title the actual board name"). Until the lineup has answered there is no
  // name to give, and the title waits rather than guessing one.
  const wiresDiagramHtml = (wires, made, boardName) => {
    if (wires.length === 0) return "";
    const height = diagramHeight(wires.length);
    const title = `The wires: ${plural(wires.length, ["wire", "wires"])} leaving ${boardName || "the Body Controller"}`;
    return (
      svgOpen(title, height) +
      svgHead(boardName, made) +
      svgBoard(wires.length, plural(wires.length, ["wire", "wires"])) +
      wires.map((wire, index) => svgLink(index, wire)).join("") +
      svgFoot(height) +
      `</svg>`
    );
  };

  // ---------------------------------------------------------------------------
  // The subtitle and the rail line
  //
  // Both are generated, and that is the point: each has exactly one home, so
  // the screen and the bench copy cannot come to say different things. Putting
  // either in data/wiring.html as well would be the second copy this whole
  // view exists to abolish. The subtitle depends on nothing the droid answers;
  // the rail line takes its figure from the droid, and stands without one
  // until the droid has answered.
  // ---------------------------------------------------------------------------
  const promiseHtml = () => esc(SUBTITLE);

  // The shared rail is described and never drawn (CONTEXT.md "Wiring"), and this
  // is the one place the droid's own pacing is stated, because the reason it
  // paces itself is the rail every servo shares.
  //
  // The cadence carries its provenance in the line after it, and that is not
  // padding. CONTEXT.md "Cadence Floor" puts "the ~450 ms cadence (that figure
  // is the dome's)" in its _Avoid_ list, and include/sequence_bulk_centre.h
  // says why both are true: the default is the DOME's measured figure, adopted
  // deliberately and explicitly as the body's stand-in until the body's own is
  // taken (#355), with "do not quietly let it become one". Stating it attributed
  // is the opposite of adopting it quietly.
  //
  // The figure is the droid's stored one, and so is whose it is. One a builder
  // set is not called the dome's and is stated exactly; the "~" belongs to the
  // dome's measured figure. Either way the body's is still unmeasured, which
  // the line goes on saying. Until the droid has answered
  // there is no figure to state, and none is invented.
  const cadenceOf = (config) => {
    const servo = config && typeof config.servo === "object" && config.servo ? config.servo : {};
    const ms = Number(servo.cadenceFloorMs);
    return Number.isFinite(ms) && ms > 0 ? { ms, dome: servo.cadenceFloorSource === "dome" } : null;
  };

  const UNMEASURED = "Nobody has measured the body's yet.";

  const railHtml = (cadence = null) =>
    `<p class="hint">Every servo shares one supply, and too many starting at once sag it. ` +
    `So the droid starts its own moves, like centring every output, ` +
    (cadence
      ? `<b>${esc(`${cadence.dome ? "~" : ""}${cadence.ms} ms (one servo at a time)`)}</b> apart.</p>` +
        `<p class="hint">${
          cadence.dome ? "That figure is the dome's, from its seven ring servos." : "That figure was set on this droid."
        } ${UNMEASURED}</p>`
      : `one servo at a time.</p>`) +
    `<div class="note note-info"><b>Power wiring is up to you; nothing here draws it.</b> ` +
    `Size the rail for stall current: a servo fighting a linkage pulls several times its ` +
    `idle draw.</div>`;

  // ---------------------------------------------------------------------------
  // Product wiring: how to wire and power each product on the droid (#458)
  //
  // One card for each fitted product that has one: on the screen it opens on
  // the row of the parts wiring table its product is on, and on paper every
  // one follows Power wiring (operator, 2026-10-01 on #463). A card's facts -
  // supply, draw, logic level, each wire, the hazards - are the wiring card
  // its spec sheet carries, generated into the image
  // (tools/generate_wiring_cards.py) and handed over in the model as `cards`,
  // keyed by Component Registry id.
  // Nothing here restates one. An image built without them hands over none,
  // and then no row offers a card and the paper has no section for them: no
  // heading and no placeholder (ADR 0065, amended 2026-09-30).
  //
  // WHICH PRODUCTS are fitted is the Component Picker's answer, read by the
  // screen caller (fittedProducts() below) and handed over as `products`.
  //
  // THE PINS BESIDE A CARD ARE THE RUNNING BOARD'S, never card text: a card is
  // per product and pins are per board. They are the same answers the drawing
  // is made from - the product's Board Lane with the label the board prints
  // for it, or the Outputs it answers for - so a card and the wire above it
  // cannot name two different pins. A product no lane and no Output reports
  // (the dome ESC, the RC receiver: UNSEEN below) gets no pins line.
  //
  // The markup is plain: a heading, a definition list, two lists. The saved
  // bench copy has no stylesheet, and this is what reads on paper without one.
  // ---------------------------------------------------------------------------
  const productPins = (product, model) => {
    const lane = product.lane ? loomRows(model).find((each) => each.key === product.lane) : null;
    // A lane that shares its UART says so here in the drawing's own words,
    // so the pins beside a card never read as a link of the product's own.
    if (lane) return [lane.label, laneDetail(lane), lane.shared ? LANE_SHARED : ""].filter(Boolean);
    if (!product.outputs) return [];
    // An Output Address opens with the protocol that reaches it (`ledc:3`),
    // which is the protocol the product's registry row declares.
    return (model.outputs || [])
      .filter((output) => output.address.startsWith(`${product.outputs}:`))
      .map((output) => output.name);
  };

  const cardFact = (term, html) => `<dt>${esc(term)}</dt><dd>${html}</dd>`;

  const productCardHtml = (product, card, model) => {
    const pins = productPins(product, model);
    const wires = card.wires.map((wire) =>
      `<li><b>${esc(wire.from)}</b> to ${esc(wire.to)}` +
      (wire.note ? ` <span class="wcard-note">- ${esc(wire.note)}</span>` : "") +
      `</li>`).join("");
    const hazards = card.hazards.map((hazard) => `<li>${esc(hazard)}</li>`).join("");
    return (
      `<section class="wcard" data-product="${escAttr(product.id)}">` +
      `<h3 class="wcard-name">${esc(product.name)}</h3>` +
      `<dl class="wcard-facts">` +
      (pins.length ? cardFact("Pins", `<span class="wcard-pins">${pins.map(esc).join(" · ")}</span>`) : "") +
      cardFact("Supply", esc(card.supply)) +
      cardFact("Draw", esc(card.draw)) +
      cardFact("Logic", esc(card.logic)) +
      (wires ? cardFact("Wires", `<ul>${wires}</ul>`) : "") +
      (hazards ? cardFact("Hazards", `<ul class="wcard-hazards">${hazards}</ul>`) : "") +
      `</dl></section>`
    );
  };

  // The fitted products this image carries a card for, in the order given.
  const cardedProducts = ({ cards, products = [] } = {}) =>
    products.filter((product) => cards && Object.hasOwn(cards, product.id));

  // ---------------------------------------------------------------------------
  // The parts wiring table, as text
  //
  // Wiring has one table of what is on which wire (operator, 2026-10-01 on
  // #463), and the sheet carries it: every Part on an Output with what is on
  // its wire, the free Outputs in one row, and every link, fitted or not.
  // The screen's copy of it is the one a builder sets the wiring in
  // (data/parts_mapping.js picker()); this is that table with nothing to
  // press, for paper. Both read the same Outputs and the same links
  // (linkRows()), count with the same function and are headed with the same
  // words (PAParts.TABLE), so the page and the paper cannot come to list a
  // wire differently.
  //
  // A box to tick stands before every wire a builder has to run, as one
  // stands beside every wire of the drawing (svgTick()).
  // ---------------------------------------------------------------------------
  const NOT_FITTED = "not fitted";

  // The family whose product is the board itself (Component Registry id).
  const BOARD_FAMILY = "body_controller";

  // What is on an Output's wire, by its own name: the Light Type's or the
  // servo model's, as the table's row offers them.
  const onTheWire = (output) => (output.light ? output.light.label : output.servo ? output.servo.label : "a servo");

  // The links as the table lists them, read only: every Board Lane the
  // firmware reports, by what the board prints for it, with where it is
  // routed while its component is fitted. `product` is the fitted product on
  // the link where this image carries its wiring card (#458), and `route` is
  // where the link is switched.
  //
  // Then a row for each fitted product with a card that no row above carries:
  // the dome's ESC, the radio and its receiver, which no Board Lane reports
  // (UNSEEN below). A card opens on the row of the product it belongs to, so
  // without a row these three had theirs on paper only (operator, 2026-10-01
  // on #463: they "get read-only rows in the links group ... so every fitted
  // product's card opens on screen as well as on paper"). Such a row says
  // what the droid can say: the product, and what the board prints for it
  // where the config carries a label. Where it is routed nothing reports, so
  // that cell is empty rather than guessed. The board's own card and the
  // card of the Outputs on it are the board group's (boardCards()).
  const ownsBoardCard = (product) => product.family === BOARD_FAMILY || Boolean(product.outputs);

  const linkRows = (model = {}) => {
    const carded = cardedProducts(model);
    const lanes = loomRows(model);
    const toggles = componentIndex(model.components);
    const route = { href: MOVES.configuration.href, label: "Configuration" };
    return [
      ...lanes.map((lane) => {
        const product = lane.on ? carded.find((each) => each.lane === lane.key) : null;
        return {
          key: lane.key,
          name: lane.name,
          output: lane.label || `UART ${lane.uart}`,
          note: lane.on && lane.shared ? LANE_SHARED : "",
          wire: lane.on ? `serial · ${laneDetail(lane)}` : NOT_FITTED,
          fitted: lane.on,
          product: product ? { id: product.id, name: product.name } : null,
          route,
        };
      }),
      ...carded
        .filter((product) => !ownsBoardCard(product) && !lanes.some((lane) => lane.on && lane.key === product.lane))
        .map((product) => ({
          key: `product:${product.id}`,
          name: product.title || product.name,
          output: labelOf(toggles, product.lane),
          note: "",
          wire: "",
          fitted: true,
          product: { id: product.id, name: product.name },
          route,
        })),
    ];
  };

  const TICK_BOX =
    `<svg class="sheet-tick" width="12" height="12" viewBox="0 0 12 12" aria-hidden="true">` +
    `<rect x="0.6" y="0.6" width="10.8" height="10.8" fill="${PLATE}" stroke="${INK}" stroke-width="1.2"/></svg>`;

  const sheetRow = (cells, tag = "td") => `<tr>${cells.map((cell) => `<${tag}>${cell}</${tag}>`).join("")}</tr>`;

  const partsSheetHtml = (model = {}) => {
    const { parts = [], outputs = [] } = model;
    const words = window.PAParts.TABLE;
    const group = (label) => `<tr><th colspan="${words.columns.length + 1}">${esc(label)}</th></tr>`;
    // In the table's own order: the catalog's, then any Part this page has
    // no name for, by its id, so a wire with a Part on it is never left off.
    const place = new Map(parts.map((part, index) => [part.id, index]));
    const wired = outputs
      .flatMap((output) => output.parts.map((id) => ({ id, output })))
      .sort((a, b) => (place.get(a.id) ?? parts.length) - (place.get(b.id) ?? parts.length));
    const free = outputs.filter((output) => !hasPart(output)).map((output) => output.name);
    const links = linkRows(model);
    return (
      `<table class="sheet-table"><thead>${sheetRow(["", ...words.columns.map(esc)], "th")}</thead><tbody>` +
      group(words.board) +
      wired.map(({ id, output }) =>
        sheetRow([TICK_BOX, esc(partNames(parts, [id])[0]), `<b>${esc(output.name)}</b>`, esc(onTheWire(output))])).join("") +
      sheetRow(["", `<i>${esc(FREE)}</i>`, `<b>${esc(free.join(", ") || "none")}</b>`, ""]) +
      (links.length
        ? group(words.links) +
          links.map((link) =>
            sheetRow([link.fitted ? TICK_BOX : "", esc(link.name), `<b>${esc(link.output)}</b>`, esc(link.wire)])).join("")
        : "") +
      `</tbody></table>`
    );
  };

  // ---------------------------------------------------------------------------
  // wiringDocument()
  // The whole sheet, as markup, from one read of the droid. Pure: the minute it
  // is stamped with arrives in the model as a sheetStamp() string, so the
  // caller that names a file after that minute holds the very string the
  // pictures print.
  // ---------------------------------------------------------------------------
  const wiringDocument = (model = {}) => {
    const droidName = typeof model.droidName === "string" ? model.droidName : "";
    const boardName = typeof model.boardName === "string" ? model.boardName : "";
    const stamp = typeof model.stamp === "string" ? model.stamp : sheetStamp();
    const made = { droidName, stamp };
    const wires = sheetWires(model);
    // The count is the lines drawn, and the Outputs left free beside it.
    const free = (model.outputs || []).filter((output) => !hasPart(output)).length;
    const carded = cardedProducts(model);

    return {
      promise: PROMISE,
      scope: SCOPE,
      promiseHtml: promiseHtml(),
      railHtml: railHtml(model.cadence || null),
      plates: PLATES,
      droidName,
      stamp,
      // Whose sheet and from when, under the title: a printed page loses the
      // browser's own header first.
      madeHtml: `${droidName ? `${esc(droidName)} - ` : ""}made ${esc(stampText(stamp))}`,
      fileName: sheetFileName(droidName, stamp),
      wires,
      wiresSummary:
        `${plural(wires.length, ["wire", "wires"])}` +
        (free ? ` · ${plural(free, ["output", "outputs"])} ${FREE}` : ""),
      wiresHtml: wires.length ? wiresDiagramHtml(wires, made, boardName) : `<p class="hint">Nothing is wired yet.</p>`,
      // Empty where the table's own script did not load: the sheet then has
      // the wires and their power, and no table section, rather than nothing.
      partsSummary: window.PAParts ? window.PAParts.wiredSummary(model.outputs || []) : "",
      partsHtml: window.PAParts ? partsSheetHtml(model) : "",
      // Empty when no fitted product has a card, which is every droid on an
      // image built without them: both callers then leave the section out.
      productsSummary: plural(carded.length, ["product", "products"]),
      productsHtml: carded.map((product) => productCardHtml(product, model.cards[product.id], model)).join(""),
    };
  };

  // ---------------------------------------------------------------------------
  // wiringSheetFile()
  // The bench copy: what wiringDocument() made, in a file that stands alone.
  //
  // It is a WRAPPER and nothing more. Every heading, wire, count,
  // picture and sentence in it is a string the generator returned; this adds
  // only the frame a file needs to be a page.
  //
  // STANDALONE, WHICH IS FOUR DECISIONS. No stylesheet, no <script> and no
  // <img>: it fetches nothing when it is opened, so it opens on a laptop at the
  // bench with no droid in reach. No <style> either: the pictures carry their
  // own paper (see "The paper beneath the stylesheet"), and the tables and
  // prose are plain HTML a browser prints legibly on its own. The one <link> is
  // an icon that is an empty data: URL, because without one a browser goes
  // looking for a favicon beside the file. It carries no link back to the
  // droid: the Unused list's links to Parts were the last, and that list is
  // Parts' own now (#411).
  //
  // WHAT IT LEAVES OUT: every control. The parts wiring table is in it as
  // text (partsSheetHtml()), and the controls a builder sets the wiring with
  // are the screen's alone. So is the list of what does not line up, whose
  // every value is live (#454). The bench copy is the wires, the table of
  // them and their power, and it ends with the product wiring cards where the
  // image carries them (#458).
  //
  // `boardArt` is the one thing the file is handed besides the sheet: the
  // board's picture, already made standalone by the caller (boardArtForFile()),
  // put into every board slot the generator left. Without it the slots stay
  // empty and the board is a plain block, which is still a true sheet.
  // ---------------------------------------------------------------------------
  const wiringSheetFile = (sheet, boardArt = "") => {
    const madeAt = stampText(sheet.stamp);
    const about = sheet.droidName ? `${sheet.droidName} - ${madeAt}` : madeAt;
    const pictured = (html) =>
      boardArt ? html.split(BOARD_SLOT).join(BOARD_SLOT.replace("></div>", `>${boardArt}</div>`)) : html;
    return (
      "<!doctype html>\n" +
      '<html lang="en"><head><meta charset="utf-8">' +
      `<title>Wiring - ${esc(about)}</title>` +
      '<link rel="icon" href="data:,">' +
      "</head><body>" +
      `<h1>Wiring</h1>` +
      `<p>${sheet.madeHtml}</p>` +
      `<p>${sheet.promiseHtml}</p>` +
      `<h2>${esc(sheet.plates.wires)}</h2><p>${sheet.wiresSummary}</p>${pictured(sheet.wiresHtml)}` +
      (sheet.partsHtml
        ? `<h2>${esc(sheet.plates.parts)}</h2><p>${esc(sheet.partsSummary)}</p>${sheet.partsHtml}`
        : "") +
      `<h2>${esc(sheet.plates.rail)}</h2>${sheet.railHtml}` +
      (sheet.productsHtml
        ? `<h2>${esc(sheet.plates.products)}</h2><p>${sheet.productsSummary}</p>${sheet.productsHtml}`
        : "") +
      "</body></html>\n"
    );
  };

  // ===========================================================================
  // What does not line up
  //
  // What the builder told the droid, set against what the droid reports, one
  // row per thing either side speaks about (research 9.2, "the contradiction
  // engine"; #454). The second side is a reading wherever the droid has one,
  // which is what makes this stronger than a second declaration.
  //
  // FOUR STATES, AND EVERY ROW HAS EXACTLY ONE (research 5.3):
  //   declared      the builder said so, and nothing has confirmed it -
  //                 including a thing the droid asked and got no answer from
  //                 (ADR 0059's declared-but-not-detected): silence is not a
  //                 second answer, so it never reads as contradicted
  //   observed      the droid saw it: a module answered, a servo's ends were
  //                 recorded by the builder driving it and watching it move
  //   contradicted  declared one thing and observed another, or two of the
  //                 builder's own answers that cannot both be true
  //   not probed    nobody has looked, or nobody can: its own row and its own
  //                 words, never a default, and never amber (#402)
  //
  // ONE FUNCTION DECIDES, EVERY RENDERER READS (the reference's wiring.js,
  // where `live` and `why` come from the same comparison on adjacent lines).
  // lineUp() returns rows of { key, subject, declared, observed, state, light,
  // why, move }; lineUpHtml() reads state, light, why and move and works none
  // of them out again.
  //
  // It is the SCREEN's and never the sheet's. Every observed value here is
  // live, and a printed sheet cannot carry a live value without freezing a lie
  // on paper (#293), so wiringDocument() does not call it, the saved bench copy
  // does not carry it, and nothing is painted onto the drawn wires: the list
  // is where each drawn wire gets its state.
  //
  // IT READS, IT NEVER RESOLVES. A row names both facts and the answer to
  // change, and its route is a link to where that answer lives. Nothing here
  // writes to the droid, unmaps a Part or overwrites the stated Dome Design
  // (CONTEXT.md "Dome Design", #373).
  // ===========================================================================
  const STATES = Object.freeze({
    contradicted: "contradicted",
    declared: "declared",
    notProbed: "not probed",
    observed: "observed",
  });
  // The order a builder has to deal with them in: what cannot be true first,
  // what nobody has confirmed next, then what nobody could look at, and what
  // the droid has seen last.
  const STATE_ORDER = [STATES.contradicted, STATES.declared, STATES.notProbed, STATES.observed];

  // Where each answer is changed. Every one is a surface this image serves
  // on every board (data/shell.js SURFACES), never one-shot Setup (#298).
  const MOVES = Object.freeze({
    configuration: Object.freeze({ href: "#configuration", label: "Change it in Configuration" }),
    servos: Object.freeze({ href: "#servo", label: "Record its ends on Servos" }),
    parts: Object.freeze({ href: "#parts", label: "Add it back on Parts" }),
  });

  // What the list cannot see, said on its face (#350 finding 3): no Board
  // Lane reports these two, so the drawing gives them no pin and no row here
  // can check their wires.
  const UNSEEN =
    "The dome ESC and the RC receiver report no pins in this image, so their wires " +
    "are not drawn and cannot be checked here.";

  const row = (fields) => ({ light: null, why: "", move: null, ...fields });

  // What a declared row the droid asked and heard nothing from says: the
  // answer stands, and the next move is at the far end of the wire.
  const SILENT = "Asked, and no answer. Check its wire and its power.";

  // A Health Signal's answer as a row: its own word and its own light, so the
  // list never reads a signal differently from the Status Plate (CONTEXT.md
  // "Health Signal"). ok is observed; fail and a grey "asked, nobody answered"
  // are declared; anything else nobody could ask is not probed.
  const signalRow = (base, answer, { askedWithNoAnswer = [], silent = SILENT } = {}) => {
    const { state, word } = answer;
    if (state === "ok") return row({ ...base, observed: word, light: "ok", state: STATES.observed });
    if (state === "fail" || askedWithNoAnswer.includes(word)) {
      return row({ ...base, observed: word, light: state, state: STATES.declared, why: silent, move: MOVES.configuration });
    }
    return row({ ...base, observed: word, light: "off", state: STATES.notProbed });
  };

  // The Foot Drive: fitted is the builder's answer, and the wheel controller's
  // readings are the droid's. Only a foot drive that declares it reports
  // anything can be asked (DRIVE_CAP_REPORTS_FEEDBACK, #446); the firmware
  // sends the `hoverboard` block only while its readings are valid
  // (src/web/status_json.cpp).
  const footDriveRow = (base, { status, reportsFeedback, words, restart }) => {
    if (status === null || reportsFeedback === null) {
      return row({ ...base, observed: words.waiting, light: "off", state: STATES.notProbed });
    }
    // The frame carries no `drive` key at all while the feet are not running
    // this boot (data/drive.js renderReading()): saved on, and started off.
    // Nobody is asking, so the silence below would be a lie.
    if (!Object.prototype.hasOwnProperty.call(status, "drive")) {
      return row({ ...base, observed: "Off", light: "off", state: STATES.notProbed,
        why: "The droid started with it off. Restart the droid to use it.", move: restart });
    }
    if (reportsFeedback === false) {
      return row({
        ...base,
        observed: "Reports nothing back",
        light: "off",
        state: STATES.notProbed,
        why: "This foot drive sends no readings, so the droid cannot check it.",
      });
    }
    if (status.hoverboard && typeof status.hoverboard === "object") {
      return row({ ...base, observed: "Readings arriving", state: STATES.observed });
    }
    // data/drive.js renderHoverboard()'s own words for the same silence.
    return row({
      ...base,
      observed: "Nothing from the wheel controller yet.",
      state: STATES.declared,
      why: SILENT,
      move: MOVES.configuration,
    });
  };

  // One row per serial link the drawing shows, and only those: the same
  // lanes sheetWires() draws, so no drawn wire is covered only by a caveat.
  const laneRow = (lane, input) => {
    const { status, words, soundName } = input;
    const readers = window.PAHealthSignals;
    const base = { key: `lane:${lane.key}`, subject: lane.name, declared: "Fitted" };
    if (lane.key === "drive") return footDriveRow(base, input);
    if (lane.key === "audio") {
      const answer = readers.readSoundLink(status, words);
      const sound = signalRow({ ...base, declared: soundName || base.declared }, answer);
      // Switched on, and the reader's Off: the frame carries no running sound
      // module this boot - no block at all, or one saying its output is off
      // (#370). The reader does not say which, so neither does this.
      return answer.word === "Off" ? { ...sound, why: "The droid is not running it this boot." } : sound;
    }
    if (lane.key === "protor2link") {
      // Enabled and never answered: the droid is asking, nobody answers.
      return signalRow(base, readers.readProtoR2link(status, words), { askedWithNoAnswer: ["Not seen"] });
    }
    return row({ ...base, observed: words.unknown, light: "off", state: STATES.notProbed,
      why: "Nothing in this image reports on this link." });
  };

  // The RC receiver has no lane (UNSEEN), but its link is a Health Signal the
  // droid reports: the Status Plate's RC word, from the same model.
  const receiverRow = ({ status, words, rcName }) => {
    const base = { key: "rc", subject: "RC receiver", declared: rcName || "Fitted" };
    if (status === null) return row({ ...base, observed: words.waiting, light: "off", state: STATES.notProbed });
    const signals = window.PAHealthSignals.deriveHealthSignals(status, { unknown: words.unknown });
    const sbus = signals.find((signal) => signal.id === "h-sbus");
    const answer = { state: sbus.state, word: sbus.reason };
    if (answer.state === "off") {
      return row({ ...base, observed: answer.word, light: "off", state: STATES.notProbed,
        why: "Its input is switched off, so the droid is not listening.", move: MOVES.configuration });
    }
    return signalRow(base, answer, { silent: "Asked, and no answer. Check the receiver and the radio." });
  };

  // The dome's panels against the stated Dome Design, from the one function
  // that compares them (data/dome_layout.js statedDesignDifference(), #368):
  // pies and side panels only, because holos, lights and fixtures are spelled
  // differently on the two sides and are not matched by guesswork.
  const domePanelsRow = ({ difference, designLabel }) => {
    const base = { key: "dome-panels", subject: "Dome panels", declared: designLabel || "No dome design" };
    if (difference === null) {
      return row({ ...base, observed: "No live dome layout", light: "off", state: STATES.notProbed });
    }
    if (!difference.comparable) {
      return row({ ...base, observed: "Nothing to compare", state: STATES.declared,
        why: "Your dome design records no panel list to check the dome against." });
    }
    const declared = difference.designLabel;
    if (!difference.differs) return row({ ...base, declared, observed: "The same panels", state: STATES.observed });
    return row({
      ...base,
      declared,
      observed: `It ${difference.differs}`,
      light: "warn",
      state: STATES.contradicted,
      why: difference.sentence,
      move: MOVES.configuration,
    });
  };

  // An Output the drawing shows: a Part on it. Nothing reads a servo back -
  // no encoder, no feedback path (#318) - so a pulse on the pin is never a
  // servo being there. The one thing that has ever confirmed one moves is the
  // builder recording its ends by moving it: `calibrated`. Until then it is
  // amber, as CONTEXT.md "Status Color" puts an uncalibrated Servo Output.
  const outputRow = (output, parts) => {
    const base = {
      key: output.address,
      subject: `${output.name} · ${partNames(parts, output.parts).join(" + ")}`,
      // What the builder put on the wire, by its own name.
      declared: onTheWire(output),
    };
    if (output.light) {
      return row({ ...base, observed: "Nothing reads a light back", light: "off", state: STATES.notProbed });
    }
    if (output.calibrated) {
      return row({ ...base, observed: "Ends recorded", state: STATES.observed,
        why: "You moved it and saw it move when you recorded its ends." });
    }
    return row({
      ...base,
      observed: "Ends not recorded",
      light: "warn",
      state: STATES.declared,
      why: "Nothing reads a servo back. Recording its ends is how you see it move.",
      move: MOVES.servos,
    });
  };

  // Saved but not yet applied (ADR 0059): the Output's row as saved against
  // what the droid started with (data/outputs.js `started`, from `activeWired`
  // and `activeLight`). The comparison is the one the picker's timing line
  // makes (data/parts_mapping.js paintTiming()); that line says something is
  // waiting, and this row says which Output and what it is still running.
  const lightWord = (token) => window.PAOutputs?.lightType?.(token)?.label || token;
  const wireWords = (wired, light) => `${wired ? "wired" : FREE}${light ? `, ${lightWord(light)}` : ""}`;

  const pendingRow = (output, restart) => row({
    key: `pending:${output.address}`,
    subject: output.name,
    declared: `Saved ${wireWords(output.wired, output.light ? output.light.id : null)}`,
    observed: `Running ${wireWords(output.started.wired, output.started.light)}`,
    state: STATES.declared,
    why: `The droid is still running the old answer. Restart the droid to use ${output.name}.`,
    move: restart,
  });

  const isPending = (output) =>
    Boolean(output.started) &&
    (output.wired !== output.started.wired || (output.light ? output.light.id : null) !== output.started.light);

  // Two of the builder's answers that cannot both be true: the Part is off
  // the droid, and an Output still claims it (data/parts_mapping.js
  // offButMapped(), the rule the droid picture and Parts read too). There is
  // no observed side, and the sentence says so.
  const offRow = (part, output) => row({
    key: `off:${part.id}`,
    subject: part.name,
    declared: "Off your droid",
    observed: `Your wiring: ${output.name}`,
    light: "warn",
    state: STATES.contradicted,
    why:
      `You took it off your droid, and your wiring still puts it on ${output.name}. ` +
      "Both are your answers: add it back, or change its output above.",
    move: MOVES.parts,
  });

  /**
   * Every row of what the builder said against what the droid reports, in
   * the order a builder deals with them. Pure: everything it reads arrives in
   * `input`, the Live Reading's status included.
   *
   * @param {object} input
   * @param {object[]} input.outputs - data/outputs.js's Outputs
   * @param {object[]} input.parts - the Droid Parts Catalog's parts
   * @param {string[]|null} input.fitted - the Fitted Parts, null before the
   *   Droid Build has answered
   * @param {object} input.lanes, input.components, input.capabilities - as
   *   loomRows() takes them
   * @param {object|null} input.status - the Live Reading's status, null
   *   before the droid has sent a good frame
   * @param {{unknown: string, waiting: string}} input.words - the Live
   *   Reading's two words
   * @param {boolean} input.rcFitted - the Radio Controller is not answered
   *   Not fitted, which declares nothing and has no row
   * @param {boolean|null} input.reportsFeedback - the fitted Foot Drive
   *   declares it reports readings; null before the lineup has answered
   * @param {object|null} input.difference - statedDesignDifference()
   * @param {{href: string, label: string}} input.restart - where a builder
   *   restarts the droid (data/apply_timing.js RESTART_ROUTE)
   * @returns {object[]}
   */
  const lineUp = (input = {}) => {
    const { outputs = [], parts = [], fitted = null, restart = null } = input;
    const lanes = loomRows(input).filter((lane) => lane.on);
    const rows = [
      ...lanes.map((lane) => laneRow(lane, input)),
      ...(input.rcFitted ? [receiverRow(input)] : []),
      ...(lanes.some((lane) => lane.key === "protor2link") ? [domePanelsRow(input)] : []),
      ...outputs.filter(hasPart).map((output) => outputRow(output, parts)),
      ...outputs.filter(isPending).map((output) => pendingRow(output, restart)),
      ...parts
        .map((part) => ({ part, output: window.PAParts.offButMapped(part.id, fitted, outputs) }))
        .filter(({ output }) => output !== null)
        .map(({ part, output }) => offRow(part, output)),
    ];
    return rows
      .map((each, at) => ({ each, at }))
      .sort((a, b) => STATE_ORDER.indexOf(a.each.state) - STATE_ORDER.indexOf(b.each.state) || a.at - b.at)
      .map(({ each }) => each);
  };

  // The section's subtitle: a count per state that has any, contradictions
  // first (docs/ui-copy-voice.md rule 8).
  const lineUpSummary = (rows) => {
    if (rows.length === 0) return "nothing to check yet";
    return STATE_ORDER
      .map((state) => [state, rows.filter((each) => each.state === state).length])
      .filter(([, count]) => count > 0)
      .map(([state, count]) => `${count} ${state}`)
      .join(" · ");
  };

  // Reads state, light, why and move off each row and nothing else. A
  // builder's answer takes no color: it is a value, not a health signal.
  const lineUpHtml = (rows) => {
    const body = rows.length
      ? '<table class="lineup-table"><thead><tr><th scope="col"></th><th scope="col">You said</th>' +
        '<th scope="col">The droid reports</th><th scope="col">State</th><th scope="col"></th></tr></thead><tbody>' +
        rows.map((each) =>
          `<tr class="lineup-row" data-row="${escAttr(each.key)}" data-state="${escAttr(each.state)}">` +
          `<th scope="row">${esc(each.subject)}</th>` +
          `<td>${esc(each.declared)}</td>` +
          `<td class="lineup-observed">` +
          (each.light ? `<span class="indicator ${escAttr(each.light)}" aria-hidden="true"></span>` : "") +
          `${esc(each.observed)}</td>` +
          `<td class="lineup-state">${esc(each.state)}</td>` +
          `<td class="lineup-why">${esc(each.why)}` +
          (each.move ? `${each.why ? " " : ""}<a class="lineup-move" href="${escAttr(each.move.href)}">${esc(each.move.label)}</a>` : "") +
          `</td></tr>`).join("") +
        "</tbody></table>"
      : '<p class="hint">Nothing to check yet: no Part is on an output and nothing is fitted.</p>';
    return body + `<p class="hint">${esc(UNSEEN)}</p>`;
  };

  window.PAWiring = Object.freeze({
    SUBTITLE,
    PROMISE,
    SCOPE,
    PLATES,
    sheetWires,
    loomRows,
    promiseHtml,
    cadenceOf,
    railHtml,
    sheetStamp,
    wiringDocument,
    wiringSheetFile,
    STATES,
    lineUp,
    lineUpSummary,
    lineUpHtml,
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
  // The config's Component Toggles, for the Board Lanes' switches and labels.
  // The Outputs are data/outputs.js's, read at paint time.
  let components = {};
  // The whole GET /api/config answer, for what the list of what does not
  // line up reads beside the toggles: the Radio Controller's answer.
  let config = {};
  let answered = false;
  // The Live Reading's status frame, null until the droid has sent a good
  // one: the observed side of the list (data/live_reading.js).
  let status = null;

  // The product the board's GPIO outputs are - "Body controller board GPIO" -
  // which the picker pictures with the Body Controller this image runs on.
  // Its lineup entry gives the diagram both its picture and its title (see
  // "The board's picture" below).
  const BOARD_GPIO_PRODUCT = "esp32_gpio_ledc";
  const boardArtId = () => window.ComponentPicker?.artIdFor?.(BOARD_GPIO_PRODUCT) || null;

  // ---------------------------------------------------------------------------
  // The product wiring cards, and which products are on the droid (#458)
  //
  // The cards are in this page's own document where the image carries them,
  // inlined at build time from the asset set's partial, and nowhere where it
  // does not. That is the whole test: this file never asks which board it is
  // on (ADR 0065). A card that will not parse is a broken build, and it fails
  // here, loudly, rather than drawing a page that quietly lost its hazards.
  //
  // Which product is fitted in a family is the Component Picker's answer, the
  // one Configuration shows (productOf()): the family's Component Member where
  // it has one, else the one product this image carries for it, and for the
  // Radio Controller the radio and the RC Receiver it talks to. A family
  // with a Component Toggle is on the droid only while that toggle is on, so
  // a family answered Not fitted has no card. The families are listed in the
  // order Configuration asks them, by their Component Registry ids; no
  // product is named here. Each product carries its family, which is how the
  // table tells the board's own card from the card of the Outputs on it, and
  // its `title`: the builder's word for the family, as Configuration heads
  // it, which is what a row of the table calls a product no Board Lane names.
  // ---------------------------------------------------------------------------
  const cardSource = document.getElementById("wiring-product-cards");
  const productCards = cardSource ? JSON.parse(cardSource.textContent) : null;

  // THIS LIST HAS A SECOND HOME. data/configuration.html declares the same
  // pairing on its picker hosts, as data-component-family beside
  // data-component-toggle (the toggle's element id there, its GET /api/config
  // key here). The Component Picker learns it only from those hosts when
  // Configuration mounts it, and Wiring does not mount Configuration, so it
  // cannot be read from the picker here. A family added to Configuration is
  // added to this list too, or its product has no wiring card.
  const PRODUCT_FAMILIES = [
    { family: BOARD_FAMILY },
    { family: "foot_drive", toggle: "drive" },
    { family: "dome_rotation", toggle: "domeEsc", title: "Dome Rotation" },
    { family: "dome_controller", toggle: "protoR2link" },
    { family: "sound", toggle: "audio" },
    // The one family whose product is reached on the Outputs.
    { family: "body_servo_controller", outputs: true },
  ];

  const fittedProducts = () => {
    const picker = window.ComponentPicker;
    if (!productCards || !picker?.answered?.()) return [];
    const toggles = componentIndex(components);
    const found = [];
    PRODUCT_FAMILIES.forEach(({ family, toggle = "", outputs = false, title = "" }) => {
      if (toggle && !switchedOn(toggles, toggle)) return;
      const chosen = picker.productOf(family);
      // The Outputs' family keeps the board's own GPIO whatever is chosen: a
      // PCA9685 adds its Outputs beside the board's, which keep working
      // (#444), so both are on the droid. The chosen member joins it when it
      // is another product and the image carries a card for it.
      const parts = outputs
        ? [picker.partOf(BOARD_GPIO_PRODUCT),
          chosen && chosen.id !== BOARD_GPIO_PRODUCT && Object.hasOwn(productCards, chosen.id) ? chosen : null]
        : [chosen];
      parts.filter(Boolean).forEach((part) => {
        // A Board Lane's key is its Component Toggle's, folded to lower case
        // (componentIndex() above).
        found.push({ id: part.id, name: part.name, family, title, lane: toggle.toLowerCase(), outputs: outputs ? part.protocol : "" });
      });
    });
    if (!picker.isRadioNotFitted()) {
      // The radio and the receiver it talks to are two products of one
      // family, and each is a row of its own.
      [[picker.chosenPart("radio_controller"), "Radio Controller"], [picker.chosenReceiverPart(), "RC receiver"]].forEach(([part, title]) => {
        if (part) found.push({ id: part.id, name: part.name, family: "radio_controller", title, lane: "", outputs: "" });
      });
    }
    return found;
  };

  const model = () => ({
    parts: window.DroidParts?.parts || [],
    outputs: window.PAOutputs.list(),
    components,
    lanes: identity?.board_lanes || {},
    capabilities: identity?.board_capabilities || {},
    boardName: window.ComponentPicker?.artPartFor?.(BOARD_GPIO_PRODUCT)?.name || "",
    droidName: typeof identity?.droidName === "string" ? identity.droidName : "",
    cadence: cadenceOf(config),
  });

  // The pieces that do not wait for the droid: the two sentences and the
  // plate headings. They go up at mount rather than on the first answer,
  // because the promise is what tells a builder whether to read the rest at
  // all, and a header that is blank until a fetch lands has nothing to say in
  // exactly the moment it matters.
  write("wiring-promise", promiseHtml());
  write("wiring-rail", railHtml());
  write("wiring-wires-heading", esc(PLATES.wires));
  write("wiring-parts-heading", esc(PLATES.parts));
  write("wiring-rail-heading", esc(PLATES.rail));
  write("wiring-products-heading", esc(PLATES.products));

  // What the sheet is made from: the droid's answers, and the product wiring
  // cards with the products fitted. The list of what does not line up reads
  // model() on every Live Reading frame and no card, so the cards ride here.
  const sheetModel = () => ({ ...model(), cards: productCards, products: fittedProducts() });

  // The cards the board's own group offers: the Body Controller's, and the one
  // for the Outputs on it. Named for what each is a card of.
  const boardCards = () =>
    cardedProducts(sheetModel())
      .filter(ownsBoardCard)
      .map((product) => ({ id: product.id, word: product.outputs ? "servo wiring card" : "board card" }));

  const cardHtmlFor = (id) => {
    const sheet = sheetModel();
    const product = cardedProducts(sheet).find((each) => each.id === id);
    return product ? productCardHtml(product, sheet.cards[id], sheet) : "";
  };

  // The parts wiring table, where a Part is put on an Output, moved with the
  // question first, or taken off, and what is on its wire is chosen
  // (data/parts_mapping.js picker(), #347, #411, #463). Mounted here and never
  // made by wiringDocument(), which makes the same table as text: the printed
  // sheet stays a reference that writes nothing (CONTEXT.md "Wiring"). The
  // links and the cards are handed over as this file reads them, and
  // only once the droid has answered: a lane whose switch has not been read
  // is not yet known to be fitted.
  const partsTable = window.PAParts?.picker({
    table: document.getElementById("wiring-parts-table"),
    summary: document.getElementById("wiring-parts-summary"),
    feedback: document.getElementById("wiring-parts-feedback"),
    dialog: document.getElementById("wiring-move-dialog"),
    timing: document.getElementById("wiring-parts-timing"),
    find: document.getElementById("wiring-find"),
    links: () => (answered ? linkRows(sheetModel()) : []),
    cards: productCards ? { board: boardCards, html: cardHtmlFor } : null,
  });

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
    const photo = document.querySelector(".wd-board-slot")?.querySelector("img");
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

  // The wire colors as the bench copy needs them. The file carries no
  // stylesheet, so each --wire-* token the pictures name is resolved to the
  // value the stylesheet gives it right now, and the file is inked with that.
  // The stylesheet stays the one place a wire color is written down.
  const inkedForFile = (file) => {
    const style = window.getComputedStyle?.(document.documentElement);
    if (!style) return file;
    return file.replace(/var\((--wire-(?:\d+|off))\)/g, (whole, token) => style.getPropertyValue(token).trim() || whole);
  };

  // The lineup can answer after the sheet is up; the board's picture and its
  // name in the diagram's title follow it.
  window.ComponentPicker?.onChange?.(() => {
    if (answered) paint();
  });

  // The pictures on screen carry the minute they were drawn - when this
  // surface last read the droid, or when its sheet was last saved - so a
  // screenshot of them says when it was true, the same as the saved copy does.
  const paint = (stamp = sheetStamp()) => {
    const sheet = wiringDocument({ ...sheetModel(), stamp });
    // The droid has answered, so the rail line can state its figure.
    write("wiring-rail", sheet.railHtml);
    write("wiring-wires-summary", sheet.wiresSummary);
    write("wiring-wires", sheet.wiresHtml);
    fillBoardArt();
    // What only paper shows (data/style.css, @media print): whose sheet and
    // from when, the parts wiring table as text in place of its controls, and
    // every fitted product's card after Power wiring. The cards' plate is in
    // the document only where the image carries them, and prints only while a
    // fitted product has one. On the screen a card opens on its product's row.
    write("wiring-made", sheet.madeHtml);
    write("wiring-parts-sheet", sheet.partsHtml);
    write("wiring-products-summary", sheet.productsSummary);
    write("wiring-products", sheet.productsHtml);
    document.getElementById("wiring-products-card")?.classList.toggle("hidden", sheet.productsHtml === "");
    // The table's links and cards follow the same answers.
    partsTable?.repaint();
    paintLineUp();
    return sheet;
  };

  // ---------------------------------------------------------------------------
  // The list of what does not line up, on screen
  //
  // Mounted here and never made by wiringDocument(): every observed value in
  // it is live, and the printed sheet stays a reference that carries none
  // (lineUp() above). Its inputs are the answers this surface already holds -
  // the Outputs and the config from the one section read, the Droid Build
  // adopted from that same config, the lineup the Component Picker read, the
  // dome's layout as data/dome_layout.js resolved it - and the Live Reading
  // the shell already runs. It starts no read of its own.
  // ---------------------------------------------------------------------------
  // "MK4 Complex": the stated Dome Design in the catalog's own short words,
  // as the Droid Build picker names it (data/droid_build_picker.js).
  const domeDesignLabel = () => {
    const build = window.DroidBuild?.current?.();
    const design = build ? (window.DroidParts?.designs || []).find((row) => row.id === build.dome.design) : null;
    if (!design) return "";
    const variant = (design.variants || []).find((row) => row.id === build.dome.variant);
    return variant ? `${design.short} ${variant.label}` : design.short;
  };

  const lineUpInput = () => {
    const picker = window.ComponentPicker;
    const rc = config.rc && typeof config.rc === "object" ? config.rc : null;
    return {
      ...model(),
      fitted: window.DroidBuild?.current?.()?.fitted ?? null,
      status,
      words: { unknown: window.PALiveReading.UNKNOWN, waiting: window.PALiveReading.WAITING },
      soundName: picker?.chosenPart?.("sound")?.name || "",
      // The Radio Controller answered Not fitted declares nothing, so it has
      // no row (#369); the picker's own rule for that answer.
      rcFitted: rc !== null && !(picker?.isRadioNotFitted?.() ?? rc.inputMode === "not_fitted"),
      rcName: picker?.chosenPart?.("radio_controller")?.name || picker?.chosenReceiverPart?.()?.name || "",
      reportsFeedback: picker?.footDriveReportsFeedback?.() ?? null,
      difference: window.DomeLayout?.statedDesignDifference?.() ?? null,
      designLabel: domeDesignLabel(),
      restart: window.PAApplyTiming?.RESTART_ROUTE || null,
    };
  };

  // Written only when what it says has changed: the Live Reading repaints it
  // on every frame, and a table rebuilt under a builder's pointer takes the
  // link they were about to press out from under them.
  let lineUpDrawn = null;
  const paintLineUp = () => {
    if (!answered) return;
    const rows = lineUp(lineUpInput());
    const html = lineUpHtml(rows);
    const summary = lineUpSummary(rows);
    if (lineUpDrawn === summary + html) return;
    lineUpDrawn = summary + html;
    const sub = document.getElementById("wiring-lineup-summary");
    if (sub) {
      sub.classList.remove("waiting");
      sub.textContent = summary;
    }
    write("wiring-lineup", html);
  };

  const onWiring = () => answered && document.body?.dataset?.page === "wiring";
  window.PALiveReading?.subscribe((reading) => {
    status = reading.status;
    if (onWiring()) paintLineUp();
  });
  window.DroidBuild?.onChange?.(() => {
    if (onWiring()) paintLineUp();
  });
  window.DomeLayout?.onChange?.(() => {
    if (onWiring()) paintLineUp();
  });

  // ---------------------------------------------------------------------------
  // The bench copy's caller
  //
  // The one act on this surface, and it writes nothing to the droid: it saves
  // a file on the computer in front of you. The sheet it saves is made from the
  // same read the screen is showing, and the screen is repainted with that
  // very sheet in the same moment, so the page you are looking at and the file
  // you just saved carry the same minute, the same wires and the same counts.
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
      const file = inkedForFile(wiringSheetFile(sheet, boardArtForFile()));
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

  // A Part moved in the table lands on the droid and is read back, and the
  // wire it now hangs off names it: the sheet follows every read of the
  // Outputs once the droid has answered. Only while Wiring is on screen - the
  // other surfaces read the same module, and a sheet repainted out of sight
  // is repainted again on the way back (loadSheet() below).
  window.PAOutputs.onChange(() => {
    if (answered && document.body?.dataset?.page === "wiring") paint();
  });

  // One section for one answer. The sheet is a join of three reads and a half
  // answer is not a sheet -- a table painted from outputs the droid reported
  // and toggles it did not would say a wire is live when it is switched off --
  // so both reads are in the one section run (data/outputs.js load()) and
  // either one failing is the section failing, which is what the Page Recovery
  // View is for. The same read paints the parts wiring table: GET /api/config is
  // asked once.
  const loadSheet = async ({ handle = null } = {}) => {
    const answer = await window.PAOutputs.load({ handle });
    config = answer.config && typeof answer.config === "object" ? answer.config : {};
    components = config.components && typeof config.components === "object" ? config.components : {};
    // The Droid Build and the Component Members ride this same answer, so
    // neither module reads GET /api/config again for the list.
    window.DroidBuild?.adopt?.(config);
    window.ComponentPicker?.adopt?.(config);
    answered = true;
    paint();
    saveLink?.setAttribute("aria-disabled", "false");
  };

  // Identity is fetched once at boot by the shell, so a surface mounted later
  // reads the cache and also listens: the shell replays the outcome to a late
  // surface, and POST /api/identity republishes it (data/shell.js).
  //
  // It repaints only once the droid's own table has answered. The lanes alone
  // would draw a sheet with no outputs at all - which is the right answer for
  // a fresh droid and a wrong one for every other, so it is not a sheet worth
  // flashing up on the way to the real one.
  window.addEventListener("pa:identity-available", (event) => {
    identity = event.detail;
    if (answered) paint();
  });

  if (window.PABootstrap) {
    window.PABootstrap.setResourceLabels?.({
      "/droid_parts.js": "parts list",
      "/outputs.js": "the outputs",
      "/wiring.js": "the wiring sheet",
      "/output_settings.js": "the outputs",
      "/dome_command_map.js": "the dome's commands",
      "/find_by_moving.js": "find by moving",
      "/parts_mapping.js": "the parts on each output",
      "/droid_build.js": "your droid build",
      "/dome_layout.js": "the dome's layout",
    });
    window.PABootstrap.registerSection("wiring-sheet", loadSheet, {
      label: "the wiring sheet",
    });
  } else {
    loadSheet().catch((error) => console.warn("[wiring] sheet unavailable:", error));
  }

  // Owned by this surface, so the shell stops it when the operator leaves and
  // starts it again on the way back (#360). There is no cadence: the one live
  // reading here - the list of what does not line up - rides the Live Reading
  // the shell already runs, and the one thing that must not go stale is the
  // sheet after a Part was moved on Servos - which is a return, not a tick.
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

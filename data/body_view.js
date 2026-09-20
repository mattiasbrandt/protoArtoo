// =============================================================================
// data/body_view.js
//
// The one renderer for a picture of the droid, and the selection panel beside
// it (ADR 0063 as amended 2026-09-19, #352, #372).
//
// A BODY VIEW IS A STATE DISPLAY, NOT A PART FINDER. Its job is showing many
// parts at once, which a named list cannot do (#317, closed 2026-09-09 under
// the title "the picture is for watching the droid, not for finding parts on
// it"). What it draws is the operator's pick on #408: ONE CARD WITH THREE
// FACES, Front, Rear and Dome (Top), switched by the tab text at every width.
// The front and rear are R2-D2 as a builder recognises him - the operator's own
// hand-edited line art (data/body_art.js) - with every Part's footprint laid
// over it where the Part actually sits. The dome is top-down, because no front
// or rear view can fit every panel, and it is drawn in the same line language
// from the vendored MK4 dome map (data/dome_panel_model.js).
//
// Six rules shape this file. Each one is a decision somebody took, not a
// preference, and each is restated where it is kept.
//
// THE VIEW NEVER WRITES. There is no PAApi call anywhere below and there must
// not be one. The renderer's whole output is "this marker was picked", "this
// face was chosen" and "this Common Addition was asked for"; what those mean
// belongs to the caller, and the selection panel is the only writer on the
// surface. If this file ever knows what its caller will do with a pick, the
// seam is in the wrong place.
//
// ONE KIND OF STATE AT A TIME. An update carries one `kind` -- what the droid
// was last told, or what a routine says at a chosen moment. Both are commanded
// intent and they draw IDENTICALLY, which is the point: a view that drew them
// differently would ask a builder to remember which half they were looking at,
// the confusion #298 spent a ticket undoing. The live kind is the only thing
// Parts ever shows, so it prints no sentence (the pick dropped it); a routine's
// moment says so under the picture, because a Sequence's picture must say
// which kind it is. This file never computes a mark, it only draws one, so
// there is no branch here that could tell the two kinds apart.
//
// NOTHING IS READ BACK FROM A SERVO. Every position below is a width the
// controller put on a pin. A jammed servo reports exactly what a free one does
// (ADR 0045), so no word on this surface presents a mark as where a horn
// actually is.
//
// A CLICK ONLY EVER SELECTS. Picking fills the panel beside the drawing with
// the Part's name, its Servo, its state and the acts available on it as NAMED
// BUTTONS. One gesture with one meaning, on a surface where most parts
// mid-build are unfitted, unassigned or both -- and the panel is also the
// visible explanation ADR 0059 requires at the entrance of a control that
// moves something.
//
// REPAINT IN PLACE, NEVER REBUILD. Every node is built once, in mountDrawing();
// update() writes classList, textContent, attributes and `hidden` on nodes
// that already exist and never innerHTML (after r2d2-astromech-simulator
// v1.79.0, src/js/maestro/hw-table.js:174). A rebuild would also drop the
// current selection, which is the thing a builder is holding. So a Part the
// droid does not carry is built and HIDDEN, not left out: adding an arm shows
// a node, it never rebuilds the picture.
//
// A STATE IS ONE CLASS, COMPUTED ONCE, AND ITS WORD IS FIXED. markClass() is
// the whole vocabulary and LEGEND_TEXT is every word it can say, shared by the
// body and the dome so Closed, Open, Unassigned and Picked mean and look the
// same on both. Color is spent on exactly two of them (ADR 0063 as amended):
// the interaction accent fills an OPEN Part, the way the interactive dome
// always drew an open panel, and amber marks ends not measured. Closed spends
// no color; every other state is carried by line weight, dash and ink.
// =============================================================================
(() => {
  "use strict";

  // ---------------------------------------------------------------------------
  // The vocabulary
  //
  // The renderer owns the words and the caller owns which one a Part is in,
  // because working that out needs the Output rows and this file has none.
  // ---------------------------------------------------------------------------

  // One word per state, always all seven, in this order (operator, 2026-09-19:
  // "make it short and concise. ONE word only", and always shown). A legend
  // that never changes shape cannot drift from the surface either.
  const LEGEND_TEXT = Object.freeze({
    closed: "Closed",
    open: "Open",
    unmeasured: "Unmeasured",
    limp: "Limp",
    unassigned: "Unassigned",
    unfitted: "Unfitted",
    selected: "Picked",
  });
  const LEGEND_ORDER = Object.freeze(Object.keys(LEGEND_TEXT));

  // The marks a caller may hand this renderer. `openable` is the only mark
  // that carries a position, as `at` -- a fraction of the travel the builder
  // RECORDED, 0 at the close end and 1 at the open end. An Output nobody has
  // measured has no travel for a fraction to be of, which is why `unmeasured`
  // is its own mark and not an `openable` with a made-up number. `unknown` is
  // "the droid has not said yet", and draws no state at all.
  const MARKS = Object.freeze({
    OPENABLE: "openable",
    UNMEASURED: "unmeasured",
    LIMP: "limp",
    UNASSIGNED: "unassigned",
    UNKNOWN: "unknown",
  });
  const MARK_TOKENS = Object.freeze(Object.keys(MARKS).map((key) => MARKS[key]));

  // Every class markClass() can return. `unknown` is drawable but has no legend
  // word, because it is not a state of the droid.
  const STATE_CLASSES = Object.freeze([
    "closed",
    "open",
    "unmeasured",
    "limp",
    "unassigned",
    "unfitted",
    "unknown",
  ]);

  // A state is one class, computed once, here, for the body and the dome alike.
  // A Part not on this build is Unfitted whatever its Output says: the droid
  // does not have it, so nothing else about it is a claim worth drawing.
  const markClass = (mark, fitted) => {
    if (!fitted) return "unfitted";
    const token = mark ? mark.mark : undefined;
    if (token === MARKS.OPENABLE) return typeof mark.at === "number" && mark.at >= 0.5 ? "open" : "closed";
    return MARK_TOKENS.indexOf(token) === -1 ? "unknown" : token;
  };

  // The two kinds of state this renderer can show. Only a routine's moment
  // says so on the picture; see the second rule in the header.
  const STATE_KINDS = Object.freeze({
    LIVE: "live",
    POSE: "pose",
  });
  const KIND_SAID = Object.freeze({
    live: "Showing: what the droid was last told",
    pose: "Showing: this moment of the routine",
  });

  // ---------------------------------------------------------------------------
  // The faces
  // ---------------------------------------------------------------------------
  const FACES = Object.freeze([
    Object.freeze({ id: "front", label: "Front", half: "body" }),
    Object.freeze({ id: "rear", label: "Rear", half: "body" }),
    Object.freeze({ id: "dome", label: "Dome (Top)", half: "dome" }),
  ]);
  const faceById = (id) => FACES.find((face) => face.id === id) || null;

  // ---------------------------------------------------------------------------
  // The one position table
  //
  // Where every body Part sits, in the line art's own 894 x 894 viewBox
  // (data/body_art.js). EVERY VALUE IS THE OPERATOR'S OWN PLACEMENT, made
  // against the real art with the #408 pick's placement editor and pasted in
  // unchanged, one object per face; `hinge` and `label` ride along from that
  // export so a new placement pastes straight over this one, and nothing below
  // reads them. This is the only place a body position exists: the catalog
  // carries compass words, not coordinates (docs/droid-parts.yaml), and nothing
  // here is ever written back to it as a bearing.
  //
  // A Part is on exactly one face. placementFor() refuses a second one out
  // loud rather than drawing a door twice.
  // ---------------------------------------------------------------------------
  const GEOMETRY = Object.freeze({
    front: Object.freeze({
      doorFL: { x: 276, y: 294, w: 41, h: 237, hinge: "left", label: "FL" },
      doorFR: { x: 579, y: 292, w: 40, h: 237, hinge: "right", label: "FR" },
      smallDoor: { x: 372, y: 457, w: 24, h: 75, hinge: "top", label: null },
      // The two utility arms are the same part twice, so the same size, and a
      // rounded bar (rx: h/2) rather than a box, which read as UI chrome sitting
      // on the droid rather than an arm.
      utilUp: { x: 350, y: 300, w: 194, h: 31, hinge: "top", label: "Up", rx: 15.5 },
      utilLo: { x: 350, y: 343, w: 194, h: 31, hinge: "top", label: "Lo", rx: 15.5 },
      dataport: { x: 498, y: 399, w: 59, h: 130, hinge: "left", label: "Data" },
      chargebay: { x: 328, y: 385, w: 71, h: 62, hinge: "right", label: "Chg" },

      bodyPanel1: { x: 491, y: 589, w: 36, h: 64, hinge: "left", label: "1" },
      bodyPanel2: { x: 534, y: 589, w: 13, h: 64, hinge: "left", label: "2" },
      bodyPanel3: { x: 551, y: 589, w: 14, h: 65, hinge: "right", label: "3" },
      bodyPanel4: { x: 359, y: 548, w: 40, h: 33, hinge: "right", label: "4" },
      bodyPanel5: { x: 409, y: 547, w: 78, h: 36, hinge: "top", label: "5" },
      bodyPanel6: { x: 532, y: 545, w: 89, h: 37, hinge: "top", label: "6" },

      // The gripper sits behind doorFR and the interface arm behind doorFL,
      // confirmed by the operator against the real droid (2026-09-18). Each
      // end effector rides with its own arm rather than being placed apart.
      gripArm: { x: 590, y: 302, w: 16, h: 218, hinge: "left", label: null },
      interArm: { x: 288, y: 304, w: 16, h: 217, hinge: "right", label: null },
      gripClaw: { x: 583, y: 420, w: 18, h: 40, hinge: "left", label: null },
      interTool: { x: 280, y: 420, w: 18, h: 40, hinge: "right", label: null },
    }),
    rear: Object.freeze({
      // The rear is the same traced envelope as the front, so the rear
      // breadpans sit at exactly the front breadpans' spot.
      doorRL: { x: 276, y: 294, w: 41, h: 237, hinge: "left", label: "RL" },
      doorRR: { x: 579, y: 292, w: 40, h: 237, hinge: "right", label: "RR" },
      bodyPanel7: { x: 266, y: 545, w: 58, h: 34, hinge: "top", label: "7" },
      bodyPanel8: { x: 565, y: 546, w: 60, h: 33, hinge: "top", label: "8" },
    }),
  });

  // Surface details: drawing only, so the picture keeps reading as R2 once the
  // Parts have their own boxes over the art. Never a Part, never selectable,
  // never a state, never in the legend -- and named the way Printed Droid names
  // them, which a hover shows. The operator's placement, like GEOMETRY.
  const SURFACE_DETAILS = Object.freeze({
    vents: { label: "Louvered Exhaust Vents", face: "front", x: 261, y: 546, w: 71, h: 103 },
    coinSlots: { label: "Coin Slots", face: "front", x: 339, y: 452, w: 21, h: 87 },
    coinReturns: { label: "Coin Returns", face: "front", x: 341, y: 590, w: 65, h: 60 },
    powerCouplings: { label: "Power Couplings", face: "front", x: 418, y: 590, w: 61, h: 59 },
    octagonalPorts: { label: "Octagonal Ports", face: "front", x: 576, y: 587, w: 59, h: 65 },
    restrainingBolt: { label: "Restraining Bolt", face: "front", x: 521, y: 432, w: 16, h: 17 },
    rearPowerCoupling: { label: "Rear Power Coupling", face: "rear", x: 411, y: 599, w: 60, h: 55 },
    rearOctagonalPort: { label: "Rear Octagonal Port", face: "rear", x: 563, y: 598, w: 60, h: 57 },
  });

  // The Common Additions the Parts list offers, one "Add" per arm and the end
  // effector it always comes with: a builder does not fit a gripper arm without
  // its claw. Every arm is a Common Addition, off by default (#409); the
  // catalog says WHICH Parts are additions, and this says only how the list
  // groups them.
  const ADDITION_GROUPS = Object.freeze([
    Object.freeze({ key: "utilArms", label: "Utility arms", ids: Object.freeze(["utilUp", "utilLo"]) }),
    Object.freeze({ key: "gripArm", label: "Gripper arm and claw", ids: Object.freeze(["gripArm", "gripClaw"]) }),
    Object.freeze({ key: "interArm", label: "Interface arm and tool", ids: Object.freeze(["interArm", "interTool"]) }),
  ]);

  // Where the "dome parts pending" stamp sits on a body face: over the dome's
  // crown, in the art's viewBox.
  const DOME_PENDING_AT = Object.freeze({ front: { x: 447, y: 160 }, rear: { x: 447, y: 140 } });

  // ---------------------------------------------------------------------------
  // The dome, top-down
  //
  // The vendored MK4 map (window.DOME_PANEL_MAP_SVG) is the ONE copy of the
  // dome's geometry protoArtoo carries, and tools/check_dome_panel_drift.py
  // holds it against the dome's own map. So this reads its pieces out of that
  // string rather than keeping a second copy that nothing checks, and redraws
  // them in the body's line language.
  //
  // Which catalog Parts each drawn piece stands for. One piece can be several
  // Parts: a holoprojector is a pan and a tilt axis (a Part is one thing an
  // Output drives, #320) on one device, and the vendored map draws Side Panels
  // 5 and 6 as one fixed wedge. A light is never listed here: it joins the
  // piece of the panel it sits on (`sitsOn`), so the two can never disagree
  // about where they both are.
  // ---------------------------------------------------------------------------
  const DOME_PIECE_PARTS = Object.freeze({
    pp1: ["pie1"],
    pp2: ["pie2"],
    pp3: ["pie3"],
    pp4: ["pie4"],
    pp5: ["pie5"],
    pp6: ["pie6"],
    p1: ["panel1"],
    p2: ["panel2"],
    p3: ["panel3"],
    p4: ["panel4"],
    p7: ["panel7"],
    p11: ["panel11"],
    p13: ["panel13"],
    r_p8: ["panel8"],
    r_p9: ["panel9"],
    r_p10: ["panel10"],
    r_p12: ["panel12"],
    r_p14: ["panel14"],
    r_merge: ["panel5", "panel6"],
    hp1: ["hp1Pan", "hp1Tilt"],
    hp2: ["hp2Pan", "hp2Tilt"],
    hp3: ["hp3Pan", "hp3Tilt"],
  });

  // The guide circles the dome is drawn on: the outer edge of the side-panel
  // ring, its dashed inner edge, the pie ring's outer and inner edges, and the
  // hub. Radii in the vendored map's 480 x 480 viewBox.
  const DOME_GUIDES = Object.freeze([
    { r: 172 },
    { r: 146, dashed: true },
    { r: 119 },
    { r: 54 },
    { r: 22, hub: true },
  ]);
  const DOME_VIEWBOX = "0 0 480 480";
  const DOME_CENTRE = 240;

  // Markup is built ONCE, as a string, and every later frame writes onto the
  // nodes that string made. Deliberately not document.createElementNS(): the
  // picture is parsed as part of the host document, so the browser puts it in
  // the SVG namespace itself.
  const esc = (value) =>
    window.PAUtils && window.PAUtils.escapeHtml
      ? window.PAUtils.escapeHtml(String(value))
      : String(value).replace(/[&<>"']/g, (ch) => `&#${ch.charCodeAt(0)};`);

  const num = (value) => {
    const n = Number(value);
    return Number.isFinite(n) ? n : null;
  };

  // The dome's pieces, read out of the vendored map: each selectable pie and
  // side panel by its `id`, the fixed wedges likewise, and the three
  // holoprojectors by the name their <title> starts with, since the vendored
  // map gives them no id. Only the geometry is taken -- the map's own fills,
  // labels, callouts and legend are its old look, which this redraw replaces.
  // Returns [] for a missing or unreadable map, and the dome face then says so
  // rather than drawing an empty dome, which would read as "fitted nothing".
  const readDomePieces = (svgText) => {
    if (typeof svgText !== "string" || svgText === "" || typeof document === "undefined") return [];
    const holder = document.createElement("div");
    holder.innerHTML = svgText;
    const svg = holder.querySelector("svg");
    if (!svg) return [];
    const pieces = [];
    const seen = new Set();
    const shapeOf = (node) => {
      const tag = String(node.tagName).toLowerCase();
      if (tag === "path") {
        const d = node.getAttribute("d");
        return d ? { tag, d } : null;
      }
      const cx = num(node.getAttribute("cx"));
      const cy = num(node.getAttribute("cy"));
      if (cx === null || cy === null) return null;
      if (tag === "circle") {
        const r = num(node.getAttribute("r"));
        return r === null ? null : { tag, cx, cy, r };
      }
      if (tag === "ellipse") {
        const rx = num(node.getAttribute("rx"));
        const ry = num(node.getAttribute("ry"));
        if (rx === null || ry === null) return null;
        return { tag, cx, cy, rx, ry, transform: node.getAttribute("transform") || "" };
      }
      return null;
    };
    Array.from(svg.children).forEach((node) => {
      let id = node.getAttribute("id");
      if (!id && node.classList.contains("hp")) {
        const title = node.querySelector("title");
        const match = title ? /^HP(\d+)/.exec(title.textContent.trim()) : null;
        id = match ? `hp${match[1]}` : null;
      }
      if (!id || !DOME_PIECE_PARTS[id] || seen.has(id)) return;
      const shape = shapeOf(node);
      if (!shape) return;
      seen.add(id);
      pieces.push({ id, target: node.getAttribute("data-target") || null, shape });
    });
    return pieces;
  };

  // ---------------------------------------------------------------------------
  // placementFor()
  //
  // Which markers this set of Parts makes, on which face, and what each one
  // stands for. Pure apart from reading the dome map, and exported so a test can
  // read the placement without drawing it.
  //
  // A body marker is one Part at its GEOMETRY entry. A dome marker is one
  // vendored piece and every Part it stands for, lights included through
  // `sitsOn`. A Part with no place on any face is not drawn and not listed
  // beside the picture: the "Other part" slots are the case, and they stay on
  // the Parts table (ADR 0063 as amended).
  // ---------------------------------------------------------------------------
  // What a piece is called. One Part gives its own name. The axes of one
  // holoprojector share a name up to the axis word ("Holoprojector 1 pan",
  // "Holoprojector 1 tilt"), and the device is called by that shared part;
  // anything else is its Parts' names together - never a made-up device name.
  const pieceLabel = (own) => {
    if (own.length === 1) return own[0].name;
    const devices = own.map((part) =>
      part.axis && part.name.endsWith(` ${part.axis}`) ? part.name.slice(0, -part.axis.length - 1) : null
    );
    if (devices.every((name) => name !== null && name === devices[0])) return devices[0];
    return own.map((part) => part.name).join(" · ");
  };

  const placementFor = (parts, domeSvg) => {
    const rows = Array.isArray(parts) ? parts.filter((part) => part && part.id) : [];
    const byId = new Map(rows.map((part) => [part.id, part]));
    const markers = [];

    // Body markers in position-table order, front then rear, which is also the
    // order the Parts list beside the picture reads in.
    const tableOrder = ["front", "rear"].reduce((ids, face) => ids.concat(Object.keys(GEOMETRY[face])), []);
    const bodyRows = rows
      .filter((part) => tableOrder.indexOf(part.id) !== -1)
      .sort((a, b) => tableOrder.indexOf(a.id) - tableOrder.indexOf(b.id));
    bodyRows.forEach((part) => {
      const faces = ["front", "rear"].filter((face) => GEOMETRY[face][part.id]);
      if (faces.length > 1) {
        // Never swallowed, and never drawn twice: a door on two faces is a
        // position table that disagrees with itself.
        console.error(`[body-view] ${part.id} is placed on ${faces.join(" and ")}; drawn on ${faces[0]} only`);
      }
      const face = faces[0];
      markers.push({
        id: part.id,
        face,
        half: "body",
        parts: [part.id],
        label: part.name,
        geom: GEOMETRY[face][part.id],
      });
    });

    // Dome markers in catalog order - the pies, then the side panels by number,
    // then the holoprojectors - rather than the order the vendored map happens
    // to draw them in.
    const catalogIndex = (piece) => {
      const found = DOME_PIECE_PARTS[piece.id]
        .map((id) => rows.findIndex((part) => part.id === id))
        .filter((index) => index !== -1);
      return found.length ? Math.min(...found) : Infinity;
    };
    readDomePieces(domeSvg)
      .sort((a, b) => catalogIndex(a) - catalogIndex(b))
      .forEach((piece) => {
        const hosts = DOME_PIECE_PARTS[piece.id].filter((id) => byId.has(id));
        if (hosts.length === 0) return;
        const carried = rows
          .filter((part) => part.sitsOn && hosts.indexOf(part.sitsOn) !== -1)
          .map((part) => part.id);
        const own = hosts.map((id) => byId.get(id));
        // A holoprojector pans and tilts and never opens, which the catalog says
        // on every one of its Parts (`axis`). It carries no open state and no
        // Open action anywhere (operator, 2026-09-19).
        const panTilt = own.every((part) => Boolean(part.axis));
        markers.push({
          id: `dome-${piece.id}`,
          face: "dome",
          half: "dome",
          parts: hosts.concat(carried),
          label: pieceLabel(own),
          shape: piece.shape,
          target: piece.target,
          panTilt,
        });
      });

    return { markers };
  };

  const rectsOverlap = (a, b) => a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;

  // ---------------------------------------------------------------------------
  // mountDrawing()
  //
  // Builds the card once -- the three face tabs, the three pictures, and the
  // legend and Parts list in the rail beside them -- and hands back the handle
  // its caller repaints it through. The handle is the whole of what a caller
  // may do to the drawing: there is no way from out here to write a class or a
  // label onto a marker except by handing update() a model.
  //
  // @param {Element} host - where the picture goes. Written once, here.
  // @param {object} options
  //   parts     - the Part records to place, from window.DroidParts.parts.
  //   art       - {front, rear} line art (window.BodyArt).
  //   domeSvg   - the vendored MK4 dome map (window.DOME_PANEL_MAP_SVG).
  //   railHost  - where the legend and the Parts list go, after whatever the
  //               caller already put there (the selection panel).
  //   onPick    - called with a marker id when a builder picks one, on the
  //               picture or in the Parts list. The pick is the caller's to
  //               apply through select().
  //   onFace    - called with a face id once the shown face has changed.
  //   onAdd     - called with an ADDITION_GROUPS entry when a builder presses
  //               its Add. The Droid Build is the caller's to change.
  // @returns {{update, select, selected, showFace, face, partsOf, markerOf,
  //            markerIds}}
  // ---------------------------------------------------------------------------
  const mountDrawing = (host, options) => {
    const opts = options || {};
    const { markers } = placementFor(opts.parts, opts.domeSvg);
    const art = opts.art || {};
    const onPick = typeof opts.onPick === "function" ? opts.onPick : () => {};
    const onFace = typeof opts.onFace === "function" ? opts.onFace : () => {};
    const onAdd = typeof opts.onAdd === "function" ? opts.onAdd : () => {};
    const byMarker = new Map(markers.map((marker) => [marker.id, marker]));

    // A footprint is always one shape, and state is carried by its own stroke
    // and fill: a second shape nested inside the first read as clutter wherever
    // it sat (the #408 mock's "extra square" reports). No `id` attribute
    // anywhere -- a document may hold more than one picture, and SVG ids must
    // be unique per document; identity rides on data-marker.
    const footprintHtml = (marker) => {
      if (marker.geom) {
        const { x, y, w, h, rx } = marker.geom;
        return (
          `<rect class="bv-footprint" x="${x}" y="${y}" width="${w}" height="${h}" ` +
          `rx="${rx === undefined ? 1.5 : rx}"></rect>`
        );
      }
      const shape = marker.shape;
      if (shape.tag === "path") return `<path class="bv-footprint" d="${esc(shape.d)}"></path>`;
      if (shape.tag === "circle") {
        return `<circle class="bv-footprint" cx="${shape.cx}" cy="${shape.cy}" r="${shape.r}"></circle>`;
      }
      const transform = shape.transform ? ` transform="${esc(shape.transform)}"` : "";
      return (
        `<ellipse class="bv-footprint" cx="${shape.cx}" cy="${shape.cy}" rx="${shape.rx}" ` +
        `ry="${shape.ry}"${transform}></ellipse>`
      );
    };

    // <title> first: a browser picks up an SVG hover tooltip more reliably as
    // the element's first child than its last.
    const markerHtml = (marker) =>
      `<g class="bv-part" data-marker="${esc(marker.id)}" role="button" tabindex="0" ` +
      `aria-pressed="false" aria-label="${esc(marker.label)}">` +
      `<title>${esc(marker.label)}</title>` +
      footprintHtml(marker) +
      `</g>`;

    const decorHtml = (id, detail) =>
      `<g class="bv-decor" data-decor="${esc(id)}" aria-hidden="true">` +
      `<title>${esc(detail.label)}</title>` +
      `<rect class="bv-decor-box" x="${detail.x}" y="${detail.y}" width="${detail.w}" height="${detail.h}" rx="1"></rect>` +
      `</g>`;

    const bodyFaceHtml = (face) =>
      `<svg class="bv-svg" viewBox="0 0 894 894" role="group" aria-label="${esc(face.label)} of the droid">` +
      `<g class="bv-art" aria-hidden="true">${face.id === "front" ? art.front || "" : art.rear || ""}</g>` +
      Object.keys(SURFACE_DETAILS)
        .filter((id) => SURFACE_DETAILS[id].face === face.id)
        .map((id) => decorHtml(id, SURFACE_DETAILS[id]))
        .join("") +
      `<text class="bv-dome-pending" x="${DOME_PENDING_AT[face.id].x}" y="${DOME_PENDING_AT[face.id].y}">` +
      `Dome parts pending</text>` +
      markers
        .filter((marker) => marker.face === face.id)
        .map(markerHtml)
        .join("") +
      `</svg>`;

    const domeFaceHtml = () =>
      `<svg class="bv-svg bv-svg-dome" viewBox="${DOME_VIEWBOX}" role="group" aria-label="Dome, from the top">` +
      DOME_GUIDES.map(
        (guide) =>
          `<circle class="bv-guide${guide.dashed ? " is-dashed" : ""}${guide.hub ? " is-hub" : ""}" ` +
          `cx="${DOME_CENTRE}" cy="${DOME_CENTRE}" r="${guide.r}"></circle>`
      ).join("") +
      `<g class="bv-dome-pieces">` +
      markers
        .filter((marker) => marker.face === "dome")
        .map(markerHtml)
        .join("") +
      `</g>` +
      `</svg>` +
      `<p class="bv-dome-note" hidden></p>`;

    host.innerHTML =
      `<p class="bv-stamp" hidden></p>` +
      `<div class="bv-card">` +
      `<div class="bv-tabs">` +
      FACES.map(
        (face) =>
          `<button type="button" class="bv-tab" data-face-tab="${esc(face.id)}" aria-pressed="false">` +
          `${esc(face.label)}</button>`
      ).join("") +
      `</div>` +
      FACES.map(
        (face) =>
          `<div class="bv-face" data-face="${esc(face.id)}" hidden>` +
          (face.id === "dome" ? domeFaceHtml() : bodyFaceHtml(face)) +
          `</div>`
      ).join("") +
      `<p class="bv-said" hidden></p>` +
      `</div>`;

    // The legend and the Parts list, in the rail. One of each for the whole
    // card: the legend is the same seven words on every face, and the list
    // shows the rows of whichever half the shown face belongs to.
    const legend = document.createElement("div");
    legend.className = "bodyview-panel bv-legend";
    legend.innerHTML =
      `<h3 class="bodyview-panel-title">Legend</h3>` +
      LEGEND_ORDER.map(
        (cls) =>
          `<div class="bv-legend-row"><span class="bv-swatch is-${cls}" aria-hidden="true"></span>` +
          `<span class="bv-legend-word">${esc(LEGEND_TEXT[cls])}</span></div>`
      ).join("");

    const list = document.createElement("div");
    list.className = "bodyview-panel bv-list";
    list.innerHTML =
      `<h3 class="bodyview-panel-title">Parts</h3>` +
      markers
        .map(
          (marker) =>
            `<div class="bv-list-row" data-list-marker="${esc(marker.id)}" hidden>` +
            `<button type="button" class="bv-list-pick" data-pick-marker="${esc(marker.id)}">${esc(marker.label)}</button>` +
            `<span class="bv-swatch" aria-hidden="true"></span>` +
            `</div>`
        )
        .join("") +
      ADDITION_GROUPS.map(
        (group) =>
          `<div class="bv-list-row is-addition" data-add-row="${esc(group.key)}" hidden>` +
          `<span class="bv-list-name">${esc(group.label)}</span>` +
          `<button type="button" class="btn btn-sm" data-add-group="${esc(group.key)}">Add</button>` +
          `</div>`
      ).join("");

    const rail = opts.railHost || null;
    if (rail) {
      rail.appendChild(legend);
      rail.appendChild(list);
    }

    const stamp = host.querySelector(".bv-stamp");
    const said = host.querySelector(".bv-said");
    const domeNote = host.querySelector(".bv-dome-note");
    const domePieces = host.querySelector(".bv-dome-pieces");
    // NodeLists have forEach and not map, so every list read here goes through
    // Array.from first.
    const all = (root, selector) => Array.from(root.querySelectorAll(selector));
    const tabs = new Map(all(host, "[data-face-tab]").map((node) => [node.dataset.faceTab, node]));
    const panels = new Map(all(host, ".bv-face").map((node) => [node.dataset.face, node]));
    const pendings = all(host, ".bv-dome-pending");
    const decors = all(host, "[data-decor]").map((node) => ({
      node,
      detail: SURFACE_DETAILS[node.dataset.decor],
    }));
    const nodes = new Map();
    host.querySelectorAll("[data-marker]").forEach((cell) => {
      nodes.set(cell.dataset.marker, {
        cell,
        title: cell.querySelector("title"),
        marker: byMarker.get(cell.dataset.marker),
      });
    });
    const rows = new Map();
    list.querySelectorAll("[data-list-marker]").forEach((row) => {
      rows.set(row.dataset.listMarker, { row, swatch: row.querySelector(".bv-swatch") });
    });
    const addRows = new Map(all(list, "[data-add-row]").map((row) => [row.dataset.addRow, row]));

    let currentFace = "front";
    // One selection per half, so looking at the dome never drops the body Part
    // a builder had picked, and the other way round.
    const selectedIn = { body: null, dome: null };
    let shownIds = new Set(markers.map((marker) => marker.id));

    const halfNow = () => faceById(currentFace).half;

    const paintSelection = (markerId, on) => {
      const node = nodes.get(markerId);
      if (node) {
        node.cell.classList.toggle("is-selected", on);
        node.cell.setAttribute("aria-pressed", on ? "true" : "false");
      }
      const entry = rows.get(markerId);
      if (entry) entry.row.classList.toggle("is-selected", on);
    };

    // Select a marker, or pass null to clear the shown half's selection. A
    // marker selects within its own half; the other half's pick is untouched.
    const select = (markerId) => {
      const marker = markerId === null || markerId === undefined ? null : byMarker.get(markerId) || null;
      const half = marker ? marker.half : halfNow();
      const next = marker ? marker.id : null;
      if (selectedIn[half] === next) return next;
      if (selectedIn[half] !== null) paintSelection(selectedIn[half], false);
      selectedIn[half] = next;
      if (next !== null) paintSelection(next, true);
      return next;
    };

    const paintRows = () => {
      const half = halfNow();
      rows.forEach((entry, markerId) => {
        const marker = byMarker.get(markerId);
        entry.row.hidden = marker.half !== half || !shownIds.has(markerId);
      });
      addRows.forEach((row, key) => {
        const group = ADDITION_GROUPS.find((each) => each.key === key);
        const placeable = group.ids.every((id) => byMarker.has(id));
        row.hidden = half !== "body" || !placeable || group.ids.every((id) => shownIds.has(id));
      });
    };

    const showFace = (faceId) => {
      const face = faceById(faceId);
      if (!face) return currentFace;
      const changed = face.id !== currentFace;
      currentFace = face.id;
      panels.forEach((panel, id) => {
        panel.hidden = id !== currentFace;
      });
      tabs.forEach((tab, id) => {
        tab.classList.toggle("is-current", id === currentFace);
        tab.setAttribute("aria-pressed", id === currentFace ? "true" : "false");
      });
      paintRows();
      if (changed) onFace(currentFace);
      return currentFace;
    };

    // Only classList, textContent, attributes and `hidden` on nodes that
    // already exist -- never innerHTML, which is the rebuild rule. The
    // selection is deliberately untouched: a repaint that dropped it would take
    // the panel out from under a builder reading it, once a second.
    //
    // @param {object} model
    //   kind     - STATE_KINDS.LIVE (default) or STATE_KINDS.POSE
    //   said     - the POSE sentence, when a caller has a better one
    //   marks    - {markerId: {mark, at?, said?}}; an absent marker is unknown
    //   shown    - marker ids on this droid's picture; absent means every one
    //              that is not a Common Addition
    //   fitted   - marker ids the droid carries; absent means all of them. A
    //              shown marker that is not fitted draws Unfitted.
    //   stamp    - the design line over the picture, or "" for none
    //   domePending - the stated dome's parts are not known: say so over the
    //              crown of both body faces
    //   domeNote - why there is no dome drawing, or "" when there is one
    const update = (model) => {
      const state = model || {};
      const kind = state.kind === STATE_KINDS.POSE ? STATE_KINDS.POSE : STATE_KINDS.LIVE;
      said.hidden = kind === STATE_KINDS.LIVE;
      said.textContent = kind === STATE_KINDS.LIVE ? "" : state.said || KIND_SAID[kind];

      const additionIds = new Set();
      ADDITION_GROUPS.forEach((group) => group.ids.forEach((id) => additionIds.add(id)));
      shownIds = Array.isArray(state.shown)
        ? new Set(state.shown.filter((id) => byMarker.has(id)))
        : new Set(markers.filter((marker) => !additionIds.has(marker.id)).map((marker) => marker.id));
      const fitted = Array.isArray(state.fitted) ? new Set(state.fitted) : null;

      stamp.hidden = !state.stamp;
      stamp.textContent = state.stamp || "";
      stamp.classList.toggle("is-pending", state.domePending === true);
      pendings.forEach((node) => node.classList.toggle("is-absent", state.domePending !== true));
      domeNote.hidden = !state.domeNote;
      domeNote.textContent = state.domeNote || "";
      domePieces.classList.toggle("is-absent", Boolean(state.domeNote));

      // A surface detail is drawn under the Parts, and it is left out wherever
      // a shown Part's box already covers the same ground, so no dashed line
      // pokes out of or sits inside a Part's own rectangle. Checked against
      // what is actually shown rather than a list of ids, so an added arm or a
      // moved placement is still caught.
      decors.forEach(({ node, detail }) => {
        const covered = markers.some(
          (marker) =>
            marker.face === detail.face && shownIds.has(marker.id) && rectsOverlap(detail, marker.geom)
        );
        node.classList.toggle("is-absent", covered);
      });

      const marks = state.marks || {};
      nodes.forEach((node, markerId) => {
        const marker = node.marker;
        const shown = shownIds.has(markerId);
        node.cell.classList.toggle("is-absent", !shown);
        const mark = marks[markerId] || {};
        if (mark.mark !== undefined && MARK_TOKENS.indexOf(mark.mark) === -1) {
          // Never swallowed: a caller handing this a mark the renderer does not
          // draw has a defect, and letting it fall through quietly would draw
          // the part as "the droid has not said yet", which is a different
          // claim about the droid.
          console.error(`[body-view] ${markerId} was given the unknown mark ${mark.mark}`);
        }
        const isFitted = fitted === null || marker.parts.some((id) => fitted.has(id));
        // A holoprojector has no state to draw: it never opens, and a Closed
        // would promise that it could.
        const cls = marker.panTilt && isFitted ? null : markClass(mark, isFitted);
        STATE_CLASSES.forEach((each) => node.cell.classList.toggle(`is-${each}`, each === cls));
        const word = cls === null ? "" : cls === "unknown" ? mark.said || "" : LEGEND_TEXT[cls];
        const describe = word ? `${marker.label} — ${word}` : marker.label;
        node.title.textContent = describe;
        node.cell.setAttribute("aria-label", describe);
        const entry = rows.get(markerId);
        if (entry) {
          STATE_CLASSES.forEach((each) => entry.swatch.classList.toggle(`is-${each}`, each === cls));
          entry.swatch.setAttribute("title", word);
        }
        // A pick on a Part the droid no longer shows is let go, so the panel
        // never describes something the picture does not draw.
        if (!shown && selectedIn[marker.half] === markerId) {
          paintSelection(markerId, false);
          selectedIn[marker.half] = null;
        }
      });
      paintRows();
    };

    const pickFrom = (event) => {
      const target = event && event.target;
      const cell = target && target.closest ? target.closest("[data-marker]") : null;
      if (!cell) return;
      // The panel describes the shown face's half, so a pick always lands on
      // the face it was made on - the same turn the Parts list makes.
      const marker = byMarker.get(cell.dataset.marker);
      if (marker && marker.face !== currentFace) showFace(marker.face);
      onPick(cell.dataset.marker);
    };
    host.querySelectorAll(".bv-svg").forEach((svg) => {
      svg.addEventListener("click", pickFrom);
      // A marker is a button, so it answers the two keys a button answers.
      // Space scrolls the page by default and Enter does not, which is why
      // only one of them is stopped.
      svg.addEventListener("keydown", (event) => {
        if (!event || (event.key !== "Enter" && event.key !== " ")) return;
        if (event.key === " " && typeof event.preventDefault === "function") event.preventDefault();
        pickFrom(event);
      });
    });

    host.querySelector(".bv-tabs").addEventListener("click", (event) => {
      const tab = event.target && event.target.closest ? event.target.closest("[data-face-tab]") : null;
      if (tab) showFace(tab.dataset.faceTab);
    });

    list.addEventListener("click", (event) => {
      const target = event && event.target;
      if (!target || !target.closest) return;
      const pickButton = target.closest("[data-pick-marker]");
      if (pickButton) {
        const marker = byMarker.get(pickButton.dataset.pickMarker);
        // A row can name a Part on the face that is not shown; picking it
        // turns the picture to that face first, so the pick is on screen.
        if (marker && marker.face !== currentFace) showFace(marker.face);
        onPick(pickButton.dataset.pickMarker);
        return;
      }
      const addButton = target.closest("[data-add-group]");
      if (addButton) {
        const group = ADDITION_GROUPS.find((each) => each.key === addButton.dataset.addGroup);
        if (group) onAdd(group);
      }
    });

    showFace(currentFace);
    update({ kind: STATE_KINDS.LIVE });

    return {
      update,
      select,
      // The pick in the half the shown face belongs to, or null.
      selected: () => selectedIn[halfNow()],
      showFace,
      face: () => currentFace,
      partsOf: (markerId) => (byMarker.has(markerId) ? byMarker.get(markerId).parts.slice() : []),
      // What a caller needs to describe a marker without reaching into the
      // markup: its face, half, label, Parts, the dome's command target, and
      // whether it is a holoprojector.
      markerOf: (markerId) => {
        const marker = byMarker.get(markerId);
        if (!marker) return null;
        return {
          id: marker.id,
          face: marker.face,
          half: marker.half,
          label: marker.label,
          parts: marker.parts.slice(),
          target: marker.target || null,
          panTilt: marker.panTilt === true,
        };
      },
      markerIds: () => Array.from(nodes.keys()),
    };
  };

  // ---------------------------------------------------------------------------
  // mountPanel()
  //
  // The selection panel beside the drawing: the one writer on the surface, and
  // the visible explanation ADR 0059 requires at the entrance of a control that
  // moves something.
  //
  // THE ACTS ARE THE CALLER'S, NAMED BY THE CALLER, RUN BY THE CALLER. This
  // builds the buttons and repaints their state; it never decides what one
  // does, which is the same seam the drawing keeps. An act that cannot run now
  // is REFUSED rather than hidden, and says why in one line beside the buttons:
  // #298's rule is that every no names the builder's next move. An act that
  // does not apply to a Part at all -- Open it on a holoprojector -- is not
  // offered, which is a different thing from refused.
  //
  // @param {Element} host
  // @param {object} options
  //   acts  - [{id, label, ariaLabel}] in the order they are offered
  //   onAct - called with the act id when a builder presses one
  // @returns {{show, clear, root}}
  // ---------------------------------------------------------------------------
  const mountPanel = (host, options) => {
    const opts = options || {};
    const acts = Array.isArray(opts.acts) ? opts.acts : [];
    const onAct = typeof opts.onAct === "function" ? opts.onAct : () => {};

    const root = document.createElement("div");
    root.className = "bodyview-panel";

    const title = document.createElement("h3");
    title.className = "bodyview-panel-title";
    root.appendChild(title);

    const subtitle = document.createElement("p");
    subtitle.className = "bodyview-panel-kind";
    root.appendChild(subtitle);

    const facts = document.createElement("dl");
    facts.className = "bodyview-panel-facts";
    root.appendChild(facts);

    const actRow = document.createElement("div");
    actRow.className = "bodyview-panel-acts";
    const buttons = new Map();
    acts.forEach((act) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "btn btn-sm bodyview-act";
      button.dataset.act = act.id;
      button.textContent = act.label;
      if (act.ariaLabel) button.setAttribute("aria-label", act.ariaLabel);
      actRow.appendChild(button);
      buttons.set(act.id, { button, label: act.label });
    });
    root.appendChild(actRow);

    const why = document.createElement("p");
    why.className = "bodyview-panel-why";
    why.setAttribute("role", "status");
    why.setAttribute("aria-live", "polite");
    root.appendChild(why);

    host.appendChild(root);

    actRow.addEventListener("click", (event) => {
      const button = event.target && event.target.closest ? event.target.closest("[data-act]") : null;
      // A browser delivers no click to a disabled button; this is the rule
      // itself, the way every act on Parts states it for its own controls.
      if (!button || button.disabled) return;
      onAct(button.dataset.act);
    });

    // Repainted in place like the drawing, and for the same reason: this panel
    // is redrawn on every frame of a once-a-second feed, and a rebuilt button
    // is a button that moves out from under a finger.
    //
    // @param {object} view - {title, subtitle, facts: [{term, value}], why,
    //   acts: {id: {enabled, shown?, label?}}}. An act is shown unless it says
    //   `shown: false`, and keeps its own label unless it names another.
    const show = (view) => {
      const description = view || {};
      root.hidden = false;
      title.textContent = description.title || "";
      subtitle.textContent = description.subtitle || "";
      subtitle.hidden = !description.subtitle;
      facts.textContent = "";
      (description.facts || []).forEach((fact) => {
        const term = document.createElement("dt");
        term.textContent = fact.term;
        const value = document.createElement("dd");
        value.textContent = fact.value;
        facts.appendChild(term);
        facts.appendChild(value);
      });
      const states = description.acts || {};
      buttons.forEach(({ button, label }, id) => {
        const act = states[id] || {};
        const enabled = act.enabled === true;
        button.hidden = act.shown === false;
        button.textContent = act.label || label;
        button.disabled = !enabled;
        button.setAttribute("aria-disabled", enabled ? "false" : "true");
      });
      why.textContent = description.why || "";
    };

    // Nothing picked: the panel is blank, with no act offered and no prompt
    // (the pick dropped the empty-panel placeholder).
    const clear = (prompt) => {
      root.hidden = !prompt;
      title.textContent = "";
      subtitle.textContent = "";
      subtitle.hidden = true;
      facts.textContent = "";
      buttons.forEach(({ button, label }) => {
        button.textContent = label;
        button.disabled = true;
        button.setAttribute("aria-disabled", "true");
      });
      why.textContent = prompt || "";
    };

    clear();
    return { show, clear, root };
  };

  const api = Object.freeze({
    LEGEND_TEXT,
    LEGEND_ORDER,
    MARKS,
    MARK_TOKENS,
    STATE_KINDS,
    KIND_SAID,
    FACES,
    GEOMETRY,
    SURFACE_DETAILS,
    ADDITION_GROUPS,
    DOME_PIECE_PARTS,
    markClass,
    placementFor,
    mountDrawing,
    mountPanel,
  });

  if (typeof window !== "undefined") {
    window.BodyView = api;
  }

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})();

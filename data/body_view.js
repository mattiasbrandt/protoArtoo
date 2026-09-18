// =============================================================================
// data/body_view.js
//
// The one renderer for a picture of the droid, and the selection panel beside
// it (ADR 0063, #352).
//
// A BODY VIEW IS A STATE DISPLAY, NOT A PART FINDER. Its job is showing many
// parts at once, which a named list cannot do; finding a part is not its job,
// because a body part's name already carries its location in a way a dome
// part's number never does (#317, closed 2026-09-09 under the title "the
// picture is for watching the droid, not for finding parts on it"). So this
// draws enough to read at a glance and not enough to point at, and it is an
// ARRANGEMENT rather than a likeness: a recognisable MK-series silhouette
// would be artwork per design, and would show someone else's droid to the
// builder #333 provided for.
//
// Five rules shape this file. Each one is a decision somebody took, not a
// preference, and each is restated where it is kept.
//
// THE VIEW NEVER WRITES. There is no PAApi call anywhere below and there must
// not be one. The renderer's whole output is "this marker was picked"; what
// that means belongs to the caller, and the selection panel is the only writer
// on the surface. If this file ever knows what its caller will do with a pick,
// the seam is in the wrong place.
//
// ONE KIND OF STATE AT A TIME, AND IT SAYS WHICH. An update carries one `kind`
// -- what the droid was last told, or what a routine says at a chosen moment --
// and the picture draws that kind's sentence on itself, so the answer survives
// a screenshot. Both kinds are commanded intent and they draw IDENTICALLY,
// which is the point: a view that drew them differently would ask a builder to
// remember which half they were looking at, the confusion #298 spent a ticket
// undoing. The seam is what guarantees it -- this file never computes a mark,
// it only draws one, so there is no branch here that could tell the two apart.
//
// NOTHING IS READ BACK FROM A SERVO. Every position below is a width the
// controller put on a pin. A jammed servo reports exactly what a free one does
// (ADR 0045), so no word on this surface may present a mark as where a horn
// actually is, and an Output nothing is driving draws NO position at all --
// absent, not dimmed, because a dimmed position mark is still a claim.
//
// A CLICK ONLY EVER SELECTS. Picking fills the panel beside the drawing with
// the Part's name, its Part Kind, what drives it, and the acts available on it
// as NAMED BUTTONS. One gesture with one meaning, on a surface where most parts
// mid-build are unfitted, unclaimed or both -- and the panel is also the visible
// explanation ADR 0059 requires at the entrance of a control that moves
// something: one slot rather than forty labels, and reachable on a tablet,
// which a `title` is not.
//
// REPAINT IN PLACE, NEVER REBUILD. update() writes classList, textContent and
// style on nodes that already exist and never innerHTML (after
// r2d2-astromech-simulator v1.79.0, src/js/maestro/hw-table.js:174, whose
// per-frame pass touches only those three). Here a rebuild would also drop the
// current selection, which is the thing a builder is holding.
//
// COLOUR IS NOT SPENT HERE AT ALL. #327 reserves colour for two meanings --
// amber for "you can act on this", red for "stopped or refused" -- and neither
// is a fact about where a door is. So a part that is open is drawn OPEN: the
// leaf swings, and the picture shows the position rather than keying it to a
// hue. Part Kind is carried the same way, by treatment (ADR 0063's correction
// to CONTEXT.md, which had said Part Kind "colours a surface"). There is no
// `planned` state on this surface and there is no code below that could draw
// one: a roadmap part is a registry row, and a body view sees only `supported`
// parts the firmware can actually move.
// =============================================================================
(() => {
  "use strict";

  // ---------------------------------------------------------------------------
  // The one nominal layout table
  //
  // The catalog cannot place a body part: all 29 `bearingDeg` values in
  // docs/droid-parts.yaml are dome entries, body entries carry a compass word
  // and nothing else, and the catalog's own header says those words are
  // "position words only until measured on our droid". Body coordinates cannot
  // be taken from the MK4 model either -- that is provenance, not convenience
  // (#317). So a layout lives HERE, on the surface that draws it, and it is
  // ONE table: two of them would be exactly the drift D2 exists to catch.
  //
  // What it is: two elevations, a front and a rear, drawn side by side (#372).
  // Each is a dome band above a body band, and each band is three columns
  // reading left to right as a builder facing that side of the droid sees
  // them. Three catalog facts place a marker and nothing else does:
  //
  //   the compass word's `front`/`rear`  -> which elevation
  //   the compass word's `left`/`right`  -> which column; neither is the centre
  //   the Part's `half`                  -> which band; the section the catalog
  //                                         declares it in, dome or body
  //
  // Within a column, markers stack in catalog order: the words give a
  // quadrant, not a spot, so the order is an arrangement and says nothing about
  // height on the droid. That is the whole of "nominal", and it is deliberately
  // not a likeness.
  //
  // A PART IS ON EXACTLY ONE ELEVATION, OR ON NONE AND REPORTED. Every SLOTS
  // entry names one face, so a placed Part cannot land on both; a word the
  // table does not name, or a half it does not name, goes to `unplaced` rather
  // than onto a face (the disjoint-sections rule after r2d2-astromech-simulator
  // v1.79.0, src/js/app/wiring.js:526 -- "a row must appear exactly once").
  // How many Parts each face carries is a consequence of the catalog's words,
  // never a number this file holds; there are no side elevations, because
  // nothing is left over to draw on one (ADR 0063).
  // ---------------------------------------------------------------------------
  const FACES = [
    { id: "front", label: "Front view" },
    { id: "rear", label: "Rear view" },
  ];
  // Top to bottom on each elevation, keyed by the catalog's `half`. The dome
  // sits on the body, so it is drawn above it; that is the only vertical fact
  // the catalog gives, and the only one this draws.
  const BANDS = [
    { id: "dome", label: "Dome" },
    { id: "body", label: "Body" },
  ];
  const COLUMNS = ["left", "centre", "right"];

  // Where a compass word puts a marker. A word this does not name is a word the
  // catalog has not used; such a Part is reported unplaced rather than dropped
  // somewhere plausible -- "the difference between the map is incomplete and
  // the map is lying" (#317, #293).
  const SLOTS = Object.freeze({
    "front": { face: "front", column: "centre" },
    "front-left": { face: "front", column: "left" },
    "front-right": { face: "front", column: "right" },
    // A part at the side is on the front face's outer column: it is what a
    // builder standing in front of the droid sees at the silhouette's edge, and
    // ADR 0063 draws no side elevation for it to go on.
    "left": { face: "front", column: "left" },
    "right": { face: "front", column: "right" },
    "rear": { face: "rear", column: "centre" },
    "rear-left": { face: "rear", column: "left" },
    "rear-right": { face: "rear", column: "right" },
  });

  // The picture's own numbers, in viewBox units. A marker is a footprint with a
  // leaf hinged on its inner edge; the leaf is what swings when the part is
  // open, so the whole of "what is open" is drawn rather than keyed to a colour.
  const GEOMETRY = Object.freeze({
    columnWidth: 124,
    columnGap: 12,
    markerHeight: 26,
    markerGap: 8,
    facePadding: 14,
    headingHeight: 26,
    // A band's own heading, and the air between the dome band and the body
    // band below it.
    bandHeadingHeight: 20,
    bandGap: 10,
    // The line at the foot of each elevation that says which kind of state it
    // shows, and one line of the legend under both.
    saidHeight: 22,
    legendRowHeight: 22,
    legendSwatchWidth: 36,
    legendSwatchHeight: 14,
    // How far the leaf swings at the open end. Enough to read as open at a
    // glance across a whole droid's worth of markers, and short of a right
    // angle so a swung leaf stays inside its own column rather than landing on
    // the marker beside it.
    maxSwingDeg: 42,
  });

  // ---------------------------------------------------------------------------
  // The marks a caller may hand this renderer
  //
  // The renderer owns the vocabulary and the caller owns which one a Part is
  // in, because working that out needs the Output rows and this file has none.
  // That split is what makes "live state and a routine's moment draw
  // identically" true by construction rather than by care: there is no branch
  // below that can tell one caller's marks from another's.
  //
  // `driven` is the only mark that carries a position, and it carries it as
  // `at` -- a fraction of the travel the builder RECORDED, 0 at the close end
  // and 1 at the open end. An Output nobody has measured has no travel for a
  // fraction to be of, which is why `unmeasured` is its own mark and not a
  // `driven` with a made-up number.
  //
  // `means` is the sentence the legend says for the mark, written beside the
  // token it defines so a mark cannot exist without one and the legend cannot
  // define a mark that is gone -- the drift the reference's hand-written
  // footer shipped (#293's reading of wiring.js:645, and data/wiring.js's
  // TIERS for the same rule on the Wiring sheet).
  // ---------------------------------------------------------------------------
  const MARK_DEFINITIONS = Object.freeze([
    {
      key: "DRIVEN",
      token: "driven",
      means: "An output drives it. The edge swung out is how far open it was told to be.",
    },
    {
      key: "UNMEASURED",
      token: "unmeasured",
      means: "An output drives it, and nobody has measured its ends, so no position is drawn.",
    },
    { key: "LIMP", token: "limp", means: "Its output has let go, so it is told no position at all." },
    { key: "UNDRIVEN", token: "undriven", means: "Nothing drives it yet." },
    { key: "UNKNOWN", token: "unknown", means: "The droid has not said yet." },
  ]);
  const MARKS = Object.freeze(
    MARK_DEFINITIONS.reduce((marks, definition) => {
      marks[definition.key] = definition.token;
      return marks;
    }, {})
  );
  const MARK_TOKENS = Object.freeze(MARK_DEFINITIONS.map((definition) => definition.token));

  // What the legend can say, in the order it says it: every mark, and then the
  // one Part Kind treatment this renderer draws. `swatch` is the class list a
  // legend swatch wears, which is the SAME class list a marker in that state
  // wears -- so the swatch is drawn by the very rules that draw the marker, and
  // cannot come to look different from it.
  const LEGEND = Object.freeze(
    MARK_DEFINITIONS.map((definition) => ({
      id: definition.token,
      swatch: `is-${definition.token}` + (definition.token === "driven" ? " has-position" : ""),
      means: definition.means,
    })).concat([
      {
        id: "light",
        swatch: "is-driven partkind-light",
        means: "A light. It has no travel, so it never swings.",
      },
    ])
  );

  // The two kinds of state this surface can show, and the sentence each one
  // draws on the picture. Both are commanded intent, which is why the two
  // sentences differ only in where the intent came from and never in how
  // certain it is.
  const STATE_KINDS = Object.freeze({
    LIVE: "live",
    POSE: "pose",
  });
  const KIND_SAID = Object.freeze({
    live: "Showing: what the droid was last told",
    pose: "Showing: what this routine says at this moment",
  });

  // Markup is built ONCE, as a string, and every later frame writes onto the
  // nodes that string made -- the shape data/parts.js and
  // data/dome_layout_render.js both use. Deliberately not document.
  // createElementNS(): the picture is parsed as part of the host document, so
  // the browser puts it in the SVG namespace itself, and there is one place
  // (below) where the picture's shape is written rather than fifty
  // createElementNS calls to read it out of.
  const esc = (value) =>
    window.PAUtils && window.PAUtils.escapeHtml
      ? window.PAUtils.escapeHtml(String(value))
      : String(value).replace(/[&<>"']/g, (ch) => `&#${ch.charCodeAt(0)};`);

  // ---------------------------------------------------------------------------
  // placementFor()
  //
  // Which markers this set of Parts makes, and where each one sits. Pure, and
  // exported so a test can read the arrangement without a document.
  //
  // ONE MARKER CAN STAND FOR SEVERAL PARTS, and the catalog says which, so
  // nothing here matches on a name. A holoprojector is a pan Part, a tilt Part
  // and a light Part, because a Part is one thing an Output drives (#320) --
  // and it is one device on the droid, so it is one marker offering its Parts
  // in the panel. Two catalog fields carry that:
  //
  //   `unit` -- the device number within a section, which is what makes
  //   hp1Pan and hp1Tilt one holoprojector and hp2Pan another;
  //   `sitsOn` -- the Part this one is carried by, which is what makes a light
  //   share the marker of the panel it lights.
  //
  // Today that gives a holoprojector marker offering TWO Parts, because the
  // catalog declares no holo light yet (0 light Parts on the body; A1c adds
  // them). The rule is the third Part's, not a count this file holds: the day a
  // light declares `sitsOn: hp1Pan`, the marker offers three with no change
  // here.
  //
  // A Part with no position word, or none of the two halves, is not placed and
  // not dropped either: it comes back in `unplaced`, and C4d (#374) is the
  // slice that names them beside the drawing. `other1`..`other10` have no
  // position word by definition.
  // ---------------------------------------------------------------------------

  // The Part a marker stands on: the one this Part is carried by, following the
  // chain so a light on a panel lands where the panel does. Bounded by a hop
  // count, so a `sitsOn` cycle in a hand-edited catalog stops rather than
  // hanging the page. A Part that is carried by nothing is its own host, which
  // is what makes this the answer for every Part rather than a special case.
  const hostOf = (part, byId) => {
    let host = part;
    let hops = 0;
    while (host.sitsOn && byId.has(host.sitsOn) && hops < 8) {
      host = byId.get(host.sitsOn);
      hops += 1;
    }
    return host;
  };

  // The device a Part belongs to. `unit` is what makes hp1Pan and hp1Tilt one
  // holoprojector and hp2Pan another; a host with no unit is one device on its
  // own. Section as well as unit, so unit 1 of two different sections is two
  // markers.
  const markerKeyFor = (host) =>
    host.unit === undefined || host.unit === null ? host.id : `${host.section}:${host.unit}`;

  const placementFor = (parts) => {
    const rows = Array.isArray(parts) ? parts.filter((part) => part && part.id) : [];
    const byId = new Map(rows.map((part) => [part.id, part]));
    const markers = [];
    const byKey = new Map();
    const unplaced = [];

    rows.forEach((part) => {
      // The host's position word, never the carried Part's: a light takes the
      // position of the thing it is bolted to, which is what stops the two ever
      // disagreeing about where they both are (data/droid_parts.js).
      const host = hostOf(part, byId);
      const key = markerKeyFor(host);
      const slot = SLOTS[host.position];
      // The host's half too, for the same reason: a light is in the band of the
      // panel it is bolted to. A half this table does not name is reported
      // rather than drawn in whichever band is nearest.
      const band = BANDS.find((each) => each.id === host.half);
      if (!slot || !band) {
        unplaced.push(part.id);
        return;
      }
      let marker = byKey.get(key);
      if (!marker) {
        marker = {
          id: key,
          face: slot.face,
          band: band.id,
          column: slot.column,
          parts: [],
          label: "",
          index: 0,
        };
        byKey.set(key, marker);
        markers.push(marker);
      }
      marker.parts.push(part.id);
    });

    // A marker's label is the one thing a reader needs on the picture. One Part
    // gives its own name; several give the shorthand they share where there is
    // one, and the first Part's name otherwise -- never a made-up device name.
    markers.forEach((marker) => {
      const first = byId.get(marker.parts[0]);
      if (marker.parts.length === 1) {
        marker.label = first.shorthand || first.name;
        return;
      }
      const stems = marker.parts
        .map((id) => byId.get(id))
        .map((part) => (part && part.shorthand ? part.shorthand.split("-")[0] : null))
        .filter((text) => text !== null);
      const shared = stems.length === marker.parts.length && new Set(stems).size === 1;
      marker.label = shared ? stems[0] : first.name;
    });

    // Laid out in catalog order within each column of each band, so the
    // arrangement is stable across repaints and across droids: two builders
    // comparing screenshots see the same thing in the same place.
    FACES.forEach((face) => {
      BANDS.forEach((band) => {
        COLUMNS.forEach((column) => {
          let index = 0;
          markers.forEach((marker) => {
            if (marker.face !== face.id || marker.band !== band.id || marker.column !== column) return;
            marker.index = index;
            index += 1;
          });
        });
      });
    });

    return { markers, unplaced };
  };

  // ---------------------------------------------------------------------------
  // mountDrawing()
  //
  // Builds the picture once and hands back the handle its caller repaints it
  // through. The handle is the whole of what a caller may do to the drawing:
  // there is no way from out here to write a class, a label or a position onto
  // a marker except by handing update() a model, which is what keeps the two
  // kinds of state indistinguishable.
  //
  // @param {Element} host - where the picture goes. Written once, here.
  // @param {object} options
  //   parts   - the Part records to draw, from window.DroidParts.parts. WHICH
  //             Parts is the caller's to decide; this draws what it is given.
  //   onPick  - called with the marker id when a builder picks one. The ONLY
  //             thing this renderer ever reports.
  // @returns {{update, select, selected, partsOf, markerIds, unplaced}}
  // ---------------------------------------------------------------------------
  const mountDrawing = (host, options) => {
    const opts = options || {};
    const { markers, unplaced } = placementFor(opts.parts);
    const onPick = typeof opts.onPick === "function" ? opts.onPick : () => {};

    const faceWidth =
      COLUMNS.length * GEOMETRY.columnWidth +
      (COLUMNS.length - 1) * GEOMETRY.columnGap +
      2 * GEOMETRY.facePadding;
    const faceX = (faceIndex) => faceIndex * (faceWidth + GEOMETRY.columnGap);
    const rowPitch = GEOMETRY.markerHeight + GEOMETRY.markerGap;
    const width = FACES.length * faceWidth + (FACES.length - 1) * GEOMETRY.columnGap;

    // A band is drawn when either elevation has something in it, and it is as
    // deep on both as its deepest column on either, so the dome band of the
    // front view lines up with the dome band of the rear one and the eye can
    // cross between them. A band with nothing in it on both faces is not drawn:
    // an empty heading would be a claim about a part of the droid nobody has.
    const bands = BANDS.map((band) => ({
      id: band.id,
      label: band.label,
      depth: markers
        .filter((marker) => marker.band === band.id)
        .reduce((most, marker) => Math.max(most, marker.index + 1), 0),
    })).filter((band) => band.depth > 0);
    const bandTop = new Map();
    let cursor = GEOMETRY.headingHeight + GEOMETRY.facePadding;
    bands.forEach((band) => {
      bandTop.set(band.id, cursor);
      cursor += GEOMETRY.bandHeadingHeight + band.depth * rowPitch + GEOMETRY.bandGap;
    });
    // The sentence saying which kind of state this is rides at the foot of
    // EACH elevation, inside its plate, so a screenshot cropped to one face
    // still says which face it is and what it is showing (after
    // r2d2-astromech-simulator v1.79.0, src/js/app/wiring.js:481, which states
    // scope per diagram because a page gets cropped).
    const saidY = cursor + GEOMETRY.saidHeight - 8;
    const faceHeight = cursor + GEOMETRY.saidHeight + GEOMETRY.facePadding;
    const legendTop = faceHeight + GEOMETRY.bandGap;

    const markerAt = (marker, faceIndex) => {
      const x =
        faceX(faceIndex) +
        GEOMETRY.facePadding +
        COLUMNS.indexOf(marker.column) * (GEOMETRY.columnWidth + GEOMETRY.columnGap);
      const y = bandTop.get(marker.band) + GEOMETRY.bandHeadingHeight + marker.index * rowPitch;
      return { x, y };
    };

    // A marker is a footprint -- where the part is, always drawn, because a part
    // on the droid is on the picture -- and a leaf hinged on the footprint's
    // leading corner. The leaf is what swings when the controller has told the
    // part to be open, so the position is DRAWN rather than keyed to a colour,
    // and it is absent entirely whenever there is no recorded travel for it to
    // be a position within.
    //
    // No `id` attribute anywhere: a document may hold more than one of these
    // pictures at once -- the same reason data/dome_layout_render.js carries
    // identity on a data attribute -- and SVG ids must be unique per document.
    const markerHtml = (marker, faceIndex) => {
      const { x, y } = markerAt(marker, faceIndex);
      return (
        `<g class="bodyview-marker" data-marker="${esc(marker.id)}" role="button" tabindex="0" ` +
        `aria-pressed="false" aria-label="${esc(marker.label)}">` +
        `<rect class="bodyview-footprint" x="${x}" y="${y}" ` +
        `width="${GEOMETRY.columnWidth}" height="${GEOMETRY.markerHeight}" rx="2"></rect>` +
        `<rect class="bodyview-leaf" x="${x}" y="${y}" ` +
        `width="${GEOMETRY.columnWidth}" height="${GEOMETRY.markerHeight}" rx="2"></rect>` +
        `<text class="bodyview-label" x="${x + GEOMETRY.columnWidth / 2}" ` +
        `y="${y + GEOMETRY.markerHeight / 2}">${esc(marker.label)}</text>` +
        `<title>${esc(marker.label)}</title>` +
        `</g>`
      );
    };

    // A band's heading carries its count, read off the markers actually drawn
    // in it (docs/ui-copy-voice.md rule 8) -- which is how the front view comes
    // to say how many body parts it carries without this file ever holding
    // the number.
    const bandHtml = (band, face, faceIndex) => {
      const x0 = faceX(faceIndex);
      const top = bandTop.get(band.id);
      const here = markers.filter((marker) => marker.face === face.id && marker.band === band.id);
      const count = here.reduce((sum, marker) => sum + marker.parts.length, 0);
      const counted =
        count === 0 ? "none on this side" : `${count} ${count === 1 ? "part" : "parts"}`;
      return (
        `<g class="bodyview-band" data-band="${esc(band.id)}">` +
        `<line class="bodyview-band-rule" x1="${x0 + GEOMETRY.facePadding}" y1="${top}" ` +
        `x2="${x0 + faceWidth - GEOMETRY.facePadding}" y2="${top}"></line>` +
        `<text class="bodyview-band-label" x="${x0 + GEOMETRY.facePadding}" ` +
        `y="${top + GEOMETRY.bandHeadingHeight - 7}">${esc(band.label)} · ${counted}</text>` +
        here.map((marker) => markerHtml(marker, faceIndex)).join("") +
        `</g>`
      );
    };

    const faceHtml = (face, faceIndex) => {
      const x0 = faceX(faceIndex);
      return (
        `<g class="bodyview-face" data-face="${esc(face.id)}">` +
        `<rect class="bodyview-face-plate" x="${x0}" y="0" width="${faceWidth}" ` +
        `height="${faceHeight}" rx="4"></rect>` +
        `<text class="bodyview-face-label" x="${x0 + GEOMETRY.facePadding}" ` +
        `y="${GEOMETRY.facePadding + 10}">${esc(face.label)}</text>` +
        bands.map((band) => bandHtml(band, face, faceIndex)).join("") +
        `<text class="bodyview-said" x="${x0 + GEOMETRY.facePadding}" y="${saidY}">` +
        `${esc(KIND_SAID.live)}</text>` +
        `</g>`
      );
    };

    // The legend, under both elevations and inside the viewBox so it travels
    // with a screenshot. Every entry is built here, once, and update() shows
    // exactly the ones the picture is drawing at that moment and no others --
    // generated from the states drawn rather than a fixed list (ADR 0063), and
    // still a repaint in place rather than a rebuild. A swatch wears a marker's
    // own classes, so it is drawn by the rules that draw the marker.
    const legendHtml = (entry) => {
      const w = GEOMETRY.legendSwatchWidth;
      const h = GEOMETRY.legendSwatchHeight;
      const top = (GEOMETRY.legendRowHeight - h) / 2;
      const swing = (GEOMETRY.maxSwingDeg * 0.6).toFixed(1);
      return (
        `<g class="bodyview-legend-item" data-legend="${esc(entry.id)}" display="none">` +
        `<g class="bodyview-marker bodyview-swatch ${esc(entry.swatch)}" aria-hidden="true">` +
        `<rect class="bodyview-footprint" x="0" y="${top}" width="${w}" height="${h}" rx="2"></rect>` +
        `<rect class="bodyview-leaf" x="0" y="${top}" width="${w}" height="${h}" rx="2" ` +
        `transform="rotate(${swing} 0 ${top})"></rect>` +
        `</g>` +
        `<text class="bodyview-legend-text" x="${w + 10}" y="${GEOMETRY.legendRowHeight / 2}">` +
        `${esc(entry.means)}</text>` +
        `</g>`
      );
    };

    host.innerHTML =
      `<svg class="bodyview-svg" viewBox="0 0 ${width} ${faceHeight}" role="group" ` +
      `data-state-kind="${STATE_KINDS.LIVE}">` +
      FACES.map(faceHtml).join("") +
      `<g class="bodyview-legend">` +
      LEGEND.map(legendHtml).join("") +
      `</g>` +
      `</svg>`;

    const svg = host.querySelector(".bodyview-svg");
    // One `said` line per elevation, all written together: one kind of state at
    // a time, so every face says the same sentence.
    const saids = host.querySelectorAll(".bodyview-said");
    const legendNodes = LEGEND.map((entry) => ({
      entry,
      node: host.querySelectorAll("[data-legend]").find((each) => each.dataset.legend === entry.id),
    }));
    const nodes = new Map();
    host.querySelectorAll("[data-marker]").forEach((cell) => {
      const marker = markers.find((each) => each.id === cell.dataset.marker);
      nodes.set(cell.dataset.marker, {
        cell,
        leaf: cell.querySelector(".bodyview-leaf"),
        title: cell.querySelector("title"),
        marker,
      });
    });

    let selectedId = null;

    const select = (markerId) => {
      const next = nodes.has(markerId) ? markerId : null;
      if (selectedId === next) return next;
      if (selectedId !== null && nodes.has(selectedId)) {
        const previous = nodes.get(selectedId).cell;
        previous.classList.remove("is-selected");
        previous.setAttribute("aria-pressed", "false");
      }
      selectedId = next;
      if (selectedId !== null) {
        const cell = nodes.get(selectedId).cell;
        cell.classList.add("is-selected");
        cell.setAttribute("aria-pressed", "true");
      }
      return selectedId;
    };

    // Only classList, textContent and attributes on nodes that already exist --
    // never innerHTML, which is the rebuild rule (hw-table.js:174); an SVG
    // geometry attribute is where a `style` write goes on a drawing. The
    // selection is deliberately untouched: a repaint that dropped it would take
    // the panel out from under a builder reading it, once a second.
    const update = (model) => {
      const state = model || {};
      const kind = state.kind === STATE_KINDS.POSE ? STATE_KINDS.POSE : STATE_KINDS.LIVE;
      saids.forEach((said) => {
        said.textContent = state.said || KIND_SAID[kind];
      });
      svg.setAttribute("data-state-kind", kind);
      const marks = state.marks || {};
      // What this frame actually draws, which is the whole of what the legend
      // may define.
      const drawn = new Set();
      nodes.forEach((node, markerId) => {
        const mark = marks[markerId] || {};
        let token = mark.mark;
        if (MARK_TOKENS.indexOf(token) === -1) {
          if (token !== undefined) {
            // Never swallowed: a caller handing this a mark the renderer does
            // not draw has a defect, and letting it fall through quietly would
            // draw the part as "the droid has not said yet", which is a
            // different claim about the droid.
            console.error(`[body-view] ${markerId} was given the unknown mark ${token}`);
          }
          token = MARKS.UNKNOWN;
        }
        MARK_TOKENS.forEach((each) => node.cell.classList.toggle(`is-${each}`, each === token));
        node.cell.classList.toggle("partkind-light", mark.light === true);
        drawn.add(token);
        if (mark.light === true) drawn.add("light");
        // A position is drawn only where there is a recorded travel for it to
        // be a fraction of. Everything else has NO leaf -- absent, not dimmed,
        // because a dimmed position mark is still a claim.
        const at = token === MARKS.DRIVEN && typeof mark.at === "number" ? mark.at : null;
        node.cell.classList.toggle("has-position", at !== null);
        if (at !== null) {
          const { x, y } = markerAt(node.marker, FACES.findIndex((face) => face.id === node.marker.face));
          const swing = Math.min(1, Math.max(0, at)) * GEOMETRY.maxSwingDeg;
          node.leaf.setAttribute("transform", `rotate(${swing.toFixed(1)} ${x} ${y})`);
        }
        // The marker's accessible name, and the only place its state reaches a
        // screen reader: the picture says it in shape, and this says it in
        // words. Named apart from the picture's own `said` line above, which is
        // the state KIND rather than one marker's state.
        const describe = mark.said ? `${node.marker.label} — ${mark.said}` : node.marker.label;
        node.title.textContent = describe;
        node.cell.setAttribute("aria-label", describe);
      });

      // The legend says the states drawn this frame, in LEGEND's order, and
      // nothing else; the picture grows by exactly the rows it shows.
      let rows = 0;
      legendNodes.forEach(({ entry, node }) => {
        const shown = drawn.has(entry.id);
        node.setAttribute("display", shown ? "inline" : "none");
        if (!shown) return;
        node.setAttribute("transform", `translate(0 ${legendTop + rows * GEOMETRY.legendRowHeight})`);
        rows += 1;
      });
      const height = rows === 0 ? faceHeight : legendTop + rows * GEOMETRY.legendRowHeight;
      svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
    };

    const pick = (event) => {
      const target = event && event.target;
      const cell = target && target.closest ? target.closest("[data-marker]") : null;
      if (!cell) return;
      onPick(cell.dataset.marker);
    };
    svg.addEventListener("click", pick);
    // A marker is a button, so it answers the two keys a button answers. Space
    // scrolls the page by default and Enter does not, which is why only one of
    // them is stopped.
    svg.addEventListener("keydown", (event) => {
      if (!event || (event.key !== "Enter" && event.key !== " ")) return;
      if (event.key === " " && typeof event.preventDefault === "function") event.preventDefault();
      pick(event);
    });

    update({ kind: STATE_KINDS.LIVE });

    return {
      update,
      select,
      selected: () => selectedId,
      partsOf: (markerId) => (nodes.has(markerId) ? nodes.get(markerId).marker.parts.slice() : []),
      markerIds: () => Array.from(nodes.keys()),
      // Reported rather than silently omitted, so C4d can name them beside the
      // drawing: a map that is incomplete and says so beats one that lies.
      unplaced: unplaced.slice(),
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
  // #298's rule is that every no names the builder's next move, and a button
  // that vanished would name nothing at all.
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
      buttons.set(act.id, button);
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
    const show = (view) => {
      const description = view || {};
      root.hidden = false;
      title.textContent = description.title || "";
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
      buttons.forEach((button, id) => {
        const act = states[id] || {};
        const enabled = act.enabled === true;
        button.disabled = !enabled;
        button.setAttribute("aria-disabled", enabled ? "false" : "true");
      });
      why.textContent = description.why || "";
    };

    const clear = (prompt) => {
      root.hidden = false;
      title.textContent = "";
      facts.textContent = "";
      buttons.forEach((button) => {
        button.disabled = true;
        button.setAttribute("aria-disabled", "true");
      });
      why.textContent = prompt || "";
    };

    clear();
    return { show, clear, root };
  };

  const api = Object.freeze({
    MARKS,
    MARK_TOKENS,
    STATE_KINDS,
    KIND_SAID,
    FACES,
    BANDS,
    COLUMNS,
    SLOTS,
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

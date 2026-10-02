// =============================================================================
// data/dome_bearing.js
//
// The Dome Bearing in the browser (ADR 0051, #445): where the dome believes it
// points, and the one marker every dome drawing shows for it.
//
// ONE ACCESSOR. Every drawing of the dome - the Dashboard's, the Parts
// picture's, the Sequences picker's - and the Dome page's readout read the
// bearing through read() here, so no two surfaces can disagree about where the
// dome points. It comes from the status frame's `domeBearing` and
// `domeBearingDeg`, through the Live Reading.
//
// UNKNOWN IS ITS OWN ANSWER. read() is {believed: true, deg} or {believed:
// false, deg: null}; a frame that does not say "believed" with a number is
// unknown, never 0. The marker then takes its own treatment - a dashed rim and
// no pointer - so a lost belief can never draw as "pointing front".
//
// THE DRAWING'S OWN FRAME. Every drawing is the vendored top view, the droid's
// front at the bottom, clockwise on screen being clockwise seen from above (a
// Part at `bearing_deg` 24 is drawn top right of centre, one at 202 bottom
// left). The Dome Bearing is clockwise from above from the droid's front, so the
// pointer is drawn at the bottom and turned about the centre by the bearing:
// 90 lands on the left, where `bearing_deg` 270 is drawn. The drawing never
// turns and the picker's targets never move; only the pointer does.
//
// A renderer writes markerSvg() into its <svg>, or mount()s it onto one it has
// drawn; this module repaints every marker in the document when the bearing
// changes. No text on the drawing.
// =============================================================================
(() => {
  if (window.PADomeBearing) return;

  // The vendored map's frame (data/dome_panel_model.js): 480 x 480, centre
  // 240, the dome's rim at 172.
  const FRAME = Object.freeze({ cx: 240, cy: 240, rim: 172 });

  let reading = Object.freeze({ believed: false, deg: null });

  const fromStatus = (status) => {
    const deg = Number(status?.domeBearingDeg);
    if (status?.domeBearing === "believed" && status?.domeBearingDeg !== null && Number.isFinite(deg)) {
      return Object.freeze({ believed: true, deg: ((deg % 360) + 360) % 360 });
    }
    return Object.freeze({ believed: false, deg: null });
  };

  // The marker for a drawing whose centre and scale may differ from the
  // vendored map's (a dome-served layout, data/dome_layout_render.js): the
  // pointer sits just outside the rim, at the bottom, and is turned by paint().
  const markerSvg = ({ cx = FRAME.cx, cy = FRAME.cy, scale = 1 } = {}) => {
    // An arrow out of the rim, pointing the way the dome faces.
    const rim = FRAME.rim * scale;
    const base = cy + rim + 4 * scale;
    const tip = base + 22 * scale;
    const half = 11 * scale;
    // Drawn as the bearing stands now, so a drawing made between two frames
    // is right before the next repaint.
    const unknown = reading.believed ? "" : " is-unknown";
    const turned = reading.believed ? ` transform="rotate(${reading.deg} ${cx} ${cy})"` : "";
    return (
      `<g class="dome-bearing-marker${unknown}" data-dome-bearing data-cx="${cx}" data-cy="${cy}" aria-hidden="true">` +
      `<circle class="dome-bearing-unknown" cx="${cx}" cy="${cy}" r="${rim + 8 * scale}"></circle>` +
      `<g class="dome-bearing-pointer"${turned}>` +
      `<line class="dome-bearing-tick" x1="${cx}" y1="${cy + rim - 10 * scale}" x2="${cx}" y2="${base}"></line>` +
      `<path class="dome-bearing-head" d="M${cx} ${tip} L${cx - half} ${base} L${cx + half} ${base} Z"></path>` +
      `</g>` +
      `</g>`
    );
  };

  // Add the marker to a drawing already on the page, on top of its pieces,
  // in that drawing's own frame (its viewBox): the Dashboard's dome, which may
  // be the dome's own layout or the vendored map. A drawing that has one keeps
  // it.
  const mount = (svg) => {
    if (!svg || svg.querySelector("[data-dome-bearing]")) return;
    const box = String(svg.getAttribute("viewBox") || "").split(/[\s,]+/).map(Number);
    const frame =
      box.length === 4 && box.every(Number.isFinite) && box[2] > 0 && box[3] > 0
        ? { cx: box[0] + box[2] / 2, cy: box[1] + box[3] / 2, scale: Math.min(box[2], box[3]) / 480 }
        : {};
    svg.insertAdjacentHTML("beforeend", markerSvg(frame));
  };

  const paintOne = (marker) => {
    marker.classList.toggle("is-unknown", !reading.believed);
    const pointer = marker.querySelector(".dome-bearing-pointer");
    if (!pointer) return;
    if (reading.believed) {
      pointer.setAttribute("transform", `rotate(${reading.deg} ${marker.dataset.cx} ${marker.dataset.cy})`);
    } else {
      pointer.removeAttribute("transform");
    }
  };

  // Every marker under `root`, the whole document by default.
  const paint = (root = document) => {
    root.querySelectorAll("[data-dome-bearing]").forEach(paintOne);
  };

  const listeners = new Set();

  const adopt = (status) => {
    const next = fromStatus(status);
    if (next.believed === reading.believed && next.deg === reading.deg) return;
    reading = next;
    paint();
    listeners.forEach((listener) => {
      try {
        listener(reading);
      } catch (error) {
        console.error("[dome-bearing] listener failed", error);
      }
    });
  };

  window.PADomeBearing = Object.freeze({
    read: () => reading,
    markerSvg,
    mount,
    paint,
    // listener(reading) now and on every change; returns the call that stops it.
    subscribe(listener) {
      listeners.add(listener);
      listener(reading);
      return () => listeners.delete(listener);
    },
  });

  window.PALiveReading?.subscribe((live) => adopt(live?.status));
})();

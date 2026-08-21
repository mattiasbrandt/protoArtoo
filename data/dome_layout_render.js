/**
 * data/dome_layout_render.js
 *
 * SVG renderer for dome layout picker from structured geometry.
 * Converts the dome-served layout view-model (window.DomeLayout, see
 * data/dome_layout.js) into an interactive SVG picker matching data/panels.html
 * visual language.
 *
 * Exported: window.DomeLayoutRender
 * Main API: renderPicker(model) -> SVG string
 *
 * Geometry types handled:
 *   - svg_path: d attribute
 *   - circle: cx, cy, r
 *   - ellipse: cx, cy, rx, ry, rotation
 *   - point: cx, cy, r (defaults to MARKER_RADIUS if r not provided)
 *
 * State classes (driven by severity and selectableForNewStep):
 *   - Selectable: .is-ring, .is-pie, role="button", tabindex="0", data-element-id
 *   - Context (non-selectable): .is-fixed, .is-holo, .is-psi, .is-logic
 *   - State: .is-disabled, .is-inactive, .is-unmapped, .is-unverified
 *   - Selection: .selected (toggled by seq.js)
 *
 * Labels: anchored at label_anchor; callouts draw leader + bubble + text inside.
 * Elements with in_layout:false are SKIPPED (not rendered at all).
 */

window.DomeLayoutRender = (() => {
  const MARKER_RADIUS = 6;

  /**
   * Escape attribute values for safe inline XML.
   */
  function escapeAttr(value) {
    if (value === null || value === undefined) {
      return "";
    }
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  /**
   * Convert geometry object to SVG element markup.
   * Pure function; exported for testing.
   *
   * @param {Object} geometry - {type, cx?, cy?, r?, d?, rx?, ry?, rotation?}
   * @returns {string} SVG element string
   */
  function geometryToSvg(geometry) {
    if (!geometry || !geometry.type) {
      return "";
    }

    switch (geometry.type) {
      case "svg_path":
        return `<path d="${escapeAttr(geometry.d)}"/>`;

      case "circle": {
        const cx = Number(geometry.cx);
        const cy = Number(geometry.cy);
        const r = Number(geometry.r);
        if (!Number.isFinite(cx) || !Number.isFinite(cy) || !Number.isFinite(r)) {
          return "";
        }
        return `<circle cx="${cx}" cy="${cy}" r="${r}"/>`;
      }

      case "ellipse": {
        const cx = Number(geometry.cx);
        const cy = Number(geometry.cy);
        const rxVal = geometry.rx !== undefined ? Number(geometry.rx) : 0;
        const ryVal = geometry.ry !== undefined ? Number(geometry.ry) : 0;
        const rotationVal = geometry.rotation !== undefined ? Number(geometry.rotation) : 0;
        if (!Number.isFinite(cx) || !Number.isFinite(cy) || !Number.isFinite(rxVal) || !Number.isFinite(ryVal) || !Number.isFinite(rotationVal)) {
          return "";
        }
        const transform =
          rotationVal !== 0
            ? ` transform="rotate(${rotationVal} ${cx} ${cy})"`
            : "";
        return `<ellipse cx="${cx}" cy="${cy}" rx="${rxVal}" ry="${ryVal}"${transform}/>`;
      }

      case "point": {
        const cx = Number(geometry.cx);
        const cy = Number(geometry.cy);
        const rVal = geometry.r !== undefined ? Number(geometry.r) : MARKER_RADIUS;
        if (!Number.isFinite(cx) || !Number.isFinite(cy) || !Number.isFinite(rVal)) {
          return "";
        }
        return `<circle cx="${cx}" cy="${cy}" r="${rVal}"/>`;
      }

      default:
        return "";
    }
  }

  /**
   * Determine CSS class string for element state.
   * Maps to vendored MK4 SVG class names: .pr (ring), .pp (pie), .pf (fixed), .hp (holo).
   * Severity modifiers (.is-inactive, etc.) layer on top.
   *
   * @param {Object} elem - element from model
   * @returns {string} space-separated class names
   */
  function stateClasses(elem) {
    const classes = [];

    // Element type classes — VENDORED NAMES
    if (elem.element_type === "panel") {
      if (elem.panel_kind === "ring") {
        classes.push("pr");  // ring panel — blue, selectable
      } else if (elem.panel_kind === "pie") {
        classes.push("pp");  // pie panel — darker blue, selectable
      } else if (elem.panel_kind === "fixed") {
        classes.push("pf");  // fixed panel — gray, non-selectable
      }
    } else if (elem.element_type === "holo") {
      classes.push("hp");  // holo marker — green
    } else if (elem.element_type === "psi") {
      classes.push("pf");  // PSI marker — use fixed styling
    } else if (elem.element_type === "logic") {
      classes.push("pf");  // Logic marker — use fixed styling
    }

    // Severity state classes — modifiers layered on base classes
    if (elem.severity === "inactive") {
      classes.push("is-inactive");
    } else if (elem.severity === "disabled") {
      classes.push("is-disabled");
    } else if (elem.severity === "unmapped") {
      classes.push("is-unmapped");
    } else if (elem.severity === "unverified") {
      classes.push("is-unverified");
    }

    return classes.join(" ");
  }

  /**
   * Render a single element's geometry with state-driven styling.
   * Selectable panels get data-element-id; non-selectable get context classes.
   * INLINE construction: all attributes built in one pass (no string-splice injection).
   *
   * @param {Object} elem - element from model
   * @returns {string} SVG markup for the element
   */
  function renderElement(elem) {
    // Skip elements not in layout
    if (!elem.in_layout) {
      return "";
    }

    const geom = elem.geometry;
    if (!geom || !geom.type) {
      return "";
    }

    // Identity and selectability are SEPARATE concerns:
    //  - Every commandable, mapped panel gets data-element-id so seq.js can
    //    reverse-highlight a saved step's target and surface a non-actionable
    //    advisory EVEN when the panel is currently disabled/inactive/unverified.
    //    (A saved ":OPP3" step must still find PP3 in the live picker.)
    //  - data-selectable reflects whether a NEW step may be authored right now
    //    (the editor availability gate); seq.js blocks authoring when "false".
    const isCommandablePanel = elem.element_type === "panel" && elem.mapped;

    const classes = stateClasses(elem);

    // Build attribute string
    let attrs = "";

    // Geometry attributes (type-specific)
    if (geom.type === "svg_path") {
      attrs += ` d="${escapeAttr(geom.d)}"`;
    } else if (geom.type === "circle") {
      const cx = Number(geom.cx);
      const cy = Number(geom.cy);
      const r = Number(geom.r);
      if (!Number.isFinite(cx) || !Number.isFinite(cy) || !Number.isFinite(r)) {
        return "";
      }
      attrs += ` cx="${cx}" cy="${cy}" r="${r}"`;
    } else if (geom.type === "ellipse") {
      const cx = Number(geom.cx);
      const cy = Number(geom.cy);
      const rxVal = geom.rx !== undefined ? Number(geom.rx) : 0;
      const ryVal = geom.ry !== undefined ? Number(geom.ry) : 0;
      const rotationVal = geom.rotation !== undefined ? Number(geom.rotation) : 0;
      if (!Number.isFinite(cx) || !Number.isFinite(cy) || !Number.isFinite(rxVal) || !Number.isFinite(ryVal) || !Number.isFinite(rotationVal)) {
        return "";
      }
      attrs += ` cx="${cx}" cy="${cy}" rx="${rxVal}" ry="${ryVal}"`;
      if (rotationVal !== 0) {
        attrs += ` transform="rotate(${rotationVal} ${cx} ${cy})"`;
      }
    } else if (geom.type === "point") {
      const cx = Number(geom.cx);
      const cy = Number(geom.cy);
      const rVal = geom.r !== undefined ? Number(geom.r) : MARKER_RADIUS;
      if (!Number.isFinite(cx) || !Number.isFinite(cy) || !Number.isFinite(rVal)) {
        return "";
      }
      attrs += ` cx="${cx}" cy="${cy}" r="${rVal}"`;
    }

    // Identity + selectability
    if (isCommandablePanel) {
      attrs +=
        ` data-element-id="${escapeAttr(elem.id)}"` +
        ` data-selectable="${elem.selectableForNewStep ? "true" : "false"}"` +
        ` role="button" tabindex="0"`;
      // Non-actionable-but-identified panels (disabled/inactive/unverified) are
      // still focusable so the operator can read the advisory, but marked disabled.
      if (!elem.selectableForNewStep) {
        attrs += ` aria-disabled="true"`;
      }
    }

    // CSS classes
    if (classes) {
      attrs += ` class="${classes}"`;
    }

    // Note: no bare `id` attribute here — a step's fields container can render
    // more than one instance of this picker at once (multiple expanded panel
    // steps), and SVG element ids must be unique per document. data-element-id
    // already carries identity for selection/highlighting.

    // Build title for accessibility
    let titleText = escapeAttr(elem.id);
    if (elem.label && elem.label !== elem.id) {
      titleText += ` — ${escapeAttr(elem.label)}`;
    }
    if (elem.severity && elem.severity !== "null") {
      titleText += ` (${elem.severity})`;
    }

    // Build the complete element tag (inline, no injection)
    let tag = "";
    switch (geom.type) {
      case "svg_path":
        tag = `<path${attrs}><title>${titleText}</title></path>`;
        break;
      case "circle":
      case "point":
        tag = `<circle${attrs}><title>${titleText}</title></circle>`;
        break;
      case "ellipse":
        tag = `<ellipse${attrs}><title>${titleText}</title></ellipse>`;
        break;
    }

    return tag;
  }

  /**
   * Render a label at the given anchor position.
   * If callout is provided, use callout bubble style; otherwise inline.
   *
   * @param {Object} elem - element from model
   * @returns {string} SVG markup for label + optional callout
   */
  function renderLabel(elem) {
    if (!elem.label_anchor) {
      return "";
    }

    const xVal = Number(elem.label_anchor.x);
    const yVal = Number(elem.label_anchor.y);
    if (!Number.isFinite(xVal) || !Number.isFinite(yVal)) {
      return "";
    }

    // Determine label CSS class based on element type and state
    let labelClass = "lf";
    if (elem.element_type === "panel") {
      if (elem.panel_kind === "pie") {
        // Use dark text (.lpf) for unserviced pies (unverified, disabled, inactive)
        // Use light text (.lp) for normal pies
        labelClass =
          elem.severity === "unverified" || elem.severity === "disabled" || elem.severity === "inactive"
            ? "lpf"
            : "lp";
      } else if (elem.panel_kind === "fixed") {
        labelClass = "lf2";
      }
    } else if (elem.element_type === "holo") {
      labelClass = "lh";
    }

    let svg = "";

    if (elem.callout) {
      const cx_callout = Number(elem.callout.x);
      const cy_callout = Number(elem.callout.y);
      const r_callout = Number(elem.callout.r);
      const connectorTo = elem.callout.connector_to || {};
      const cx_connector = Number(connectorTo.x);
      const cy_connector = Number(connectorTo.y);

      // Validate all callout coordinates
      if (!Number.isFinite(cx_callout) || !Number.isFinite(cy_callout) || !Number.isFinite(r_callout) ||
          !Number.isFinite(cx_connector) || !Number.isFinite(cy_connector)) {
        return "";
      }

      // Determine line and bubble classes based on element type
      let lineClass = "conn-f";
      let bubbleClass = "cf";
      if (elem.element_type === "panel" && elem.panel_kind === "ring") {
        lineClass = "conn-r";
        bubbleClass = "cr";
        labelClass = "lt";
      }

      // Draw connector line from callout to element
      svg += `<line x1="${cx_callout}" y1="${cy_callout}" x2="${cx_connector}" y2="${cy_connector}" class="${lineClass}"/>`;

      // Draw callout bubble
      svg += `<circle class="${bubbleClass}" cx="${cx_callout}" cy="${cy_callout}" r="${r_callout}"/>`;
    }

    // Draw label text
    svg += `<text class="${labelClass}" x="${xVal}" y="${yVal}">${escapeAttr(elem.label)}</text>`;

    return svg;
  }

  /**
   * Main renderer: convert layout model to SVG string.
   * Renders in layers: scaffolding (background) → geometry (sorted) → labels.
   * Skips elements with in_layout:false.
   *
   * @param {Object} model - {source, runtimeVerified, warning, viewBox, elements: [...]}
   * @returns {string} Complete SVG markup
   */
  function renderPicker(model) {
    if (!model || !model.elements || !model.viewBox) {
      return "";
    }

    // Parse viewBox to derive dome center and a scale factor for scaffolding.
    // For the MK4 dome: "0 0 480 480" → center (240, 240), scale 1.
    // Per ADR 0009, custom dome layout templates (layout_source "custom") are
    // a real schema-1 feature, not just MK4 — a differently sized viewBox must
    // scale scaffolding proportionally rather than draw fixed MK4 pixel radii
    // into whatever space the template declares.
    const vbParts = model.viewBox.split(/[\s,]+/).map(Number);
    let centerX = 240, centerY = 240, scale = 1;
    let safeViewBox = model.viewBox;
    // Validate viewBox: must be exactly 4 finite numbers
    if (vbParts.length !== 4 || !vbParts.every(Number.isFinite) || vbParts[2] <= 0 || vbParts[3] <= 0) {
      // Use safe default if viewBox is invalid
      safeViewBox = "0 0 480 480";
    } else {
      centerX = vbParts[0] + vbParts[2] / 2;
      centerY = vbParts[1] + vbParts[3] / 2;
      scale = Math.min(vbParts[2], vbParts[3]) / 480;
    }

    // Scaffolding radii, expressed as MK4-reference pixel values (480x480
    // viewBox) and scaled to the model's actual viewBox above.
    const r_dbg = 172 * scale;    // dome background circle
    const r_pbg = 119 * scale;    // pie backing circle
    const r_rl_outer = 172 * scale;  // outer ring guide
    const r_rl_inner_dashed = 146 * scale;  // inner dashed ring guide
    const r_rl_inner = 54 * scale;   // inner circle guide
    const r_hub = 22 * scale;     // center hub

    // Render scaffolding layer (before elements, so it sits in background)
    const scaffolding = `<circle class="dbg" cx="${centerX}" cy="${centerY}" r="${r_dbg}"/>
<circle class="pbg" cx="${centerX}" cy="${centerY}" r="${r_pbg}"/>
<circle class="rl" cx="${centerX}" cy="${centerY}" r="${r_rl_outer}"/>
<circle class="rl" cx="${centerX}" cy="${centerY}" r="${r_rl_inner_dashed}" stroke-dasharray="3,2"/>
<circle class="rl" cx="${centerX}" cy="${centerY}" r="${r_rl_inner}"/>
<circle class="pf" cx="${centerX}" cy="${centerY}" r="${r_hub}"/>`;

    // Drop in_layout:false elements up front so BOTH the geometry and label
    // layers skip them uniformly (a missed label layer would otherwise leak an
    // excluded element's text), then sort by render_order (ascending).
    const sortedElements = [...model.elements]
      .filter((elem) => elem.in_layout)
      .sort((a, b) => (a.render_order || 0) - (b.render_order || 0));

    // Render geometry layer (sorted by render_order)
    let geometryLayer = "";
    for (const elem of sortedElements) {
      geometryLayer += renderElement(elem);
    }

    // Render labels and callouts (after geometry for z-order)
    let labelLayer = "";
    for (const elem of sortedElements) {
      labelLayer += renderLabel(elem);
    }

    // Inline CSS styling (vendored MK4 palette + state modifiers)
    const css = `
.dbg{fill:#b8bec8;stroke:#6b7280;stroke-width:1.5}
.pbg{fill:#c4c9d4}
.pr{fill:#1e3a8a;stroke:#3b82f6;stroke-width:1;cursor:pointer;transition:fill .15s;pointer-events:auto}
.pr:hover,.pr.open{fill:#ea580c}
.pr.selected{fill:#ea580c;stroke:#fff;stroke-width:2;filter:drop-shadow(0 0 3px rgba(234, 88, 12, 0.9))}
.pp{fill:#1e3a6e;stroke:#60a5fa;stroke-width:1;cursor:pointer;transition:fill .15s;pointer-events:auto}
.pp:hover,.pp.open{fill:#ea580c}
.pp.selected{fill:#ea580c;stroke:#fff;stroke-width:2;filter:drop-shadow(0 0 3px rgba(234, 88, 12, 0.9))}
.pe{fill:#c4c9d4;stroke:#9ca3af;stroke-width:.5}
.pu{fill:#cdd3dd;stroke:#6b7280;stroke-width:1;cursor:pointer;transition:fill .15s;pointer-events:auto}
.pu:hover,.pu.open{fill:#9aa3b2}
.pu.selected{fill:#7b8494;stroke:#fff;stroke-width:2;filter:drop-shadow(0 0 3px rgba(123, 132, 148, 0.9))}
.pf{fill:#aeb6c2;stroke:#4b5563;stroke-width:1;pointer-events:none}
.rl{stroke:#6b7280;stroke-width:.8;fill:none;pointer-events:none}
.cr{fill:#0b1220;stroke:#3b82f6;stroke-width:2;cursor:pointer;pointer-events:auto}
.cr:hover{fill:#0f1e40}
.cr.selected{fill:#0f1e40;stroke:#fff;stroke-width:2.5;filter:drop-shadow(0 0 3px rgba(59, 130, 246, 0.9))}
.cf{fill:#7e8aa0;stroke:#3a4250;stroke-width:1.2;pointer-events:none}
.conn-r{stroke:#3b82f6;stroke-width:.8;opacity:.6;pointer-events:none}
.conn-f{stroke:#4b5563;stroke-width:.7;opacity:.5;pointer-events:none}
.lt{font:bold 11px monospace;fill:#fff;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lp{font:bold 12px monospace;fill:#eaf2ff;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lpf{font:bold 12px monospace;fill:#0b1220;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lf{font:bold 10px sans-serif;fill:#0b1220;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.lf2{font:bold 9px sans-serif;fill:#0b1220;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.hp{fill:#16a34a;stroke:#fff;stroke-width:.6;pointer-events:none}
.lh{font:bold 8px monospace;fill:#fff;text-anchor:middle;dominant-baseline:middle;pointer-events:none}
.is-inactive{opacity:.6}
.is-disabled{opacity:.5;cursor:not-allowed}
.is-unmapped,.is-unverified{opacity:.4}
    `.trim();

    // Assemble final SVG: scaffolding → geometry → labels
    const svg = `<svg viewBox="${safeViewBox}" xmlns="http://www.w3.org/2000/svg" class="dome-svg-picker" style="width:100%;max-width:100%;display:block;margin:0 auto">
<style>
${css}
</style>
${scaffolding}
${geometryLayer}
${labelLayer}
</svg>`;

    return svg;
  }

  // Public API
  return {
    renderPicker,
    geometryToSvg,
    escapeAttr,
    MARKER_RADIUS,
  };
})();

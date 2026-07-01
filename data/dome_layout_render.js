/**
 * data/dome_layout_render.js
 *
 * SVG renderer for dome layout picker from structured geometry.
 * Converts the dome-served layout view-model (from Slice C window.DomeLayout)
 * into an interactive SVG picker matching data/panels.html visual language.
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

      case "circle":
        return `<circle cx="${geometry.cx}" cy="${geometry.cy}" r="${geometry.r}"/>`;

      case "ellipse": {
        const rx = geometry.rx || 0;
        const ry = geometry.ry || 0;
        const rotation = geometry.rotation || 0;
        const transform =
          rotation !== 0
            ? ` transform="rotate(${rotation} ${geometry.cx} ${geometry.cy})"`
            : "";
        return `<ellipse cx="${geometry.cx}" cy="${geometry.cy}" rx="${rx}" ry="${ry}"${transform}/>`;
      }

      case "point": {
        const r = geometry.r !== undefined ? geometry.r : MARKER_RADIUS;
        return `<circle cx="${geometry.cx}" cy="${geometry.cy}" r="${r}"/>`;
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
      attrs += ` cx="${geom.cx}" cy="${geom.cy}" r="${geom.r}"`;
    } else if (geom.type === "ellipse") {
      const rx = geom.rx || 0;
      const ry = geom.ry || 0;
      const rotation = geom.rotation || 0;
      attrs += ` cx="${geom.cx}" cy="${geom.cy}" rx="${rx}" ry="${ry}"`;
      if (rotation !== 0) {
        attrs += ` transform="rotate(${rotation} ${geom.cx} ${geom.cy})"`;
      }
    } else if (geom.type === "point") {
      const r = geom.r !== undefined ? geom.r : MARKER_RADIUS;
      attrs += ` cx="${geom.cx}" cy="${geom.cy}" r="${r}"`;
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

    // Element ID
    if (elem.id) {
      attrs += ` id="${escapeAttr(elem.id)}"`;
    }

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

    const { x, y } = elem.label_anchor;

    // Determine label CSS class based on element type and state
    let labelClass = "lf";
    if (elem.element_type === "panel") {
      if (elem.panel_kind === "pie") {
        labelClass = elem.severity === "unverified" ? "lpf" : "lp";
      } else if (elem.panel_kind === "fixed") {
        labelClass = "lf2";
      }
    } else if (elem.element_type === "holo") {
      labelClass = "lh";
    }

    let svg = "";

    if (elem.callout) {
      const { x: cx_callout, y: cy_callout, r: r_callout } = elem.callout;
      const { x: cx_connector, y: cy_connector } = elem.callout.connector_to || {};

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
    svg += `<text class="${labelClass}" x="${x}" y="${y}">${escapeAttr(elem.label)}</text>`;

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

    // Parse viewBox to derive dome center and radii for scaffolding
    // For the MK4 dome: "0 0 480 480" → center (240, 240)
    const vbParts = model.viewBox.split(/[\s,]+/).map(Number);
    let centerX = 240, centerY = 240;
    if (vbParts.length >= 4) {
      centerX = vbParts[0] + vbParts[2] / 2;
      centerY = vbParts[1] + vbParts[3] / 2;
    }

    // Scaffolding radii (from vendored SVG)
    const r_dbg = 172;    // dome background circle
    const r_pbg = 119;    // pie backing circle
    const r_rl_outer = 172;  // outer ring guide
    const r_rl_inner_dashed = 146;  // inner dashed ring guide
    const r_rl_inner = 54;   // inner circle guide
    const r_hub = 22;     // center hub

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
    const svg = `<svg viewBox="${model.viewBox}" xmlns="http://www.w3.org/2000/svg" class="dome-svg-picker" style="width:100%;max-width:100%;display:block;margin:0 auto">
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

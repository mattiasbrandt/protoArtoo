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
   * Encodes element_type, panel_kind, and severity.
   *
   * @param {Object} elem - element from model
   * @returns {string} space-separated class names
   */
  function stateClasses(elem) {
    const classes = [];

    // Element type classes
    if (elem.element_type === "panel") {
      if (elem.panel_kind === "ring") {
        classes.push("is-ring");
      } else if (elem.panel_kind === "pie") {
        classes.push("is-pie");
      } else if (elem.panel_kind === "fixed") {
        classes.push("is-fixed");
      }
    } else if (elem.element_type === "holo") {
      classes.push("is-holo");
    } else if (elem.element_type === "psi") {
      classes.push("is-psi");
    } else if (elem.element_type === "logic") {
      classes.push("is-logic");
    }

    // Severity state classes
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
   *
   * @param {Object} elem - element from model
   * @returns {string} SVG markup for the element
   */
  function renderElement(elem) {
    // Skip elements not in layout
    if (!elem.in_layout) {
      return "";
    }

    const geomSvg = geometryToSvg(elem.geometry);
    if (!geomSvg) {
      return "";
    }

    // Determine if element is selectable
    const isSelectable =
      elem.element_type === "panel" &&
      elem.selectableForNewStep;

    const classes = stateClasses(elem);

    // For path/circle/ellipse, inherit classes from wrapper; for point, apply to circle
    let elementAttrs = "";
    if (isSelectable) {
      elementAttrs = ` data-element-id="${escapeAttr(elem.id)}" role="button" tabindex="0"`;
    }
    if (classes) {
      elementAttrs += ` class="${classes}"`;
    }
    if (elem.id) {
      elementAttrs += ` id="${escapeAttr(elem.id)}"`;
    }

    // Add title for accessibility
    let title = `<title>${escapeAttr(elem.id)}`;
    if (elem.label && elem.label !== elem.id) {
      title += ` — ${escapeAttr(elem.label)}`;
    }
    if (elem.severity && elem.severity !== "null") {
      title += ` (${elem.severity})`;
    }
    title += "</title>";

    // Inject attributes into the geometry SVG
    // Simple approach: insert after the opening tag
    const tagEndIdx = geomSvg.indexOf(">");
    if (tagEndIdx > 0) {
      const openTag = geomSvg.substring(0, tagEndIdx);
      const rest = geomSvg.substring(tagEndIdx);
      return openTag + elementAttrs + ">" + title + rest.substring(1);
    }

    return "";
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
   * Sorts elements by render_order for correct paint order.
   * Skips elements with in_layout:false.
   *
   * @param {Object} model - {source, runtimeVerified, warning, viewBox, elements: [...]}
   * @returns {string} Complete SVG markup
   */
  function renderPicker(model) {
    if (!model || !model.elements || !model.viewBox) {
      return "";
    }

    // Sort by render_order (ascending)
    const sortedElements = [...model.elements].sort(
      (a, b) => (a.render_order || 0) - (b.render_order || 0)
    );

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

    // Inline CSS styling (adapted from vendored SVG)
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

    // Assemble final SVG
    const svg = `<svg viewBox="${model.viewBox}" xmlns="http://www.w3.org/2000/svg" class="dome-svg-picker" style="width:100%;max-width:100%;display:block;margin:0 auto">
<style>
${css}
</style>
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

// =============================================================================
// data/product_art.js
//
// One picture frame for a product or design card, and the one lookup behind
// it (ADR 0065): the asset set's line drawing when the page inlined a symbol
// `art-<id>`, else the photograph `/<id>.webp`, else nothing - in a frame the
// same size whatever fills it. Never asking which set the image was built
// with: the document answers that.
//
// Two pickers draw pictures, the Component Picker's product cards and the
// Droid Build's design cards (#369), and a second copy of this lookup would
// be a second answer to keep in step.
// =============================================================================
(() => {
  "use strict";

  // The frame is decorative: every card names its product or design in text.
  const frame = (id) => {
    const box = document.createElement("span");
    box.className = "component-card-art";
    box.setAttribute("aria-hidden", "true");
    if (!id) return box;
    if (document.getElementById(`art-${id}`)) {
      const svgNs = "http://www.w3.org/2000/svg";
      const svg = document.createElementNS(svgNs, "svg");
      svg.setAttribute("viewBox", "0 0 400 300");
      svg.setAttribute("focusable", "false");
      const use = document.createElementNS(svgNs, "use");
      use.setAttribute("href", `#art-${id}`);
      svg.appendChild(use);
      box.appendChild(svg);
      return box;
    }
    const image = document.createElement("img");
    image.alt = "";
    // A photograph this set does not carry leaves the frame empty, never
    // dimmed: a missing picture is a cosmetic gap, not a part the board
    // cannot take (CONTEXT.md "Component Picker").
    image.onerror = () => image.remove();
    // Image fetches wait for the one-shot deferred-asset sweep, so the event
    // stream opens before they compete for the controller's connections; a
    // card drawn after that sweep sets its source directly (#202).
    if (window.PAAssetsReady) image.src = `/${id}.webp`;
    else image.dataset.deferredSrc = `/${id}.webp`;
    box.appendChild(image);
    return box;
  };

  window.PAProductArt = { frame };
})();

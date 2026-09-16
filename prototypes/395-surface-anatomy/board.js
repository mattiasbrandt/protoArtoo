// =============================================================================
// #398 - which board this mockup is showing, and everything that follows it.
//
// protoArtoo ships two Body Controllers, and the picture a product card draws
// is not the same on both. The asset set is a BUILD flag, one per board:
// platformio.ini:359 gives env:artoo_esp32 `custom_asset_set = legacy` (line
// drawings) and :522 gives env:firebeetle2_bringup `custom_asset_set = default`
// (photographs), which [env:firebeetle2] extends. tools/gzip_fsdata.py:243-269
// resolves the set and flattens it onto the image root, so the shipped page
// asks for /<id>.webp and never for a set path.
//
// The real image therefore has no runtime switch at all: it carries one set and
// that is that. The control this file draws is a REVIEW CONVENIENCE so the two
// can be compared side by side without two builds; the README says so.
//
// What follows the switch is only what genuinely differs on the two boards -
// the product art, the board picture, the Body Controller card, and the one
// line of copy that turns on PA_CAP_DEDICATED_AUDIO_UART. Everything else is
// identical on both and stays identical here; a mockup that invents a
// difference teaches the sweep a false rule.
// =============================================================================
(() => {
  // The two Body Controllers. Every value is read from the tree, not composed:
  //   assetSet     platformio.ini:359 (legacy) and :522 (default)
  //   label        data/setup.js:396-399, BOARD_LABELS - what the shipped
  //                surface calls the BOARD. It is not the product name's stem:
  //                on artoo_esp32 the board is the "Artoo Controller" and the
  //                product is the "Artoo PCB (artoo.uk)", and the switch is
  //                choosing a board, so it takes the board's own word
  //   productId    data/setup.js:404-407, BOARD_PRODUCT_IDS - a board's
  //                pictures are filed under its Component Registry id, and for
  //                the Artoo PCB the two tokens differ
  //   productName  include/component_registry.inc:106-107, the `included`
  //                rows the registry gates on PA_BOARD. A1d proved these two
  //                are the ONLY `included` flags that differ across the whole
  //                21-product vocabulary.
  const BOARDS = {
    artoo_esp32: {
      assetSet: "legacy",
      label: "Artoo Controller",
      productId: "artoo_pcb",
      productName: "Artoo PCB (artoo.uk)",
    },
    firebeetle2: {
      assetSet: "default",
      label: "FireBeetle 2",
      productId: "firebeetle2",
      productName: "FireBeetle 2 (ESP32-P4)",
    },
  };
  const DEFAULT_BOARD = "artoo_esp32";

  // What each set holds, said in two words on the switch so the review control
  // explains itself without a hover (docs/ui-copy-voice.md: no hover-only
  // explanation). data/asset-sets/legacy/_README.md is where the pair is named.
  const SET_HOLDS = { legacy: "drawings", default: "photographs" };

  // tools/gzip_fsdata.py flattens the active set onto the image root, so the
  // shipped page asks for `/dy_sv5w.webp`. Nothing is flattened here, so the
  // mockup names the tracked source directory instead - the same file, two
  // directories up. Referenced rather than copied: data/ and prototypes/ are
  // both tracked and move together, so twenty binaries never need duplicating.
  const PHOTO_ROOT = "../../data/asset-sets/default/";

  const params = new URLSearchParams(location.search);
  const requested = params.get("board");
  const id = Object.hasOwn(BOARDS, requested) ? requested : DEFAULT_BOARD;
  const board = BOARDS[id];

  // Stamped on <html> from the head, before anything paints, so the CSS rule
  // that hides the other board's copy (`[data-board-only]`) is in force for the
  // first frame rather than after a flash of both.
  document.documentElement.dataset.board = id;
  document.documentElement.dataset.assetSet = board.assetSet;

  // The same page, the other board, keeping whichever view the hash names.
  const hrefFor = (other) =>
    `${location.href.split("#")[0].split("?")[0]}?board=${other}${location.hash}`;

  // The product's own rule, flipped by hand (data/setup.js:457-466): if the
  // document defines #art-<productId> the card draws that symbol, otherwise it
  // falls back to the photograph. The legacy set supplies the symbols
  // (data/asset-sets/legacy/_product_art.html, 20 of them); the default set's
  // partial is deliberately empty of art and says so in its own comment. So
  // removing the sprite below IS choosing the default set, not a second rule.
  const artFor = (productId) =>
    document.getElementById(`art-${productId}`)
      ? `<svg aria-hidden="true" focusable="false"><use href="#art-${productId}"/></svg>`
      : `<img src="${PHOTO_ROOT}${productId}.webp" alt="">`;

  const PAGE_LINK = /^[\w.-]+\.html(#.*)?$/;

  document.addEventListener("DOMContentLoaded", () => {
    // Choosing the set: on the default set the document holds no drawings, so
    // every card falls through to its photograph on the next line.
    if (board.assetSet !== "legacy") {
      document.getElementById("product-art")?.remove();
    }

    // A card's picture. `data-product` names the Component Registry id;
    // `data-product="board"` is the running board's own, which is what makes
    // the board picture follow the same fallback as every other card
    // (#382 reclaimed firebeetle2.webp for exactly this).
    for (const slot of document.querySelectorAll("[data-product]")) {
      const productId = slot.dataset.product === "board" ? board.productId : slot.dataset.product;
      slot.innerHTML = artFor(productId);
    }

    // Carry the switch across the mockup's own links, so walking from Setup to
    // Parts does not quietly land back on the other board.
    for (const a of document.querySelectorAll("a[href]")) {
      const href = a.getAttribute("href");
      if (!PAGE_LINK.test(href)) continue;
      const [path, hash] = href.split("#");
      a.setAttribute("href", `${path}?board=${id}${hash ? `#${hash}` : ""}`);
    }
  });

  window.PABoard = { id, board, boards: BOARDS, setHolds: SET_HOLDS, hrefFor };
})();

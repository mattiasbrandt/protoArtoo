// #398 - the mockup's own small browser check.
//
// Paste the function below into the Playwright MCP browser_evaluate tool (or
// the browser console) on each of dashboard.html, parts.html, setup.html,
// setup.html#board and setup.html#receipt, once at 1440 px and once at 820 px
// wide, and on BOTH boards (?board=artoo_esp32 and ?board=firebeetle2). It
// answers the ticket's browser criteria and nothing else:
//
//   overflow     false when the document is no wider than the viewport
//   plateCells   8, the Status Plate's fixed cells
//   estopInView  true when the STOP control is inside the viewport at the top
//                of the page (it is sticky chrome, so it stays there)
//   plateInView  true when the plate's bottom edge is inside the viewport
//   h1           the page title, one per page
//   emoji        0 - no emoji anywhere in the rendered text (ADR 0066)
//   board, set   which Body Controller this render is showing and which asset
//                set it carries, so a run over both boards is legible
//   art          how many pictures rendered as a drawing and how many as a
//                photograph - the two are never mixed on one render, because
//                a build carries one set
//   otherBoard   0 - none of the other board's copy is on the screen
//
// Static checks that need the file system - no colour literal outside :root,
// no emoji in the sources - are check.py beside this file.
(() => {
  const estop = document.querySelector(".estop").getBoundingClientRect();
  const plate = document.querySelector(".plate-region").getBoundingClientRect();
  const emoji = (document.body.innerText.match(/\p{Extended_Pictographic}/gu) || []).length;
  const art = document.querySelectorAll("[data-product]");
  // On the screen, not merely in the document: the other board's copy is in
  // the markup on purpose so both readings stay legible in the source, and
  // anatomy.css removes whichever one the switch is not on.
  const onScreen = (el) => el.offsetParent !== null;
  return {
    page: document.body.dataset.page,
    hash: location.hash,
    board: document.documentElement.dataset.board,
    set: document.documentElement.dataset.assetSet,
    art: `${[...art].filter((a) => a.querySelector("svg")).length} drawn / ${[...art].filter((a) => a.querySelector("img")).length} photo`,
    otherBoard: [...document.querySelectorAll("[data-board-only]")]
      .filter((el) => el.dataset.boardOnly !== document.documentElement.dataset.board && onScreen(el)).length,
    width: innerWidth,
    scrollWidth: document.documentElement.scrollWidth,
    overflow: document.documentElement.scrollWidth > innerWidth,
    plateCells: document.querySelectorAll(".plate .chip").length,
    estopInView: estop.top >= 0 && estop.bottom <= innerHeight,
    plateInView: plate.bottom <= innerHeight && plate.top >= 0,
    h1: Array.from(document.querySelectorAll("h1")).filter((h) => h.offsetParent !== null).map((h) => h.textContent.trim()),
    emoji,
  };
})();

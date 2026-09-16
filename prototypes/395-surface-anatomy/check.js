// #398 - the mockup's own small browser check.
//
// Paste the function below into the Playwright MCP browser_evaluate tool (or
// the browser console) on each of dashboard.html, parts.html, setup.html and
// setup.html#receipt, once at 1440 px and once at 820 px wide. It answers the
// ticket's browser criteria and nothing else:
//
//   overflow     false when the document is no wider than the viewport
//   plateCells   8, the Status Plate's fixed cells
//   estopInView  true when the STOP control is inside the viewport at the top
//                of the page (it is sticky chrome, so it stays there)
//   plateInView  true when the plate's bottom edge is inside the viewport
//   h1           the page title, one per page
//   emoji        0 - no emoji anywhere in the rendered text (ADR 0066)
//
// Static checks that need the file system - no colour literal outside :root,
// no emoji in the sources - are check.py beside this file.
(() => {
  const estop = document.querySelector(".estop").getBoundingClientRect();
  const plate = document.querySelector(".plate-region").getBoundingClientRect();
  const emoji = (document.body.innerText.match(/\p{Extended_Pictographic}/gu) || []).length;
  return {
    page: document.body.dataset.page,
    hash: location.hash,
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

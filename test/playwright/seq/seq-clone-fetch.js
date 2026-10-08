#!/usr/bin/env node
// bench-auto: fixture seq.html
/**
 * test/playwright/seq/seq-clone-fetch.js
 *
 * Regression test for the /api/seq/builtins contract split (issue #2 hardware
 * gate fix). The catalog list form is metadata-only; the full factory sequence
 * (with steps) is fetched per-name only when the operator tunes one. This
 * guards that flow so a future change cannot silently reintroduce the
 * whole-catalog-with-steps response that OOM-aborted (panic-rebooted) the
 * device while AsyncTCP delivered it.
 *
 * The operator reaches it through a factory card's Tune button; the Clone
 * Factory modal this once drove was removed in b9e8b10f.
 */

const { chromium } = require("playwright");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/seq.html";

(async () => {
  let passed = 0;
  let failed = 0;

  const test = async (name, fn) => {
    try {
      await fn();
      console.log(`✓ ${name}`);
      passed++;
    } catch (error) {
      console.error(`✗ ${name}`);
      console.error(`  ${error.message}`);
      failed++;
    }
  };
  const assert = (cond, msg) => {
    if (!cond) throw new Error(msg);
  };

  // Metadata-only list: NO `steps` arrays (that is the whole point of the fix).
  const LIGHT_LIST = [
    { name: "DM:HELLO", toggleGroup: "none", suppressMs: 6000, stepCount: 3 },
    { name: "DM:PIES", toggleGroup: "pies", suppressMs: 8000, stepCount: 5 },
  ];
  const FULL_HELLO = {
    format: 1,
    name: "DM:HELLO",
    suppressMs: 6000,
    toggleGroup: "none",
    meta: { source: "factory", notes: "" },
    steps: [
      { t: 0, type: "audio", cmd: "$H" },
      { t: 0, type: "dome", cmd: ":OP00" },
      { t: 500, type: "end" },
    ],
  };

  let listCalls = 0;
  let fullCalls = 0;
  let lastFullName = null;

  const browser = await chromium.launch({ headless: process.env.HEADLESS !== "false" });
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const page = await context.newPage();

  // No Learned sequences: every factory sequence is listed with its Tune button.
  await page.route("**/api/seq/list", (route) =>
    route.fulfill({ status: 200, contentType: "application/json", body: "[]" })
  );

  // Split builtins contract: list (no name) is metadata-only; ?name= is full.
  await page.route("**/api/seq/builtins**", (route) => {
    const name = new URL(route.request().url()).searchParams.get("name");
    if (name) {
      fullCalls++;
      lastFullName = name;
      return route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify(FULL_HELLO),
      });
    }
    listCalls++;
    return route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify(LIGHT_LIST),
    });
  });

  await page.goto(TARGET_URL);
  const tuneHello = '[data-action="tune"][data-builtin-name="DM:HELLO"]';
  await page.waitForSelector(tuneHello, { state: "visible", timeout: 8000 });

  await test("the factory list renders from a metadata-only response", async () => {
    const cards = await page.$$eval(".seq-item-factory .seq-name", (els) => els.map((el) => el.textContent));
    assert(cards.includes("DM:HELLO"), "DM:HELLO card missing");
    assert(cards.includes("DM:PIES"), "DM:PIES card missing");
    // stepCount renders, in the row's Steps column, even though the list
    // payload carries no `steps` array.
    const steps = await page.$eval(
      '.seq-item-factory[data-seq-name="DM:HELLO"] td.seq-count-cell',
      (el) => el.textContent.trim()
    );
    assert(/^\d+$/.test(steps), `step count not rendered: ${steps}`);
    assert(listCalls >= 1, "builtins list was not fetched");
    assert(fullCalls === 0, "no per-name fetch should happen before Tune");
  });

  await test("Tune fetches the one factory sequence per-name and opens the editor with its steps", async () => {
    await page.click(tuneHello);
    await page.waitForSelector("#seq-editor-view:not(.hidden)", { timeout: 5000 });
    await page.waitForSelector("#seq-editor-timeline .tl-item", { timeout: 5000 });
    assert(fullCalls === 1, `expected exactly one per-name fetch, got ${fullCalls}`);
    assert(lastFullName === "DM:HELLO", `fetched wrong sequence: ${lastFullName}`);
    // Every step is on the timeline, drawn the way data/seq_timeline.js draws
    // its kind. A block a written step draws carries data-item; what the
    // droid does after the end (its ring closes) is derived and carries none,
    // so only data-item blocks are counted.
    //   $H     one block on the Sound row
    //   :OP00  opens every panel, so a block on each panel's lane - how many
    //          is the dome catalog's, not this script's, so at least one
    //   end    no block: the end line, which a press picks as step 3 at 500 ms
    const written = await page.$$eval("#seq-editor-timeline .tl-row[data-lane]", (rows) =>
      rows.map((row) => ({ lane: row.dataset.lane, blocks: row.querySelectorAll(".tl-item[data-item]").length }))
    );
    const sound = written.filter((row) => row.lane === "sound").reduce((sum, row) => sum + row.blocks, 0);
    const panels = written.filter((row) => row.lane !== "sound" && row.blocks > 0).length;
    assert(sound === 1, `the Sound row shows ${sound} blocks for the one sound step, expected 1`);
    assert(panels > 0, "no panel lane shows a block for the :OP00 step");
    assert(
      written.every((row) => row.lane === "sound" || row.blocks === 0 || /^(pie|panel)\d+$/.test(row.lane)),
      `a block on a lane no step of DM:HELLO names: ${JSON.stringify(written.filter((row) => row.blocks > 0))}`
    );
    await page.locator("#seq-editor-timeline .tl-end[data-edit]").click();
    const endSays = (await page.textContent("#seq-picked .seq-picked-head .sub")).trim();
    const endAt = await page.inputValue('#seq-picked input[data-picked="start"]');
    assert(/ · step 3$/.test(endSays), `the end line picks "${endSays}", expected the end as step 3`);
    assert(endAt === "500", `the end line starts at ${endAt} ms, expected 500`);
    const nameValue = await page.$eval("#seq-editor-name", (el) => el.value);
    assert(nameValue === "DM:HELLO", `editor name is ${nameValue}, expected DM:HELLO`);
  });

  await browser.close();
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})();

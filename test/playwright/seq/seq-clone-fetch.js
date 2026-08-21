#!/usr/bin/env node
/**
 * test/playwright/seq/seq-clone-fetch.js
 *
 * Regression test for the /api/seq/builtins contract split (issue #2 hardware
 * gate fix). The catalog list form is metadata-only; the full factory sequence
 * (with steps) is fetched per-name only when the operator clones one. This
 * guards the clone flow so a future change cannot silently reintroduce the
 * whole-catalog-with-steps response that OOM-aborted (panic-rebooted) the
 * device while AsyncTCP delivered it.
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

  // Empty learned list -> empty-state with the "Clone Factory" entry button.
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
  await page.waitForSelector("#seq-empty-clone", { state: "visible", timeout: 8000 });

  await test("clone modal lists factory rows from a metadata-only response", async () => {
    await page.click("#seq-empty-clone");
    await page.waitForSelector("#seq-clone-builtins-list .builtin-row", { timeout: 5000 });
    const rows = await page.$$eval("#seq-clone-builtins-list .builtin-row h5", (els) =>
      els.map((el) => el.textContent)
    );
    assert(rows.includes("DM:HELLO"), "DM:HELLO row missing");
    assert(rows.includes("DM:PIES"), "DM:PIES row missing");
    // stepCount renders even though the list payload carries no `steps` array.
    const stepText = await page.$eval(
      "#seq-clone-builtins-list .builtin-row .builtin-meta span",
      (el) => el.textContent
    );
    assert(/\d+ steps/.test(stepText), `step count not rendered: ${stepText}`);
    assert(listCalls >= 1, "builtins list was not fetched");
    assert(fullCalls === 0, "no per-name fetch should happen before clone");
  });

  await test("cloning fetches the one factory sequence per-name and opens the editor with its steps", async () => {
    await page.click("#seq-clone-builtins-list .builtin-row:first-child .builtin-clone-btn");
    await page.waitForSelector("#seq-editor-view:not(.hidden)", { timeout: 5000 });
    const stepRows = await page.$$eval(
      "#seq-editor-step-table .step-row",
      (els) => els.length
    );
    assert(fullCalls === 1, `expected exactly one per-name fetch, got ${fullCalls}`);
    assert(lastFullName === "DM:HELLO", `fetched wrong sequence: ${lastFullName}`);
    assert(
      stepRows === FULL_HELLO.steps.length,
      `editor shows ${stepRows} steps, expected ${FULL_HELLO.steps.length}`
    );
    const nameValue = await page.$eval("#seq-editor-name", (el) => el.value);
    assert(nameValue === "DM:HELLO", `editor name is ${nameValue}, expected DM:HELLO`);
  });

  await browser.close();
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})();

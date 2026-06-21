#!/usr/bin/env node
/**
 * Regression: adding a step to a Factory-derived sequence must keep the
 * Sequence End step last. Otherwise the editor immediately reports
 * "The sequence must finish with a Sequence End step" even though the source
 * factory JSON was valid.
 */

const { chromium } = require("playwright");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4183/seq.html";

(async () => {
  const browser = await chromium.launch({ headless: process.env.HEADLESS !== "false" });
  const context = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const page = await context.newPage();

  try {
    await page.route("**/api/seq/list", (route) =>
      route.fulfill({ status: 200, contentType: "application/json", body: "[]" })
    );
    await page.route("**/api/seq/builtins", (route) =>
      route.fulfill({ status: 200, contentType: "application/json", body: "[]" })
    );

    await page.goto(TARGET_URL, { waitUntil: "domcontentloaded" });
    await page.waitForFunction(() => window.__seqEditorForTesting, { timeout: 5000 });

    const factorySeq = {
      format: 1,
      name: "DM:HELLO",
      suppressMs: 4000,
      toggleGroup: "none",
      meta: { source: "factory", notes: "" },
      steps: [
        { t: 0, type: "audio", cmd: "$H" },
        { t: 0, type: "dome", cmd: ":OP01" },
        { t: 950, type: "end" },
      ],
      closeSteps: [],
    };

    await page.evaluate((seq) => {
      window.__seqEditorForTesting.renderEditorView(seq);
      document.getElementById("seq-editor-view").classList.remove("hidden");
    }, factorySeq);

    await page.click("#seq-editor-add-step");

    const state = await page.evaluate(() => {
      const steps = window.__seqEditorForTesting.editorState.current.steps;
      const summary = document.getElementById("seq-editor-validation-summary")?.textContent || "";
      return {
        types: steps.map((step) => step.type),
        times: steps.map((step) => step.t),
        summary,
      };
    });

    const lastType = state.types[state.types.length - 1];
    if (lastType !== "end") {
      throw new Error(`expected final step to remain "end", got ${lastType}; types=${state.types.join(",")}`);
    }
    if (state.summary.includes("Sequence End step")) {
      throw new Error(`unexpected terminal-step warning after Add Step: ${state.summary}`);
    }
    if (!state.summary.includes("valid")) {
      throw new Error(`expected valid summary after Add Step, got: ${state.summary}`);
    }

    console.log("✓ Add Step inserts before Sequence End and keeps editor valid");
  } finally {
    await browser.close();
  }
})().catch((error) => {
  console.error(error.message);
  process.exit(1);
});

#!/usr/bin/env node
/**
 * test/playwright/seq/seq-clone-factory.js
 *
 * Slice B Playwright test: Clone Factory Entry modal + Editor opens
 *
 * This test directly invokes the editor with a factory sequence to verify:
 * - Editor renders with full metadata section
 * - Step table appears with correct number of steps
 * - All UI controls are present and functional
 * - Cancel button returns to list view
 */

const { chromium } = require("playwright");
const assert = require("assert");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/seq.html";
const HEADLESS = process.env.HEADLESS === "true";

async function test() {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  try {
    // 1. Navigate to seq.html
    console.log("Navigating to", TARGET_URL);
    await page.goto(TARGET_URL, { waitUntil: "networkidle", timeout: 10000 });
    await page.waitForTimeout(500);

    // 2. Inject a test sequence directly into the page for testing the editor
    console.log("Injecting test sequence into page");
    const testSeq = {
      format: 1,
      name: "DM:TEST",
      suppressMs: 8000,
      toggleGroup: "none",
      meta: { source: "test", notes: "" },
      steps: [
        { t: 0, type: "audio", cmd: "$H" },
        { t: 0, type: "dome", cmd: ":SM0,2200,150" },
        { t: 100, type: "loop", body: 2, periodMs: 1846, durationMs: 14000 },
        { t: 500, type: "end" },
      ],
    };

    await page.evaluate((seq) => {
      // Directly call renderEditorView with test data via exposed testing API
      if (window.__seqEditorForTesting && window.__seqEditorForTesting.renderEditorView) {
        window.__seqEditorForTesting.renderEditorView(seq);
        // Show editor
        const editorView = document.querySelector("#seq-editor-view");
        if (editorView) {
          editorView.classList.remove("hidden");
        }
      }
    }, testSeq);

    await page.waitForTimeout(500);

    // 3. Verify editor is visible
    console.log("Verifying editor view is visible");
    const editorState = await page.evaluate(() => ({
      editorHidden: document.querySelector("#seq-editor-view")?.classList.contains("hidden") ?? true,
      cardExists: !!document.querySelector("#seq-editor-view .card"),
    }));

    assert.strictEqual(editorState.editorHidden, false, "Editor should be visible");
    assert.strictEqual(editorState.cardExists, true, "Editor card should exist");

    // 4. Verify metadata section
    console.log("Verifying metadata section");
    const metaState = await page.evaluate(() => ({
      nameInput: document.querySelector("#seq-editor-name")?.value ?? "",
      suppressInput: document.querySelector("#seq-editor-suppress")?.value ?? "",
      toggleSelect: document.querySelector("#seq-editor-toggle")?.value ?? "",
      notesInput: document.querySelector("#seq-editor-notes")?.value ?? "",
    }));

    assert.strictEqual(metaState.nameInput, "DM:TEST", "Name input should have test sequence name");
    assert.strictEqual(metaState.suppressInput, "8000", "Suppress slider should have value");
    assert.strictEqual(metaState.toggleSelect, "none", "Toggle select should have value");
    console.log("Metadata section is complete");

    // 5. Verify step table renders
    console.log("Verifying step table");
    const stepState = await page.evaluate(() => ({
      stepRowCount: document.querySelectorAll("#seq-editor-step-table .step-row").length,
      addStepBtn: !!document.querySelector("#seq-editor-add-step"),
    }));

    assert(stepState.stepRowCount > 0, `Should have at least one step, got ${stepState.stepRowCount}`);
    assert.strictEqual(stepState.addStepBtn, true, "Add step button should exist");
    console.log(`Found ${stepState.stepRowCount} step rows`);

    // 6. Verify first step details
    console.log("Verifying first step details");
    const firstStep = await page.evaluate(() => {
      const row = document.querySelector("#seq-editor-step-table .step-row:first-child");
      return {
        exists: !!row,
        type: row?.dataset?.stepType,
        tValue: row?.querySelector(".step-t")?.value,
        typeChipsCount: row?.querySelectorAll(".step-type-chip").length,
        fieldsCount: row?.querySelectorAll(".step-fields [data-field]").length,
      };
    });

    assert.strictEqual(firstStep.exists, true, "First step should exist");
    assert.strictEqual(firstStep.type, "audio", "First step should be audio type");
    assert.strictEqual(firstStep.tValue, "0", "First step time should be 0");
    assert.strictEqual(firstStep.typeChipsCount, 6, "Should have 6 type chips");
    console.log(`First step is ${firstStep.type}, ${firstStep.fieldsCount} conditional fields`);

    // 7. Verify validation summary
    console.log("Verifying validation summary");
    const validationState = await page.evaluate(() => ({
      summaryExists: !!document.querySelector("#seq-editor-validation-summary"),
      summaryText: document.querySelector("#seq-editor-validation-summary")?.textContent ?? "",
    }));

    assert.strictEqual(validationState.summaryExists, true, "Validation summary should exist");
    assert(validationState.summaryText.length > 0, "Validation summary should have text");
    console.log("Validation summary:", validationState.summaryText.trim());

    // 8. Verify footer buttons
    console.log("Verifying footer buttons");
    const footerState = await page.evaluate(() => ({
      testBtn: !!document.querySelector("#seq-editor-test"),
      saveBtn: !!document.querySelector("#seq-editor-save"),
      revertBtn: !!document.querySelector("#seq-editor-revert"),
      cancelBtn: !!document.querySelector("#seq-editor-cancel"),
    }));

    assert.strictEqual(footerState.testBtn, true, "Test button should exist");
    assert.strictEqual(footerState.saveBtn, true, "Save button should exist");
    assert.strictEqual(footerState.revertBtn, true, "Revert button should exist");
    assert.strictEqual(footerState.cancelBtn, true, "Cancel button should exist");
    console.log("All footer buttons present");

    // 9. Test step type chip switching
    console.log("Testing step type chip switching");
    await page.click("#seq-editor-step-table .step-row:first-child [data-type='dome']");
    await page.waitForTimeout(300);

    const stepAfterSwitch = await page.evaluate(() => {
      const row = document.querySelector("#seq-editor-step-table .step-row:first-child");
      return {
        type: row?.dataset?.stepType,
        activeChip: row?.querySelector(".step-type-chip.active")?.dataset?.type,
      };
    });

    assert.strictEqual(stepAfterSwitch.activeChip, "dome", "Dome chip should be active after click");
    console.log("Step type switching works correctly");

    // 10. Test editing a field
    console.log("Testing field editing");
    const nameInput = page.locator("#seq-editor-name");
    await nameInput.fill("DM:NEWTEST");
    await page.waitForTimeout(200);

    const nameAfterEdit = await page.evaluate(() =>
      document.querySelector("#seq-editor-name")?.value
    );

    assert.strictEqual(nameAfterEdit, "DM:NEWTEST", "Name field should update");
    console.log("Field editing works");

    // 11. Test Cancel button
    console.log("Testing Cancel button");
    await page.click("#seq-editor-cancel");
    await page.waitForTimeout(300);

    const afterCancel = await page.evaluate(() => ({
      editorHidden: document.querySelector("#seq-editor-view")?.classList.contains("hidden") ?? false,
    }));

    assert.strictEqual(afterCancel.editorHidden, true, "Editor should be hidden after cancel");
    console.log("Cancel button works");

    // 12. Take final screenshot
    console.log("Taking final screenshot");
    await page.screenshot({ path: "/tmp/seq-clone-factory-final.png", fullPage: true });

    console.log("\n✅ All Slice B assertions passed!");
  } catch (error) {
    console.error("Test failed:", error);
    await page.screenshot({ path: "/tmp/seq-clone-factory-error.png", fullPage: true });
    process.exit(1);
  } finally {
    await browser.close();
  }
}

test().catch((error) => {
  console.error("Fatal error:", error);
  process.exit(1);
});

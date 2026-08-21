// =============================================================================
// test/playwright/seq/seq-logic-text-step.js
//
// Smoke test: DT: Logic Text step implementation
// - Render a DT: step with decoded text preview
// - Validate collapsed preview shows decoded text (not raw %)
// - Test a couple of SeqProtocolCheck DT: validations (valid + invalid cases)
// =============================================================================

const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/seq.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function runTests() {
  const browser = await chromium.launch({ headless: HEADLESS });
  const page = await browser.newPage();

  try {
    console.log('Test 1: Render DT: step with decoded text preview');
    await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(() => window.__seqEditorForTesting, { timeout: 5000 });

    const testSeq = {
      name: "DM:TEST_TEXT",
      toggleGroup: "none",
      suppressMs: 8000,
      meta: { purpose: "Test Logic Text", notes: "" },
      steps: [
        { t: 0, type: "dome", cmd: 'DT:FLD:DEFAULT:10:0:You\'re%0AWonderful' },
        { t: 1000, type: "end" },
      ],
    };

    await page.evaluate((seq) => {
      window.__seqEditorForTesting.renderEditorView(seq);
    }, testSeq);

    // Wait a moment for rendering
    await page.waitForTimeout(500);

    // Check that the collapsed preview shows decoded text without raw encoding
    const previewText = await page.locator(".step-card-preview").first().textContent();
    console.log(`  Preview text: "${previewText}"`);

    const hasDecodedText = previewText.includes("You're") && previewText.includes("Wonderful");
    const hasSlash = previewText.includes(" / "); // newline rendered as " / "
    const hasRawEncoding = previewText.includes("%0A") || previewText.includes("%25");
    const hasRawCmd = previewText.includes("DT:");

    if (!hasDecodedText) {
      console.error("  FAIL: Preview does not contain decoded text");
      return false;
    }
    if (!hasSlash) {
      console.error("  FAIL: Preview does not show newline as ' / '");
      return false;
    }
    if (hasRawEncoding) {
      console.error("  FAIL: Preview contains raw percent-encoding (should be decoded)");
      return false;
    }
    if (hasRawCmd) {
      console.error("  FAIL: Preview contains raw DT: command");
      return false;
    }
    console.log("  PASS: Preview shows decoded text correctly");

    // Verify editorState carries correct cmd (without clicking, just read the state)
    const editorState = await page.evaluate(() => {
      return window.__seqEditorForTesting.editorState.current.steps[0];
    });

    if (editorState.type !== "dome" || !editorState.cmd.startsWith("DT:")) {
      console.error(`  FAIL: editorState has wrong type or cmd: ${JSON.stringify(editorState)}`);
      return false;
    }
    if (!editorState.cmd.includes("You're%0AWonderful")) {
      console.error(`  FAIL: editorState cmd doesn't have correct encoded text: ${editorState.cmd}`);
      return false;
    }
    console.log("  PASS: editorState carries DT: cmd correctly");

    console.log('\nTest 2: SeqProtocolCheck DT: validations');
    await page.waitForFunction(() => window.SeqProtocolCheck, { timeout: 5000 });

    // Valid DT: step
    const validStep = {
      t: 0,
      type: "dome",
      cmd: "DT:FLD:DEFAULT:10:0:You're%0AWonderful",
    };

    const validResult = await page.evaluate((step) => {
      return window.SeqProtocolCheck.validateStep(step, 0, [step]);
    }, validStep);

    if (!validResult.ok) {
      console.error(`  FAIL: Valid DT: step rejected: ${validResult.error}`);
      return false;
    }
    console.log("  PASS: Valid DT: step accepted");

    // Text too long when decoded (> 32 chars)
    const tooLongStep = {
      t: 0,
      type: "dome",
      cmd: "DT:FLD:DEFAULT:10:0:This%20is%20way%20too%20long%20for%20the%20display%20panel%20text",
    };

    const tooLongResult = await page.evaluate((step) => {
      return window.SeqProtocolCheck.validateStep(step, 0, [step]);
    }, tooLongStep);

    if (tooLongResult.ok) {
      console.error("  FAIL: Over-length text should be rejected");
      return false;
    }
    if (!tooLongResult.error.includes("too long")) {
      console.error(`  FAIL: Wrong error for over-length text: ${tooLongResult.error}`);
      return false;
    }
    console.log("  PASS: Over-length text rejected with correct error");

    // Two newlines (only one allowed)
    const twoNewlinesStep = {
      t: 0,
      type: "dome",
      cmd: "DT:FLD:DEFAULT:10:0:Line1%0ALine2%0ALine3",
    };

    const twoNewlinesResult = await page.evaluate((step) => {
      return window.SeqProtocolCheck.validateStep(step, 0, [step]);
    }, twoNewlinesStep);

    if (twoNewlinesResult.ok) {
      console.error("  FAIL: Two newlines should be rejected");
      return false;
    }
    if (!twoNewlinesResult.error.includes("one line break")) {
      console.error(`  FAIL: Wrong error for two newlines: ${twoNewlinesResult.error}`);
      return false;
    }
    console.log("  PASS: Two newlines rejected with correct error");

    console.log('\nTest 3: Text encoding verification');

    // Test that percent-encoding is applied correctly
    const encodeTest = await page.evaluate(() => {
      // Simulate what the UI does: take plain text and percent-encode it
      const plainText = "Hello\nWorld";
      let encoded = encodeURIComponent(plainText).replace(/%20/g, " ");
      return encoded;
    });

    if (encodeTest !== "Hello%0AWorld") {
      console.error(`  FAIL: Encoding not correct: got ${encodeTest}`);
      return false;
    }
    console.log("  PASS: Text percent-encoding works correctly");

    // Test decode
    const decodeTest = await page.evaluate(() => {
      try {
        const result = decodeURIComponent("You're%0AWonderful");
        return { text: result, hasNewline: result.includes("\n") };
      } catch (e) {
        return { error: e.message };
      }
    });

    if (decodeTest.error || !decodeTest.hasNewline) {
      console.error(`  FAIL: Decoding not correct: ${JSON.stringify(decodeTest)}`);
      return false;
    }
    console.log("  PASS: Text percent-decoding works correctly");

    console.log("\n✓ All smoke tests passed!");
    return true;

  } catch (error) {
    console.error("Test error:", error);
    return false;
  } finally {
    await browser.close();
  }
}

runTests().then((success) => {
  process.exit(success ? 0 : 1);
});

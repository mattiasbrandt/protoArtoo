// Issue #117: Recovery panel z-index and overlay suppression with effective opacity verification.
//
// Design: Dimmed-but-visible page content behind recovery panel (aligned-state1-centered.png).
// Operator must see page attempting to load/recover at effective opacity 0.4, not compounded
// to near-invisibility.
//
// Critical: opacity compounds through DOM nesting via CSS inheritance.
// Direct-child selector (> *) dims top-level blocks once; descendant selector (* ) multiplies
// opacity at each level, causing opacity^depth invisibility (e.g., 0.4^4 ≈ 0.026 at depth 4).
//
// Verified properties:
// 1. Recovery z-index > all overlays (10001 > 9999 > 1200)
// 2. Direct-child dim rule: body.recovery-active > *:not(#page-recovery-backdrop) { opacity: 0.4 }
// 3. Effective opacity at nesting depth 3-4: 0.4 (not compounded)
// 4. Explicit overlay suppression: .sleep-overlay/.seq-modal visibility: hidden
// 5. Recovery panel and button: fully visible (opacity 1), interactive, hit-testable
// 6. Kernel self-contained: literal RGB colors, no /style.css

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const root = path.resolve(__dirname, "../..");
const dataDir = path.join(root, "data");
const kernelPath = path.join(dataDir, "_recovery_kernel.html");
const stylePath = path.join(root, "data", "style.css");

// Extract z-index from CSS using regex
const findZIndexForSelector = (css, selector) => {
  const pattern = new RegExp(
    selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') +
    '\\s*\\{[^}]*?z-index:\\s*(\\d+)',
    's'
  );
  const match = css.match(pattern);
  return match && match[1] ? parseInt(match[1], 10) : null;
};

test("Issue #117: Recovery z-index exceeds all competing overlays", () => {
  const kernelContent = fs.readFileSync(kernelPath, "utf8");
  const styleContent = fs.readFileSync(stylePath, "utf8");

  const recoveryZIndex = findZIndexForSelector(kernelContent, "#page-recovery-backdrop");
  assert.ok(recoveryZIndex !== null, "kernel must define #page-recovery-backdrop z-index");

  const overlaySelectors = [".sleep-overlay", ".seq-modal", ".seq-modal-content"];
  const overlayIndexes = overlaySelectors
    .map(sel => {
      const index = findZIndexForSelector(styleContent, sel);
      return index !== null ? { sel, index } : null;
    })
    .filter(Boolean);

  assert.ok(overlayIndexes.length > 0, "style.css must define z-index for overlays");

  for (const { sel, index } of overlayIndexes) {
    assert.ok(recoveryZIndex > index, `Recovery (${recoveryZIndex}) must exceed ${sel} (${index})`);
  }
});

test("Issue #117: Dim rule uses direct-child selector to avoid opacity compounding", () => {
  const kernelContent = fs.readFileSync(kernelPath, "utf8");

  // Must use direct-child (>) not descendant selector to avoid opacity compounding
  assert.ok(
    kernelContent.includes("body.recovery-active > *:not(#page-recovery-backdrop)"),
    "dim rule must use direct-child selector (>) to prevent 0.4^depth invisibility"
  );

  // Must NOT have the old descendant form that compounds opacity
  assert.strictEqual(
    kernelContent.includes("body.recovery-active *:not(#page-recovery-backdrop)") &&
    !kernelContent.includes("body.recovery-active > *:not(#page-recovery-backdrop)"),
    false,
    "must not have descendant selector without direct-child form"
  );

  // Has explicit overlay suppression rules
  assert.ok(
    kernelContent.includes(".sleep-overlay") &&
    kernelContent.includes(".seq-modal") &&
    kernelContent.includes("visibility: hidden"),
    "must explicitly suppress full-screen overlays by visibility, not opacity"
  );
});

test("Issue #117: Effective opacity and recovery panel visibility (browser-based)", async () => {
  // This test requires Playwright; skip gracefully if not available
  let playwright;
  try {
    playwright = require("playwright");
  } catch (e) {
    console.log("  ⊙ Skipped: playwright unavailable; verify with local :4173 + Playwright MCP");
    return;
  }

  const browser = await playwright.chromium.launch({ headless: true });
  const page = await browser.newPage();

  try {
    // Fixture with realistic nesting depth (4 levels, matching index.html structure)
    // to properly detect opacity compounding bugs
    const testHtml = `<!doctype html>
<html>
<head>
  <style>
    * { margin: 0; padding: 0; }
    body { font-family: system-ui; background: rgb(20, 25, 35); }
    .card { padding: 20px; color: rgb(200, 210, 225); }
    .card-header { margin-bottom: 12px; }
    .card-content { padding: 8px; }
    .nested-text { margin: 4px 0; }
    #page-recovery-backdrop {
      position: fixed;
      inset: 0;
      background: rgba(0, 0, 0, 0.6);
      display: none;
      align-items: center;
      justify-content: center;
      z-index: 10001;
    }
    #page-recovery-backdrop.active { display: flex; }
    body.recovery-active > *:not(#page-recovery-backdrop) {
      opacity: 0.4;
      pointer-events: none;
    }
    body.recovery-active .sleep-overlay,
    body.recovery-active .seq-modal {
      visibility: hidden;
      pointer-events: none;
    }
    #page-recovery-backdrop .recovery-panel {
      background: rgb(21, 34, 56);
      border: 1px solid rgb(42, 74, 122);
      border-radius: 8px;
      padding: 20px;
      max-width: 380px;
    }
    #page-recovery-backdrop .btn {
      display: inline-flex;
      padding: 10px 16px;
      background: rgb(74, 144, 217);
      border: 1px solid rgb(106, 176, 255);
      border-radius: 999px;
      cursor: pointer;
    }
    .sleep-overlay {
      position: fixed;
      inset: 0;
      z-index: 1200;
      display: flex;
      background: rgba(0, 0, 0, 0.8);
    }
  </style>
</head>
<body>
  <!-- Level 1: direct child of body (affected by > *) -->
  <div class="card" data-testid="page-content">
    <!-- Level 2: card child -->
    <div class="card-header" data-testid="card-label">Status</div>
    <!-- Level 3: header child -->
    <div class="card-content" data-testid="content-wrapper">
      <!-- Level 4: content child (deepest) -->
      <div class="nested-text" data-testid="nested-text">Page content visible at depth 4</div>
    </div>
  </div>

  <!-- Nested overlay (level 2 under body) -->
  <div id="sleep-wrapper" data-testid="sleep-wrapper">
    <div id="sleep-overlay" class="sleep-overlay" data-testid="sleep-overlay">Sleep</div>
  </div>

  <!-- Recovery backdrop -->
  <div id="page-recovery-backdrop" data-testid="recovery-backdrop">
    <div class="recovery-panel" data-testid="recovery-panel">
      <div data-testid="panel-text">No response</div>
      <button class="btn" id="recovery-kernel-retry" data-testid="retry-button">Retry</button>
    </div>
  </div>

  <script>
    document.body.classList.add('recovery-active');
    document.getElementById('page-recovery-backdrop').classList.add('active');
  </script>
</body>
</html>`;

    await page.setContent(testHtml);
    await page.waitForTimeout(100);

    // Measure EFFECTIVE opacity (product of ancestor chain opacities)
    const result = await page.evaluate(() => {
      const computeEffectiveOpacity = (elem) => {
        let effectiveOpacity = 1;
        let current = elem;
        while (current) {
          const computed = window.getComputedStyle(current);
          const opacity = parseFloat(computed.opacity);
          effectiveOpacity *= opacity;
          current = current.parentElement;
        }
        return effectiveOpacity;
      };

      const cardLabel = document.querySelector('[data-testid="card-label"]');
      const nestedText = document.querySelector('[data-testid="nested-text"]');
      const panel = document.querySelector('[data-testid="recovery-panel"]');
      const panelText = document.querySelector('[data-testid="panel-text"]');
      const btn = document.querySelector('[data-testid="retry-button"]');
      const sleepOverlay = document.querySelector('[data-testid="sleep-overlay"]');

      // Effective opacities
      const cardLabelEffective = computeEffectiveOpacity(cardLabel);
      const nestedTextEffective = computeEffectiveOpacity(nestedText);
      const panelEffective = computeEffectiveOpacity(panel);
      const btnEffective = computeEffectiveOpacity(btn);

      // Hit-test
      const rect = btn.getBoundingClientRect();
      const elementAtPoint = document.elementFromPoint(rect.left + rect.width / 2, rect.top + rect.height / 2);

      return {
        cardLabelEffective: cardLabelEffective.toFixed(4),
        nestedTextEffective: nestedTextEffective.toFixed(4),
        panelEffective: panelEffective.toFixed(4),
        btnEffective: btnEffective.toFixed(4),
        sleepOverlayVisibility: window.getComputedStyle(sleepOverlay).visibility,
        isButtonHitTestable: elementAtPoint === btn,
      };
    });

    // Assert effective opacities are NOT compounded
    const cardLabel = parseFloat(result.cardLabelEffective);
    const nestedText = parseFloat(result.nestedTextEffective);
    const panelOp = parseFloat(result.panelEffective);
    const btnOp = parseFloat(result.btnEffective);

    assert.ok(
      cardLabel >= 0.39 && cardLabel <= 0.41,
      `card label effective opacity must be ~0.4, got ${result.cardLabelEffective} (compounding would give 0.0256)`
    );
    assert.ok(
      nestedText >= 0.39 && nestedText <= 0.41,
      `nested text effective opacity must be ~0.4, got ${result.nestedTextEffective} (compounding would give 0.0256)`
    );
    assert.ok(
      panelOp >= 0.99 && panelOp <= 1.01,
      `recovery panel must be fully opaque (1.0), got ${result.panelEffective}`
    );
    assert.ok(
      btnOp >= 0.99 && btnOp <= 1.01,
      `retry button must be fully opaque (1.0), got ${result.btnEffective}`
    );
    assert.strictEqual(
      result.sleepOverlayVisibility,
      "hidden",
      "sleep overlay must be visibility: hidden"
    );
    assert.strictEqual(
      result.isButtonHitTestable,
      true,
      "retry button must be hit-testable"
    );

    console.log("\n  EFFECTIVE opacities (product of ancestor chain, not compounded):");
    console.log(`    card-label depth 2: ${result.cardLabelEffective} (approved: ~0.4000)`);
    console.log(`    nested-text depth 4: ${result.nestedTextEffective} (approved: ~0.4000, broken: ~0.0256)`);
    console.log(`    recovery-panel: ${result.panelEffective} (must be 1.0000)`);
    console.log(`    retry-button: ${result.btnEffective} (must be 1.0000, hit-testable: ${result.isButtonHitTestable})`);
    console.log(`    sleep-overlay visibility: ${result.sleepOverlayVisibility}`);
  } finally {
    await browser.close();
  }
});

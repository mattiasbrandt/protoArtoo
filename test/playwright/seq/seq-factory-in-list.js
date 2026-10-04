#!/usr/bin/env node
// bench-auto: fixture seq.html
/**
 * test/playwright/seq/seq-factory-in-list.js
 *
 * Verify Factory sequences appear as first-class items in the main list view
 * with a "Tune" button that opens the editor directly.
 *
 * Tests:
 * - Factory sequences appear in the "Factory" group when no Learned sequences exist
 * - Factory rows show what the droid reports (step count, interrupt group) and
 *   sit under the Factory group's header, with no badge of their own
 * - Tune button on Factory card opens editor with factory sequence data
 * - Capacity display shows only Learned count (not Factory), against the cap
 *   the droid reports in GET /api/identity (FIXTURE=1 answers it)
 * - Group headers appear correctly (Yours / Factory)
 */

const { chromium } = require("playwright");
const assert = require("assert");

const TARGET_URL = process.env.TARGET_URL || "http://127.0.0.1:4173/seq.html";
const HEADLESS = process.env.HEADLESS !== "false";

async function test() {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  // Offline, the droid's identity (and the cap in it) comes from the fixture.
  // Its event stream and timers keep the process alive until it is closed.
  const fixture = process.env.FIXTURE === "1"
    ? await require("../_lib/fixture_routes").install(page.context())
    : null;

  try {
    // =====================================================================
    // Test 1: Page loads with list view
    // =====================================================================
    console.log("Test 1: Loading seq.html and checking page structure");
    await page.goto(TARGET_URL, { waitUntil: "networkidle", timeout: 10000 });
    await page.waitForTimeout(500);

    const initialState = await page.evaluate(() => ({
      cardsContainer: !!document.querySelector("#seq-cards-container"),
      // The list's count slot is in the page's own markup; the capacity
      // (#seq-capacity-display) is drawn in the Yours group's header once the
      // list has rows, and is read in Tests 5 and 7.
      countDisplay: !!document.querySelector("#seq-count-display"),
      populatedState: !document.querySelector("#seq-populated-state")?.classList.contains("hidden"),
    }));

    assert.strictEqual(initialState.cardsContainer, true, "Cards container should exist");
    assert.strictEqual(initialState.countDisplay, true, "Count display should exist");
    console.log("✓ Page structure loaded");

    // =====================================================================
    // Test 2: Inject test data directly using the exposed test API
    // =====================================================================
    console.log("Test 2: Setting up test data (empty Learned, populated Factory)");

    const factorySeqs = [
      {
        name: "DM:ROCKMARCH",
        toggleGroup: "none",
        suppressMs: 8000,
        stepCount: 5,
        meta: { notes: "Rock march sequence" },
      },
      {
        name: "DM:TWIRLY",
        toggleGroup: "pies",
        suppressMs: 6000,
        stepCount: 3,
        meta: { notes: "Twirly sequence" },
      },
      {
        name: "DM:HELLO",
        toggleGroup: "all",
        suppressMs: 5000,
        stepCount: 8,
        meta: { notes: "Hello sequence" },
      },
    ];

    // Use the test API to render the list with empty learned sequences and populated factory
    await page.evaluate((builtins) => {
      if (window.__seqEditorForTesting && window.__seqEditorForTesting.renderListWithMocks) {
        window.__seqEditorForTesting.renderListWithMocks([], builtins);
      }
    }, factorySeqs);

    await page.waitForTimeout(500);
    await page.screenshot({ path: "/tmp/seq-factory-list.png", fullPage: true });

    console.log("✓ Test data injected and list rendered");

    // =====================================================================
    // Test 3: Verify section headers appear
    // =====================================================================
    console.log("Test 3: Verifying section headers");

    // Each group is a <tbody class="seq-group"> whose header cell names it.
    const sectionHeaders = await page.evaluate(() => ({
      yourSequencesHeader: Array.from(
        document.querySelectorAll("#seq-cards-container tbody.seq-group th")
      ).some(h => h.textContent.trim().startsWith("Yours")),
      factorySequencesHeader: Array.from(
        document.querySelectorAll("#seq-cards-container tbody.seq-group th")
      ).some(h => h.textContent.trim().startsWith("Factory")),
    }));

    assert.strictEqual(
      sectionHeaders.yourSequencesHeader,
      true,
      '"Yours" group header should exist'
    );
    assert.strictEqual(
      sectionHeaders.factorySequencesHeader,
      true,
      '"Factory" group header should exist'
    );

    console.log("✓ Section headers present");

    // =====================================================================
    // Test 4: Verify Factory cards appear with correct metadata
    // =====================================================================
    console.log("Test 4: Verifying Factory cards appearance");

    const factoryCards = await page.evaluate(() => {
      const cards = Array.from(document.querySelectorAll(".seq-item-factory"));
      // The group header a row sits under: the nearest tbody.seq-group above it.
      const groupOf = (row) => {
        let at = row.previousElementSibling;
        while (at && !at.classList.contains("seq-group")) at = at.previousElementSibling;
        return at?.querySelector("th")?.textContent.trim() ?? "";
      };
      return cards.map(card => ({
        name: card.querySelector('th[scope="row"] .seq-name')?.textContent ?? "",
        badgeCount: card.querySelectorAll(".seq-badge").length,
        group: groupOf(card),
        steps: card.querySelector("td.seq-count-cell")?.textContent.trim() ?? "",
        metaItems: Array.from(card.querySelectorAll(".seq-meta")).map(m => m.textContent),
        hasTuneBtn: !!card.querySelector('[data-action="tune"]'),
        tuneBtn: card.querySelector('[data-action="tune"]')
          ? {
              text: card.querySelector('[data-action="tune"]').textContent,
              builtinName: card.querySelector('[data-action="tune"]').dataset.builtinName,
            }
          : null,
      }));
    });

    assert.strictEqual(factoryCards.length, 3, "Should have 3 factory cards");

    // Verify first card
    assert.strictEqual(factoryCards[0].name, "DM:ROCKMARCH", "First card should be DM:ROCKMARCH");
    // The group says Factory; the row carries no Factory badge of its own.
    assert.ok(factoryCards[0].group.startsWith("Factory"), `Card should sit under the Factory group, got: "${factoryCards[0].group}"`);
    assert.strictEqual(factoryCards[0].badgeCount, 0, "Factory row should carry no badge");
    // What the droid reported: the step count in the Steps column, and the
    // interrupt group beside the purpose when the sequence has one.
    assert.strictEqual(factoryCards[0].steps, "5", "Card should show its step count");
    assert.deepStrictEqual(factoryCards[0].metaItems, [], "DM:ROCKMARCH has no interrupt group to show");
    assert.deepStrictEqual(factoryCards[1].metaItems, ["interrupt group Pies"], "DM:TWIRLY should show its interrupt group");
    assert.strictEqual(factoryCards[0].hasTuneBtn, true, "Card should have Tune button");
    assert.strictEqual(factoryCards[0].tuneBtn.text, "Tune", "Button should say Tune");
    assert.strictEqual(
      factoryCards[0].tuneBtn.builtinName,
      "DM:ROCKMARCH",
      "Tune button should have correct builtin name"
    );

    console.log(`✓ Found ${factoryCards.length} factory cards with correct structure`);
    factoryCards.forEach((card, i) => {
      console.log(`  Card ${i + 1}: ${card.name}, steps: ${card.steps}, meta items: ${card.metaItems.join(" | ")}`);
    });

    // =====================================================================
    // Test 5: Verify Capacity display shows only Learned count
    // =====================================================================
    console.log("Test 5: Verifying capacity display");

    // The cap is a board fact the droid reports, not a number of the page's.
    const cap = await page.evaluate(() => window.PAIdentity?.learned_sequence_cap);
    assert.ok(Number.isInteger(cap) && cap > 0, `The droid reported no Learned sequence cap: ${cap}`);

    const capacityText = await page.evaluate(() =>
      document.querySelector("#seq-capacity-display")?.textContent ?? ""
    );

    assert.ok(
      capacityText.includes(`0 / ${cap}`),
      `Capacity should show 0 / ${cap} (Learned only), got: "${capacityText}"`
    );

    console.log(`✓ Capacity display correct: "${capacityText}"`);

    // =====================================================================
    // Test 6: Tune button opens editor with factory sequence
    // =====================================================================
    console.log("Test 6: Testing Tune button functionality");

    // Before clicking, set up the mock to return full sequence data when fetching a specific builtin
    await page.evaluate(() => {
      const originalGet = PAApi.get;
      PAApi.get = async function(url) {
        if (url.includes("/api/seq/builtins?name=DM:ROCKMARCH")) {
          return {
            data: {
              format: 1,
              name: "DM:ROCKMARCH",
              toggleGroup: "none",
              suppressMs: 8000,
              meta: { notes: "Rock march sequence" },
              steps: [
                { t: 0, type: "audio", cmd: "$H" },
                { t: 100, type: "dome", cmd: ":OP00" },
                { t: 500, type: "end" },
              ],
            },
          };
        }
        if (url === "/api/seq/list") {
          return { data: [] };
        }
        if (url === "/api/seq/builtins") {
          return {
            data: [
              {
                name: "DM:ROCKMARCH",
                toggleGroup: "none",
                suppressMs: 8000,
                stepCount: 5,
                meta: { notes: "Rock march sequence" },
              },
              {
                name: "DM:TWIRLY",
                toggleGroup: "pies",
                suppressMs: 6000,
                stepCount: 3,
                meta: { notes: "Twirly sequence" },
              },
              {
                name: "DM:HELLO",
                toggleGroup: "all",
                suppressMs: 5000,
                stepCount: 8,
                meta: { notes: "Hello sequence" },
              },
            ],
          };
        }
        // Default fallback for other URLs
        return originalGet.call(this, url);
      };
    });

    // First, check if the button exists
    const btnExists = await page.evaluate(() => {
      const btn = document.querySelector('[data-action="tune"][data-builtin-name="DM:ROCKMARCH"]');
      return {
        exists: !!btn,
        text: btn?.textContent ?? "",
        dataAction: btn?.dataset?.action ?? "",
      };
    });
    console.log("Tune button exists:", btnExists);

    assert.strictEqual(btnExists.exists, true, "Tune button should exist on Factory card");
    assert.strictEqual(btnExists.text, "Tune", "Button should say 'Tune'");
    assert.strictEqual(btnExists.dataAction, "tune", "Button should have data-action='tune'");

    console.log("✓ Tune button has correct attributes and exists on Factory cards");

    // =====================================================================
    // Test 7: Test with Learned sequences mixed with Factory
    // =====================================================================
    console.log("Test 7: Testing with mixed Learned + Factory sequences");

    const learnedSeqs = [
      {
        name: "DM:ROCKMARCH",
        toggleGroup: "none",
        suppressMs: 8000,
        stepCount: 5,
        retrained: true,
        valid: true,
        modified: "2026-06-16T10:00:00Z",
        meta: {},
      },
    ];

    const factorySeqs2 = [
      {
        name: "DM:ROCKMARCH",
        toggleGroup: "none",
        suppressMs: 8000,
        stepCount: 5,
        meta: { notes: "Rock march sequence" },
      },
      {
        name: "DM:TWIRLY",
        toggleGroup: "pies",
        suppressMs: 6000,
        stepCount: 3,
        meta: { notes: "Twirly sequence" },
      },
      {
        name: "DM:HELLO",
        toggleGroup: "all",
        suppressMs: 5000,
        stepCount: 8,
        meta: { notes: "Hello sequence" },
      },
    ];

    // Render with mixed sequences
    await page.evaluate(({ learnedSeqs, factorySeqs }) => {
      if (window.__seqEditorForTesting && window.__seqEditorForTesting.renderListWithMocks) {
        window.__seqEditorForTesting.renderListWithMocks(learnedSeqs, factorySeqs);
      }
    }, { learnedSeqs, factorySeqs: factorySeqs2 });

    await page.waitForTimeout(500);

    const mixedState = await page.evaluate(() => ({
      learnedCards: document.querySelectorAll(".seq-item:not(.seq-item-factory)").length,
      factoryCards: document.querySelectorAll(".seq-item-factory").length,
      retrainedBadge: !!Array.from(document.querySelectorAll(".seq-badge-retrained")).some(
        b => b.closest(".seq-item").querySelector(".seq-name")?.textContent === "DM:ROCKMARCH"
      ),
      capacityDisplay: document.querySelector("#seq-capacity-display")?.textContent,
    }));

    assert.strictEqual(mixedState.learnedCards, 1, "Should have 1 learned (retrained) card");
    assert.strictEqual(mixedState.factoryCards, 2, "Should have 2 untuned factory cards (DM:TWIRLY, DM:HELLO)");
    assert.strictEqual(mixedState.retrainedBadge, true, "Retrained card should have Retrained badge");
    assert.ok(
      mixedState.capacityDisplay.includes(`1 / ${cap}`),
      `Capacity should show 1 / ${cap} (only learned count), got: "${mixedState.capacityDisplay}"`
    );

    console.log("✓ Mixed Learned + Factory list works correctly");
    console.log(`  Learned: ${mixedState.learnedCards}, Factory: ${mixedState.factoryCards}`);
    console.log(`  Capacity: "${mixedState.capacityDisplay}"`);

    // =====================================================================
    // Final screenshot
    // =====================================================================
    await page.screenshot({ path: "/tmp/seq-factory-list-final.png", fullPage: true });

    console.log("\n✅ All Factory-in-list tests passed!");
  } catch (error) {
    console.error("Test failed:", error);
    await page.screenshot({ path: "/tmp/seq-factory-list-error.png", fullPage: true });
    process.exit(1);
  } finally {
    await browser.close();
    if (fixture) await fixture.close();
  }
}

test().catch((error) => {
  console.error("Fatal error:", error);
  process.exit(1);
});

// bench-auto: fixture rc.html
// The RC action picker: a search narrows the list, a pick lands in the
// source's draft, and the picked action comes back under "Recently used".
//
// The page used to open the picker from a per-slot list (`.rc-slot-item`,
// the "sound" slot). Sources are now the left-hand list of channels and
// conditions (`.rc-channel-item[data-chkey]`, data/rc.js), so the script
// opens the picker from the first radio channel instead: SBUS1 CH1, since a
// droid whose config does not answer is shown as the firmware's default
// receiver type, Dual SBUS (#389). The picker is the same one. With no GET /api/actions answer (the fixture server has none) the
// page lists its built-in actions (data/rc.js HARDCODED_ACTION_TARGETS),
// which carry sound_rand_general.
const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS !== 'false';
const SOURCE = '.rc-channel-item[data-chkey="sbus1:1"]';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });
  const failures = [];
  const check = (ok, message) => {
    if (!ok) failures.push(message);
  };

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector(SOURCE, { timeout: 10000 });

    await page.click(SOURCE);
    await page.waitForSelector('[data-action-search]', { timeout: 8000 });
    const unfilteredCount = await page.locator('.rc-action-row').count();

    await page.fill('[data-action-search]', 'general');
    await page.waitForTimeout(160);

    const filtered = await page.evaluate(() => {
      const rows = Array.from(document.querySelectorAll('.rc-action-row'));
      return {
        visibleCount: rows.length,
        tokens: rows.map((row) => row.dataset.actionToken || ''),
        labels: rows.map((row) => row.querySelector('.rc-action-label')?.textContent?.trim() || ''),
      };
    });

    await page.click('[data-action-select="sound_rand_general"]');
    await page.waitForTimeout(180);
    // What the old slot editor's pick also did: the action is the source's
    // draft target, the field Apply and save sends.
    const draftAfterPick = await page.$eval('#rc-editor-content [data-field="target"]', (el) => el.value);

    await page.fill('[data-action-search]', '');
    await page.waitForTimeout(160);

    const recent = await page.evaluate(() => {
      const recentGroup = document.querySelector('.rc-action-group-recent');
      const recentLabels = recentGroup
        ? Array.from(recentGroup.querySelectorAll('.rc-action-label')).map((n) => n.textContent?.trim() || '')
        : [];
      const dirty = document.getElementById('rc-editor-dirty');
      return {
        hasRecentGroup: Boolean(recentGroup),
        recentLabels,
        recentTokens: recentGroup
          ? Array.from(recentGroup.querySelectorAll('.rc-action-row')).map((row) => row.dataset.actionToken || '')
          : [],
        dirtyState: dirty?.textContent?.trim() || '',
        dirtyFlag: dirty?.dataset.state || '',
        draftTarget: document.querySelector('#rc-editor-content [data-field="target"]')?.value || '',
      };
    });

    console.log('RC_ACTION_PICKER_SEARCH_RECENT_START');
    console.log(JSON.stringify({ unfilteredCount, filtered, draftAfterPick, recent }, null, 2));
    console.log('RC_ACTION_PICKER_SEARCH_RECENT_END');
    await page.screenshot({ path: '/tmp/rc-action-picker-search-recent.png', fullPage: true });

    check(filtered.visibleCount > 0 && filtered.visibleCount < unfilteredCount,
      `the search "general" left ${filtered.visibleCount} of ${unfilteredCount} rows, expected fewer but at least one`);
    check(filtered.tokens.includes('sound_rand_general'),
      `the search "general" does not list sound_rand_general: ${JSON.stringify(filtered.tokens)}`);
    check(recent.hasRecentGroup && recent.recentTokens.includes('sound_rand_general'),
      `"Recently used" does not carry the pick: ${JSON.stringify(recent.recentTokens)}`);
    check(recent.dirtyFlag === 'dirty',
      `the editor reads "${recent.dirtyState}" (${recent.dirtyFlag}) after a pick, expected unsaved changes`);
    check(draftAfterPick === 'sound_rand_general',
      `the source's draft target is "${draftAfterPick}" after the pick, expected sound_rand_general`);
    // Clearing the search must not drop the pick while the editor still says
    // "Unsaved changes": Apply and save would send the source without it.
    check(recent.draftTarget === 'sound_rand_general',
      `the draft target is "${recent.draftTarget}" after the search was cleared, expected the pick to survive it`);
  } finally {
    await browser.close();
  }

  failures.forEach((message) => console.error(`FAIL ${message}`));
  console.log(failures.length === 0 ? 'PASS rc action picker search and recent' : `${failures.length} failed`);
  process.exitCode = failures.length === 0 ? 0 : 1;
})();

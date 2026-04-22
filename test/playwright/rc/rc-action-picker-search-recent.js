const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS === 'true';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('.rc-slot-item[data-slot="sound"]', { timeout: 10000 });

    await page.click('.rc-slot-item[data-slot="sound"]');
    await page.waitForSelector('[data-action-search]', { timeout: 8000 });

    await page.fill('[data-action-search]', 'general');
    await page.waitForTimeout(160);

    const filtered = await page.evaluate(() => {
      const rows = Array.from(document.querySelectorAll('.rc-action-row'));
      return {
        visibleCount: rows.length,
        labels: rows.map((row) => row.querySelector('.rc-action-label')?.textContent?.trim() || ''),
      };
    });

    await page.click('[data-action-select="sound_rand_general"]');
    await page.waitForTimeout(180);

    await page.fill('[data-action-search]', '');
    await page.waitForTimeout(160);

    const recent = await page.evaluate(() => {
      const recentGroup = document.querySelector('.rc-action-group-recent');
      const recentLabels = recentGroup
        ? Array.from(recentGroup.querySelectorAll('.rc-action-label')).map((n) => n.textContent?.trim() || '')
        : [];
      return {
        hasRecentGroup: Boolean(recentGroup),
        recentLabels,
        dirtyState: document.getElementById('rc-editor-dirty')?.textContent?.trim() || '',
      };
    });

    console.log('RC_ACTION_PICKER_SEARCH_RECENT_START');
    console.log(JSON.stringify({ filtered, recent }, null, 2));
    console.log('RC_ACTION_PICKER_SEARCH_RECENT_END');
    await page.screenshot({ path: '/tmp/rc-action-picker-search-recent.png', fullPage: true });
  } finally {
    await browser.close();
  }
})();

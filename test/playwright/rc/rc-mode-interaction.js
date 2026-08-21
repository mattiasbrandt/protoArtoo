const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS === 'true';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('.rc-mode-card', { timeout: 10000 });
    await page.click('.rc-mode-card[data-mode="single_sbus"]');
    await page.waitForTimeout(400);

    const state = await page.evaluate(() => ({
      selectedMode:
        Array.from(document.querySelectorAll('.rc-mode-card')).find((c) => c.classList.contains('selected'))
          ?.dataset.mode || null,
      feedbackText: document.getElementById('rc-mode-feedback')?.textContent?.trim() || '',
      feedbackClass: document.getElementById('rc-mode-feedback')?.className || '',
      panelTitles: Array.from(document.querySelectorAll('.rc-panel-title')).map((n) => n.textContent?.trim() || ''),
      actionButtons: {
        reset: document.getElementById('rc-reset-defaults')?.textContent?.trim() || '',
        apply: document.getElementById('rc-editor-apply')?.textContent?.trim() || '',
        revert: document.getElementById('rc-editor-revert')?.textContent?.trim() || '',
      },
    }));

    console.log('RC_MODE_INTERACTION_START');
    console.log(JSON.stringify(state, null, 2));
    console.log('RC_MODE_INTERACTION_END');
    await page.screenshot({ path: '/tmp/rc-mode-interaction.png', fullPage: true });
  } finally {
    await browser.close();
  }
})();

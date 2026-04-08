const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS === 'true';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  try {
    await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded' });
    await page.waitForTimeout(7200);

    const state = await page.evaluate(() => ({
      rcModeText: document.getElementById('rc-mode-feedback')?.textContent?.trim(),
      rcModeClass: document.getElementById('rc-mode-feedback')?.className,
      editorText: document.getElementById('rc-editor-feedback')?.textContent?.trim(),
      editorClass: document.getElementById('rc-editor-feedback')?.className,
      summaryText: document.querySelector('#rc-summary-body tr td')?.textContent?.trim(),
      selectedMode:
        Array.from(document.querySelectorAll('.rc-mode-card')).find((c) => c.classList.contains('selected'))
          ?.dataset.mode || null,
      pressedModes: Array.from(document.querySelectorAll('.rc-mode-card')).map((c) => ({
        mode: c.dataset.mode,
        pressed: c.getAttribute('aria-pressed'),
      })),
    }));

    console.log('RC_FEEDBACK_PROBE_START');
    console.log(JSON.stringify(state, null, 2));
    console.log('RC_FEEDBACK_PROBE_END');
  } finally {
    await browser.close();
  }
})();

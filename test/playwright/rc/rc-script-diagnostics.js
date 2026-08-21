const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS === 'true';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  const consoleMsgs = [];
  const pageErrors = [];
  const failedReq = [];

  page.on('console', (m) => consoleMsgs.push({ type: m.type(), text: m.text() }));
  page.on('pageerror', (e) => pageErrors.push(String(e)));
  page.on('requestfailed', (r) =>
    failedReq.push({ url: r.url(), err: r.failure()?.errorText || '' }),
  );

  try {
    await page.goto(TARGET_URL, { waitUntil: 'load' });
    await page.waitForTimeout(2500);

    const globals = await page.evaluate(() => ({
      hasPAApi: Boolean(window.PAApi),
      hasPAStatusStream: Boolean(window.PAStatusStream),
      hasRcModeFn: typeof window.loadRcMode,
      scripts: Array.from(document.querySelectorAll('script[src]')).map((s) => ({
        src: s.getAttribute('src'),
      })),
    }));

    console.log('RC_DIAG_START');
    console.log(JSON.stringify({ globals, consoleMsgs, pageErrors, failedReq }, null, 2));
    console.log('RC_DIAG_END');
  } finally {
    await browser.close();
  }
})();

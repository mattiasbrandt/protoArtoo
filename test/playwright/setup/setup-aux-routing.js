const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/setup.html';
const HEADLESS = process.env.HEADLESS === 'true';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1560, height: 920 } });

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#seg-type-aux1', { timeout: 10000 });

    await page.click('#seg-type-aux1 .seg-option[data-value="rgb"]');
    await page.waitForTimeout(160);

    const afterAux1 = await page.evaluate(() => {
      const ledCountRow = document.getElementById('aux-led-count')?.closest('.component-row');
      return {
        aux1Type: document.getElementById('type-aux1')?.value || '',
        aux2Type: document.getElementById('type-aux2')?.value || '',
        aux3Type: document.getElementById('type-aux3')?.value || '',
        routeText: document.getElementById('aux-led-route-status')?.textContent?.trim() || '',
        routeBadge: document.getElementById('aux-led-route-badge')?.textContent?.trim() || '',
        ledCountRowDisplay: ledCountRow ? getComputedStyle(ledCountRow).display : 'missing',
      };
    });

    await page.click('#seg-type-aux2 .seg-option[data-value="rgb"]');
    await page.waitForTimeout(220);

    const afterAux2 = await page.evaluate(() => {
      const ledCountRow = document.getElementById('aux-led-count')?.closest('.component-row');
      const toolbar = {
        enabled: document.getElementById('setup-enabled-summary')?.textContent?.trim() || '',
        save: document.getElementById('setup-save-summary')?.textContent?.trim() || '',
      };
      return {
        aux1Type: document.getElementById('type-aux1')?.value || '',
        aux2Type: document.getElementById('type-aux2')?.value || '',
        aux3Type: document.getElementById('type-aux3')?.value || '',
        routeText: document.getElementById('aux-led-route-status')?.textContent?.trim() || '',
        routeBadge: document.getElementById('aux-led-route-badge')?.textContent?.trim() || '',
        ledCountRowDisplay: ledCountRow ? getComputedStyle(ledCountRow).display : 'missing',
        toolbar,
      };
    });

    await page.click('#seg-type-aux2 .seg-option[data-value="none"]');
    await page.waitForTimeout(180);

    const afterClear = await page.evaluate(() => {
      const ledCountRow = document.getElementById('aux-led-count')?.closest('.component-row');
      return {
        aux1Type: document.getElementById('type-aux1')?.value || '',
        aux2Type: document.getElementById('type-aux2')?.value || '',
        aux3Type: document.getElementById('type-aux3')?.value || '',
        routeText: document.getElementById('aux-led-route-status')?.textContent?.trim() || '',
        routeBadge: document.getElementById('aux-led-route-badge')?.textContent?.trim() || '',
        ledCountRowDisplay: ledCountRow ? getComputedStyle(ledCountRow).display : 'missing',
      };
    });

    console.log('SETUP_AUX_ROUTING_START');
    console.log(JSON.stringify({ afterAux1, afterAux2, afterClear }, null, 2));
    console.log('SETUP_AUX_ROUTING_END');
    await page.screenshot({ path: '/tmp/setup-aux-routing.png', fullPage: true });
  } finally {
    await browser.close();
  }
})();

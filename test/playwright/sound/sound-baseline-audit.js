const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/sound.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function collectMetrics(page, label) {
  await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
  await page.waitForSelector('#named-sound-rows tr', { timeout: 10000 });

  const metrics = await page.evaluate(() => {
    const iconOnlyButtons = Array.from(document.querySelectorAll('button'))
      .map((button) => ({
        id: button.id || null,
        text: (button.textContent || '').trim(),
        ariaLabel: button.getAttribute('aria-label') || null,
      }))
      .filter((button) => button.text.length > 0 && !/[A-Za-z0-9]/.test(button.text));

    const tableStats = Array.from(document.querySelectorAll('.sound-table')).map((table) => {
      const rect = table.getBoundingClientRect();
      const parentRect = table.parentElement?.getBoundingClientRect();
      return {
        id: table.id || table.closest('.card')?.querySelector('h3')?.textContent?.trim() || 'unknown',
        tableWidth: Math.round(rect.width),
        parentWidth: parentRect ? Math.round(parentRect.width) : null,
        overflowsParent: parentRect ? rect.width > parentRect.width : false,
      };
    });

    return {
      viewport: { width: window.innerWidth, height: window.innerHeight },
      cards: Array.from(document.querySelectorAll('.card h3')).map((h3) => h3.textContent?.trim()),
      totalCards: document.querySelectorAll('.card').length,
      totalButtons: document.querySelectorAll('button').length,
      iconOnlyButtons,
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth,
      scrollWidth: document.documentElement.scrollWidth,
      viewportWidth: window.innerWidth,
      tableStats,
      feedbackFields: document.querySelectorAll('.feedback').length,
      namedSoundRows: document.querySelectorAll('#named-sound-rows tr').length,
      categoryRows: document.querySelectorAll('#category-sound-rows tr').length,
    };
  });

  await page.screenshot({ path: `/tmp/sound-${label}.png`, fullPage: true });
  return metrics;
}

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  try {
    const desktop = await collectMetrics(page, 'desktop-before');

    await page.setViewportSize({ width: 390, height: 844 });
    const mobile = await collectMetrics(page, 'mobile-before');

    console.log('BASELINE_METRICS_START');
    console.log(JSON.stringify({ desktop, mobile }, null, 2));
    console.log('BASELINE_METRICS_END');
    console.log('Saved screenshots: /tmp/sound-desktop-before.png, /tmp/sound-mobile-before.png');
  } catch (error) {
    console.error('Baseline audit failed:', error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

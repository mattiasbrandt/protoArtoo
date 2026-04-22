const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/sound.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function collectMetrics(page, label) {
  await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
  await page.waitForSelector('#named-sound-rows tr', { timeout: 10000 });

  const metrics = await page.evaluate(() => {
    const iconOnlyButtons = Array.from(document.querySelectorAll('button'))
      .map((button) => ({ text: (button.textContent || '').trim() }))
      .filter((button) => button.text.length > 0 && !/[A-Za-z0-9]/.test(button.text));

    const tableOverflow = Array.from(document.querySelectorAll('.sound-table-wrap')).map((wrap) => ({
      id: wrap.closest('.card')?.querySelector('h3')?.textContent?.trim() || 'unknown',
      clientWidth: Math.round(wrap.clientWidth),
      scrollWidth: Math.round(wrap.scrollWidth),
      hasInternalScroll: wrap.scrollWidth > wrap.clientWidth,
    }));

    const firstNamedRowButtons = Array.from(
      document.querySelectorAll('#named-sound-rows tr:first-child button'),
    ).map((button) => (button.textContent || '').trim());

    return {
      viewport: { width: window.innerWidth, height: window.innerHeight },
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth,
      scrollWidth: document.documentElement.scrollWidth,
      viewportWidth: window.innerWidth,
      iconOnlyButtonCount: iconOnlyButtons.length,
      firstNamedRowButtons,
      tableOverflow,
      filterCountText: document.getElementById('named-sound-filter-count')?.textContent?.trim() || '',
    };
  });

  await page.screenshot({ path: `/tmp/sound-${label}.png`, fullPage: true });
  return metrics;
}

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  try {
    const desktop = await collectMetrics(page, 'desktop-after');

    await page.fill('#named-sound-filter', 'cantina');
    await page.waitForTimeout(150);
    const filterCheck = await page.evaluate(() => {
      const visibleRows = Array.from(document.querySelectorAll('#named-sound-rows tr')).filter(
        (row) => getComputedStyle(row).display !== 'none',
      );
      return {
        visibleCount: visibleRows.length,
        visibleLabels: visibleRows.map((row) => row.querySelector('td')?.textContent?.trim() || ''),
        counter: document.getElementById('named-sound-filter-count')?.textContent?.trim() || '',
      };
    });

    await page.setViewportSize({ width: 390, height: 844 });
    const mobile = await collectMetrics(page, 'mobile-after');

    console.log('POST_CHANGE_METRICS_START');
    console.log(JSON.stringify({ desktop, filterCheck, mobile }, null, 2));
    console.log('POST_CHANGE_METRICS_END');
    console.log('Saved screenshots: /tmp/sound-desktop-after.png, /tmp/sound-mobile-after.png');
  } catch (error) {
    console.error('Post-change audit failed:', error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

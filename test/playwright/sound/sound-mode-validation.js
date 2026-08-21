const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/sound.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function getModeState(page) {
  return page.evaluate(() => {
    const isVisible = (el) => {
      if (!el) return false;
      const style = getComputedStyle(el);
      return style.display !== 'none' && style.visibility !== 'hidden';
    };

    const advancedCards = Array.from(document.querySelectorAll('.sound-advanced-card'));
    const visibleAdvancedCards = advancedCards.filter(isVisible);
    const activeButton = Array.from(
      document.querySelectorAll('#sound-mode-advanced, #sound-mode-compact'),
    ).find((btn) => btn.getAttribute('aria-pressed') === 'true');

    return {
      activeModeButton: activeButton?.id || null,
      modeFeedback: document.getElementById('sound-mode-feedback')?.textContent?.trim() || '',
      advancedCardsVisible: visibleAdvancedCards.length,
      advancedCardsTotal: advancedCards.length,
      bodyClass: document.body.className,
    };
  });
}

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#named-sound-rows tr', { timeout: 10000 });

    await page.evaluate(() => {
      localStorage.removeItem('pa.sound.viewMode');
    });
    await page.reload({ waitUntil: 'networkidle' });
    await page.waitForSelector('#named-sound-rows tr', { timeout: 10000 });

    const initial = await getModeState(page);
    await page.screenshot({ path: '/tmp/sound-mode-advanced-desktop.png', fullPage: true });

    await page.click('#sound-mode-compact');
    await page.waitForTimeout(120);
    const compact = await getModeState(page);
    await page.screenshot({ path: '/tmp/sound-mode-compact-desktop.png', fullPage: true });

    await page.click('#sound-mode-advanced');
    await page.waitForTimeout(120);
    const backToAdvanced = await getModeState(page);

    await page.fill('#named-sound-filter', 'cantina');
    await page.waitForTimeout(120);
    const filterState = await page.evaluate(() => {
      const visibleRows = Array.from(document.querySelectorAll('#named-sound-rows tr'))
        .filter((row) => getComputedStyle(row).display !== 'none')
        .map((row) => row.querySelector('td')?.textContent?.trim() || '');
      return {
        counter: document.getElementById('named-sound-filter-count')?.textContent?.trim() || '',
        visibleRows,
      };
    });

    console.log('MODE_VALIDATION_START');
    console.log(JSON.stringify({ initial, compact, backToAdvanced, filterState }, null, 2));
    console.log('MODE_VALIDATION_END');
    console.log('Saved screenshots: /tmp/sound-mode-advanced-desktop.png, /tmp/sound-mode-compact-desktop.png');
  } catch (error) {
    console.error('Mode validation failed:', error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/sound.html';
const HEADLESS = process.env.HEADLESS === 'true';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#named-sound-rows tr', { timeout: 10000 });

    const initial = await page.evaluate(() => {
      const namedRow = document.querySelector('#named-sound-rows tr');
      const categoryRow = document.querySelector('#category-sound-rows tr');
      return {
        namedDirtyVisible: !!namedRow?.querySelector('.sound-dirty-pill:not(.hidden)'),
        namedDirtyClass: namedRow?.classList.contains('sound-row-dirty') || false,
        categoryDirtyVisible: !!categoryRow?.querySelector('.sound-dirty-pill:not(.hidden)'),
        categoryDirtyClass: categoryRow?.classList.contains('sound-row-dirty') || false,
      };
    });

    await page.fill('#track-input-scream', '7');
    await page.fill('#cat-min-snd_cat_gen_lo', '3');
    await page.waitForTimeout(120);

    const afterEdit = await page.evaluate(() => {
      const namedRow = document.querySelector('#named-sound-rows tr');
      const categoryRow = document.querySelector('#category-sound-rows tr');
      return {
        namedDirtyVisible: !!namedRow?.querySelector('.sound-dirty-pill:not(.hidden)'),
        namedDirtyClass: namedRow?.classList.contains('sound-row-dirty') || false,
        categoryDirtyVisible: !!categoryRow?.querySelector('.sound-dirty-pill:not(.hidden)'),
        categoryDirtyClass: categoryRow?.classList.contains('sound-row-dirty') || false,
      };
    });

    await page.fill('#track-input-scream', '');
    await page.fill('#cat-min-snd_cat_gen_lo', '0');
    await page.waitForTimeout(120);

    const afterRevert = await page.evaluate(() => {
      const namedRow = document.querySelector('#named-sound-rows tr');
      const categoryRow = document.querySelector('#category-sound-rows tr');
      return {
        namedDirtyVisible: !!namedRow?.querySelector('.sound-dirty-pill:not(.hidden)'),
        namedDirtyClass: namedRow?.classList.contains('sound-row-dirty') || false,
        categoryDirtyVisible: !!categoryRow?.querySelector('.sound-dirty-pill:not(.hidden)'),
        categoryDirtyClass: categoryRow?.classList.contains('sound-row-dirty') || false,
      };
    });

    await page.screenshot({ path: '/tmp/sound-dirty-state.png', fullPage: true });

    console.log('DIRTY_STATE_CHECK_START');
    console.log(JSON.stringify({ initial, afterEdit, afterRevert }, null, 2));
    console.log('DIRTY_STATE_CHECK_END');
  } catch (error) {
    console.error('Dirty state test failed:', error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

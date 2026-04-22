const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/dome.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function collect(page, label) {
  await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
  await page.waitForSelector('.dome-live-track', { timeout: 10000 });
  await page.waitForTimeout(500);

  const snapshot = await page.evaluate(() => {
    const speed = document.getElementById('dome-speed-display');
    const domeFeedback = document.getElementById('dome-feedback');
    const escFeedback = document.getElementById('esc-feedback');
    const disabledCard = document.getElementById('dome-disabled-card');
    const sectionHeadings = Array.from(document.querySelectorAll('.card h3')).map((el) => (el.textContent || '').trim());
    const sliderLabels = Array.from(document.querySelectorAll('.slider-labels span')).map((el) => (el.textContent || '').trim());

    return {
      viewport: { width: window.innerWidth, height: window.innerHeight },
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth,
      scrollWidth: document.documentElement.scrollWidth,
      sectionHeadings,
      sliderLabels,
      hasLegacySlider: Boolean(document.getElementById('dome-slider')),
      hasLiveTrack: Boolean(document.querySelector('.dome-live-track')),
      liveFillWidth: getComputedStyle(document.getElementById('dome-live-fill')).width,
      speedValue: speed ? speed.textContent.trim() : '',
      hardwarePill: document.getElementById('dome-hardware-pill')?.textContent?.trim() || '',
      webPill: document.getElementById('dome-web-pill')?.textContent?.trim() || '',
      rotationState: document.getElementById('dome-rotation-state')?.textContent?.trim() || '',
      domeFeedback: domeFeedback
        ? { text: domeFeedback.textContent.trim(), className: domeFeedback.className }
        : null,
      escFeedback: escFeedback
        ? { text: escFeedback.textContent.trim(), className: escFeedback.className }
        : null,
      disabledCardHidden: disabledCard ? disabledCard.classList.contains('hidden') : null,
      iconOnlyButtons: Array.from(document.querySelectorAll('button'))
        .map((btn) => ({ id: btn.id || null, text: (btn.textContent || '').trim() }))
        .filter((btn) => btn.text.length > 0 && !/[A-Za-z0-9]/.test(btn.text)),
    };
  });

  await page.screenshot({ path: `/tmp/dome-${label}.png`, fullPage: true });
  return snapshot;
}

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 50 });
  const page = await browser.newPage({ viewport: { width: 1680, height: 980 } });
  const pageErrors = [];
  const consoleErrors = [];

  page.on('pageerror', (err) => pageErrors.push(String(err)));
  page.on('console', (msg) => {
    if (msg.type() === 'error') consoleErrors.push(msg.text());
  });

  try {
    const desktop = await collect(page, 'desktop-audit');

    await page.setViewportSize({ width: 1180, height: 900 });
    const tablet = await collect(page, 'tablet-audit');

    console.log('DOME_AUDIT_START');
    console.log(JSON.stringify({ desktop, tablet, pageErrors, consoleErrors }, null, 2));
    console.log('DOME_AUDIT_END');
    console.log('Saved screenshots: /tmp/dome-desktop-audit.png, /tmp/dome-tablet-audit.png');
  } catch (error) {
    console.error('Dome audit failed:', error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

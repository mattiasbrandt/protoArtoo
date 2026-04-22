const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/setup.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function collect(page, label) {
  await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
  await page.waitForSelector('#feature-form', { timeout: 10000 });
  await page.waitForTimeout(700);

  const metrics = await page.evaluate(() => {
    const iconOnlyButtons = Array.from(document.querySelectorAll('button'))
      .map((btn) => ({ id: btn.id || null, text: (btn.textContent || '').trim() }))
      .filter((btn) => btn.text.length > 0 && !/[A-Za-z0-9]/.test(btn.text));

    const sectionLabels = Array.from(document.querySelectorAll('.section-label')).map((el) =>
      (el.textContent || '').trim(),
    );

    const toggles = Array.from(document.querySelectorAll('.toggle-switch input[type="checkbox"]')).map((input) => ({
      id: input.id,
      checked: input.checked,
      status: input.closest('.toggle-switch')?.querySelector('.toggle-status')?.textContent?.trim() || '',
    }));

    return {
      viewport: { width: window.innerWidth, height: window.innerHeight },
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth,
      scrollWidth: document.documentElement.scrollWidth,
      sectionLabels,
      cards: Array.from(document.querySelectorAll('.card h3')).map((el) => (el.textContent || '').trim()),
      iconOnlyButtons,
      componentRows: document.querySelectorAll('.component-row').length,
      toggleCount: toggles.length,
      enabledToggles: toggles.filter((t) => t.checked).length,
      featureFeedback: {
        text: document.getElementById('feature-feedback')?.textContent?.trim() || '',
        className: document.getElementById('feature-feedback')?.className || '',
      },
      serialFeedback: {
        text: document.getElementById('serial-status-line')?.textContent?.trim() || '',
        className: document.getElementById('serial-status-line')?.className || '',
      },
      diagFeedback: {
        text: document.getElementById('diag-feedback')?.textContent?.trim() || '',
        className: document.getElementById('diag-feedback')?.className || '',
      },
    };
  });

  await page.screenshot({ path: `/tmp/setup-${label}.png`, fullPage: true });
  return metrics;
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

    await page.evaluate(() => {
      const input = document.getElementById('enable-arm1');
      if (!input) return;
      input.checked = !input.checked;
      input.dispatchEvent(new Event('change', { bubbles: true }));
    });
    await page.waitForTimeout(450);
    const interaction = await page.evaluate(() => ({
      arm1Checked: document.getElementById('enable-arm1')?.checked || false,
      arm1Status: document.getElementById('status-arm1')?.textContent?.trim() || '',
      feedbackText: document.getElementById('feature-feedback')?.textContent?.trim() || '',
      feedbackClass: document.getElementById('feature-feedback')?.className || '',
    }));

    await page.setViewportSize({ width: 1100, height: 900 });
    const tablet = await collect(page, 'tablet-audit');

    console.log('SETUP_AUDIT_START');
    console.log(JSON.stringify({ desktop, interaction, tablet, pageErrors, consoleErrors }, null, 2));
    console.log('SETUP_AUDIT_END');
    console.log('Saved screenshots: /tmp/setup-desktop-audit.png, /tmp/setup-tablet-audit.png');
  } catch (error) {
    console.error('Setup audit failed:', error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

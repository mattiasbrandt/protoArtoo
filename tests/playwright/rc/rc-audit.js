const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS === 'true';

async function collect(page, label) {
  await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
  await page.waitForSelector('.rc-mode-grid', { timeout: 10000 });
  await page.waitForTimeout(600);

  const metrics = await page.evaluate(() => {
    const isVisible = (el) => {
      if (!el) return false;
      const style = getComputedStyle(el);
      return style.display !== 'none' && style.visibility !== 'hidden';
    };

    const iconOnlyButtons = Array.from(document.querySelectorAll('button'))
      .map((button) => {
        const text = (button.textContent || '').trim();
        return { id: button.id || null, text };
      })
      .filter((button) => button.text.length > 0 && !/[A-Za-z0-9]/.test(button.text));

    const cards = Array.from(document.querySelectorAll('.card h3')).map((h3) => h3.textContent?.trim() || '');
    const feedback = Array.from(document.querySelectorAll('.feedback')).map((n) => ({
      id: n.id || null,
      text: (n.textContent || '').trim(),
      classes: n.className,
      visible: isVisible(n),
    }));

    const summaryHeaders = Array.from(document.querySelectorAll('.rc-summary-table thead th')).map((th) =>
      th.textContent?.trim(),
    );
    const summaryRows = Array.from(document.querySelectorAll('#rc-summary-body tr')).map((tr) => {
      const cells = Array.from(tr.querySelectorAll('td')).map((td) => (td.textContent || '').trim());
      return { cellCount: cells.length, first: cells[0] || '' };
    });

    const mapper = document.querySelector('.rc-mapper-grid');
    const mapperStyle = mapper ? getComputedStyle(mapper) : null;

    return {
      viewport: { width: window.innerWidth, height: window.innerHeight },
      bodyScrollWidth: document.documentElement.scrollWidth,
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth,
      cards,
      iconOnlyButtons,
      feedback,
      summaryHeaders,
      summaryRows,
      modeCards: Array.from(document.querySelectorAll('.rc-mode-card')).map((card) => ({
        mode: card.getAttribute('data-mode'),
        pressed: card.getAttribute('aria-pressed'),
        selectedClass: card.classList.contains('selected'),
      })),
      mapperColumns: mapperStyle ? mapperStyle.gridTemplateColumns : null,
      mapperWidth: mapper ? Math.round(mapper.getBoundingClientRect().width) : null,
      panelTitles: Array.from(document.querySelectorAll('.rc-panel-title')).map(
        (n) => n.textContent?.trim() || '',
      ),
      learnBannerVisible: isVisible(document.getElementById('rc-learn-banner')),
      disabledCardVisible: isVisible(document.getElementById('rc-disabled-card')),
    };
  });

  await page.screenshot({ path: `/tmp/rc-${label}.png`, fullPage: true });
  return metrics;
}

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 60 });
  const page = await browser.newPage({ viewport: { width: 1680, height: 980 } });
  const consoleErrors = [];

  page.on('console', (msg) => {
    if (msg.type() === 'error') {
      consoleErrors.push(msg.text());
    }
  });

  try {
    const desktop = await collect(page, 'desktop-audit');
    await page.setViewportSize({ width: 1100, height: 900 });
    const tablet = await collect(page, 'tablet-audit');

    console.log('RC_AUDIT_START');
    console.log(JSON.stringify({ desktop, tablet, consoleErrors }, null, 2));
    console.log('RC_AUDIT_END');
    console.log('Saved screenshots: /tmp/rc-desktop-audit.png, /tmp/rc-tablet-audit.png');
  } catch (error) {
    console.error('RC audit failed:', error.message);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

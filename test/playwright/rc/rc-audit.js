// bench-auto: fixture rc.html
// A layout audit of RC at desktop width: no sideways scroll, every button
// carries words, the Bindings table has its three columns, the source list and
// the mapping editor sit side by side, and the page logs no error of its own.
//
// The audit used to wait for the receiver-type grid (`.rc-mode-grid`) and
// record its mode cards. The type is now chosen on Configuration and RC shows
// it (data/rc.html "Receiver type"; operator, 2026-09-18 on #369), so the
// audit waits for the source list instead and records the type the card
// reads. Its second pass at 1100 px is gone: the operator surfaces are
// desktop-only, and a tablet width is not a width this project checks.
//
// The fixture server answers no /api/* route RC reads, so the page meets a
// droid that does not answer: its "Failed to load resource" lines are the
// fixture's 404s, not the page's errors, and are not counted.
const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS !== 'false';

async function collect(page, label) {
  await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
  await page.waitForSelector('#rc-channel-items .rc-channel-item', { timeout: 10000 });
  await page.waitForTimeout(600);

  const metrics = await page.evaluate(() => {
    const isVisible = (el) => {
      if (!el || el.hidden) return false;
      const style = getComputedStyle(el);
      return style.display !== 'none' && style.visibility !== 'hidden';
    };

    const iconOnlyButtons = Array.from(document.querySelectorAll('button'))
      .map((button) => {
        const text = (button.textContent || '').trim();
        return { id: button.id || null, text };
      })
      .filter((button) => button.text.length > 0 && !/[A-Za-z0-9]/.test(button.text));

    const cards = Array.from(document.querySelectorAll('.card .sect > h2, .card .sect > h3')).map((h) => h.textContent?.trim() || '');
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
      const cells = Array.from(tr.querySelectorAll('td'));
      return {
        cellCount: cells.length,
        span: cells.reduce((sum, td) => sum + (td.colSpan || 1), 0),
        first: (cells[0]?.textContent || '').trim(),
      };
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
      receiverType: document.getElementById('rc-mode-summary')?.textContent?.trim() || '',
      sources: document.querySelectorAll('#rc-channel-items .rc-channel-item').length,
      mapperColumns: mapperStyle ? mapperStyle.gridTemplateColumns : null,
      mapperWidth: mapper ? Math.round(mapper.getBoundingClientRect().width) : null,
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
  const failures = [];
  const check = (ok, message) => {
    if (!ok) failures.push(message);
  };

  page.on('console', (msg) => {
    if (msg.type() === 'error' && !msg.text().startsWith('Failed to load resource:')) {
      consoleErrors.push(msg.text());
    }
  });

  try {
    const desktop = await collect(page, 'desktop-audit');

    console.log('RC_AUDIT_START');
    console.log(JSON.stringify({ desktop, consoleErrors }, null, 2));
    console.log('RC_AUDIT_END');
    console.log('Saved screenshot: /tmp/rc-desktop-audit.png');

    check(!desktop.horizontalOverflow,
      `the page scrolls sideways: ${desktop.bodyScrollWidth} px wide in a ${desktop.viewport.width} px window`);
    check(desktop.iconOnlyButtons.length === 0, `buttons with no words: ${JSON.stringify(desktop.iconOnlyButtons)}`);
    check(JSON.stringify(desktop.summaryHeaders) === JSON.stringify(['Action', 'Source', 'Live']),
      `the Bindings table heads read ${JSON.stringify(desktop.summaryHeaders)}`);
    check(desktop.summaryRows.length > 0 && desktop.summaryRows.every((row) => row.span === desktop.summaryHeaders.length),
      `a Bindings row does not span the table's columns: ${JSON.stringify(desktop.summaryRows)}`);
    check(desktop.receiverType !== '', 'the Receiver type card reads nothing');
    check(desktop.sources > 0, 'the source list is empty');
    check((desktop.mapperColumns || '').trim().split(/\s+/).length === 2,
      `the source list and the mapping editor are not side by side: columns "${desktop.mapperColumns}"`);
    check(!desktop.learnBannerVisible, 'the Detect channel banner shows before Detect channel was pressed');
    check(consoleErrors.length === 0, `the page logged errors: ${JSON.stringify(consoleErrors)}`);
  } catch (error) {
    failures.push(`RC audit failed: ${error.message}`);
  } finally {
    await browser.close();
  }

  failures.forEach((message) => console.error(`FAIL ${message}`));
  console.log(failures.length === 0 ? 'PASS rc audit' : `${failures.length} failed`);
  process.exitCode = failures.length === 0 ? 0 : 1;
})();

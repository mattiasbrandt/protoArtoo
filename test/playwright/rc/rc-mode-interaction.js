// bench-auto: fixture rc.html
// The receiver type on RC: what the page shows for a Single SBUS droid, and
// the one receiver control the page still owns.
//
// The page used to pick the receiver type itself, from a grid of mode cards
// (`.rc-mode-card`). That choice moved to the Radio Controller cards on
// Configuration (data/component_picker.js; operator, 2026-09-18 on #369), and
// RC now SHOWS the type and links there. So instead of clicking the
// single_sbus card, the script answers GET /api/config as a Single SBUS droid
// and checks the page says so, and drives what Single SBUS still draws here:
// the SBUS1 / SBUS2 input, which saves one key at once (data/rc.js).
//
// Writes nothing past the browser: the one POST /api/config is answered here.
const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/rc.html';
const HEADLESS = process.env.HEADLESS !== 'false';

const CONFIG = { rc: { inputMode: 'single_sbus', sbus: { recvCh2: false } }, components: {} };

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1600, height: 950 } });
  const failures = [];
  const check = (ok, message) => {
    if (!ok) failures.push(message);
  };
  const posts = [];

  try {
    await page.route('**/api/config', (route) => {
      const request = route.request();
      if (request.method() === 'GET') {
        return route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(CONFIG) });
      }
      posts.push(request.postData() || '');
      return route.fulfill({ status: 200, contentType: 'application/json', body: '{"ok":true}' });
    });

    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('#sbus-recv-seg', { timeout: 10000 });
    await page.waitForFunction(() => document.getElementById('rc-mode-summary')?.textContent?.trim(), null, { timeout: 10000 });
    await page.waitForTimeout(400);

    const readState = () => page.evaluate(() => ({
      modeSummary: document.getElementById('rc-mode-summary')?.textContent?.trim() || '',
      inputMode: document.getElementById('rc-input-mode')?.value || '',
      feedbackText: document.getElementById('rc-mode-feedback')?.textContent?.trim() || '',
      feedbackClass: document.getElementById('rc-mode-feedback')?.className || '',
      changeLink: document.querySelector('#rc-receiver-card')?.closest('.card')
        ?.querySelector('a[href="#configuration"]')?.textContent?.trim() || '',
      sbusInputShown: !document.getElementById('single-sbus-recv-section')?.classList.contains('hidden'),
      sbusInputChecked: Array.from(document.querySelectorAll('#sbus-recv-seg [role="radio"]'))
        .filter((button) => button.getAttribute('aria-checked') === 'true')
        .map((button) => button.textContent?.trim() || ''),
      sbusFeedback: document.getElementById('sbus-recv-feedback')?.textContent?.trim() || '',
      cardTitles: Array.from(document.querySelectorAll('.card .sect > h2, .card .sect > h3')).map((n) => n.textContent?.trim() || ''),
      actionButtons: {
        reset: document.getElementById('rc-reset-defaults')?.textContent?.trim() || '',
        apply: document.getElementById('rc-editor-apply')?.textContent?.trim() || '',
        revert: document.getElementById('rc-editor-revert')?.textContent?.trim() || '',
      },
    }));

    const shown = await readState();
    await page.click('#sbus-recv-seg [data-value="true"]');
    // A save that never reports is a failure of its own, not a hang: the
    // checks below then say what the input read instead.
    await page
      .waitForFunction(() => /Saved at/.test(document.getElementById('sbus-recv-feedback')?.textContent || ''), null, { timeout: 8000 })
      .then(() => true, (error) => {
        check(false, `the SBUS2 pick never said "Saved at": ${String(error.message).split('\n')[0]}`);
        return false;
      });
    const afterPick = await readState();

    console.log('RC_MODE_INTERACTION_START');
    console.log(JSON.stringify({ shown, afterPick, posts }, null, 2));
    console.log('RC_MODE_INTERACTION_END');
    await page.screenshot({ path: '/tmp/rc-mode-interaction.png', fullPage: true });

    check(shown.modeSummary === 'Single SBUS', `the Receiver type card reads "${shown.modeSummary}", expected "Single SBUS"`);
    check(shown.inputMode === 'single_sbus', `the page holds mode "${shown.inputMode}", expected single_sbus`);
    check(shown.feedbackText === 'Receiver type: Single SBUS', `the receiver feedback reads "${shown.feedbackText}"`);
    check(shown.changeLink !== '', 'the Receiver type card has no link to Configuration, where the type is now chosen');
    check(shown.sbusInputShown, 'Single SBUS does not show the SBUS1 / SBUS2 input');
    check(shown.sbusInputChecked.length === 1 && /^SBUS1/.test(shown.sbusInputChecked[0]),
      `the SBUS input reads ${JSON.stringify(shown.sbusInputChecked)}, expected SBUS1 from the config`);
    check(Object.values(shown.actionButtons).every(Boolean), `an editor action has no words: ${JSON.stringify(shown.actionButtons)}`);
    check(posts.length === 1 && JSON.stringify(JSON.parse(posts[0] || 'null')) === '{"rc":{"sbus":{"recvCh2":true}}}',
      `picking SBUS2 sent ${JSON.stringify(posts)}, expected one {"rc":{"sbus":{"recvCh2":true}}}`);
    check(afterPick.sbusInputChecked.length === 1 && /^SBUS2/.test(afterPick.sbusInputChecked[0]) && /^Saved at /.test(afterPick.sbusFeedback),
      `after picking SBUS2 the input reads ${JSON.stringify(afterPick.sbusInputChecked)} and says "${afterPick.sbusFeedback}"`);
  } finally {
    await browser.close();
  }

  failures.forEach((message) => console.error(`FAIL ${message}`));
  console.log(failures.length === 0 ? 'PASS rc receiver type' : `${failures.length} failed`);
  process.exitCode = failures.length === 0 ? 0 : 1;
})();

// Availability Family treatments, read from a real browser (#341).
//
// test/test_web/test_style_token_layer.js reads the stylesheet; this reads what
// Chromium actually paints. The two catch different things: the parser catches
// the literal you wrote, and a computed-style probe catches the token you
// pointed at the wrong thing, or a rule a later one quietly overrode. The probe
// technique - paint a throwaway element with var(--token), read its computed
// colour, compare - is the reference project's own enforcement
// (r2d2-astromech-simulator v1.79.0, tests/chrome.test.js:509).
//
// Run against tools/serve_editor_fixture.py: setup.html carries a PA:INCLUDE
// that a plain static server does not expand, and none of its scripts load
// without it.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const { mkdirSync } = require('node:fs');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/setup.html';
const HEADLESS = process.env.HEADLESS === 'true';
const ARTIFACT_DIR = 'output/playwright/issue-341';

const identity = {
  droidName: 'artoo',
  mdnsUseName: true,
  board: 'artoo_esp32',
  board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
  build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
};

const config = {
  components: {
    arm1: { enabled: true, type: 'mg996r' },
    arm2: { enabled: false, type: 'mg996r' },
    drive: { enabled: true },
    audio: { enabled: true },
  },
  system: { logLevel: 3 },
};

// What a row is wearing, as the browser computed it.
const READ = `(row) => {
  const s = getComputedStyle(row);
  return {
    borderLeftStyle: s.borderLeftStyle,
    borderLeftColor: s.borderLeftColor,
    backgroundImage: s.backgroundImage,
    opacity: s.opacity,
    // While a page resource is still in flight the Page Recovery View dims the
    // whole body to 0.4, so an opacity read during "checking" is the backdrop's
    // and not this rule's. Recorded rather than hidden: the driven states below
    // are asserted on state, rail and colour, and the four families' brightness
    // is measured on the bare-class probes, which no backdrop covers.
    recoveryActive: document.body.classList.contains('recovery-active'),
  };
}`;

(async () => {
  mkdirSync(ARTIFACT_DIR, { recursive: true });
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 35 });
  const page = await browser.newPage({ viewport: { width: 1080, height: 900 } });
  const pageErrors = [];
  page.on('pageerror', (error) => pageErrors.push(String(error)));

  let identityMode = 'ok';
  await page.route('**/api/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/api/events') {
      await route.fulfill({ status: 200, contentType: 'text/event-stream', body: 'event: status\ndata: {}\n\n' });
      return;
    }
    if (path === '/api/identity') {
      if (identityMode === 'slow') {
        // Held long enough to read "checking", and not so long that the Page
        // Recovery View takes over - that backdrop dims the whole body to 0.4
        // and would be read as this rule's opacity.
        await new Promise((resolve) => setTimeout(resolve, 1500));
      }
      if (identityMode === 'error') {
        await route.fulfill({ status: 503, contentType: 'application/json', body: '{}' });
        return;
      }
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(identity) });
      return;
    }
    const body = path === '/api/config' ? config : {};
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
  });

  try {
    // The colours that must not appear on a way of saying no, resolved by the
    // browser from the tokens rather than pasted in as hex.
    await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(() => Boolean(window.PAFeatureAvailability), null, { timeout: 10000 });
    const reserved = await page.evaluate(() => {
      const probe = (value) => {
        const el = document.createElement('i');
        el.style.color = value;
        document.body.appendChild(el);
        const computed = getComputedStyle(el).color;
        el.remove();
        return computed;
      };
      return { amber: probe('var(--warning)'), red: probe('var(--danger)') };
    });

    // Each shipped state, driven through the real resolver, beside the bare
    // family class it is supposed to resolve to.
    const stateOf = async (mode, target, setup) => {
      identityMode = mode;
      await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded' });
      await page.waitForFunction(() => Boolean(window.PAFeatureAvailability), null, { timeout: 10000 });
      return page.evaluate(
        async ([apply, read, which]) => {
          const el =
            which === 'panel'
              ? document.getElementById('profiler-card')
              : document.querySelector('[data-feature-entry="system.config.enable_arm1"]');
          // eslint-disable-next-line no-eval
          eval(`(${apply})`)(el);
          await new Promise((resolve) => setTimeout(resolve, 400));
          // eslint-disable-next-line no-eval
          return { state: el.dataset.featureState, style: eval(`(${read})`)(el) };
        },
        [setup, READ, target],
      );
    };

    const observed = {
      off: await stateOf('ok', 'row', '(row) => { document.getElementById("enable-arm1").checked = false; window.PAFeatureAvailability.setIdentity(window.PAIdentity); }'),
      notOnThisBoard: await stateOf('ok', 'row', '(row) => { row.dataset.boardCapability = "PA_CAP_HOSTED_WIFI"; window.PAFeatureAvailability.setIdentity(window.PAIdentity); }'),
      notInThisBuild: await stateOf('ok', 'row', '(row) => { row.dataset.buildFlag = "PA_HEAP_PROFILE"; window.PAFeatureAvailability.setIdentity(window.PAIdentity); }'),
      // checking and identity-unavailable are only reachable through the
      // manifest fetch itself, so they are read off the profiler panel, whose
      // requirement metadata is declared in setup.html and therefore present
      // before the fetch settles. The panel is the other element the families
      // dress, so this reads both shapes rather than only the row.
      checking: await stateOf('slow', 'panel', '() => {}'),
      identityUnavailable: await stateOf('error', 'panel', '() => {}'),
    };

    // The four family classes on identical probe rows, plus the hover lift.
    const families = await page.evaluate(
      async ([read]) => {
        const classes = ['availability-change-here', 'availability-change-elsewhere', 'availability-finding-out', 'availability-settled-no'];
        const host = document.createElement('div');
        document.body.appendChild(host);
        const out = {};
        for (const cls of classes) {
          const el = document.createElement('div');
          el.className = `component-row feature-availability-row ${cls}`;
          host.appendChild(el);
          // eslint-disable-next-line no-eval
          out[cls] = eval(`(${read})`)(el);
        }
        host.remove();
        return out;
      },
      [READ],
    );

    const spendsReserved = (style) =>
      [style.borderLeftColor, style.backgroundImage].some(
        (value) => value.includes(reserved.amber) || value.includes(reserved.red),
      );

    const expectedStates = {
      off: 'off',
      notOnThisBoard: 'not-on-this-board',
      notInThisBuild: 'not-in-this-build',
      checking: 'checking',
      identityUnavailable: 'identity-unavailable',
    };
    for (const [name, { state, style }] of Object.entries(observed)) {
      assert.equal(state, expectedStates[name], `${name} did not reach the state it was driving at`);
      assert.equal(spendsReserved(style), false, `${name} (${state}) still paints a reserved colour: ${JSON.stringify(style)}`);
    }
    for (const [cls, style] of Object.entries(families)) {
      assert.equal(spendsReserved(style), false, `.${cls} paints a reserved colour: ${JSON.stringify(style)}`);
    }

    const fingerprints = Object.entries(families).map(([cls, style]) => [cls, JSON.stringify(style)]);
    for (let i = 0; i < fingerprints.length; i += 1) {
      for (let j = i + 1; j < fingerprints.length; j += 1) {
        assert.notEqual(fingerprints[i][1], fingerprints[j][1], `.${fingerprints[i][0]} and .${fingerprints[j][0]} render identically`);
      }
    }

    // A dimmed family stays inspectable: hover lifts it back.
    const lift = await page.evaluate(async () => {
      const el = document.createElement('div');
      el.className = 'component-row feature-availability-row availability-change-elsewhere';
      // A real box: :hover only fires when the pointer is inside one, and an
      // empty row collapses to zero height.
      el.style.height = '60px';
      el.id = 'availability-lift-probe';
      document.body.appendChild(el);
      el.scrollIntoView();
      return { before: getComputedStyle(el).opacity, id: el.id };
    });
    await page.hover('#availability-lift-probe');
    await page.waitForTimeout(400); // the row transitions its opacity over 0.18s
    const after = await page.evaluate(() => getComputedStyle(document.getElementById('availability-lift-probe')).opacity);
    assert.ok(Number(after) > Number(lift.before), `hover should lift a dimmed family: ${lift.before} -> ${after}`);

    identityMode = 'ok';
    await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('#profiler-card[data-feature-state]', { timeout: 10000 });
    await page.screenshot({ path: `${ARTIFACT_DIR}/availability-families.png`, fullPage: true });

    assert.deepEqual(pageErrors, [], 'the page threw while the families were rendered');

    console.log('SETUP_AVAILABILITY_FAMILIES_START');
    console.log(JSON.stringify({ reserved, observed, families, lift: { before: lift.before, after } }, null, 2));
    console.log('SETUP_AVAILABILITY_FAMILIES_END');
    console.log(`Saved screenshot under ${ARTIFACT_DIR}`);
  } catch (error) {
    console.error('Availability family check failed:', error);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

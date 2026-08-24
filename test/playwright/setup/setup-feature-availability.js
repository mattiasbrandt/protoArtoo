const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const { mkdirSync } = require('node:fs');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/setup.html';
const HEADLESS = process.env.HEADLESS === 'true';
const ARTIFACT_DIR = 'output/playwright/issue-186';

const identities = {
  default: {
    droidName: 'artoo',
    mdnsUseName: true,
    board: 'artoo_esp32',
    board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
    build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
  },
  profiler: {
    droidName: 'artoo',
    mdnsUseName: true,
    board: 'artoo_esp32',
    board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
    build_flags: { PA_HEAP_PROFILE: true, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
  },
};

const config = {
  components: {
    arm1: { enabled: true, type: 'mg996r' },
    arm2: { enabled: false, type: 'mg996r' },
    aux1: { enabled: false, type: 'none' },
    aux2: { enabled: false, type: 'none' },
    aux3: { enabled: false, type: 'none' },
    dome: { enabled: true },
    rcCh1: { enabled: true },
    rcCh2: { enabled: false },
    rcCh3: { enabled: false },
    rcCh4: { enabled: false },
    rcCh5: { enabled: false },
    rcCh6: { enabled: false },
    s1Hoverboard: { enabled: true },
    s2Sound: { enabled: true },
    s3DomeCtrl: { enabled: true },
  },
  system: { logLevel: 3 },
  aux_led_pin: 0,
  aux_led_count: 1,
};

const profiler = {
  heapFree: 126976,
  heapMin: 110592,
  heapLargest: 98304,
  fragRatio: 0.226,
  allocBlocks: 408,
  freeBlocks: 122,
  failedAllocs: 0,
  taskStacks: [
    { name: 'WebServer', hwmBytes: 2784, status: 'ok' },
    { name: 'DriveTask', hwmBytes: 1968, status: 'watch' },
  ],
  snapshots: [{ label: 'boot', heapFree: 110592, largestBlock: 90112, ts: 1280 }],
};

(async () => {
  mkdirSync(ARTIFACT_DIR, { recursive: true });
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 35 });
  const page = await browser.newPage({ viewport: { width: 1080, height: 800 } });
  let activeIdentity = identities.default;
  let identityRequests = 0;
  let profilerRequests = 0;
  const pageErrors = [];
  const consoleErrors = [];

  page.on('pageerror', (error) => pageErrors.push(String(error)));
  page.on('console', (message) => {
    if (message.type() === 'error') consoleErrors.push(message.text());
  });

  await page.route('**/api/**', async (route) => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/api/events') {
      await route.fulfill({
        status: 200,
        contentType: 'text/event-stream',
        body: 'event: status\ndata: {}\n\n',
      });
      return;
    }
    let body = {};
    if (path === '/api/identity') {
      identityRequests += 1;
      body = activeIdentity;
    } else if (path === '/api/config') {
      body = config;
    } else if (path === '/api/profiler') {
      profilerRequests += 1;
      body = profiler;
    } else if (path === '/api/status') {
      body = { uptimeMs: 42000, heapFree: 126976, heapMin: 110592, heapLargest: 98304 };
    }
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
  });

  const openScenario = async (name, identity) => {
    activeIdentity = identity;
    const identityBefore = identityRequests;
    const profilerBefore = profilerRequests;
    await page.goto(TARGET_URL, { waitUntil: 'domcontentloaded' });
    await page.waitForSelector('#profiler-card[data-feature-state]', { timeout: 10000 });
    await page.waitForTimeout(350);
    await page.screenshot({ path: `${ARTIFACT_DIR}/${name}.png`, fullPage: true });
    return {
      identityRequests: identityRequests - identityBefore,
      profilerRequests: profilerRequests - profilerBefore,
      state: await page.locator('#profiler-card').getAttribute('data-feature-state'),
      status: (await page.locator('#profiler-availability-status').textContent()).trim(),
      reason: (await page.locator('#profiler-availability-reason').textContent()).trim(),
      visible: await page.locator('#profiler-card').isVisible(),
      overflow: await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth),
    };
  };

  try {
    const defaultBuild = await openScenario('default-build', identities.default);
    assert.deepEqual(defaultBuild, {
      identityRequests: 1,
      profilerRequests: 0,
      state: 'not-in-this-build',
      status: 'Not in this build',
      reason: 'This controller was loaded without Memory Profiler.',
      visible: true,
      overflow: false,
    });

    const unavailableControl = await page.evaluate((manifest) => {
      const row = document.querySelector('[data-feature-entry="system.config.enable_arm1"]');
      row.dataset.boardCapability = 'PA_CAP_HOSTED_WIFI';
      window.PAFeatureAvailability.setIdentity(manifest);
      const input = document.getElementById('enable-arm1');
      return {
        disabled: input.disabled,
        state: row.dataset.featureState,
        status: document.getElementById('status-arm1').textContent.trim(),
        reason: document.getElementById(`${input.id}-availability-reason`).textContent.trim(),
      };
    }, identities.default);
    assert.deepEqual(unavailableControl, {
      disabled: true,
      state: 'not-on-this-board',
      status: 'Not on this board',
      reason: 'This controller board cannot run ARM1.',
    });
    await page.screenshot({ path: `${ARTIFACT_DIR}/not-on-this-board.png`, fullPage: true });

    const profilerBuild = await openScenario('profiler-build', identities.profiler);
    assert.equal(profilerBuild.identityRequests, 1);
    assert.equal(profilerBuild.profilerRequests, 1);
    assert.equal(profilerBuild.state, 'on');
    assert.equal(profilerBuild.status, 'On');
    assert.equal(profilerBuild.visible, true);
    assert.equal(profilerBuild.overflow, false);
    assert.match(await page.locator('#prof-heap-free').textContent(), /124\.0 KB/);

    const componentStates = await page.evaluate(() => ({
      on: document.getElementById('status-arm1').textContent.trim(),
      off: document.getElementById('status-arm2').textContent.trim(),
    }));
    assert.deepEqual(componentStates, { on: 'On', off: 'Off' });

    assert.deepEqual(pageErrors, []);
    assert.deepEqual(consoleErrors, []);
    console.log('SETUP_FEATURE_AVAILABILITY_START');
    console.log(JSON.stringify({ defaultBuild, unavailableControl, profilerBuild, componentStates }, null, 2));
    console.log('SETUP_FEATURE_AVAILABILITY_END');
    console.log(`Saved screenshots under ${ARTIFACT_DIR}`);
  } catch (error) {
    console.error('Setup Feature Availability check failed:', error);
    process.exitCode = 1;
  } finally {
    await browser.close();
  }
})();

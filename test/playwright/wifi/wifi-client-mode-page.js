// A droid on WiFi Client Mode reads as joined - posture, network, name and
// address, with a staged change named as waiting for a reboot - and a Save
// with both password boxes left blank keeps the saved passwords: the page
// sends no password key at all. WiFi is set on WiFi alone; Configuration
// carries no WiFi control.
// History: #288 (one home for WiFi), #344 (WiFi inside the Operator Shell),
// #404 and #407 (Configuration split from Setup, copy rewritten), 4ccdeac7
// (Save and Reboot to Apply reach the droid again).
//
// SELF-MOCKED - THE DROID IS FAKE. Every /api/** request is fulfilled inside
// the browser by the fake droid below, modelled on the firmware (named beside
// each answer), and never leaves it: nothing is read from or written to a real
// droid, whatever BASE_URL is. BASE_URL only serves the page files. The write
// guard (_lib/checks.js) lets POST /api/wifi through to the fake droid and
// aborts any other write before it leaves the browser. The browser is given
// no EventSource, so the shell takes its one-poll path (data/live_reading.js)
// and there is no status stream to fake.
//
// PRECONDITION: none - the droid's state is the script's own.
//
// WHAT IT PROVES, against the page's own words (data/wifi.js, data/wifi.html):
//   a  the Network posture plate reads the joined client, its signal and both
//      addresses, and says the saved change waits for a reboot
//   b  Active against saved shows the joined network at the droid's name and
//      address beside the saved settings, and says they wait for a reboot
//   c  the form holds the saved settings, and each password box says the
//      saved password is kept when left blank
//   d  Staged network switch says where the droid will be after the reboot,
//      and Reboot to Apply is live while a change is staged
//   e  Save with both password boxes blank sends mode and network names and
//      NO password key, so the droid keeps both saved passwords; the page
//      says the settings are saved
//   f  a Save with the client network name emptied is refused by the droid
//      and the refusal is shown on the Network name box
//   g  the page is no wider than the 1440 px window
//   h  the page threw nothing and logged no error while on WiFi
//   i  Configuration carries no WiFi mode, network name or password control
//
// DROPPED, BY DESIGN: the 820 px and 390 px overflow screenshots the older
// version took. The operator UI is desktop-only (test/playwright/README.md).
// The Configuration check (i) now opens Configuration through the shell's nav
// and looks inside that surface only; the old one waited for a visible
// #feature-form, which guided Setup hides whenever it is drawn over
// Configuration (#404). The fake droid's guided Setup record says the run is
// over, so Configuration is what is drawn.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wifi/wifi-client-mode-page.js
//   BASE_URL=http://<host>   serves the page files (default http://10.0.0.22)
//   HEADLESS=true            no window        STEP=1   wait for Enter between steps
// Offline, against tools/serve_editor_fixture.py on its own port:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     HEADLESS=true BASE_URL=http://127.0.0.1:4194 \
//     node test/playwright/wifi/wifi-client-mode-page.js
// Self-test: SELFTEST=sends-blank-password serves a data/wifi.js whose Save
// sends a blank password box; row e must FAIL. Safe at any BASE_URL, since
// the droid is always the fake one; FIXTURE=1 is not used by this script.
//
// Exit codes: 0 every row PASS, 1 any FAIL or the script could not finish,
// 2 nothing could be assessed.
const fs = require('node:fs');
const lib = require('../_lib/checks.js');

const RULE = 'WiFi Client Mode reads as joined, and a Save with blank passwords keeps them';
const ARTIFACTS = 'output/playwright/wifi';
const SELFTESTS = ['sends-blank-password'];
const SELFTEST = process.env.SELFTEST || '';

// ---------------------------------------------------------------------------
// The fake droid
// ---------------------------------------------------------------------------

const json = (payload, status = 200) => ({ status, contentType: 'application/json', body: JSON.stringify(payload) });

// GET /api/config's wifi block: password-safe, with pendingApply the saved
// settings against the ones this boot applied (src/web/api_config.cpp,
// wifiConfigsDiffer in src/config_store.cpp) and networkRecovery this boot's.
const WIFI_FIELDS = ['provisioned', 'mode', 'staSsid', 'staPassword', 'apSsid', 'apPassword'];
const wifiView = (droid) => ({
  provisioned: droid.saved.provisioned,
  mode: droid.saved.mode,
  staSsid: droid.saved.staSsid,
  staPasswordSet: droid.saved.staPassword.length > 0,
  apSsid: droid.saved.apSsid,
  apPasswordSet: droid.saved.apPassword.length > 0,
  pendingApply: WIFI_FIELDS.some((key) => droid.saved[key] !== droid.active[key]),
  networkRecovery: droid.networkRecovery,
});

// POST /api/wifi: wifiApply() in src/web/api_wifi_apply.cpp, in its order and
// with its sentences, over the saved settings (wifiWriteWindow reads them
// first), answered by webSendApplyRefusal() on a refusal.
const applyWifi = (saved, form) => {
  const has = (key) => Object.prototype.hasOwnProperty.call(form, key);
  const refuse = (error, reason, field, accepts) => ({
    status: 400,
    body: { ok: false, error, ...(field ? { field } : {}), reason, ...(accepts ? { accepts } : {}) },
  });
  if (!['wifiMode', 'staSsid', 'staPassword', 'apSsid', 'apPassword'].some(has)) {
    return refuse('no wifi fields supplied', 'missing-argument');
  }
  const working = { ...saved };
  if (has('wifiMode')) {
    if (form.wifiMode !== 'client' && form.wifiMode !== 'standalone_ap') {
      return refuse('wifiMode must be client or standalone_ap', 'out-of-range', 'wifiMode', 'client,standalone_ap');
    }
    working.mode = form.wifiMode;
  }
  const atMost = (key, max) => {
    if (!has(key)) return null;
    if (form[key].length > max) return refuse(`${key} must be at most ${max} characters`, 'out-of-range', key);
    working[key] = form[key];
    return null;
  };
  const tooLong = atMost('staSsid', 32) || atMost('staPassword', 63) || atMost('apSsid', 32);
  if (tooLong) return tooLong;
  if (has('apPassword')) {
    const length = form.apPassword.length;
    if (length !== 0 && (length < 8 || length > 63)) {
      return refuse('apPassword must be empty or 8..63 characters', 'out-of-range', 'apPassword');
    }
    working.apPassword = form.apPassword;
  }
  if (working.mode === 'client' && working.staSsid === '') {
    return refuse('staSsid is required for WiFi Client Mode', has('staSsid') ? 'out-of-range' : 'missing-argument', 'staSsid');
  }
  if (working.mode === 'standalone_ap' && working.apSsid === '') {
    return refuse('apSsid is required for Standalone AP Mode', has('apSsid') ? 'out-of-range' : 'missing-argument', 'apSsid');
  }
  working.provisioned = true;
  return { status: 200, working };
};

// The droid this script meets: joined to AstroHome as a client, with a changed
// AP network name saved but not yet applied.
const initialDroid = () => {
  const applied = {
    provisioned: true,
    mode: 'client',
    staSsid: 'AstroHome',
    staPassword: 'astro-home-pass',
    apSsid: 'protoArtoo',
    apPassword: 'field-kit-pass',
  };
  return {
    active: { ...applied },
    saved: { ...applied, apSsid: 'protoArtoo-AP' },
    networkRecovery: false,
    // GET /api/wifi, formatWifiJson() in src/web/api_status_serializers.cpp.
    diagnostics: {
      apSsid: 'protoArtoo',
      apIp: '192.168.4.1',
      staEnabled: true,
      staConnected: true,
      staIp: '10.0.0.22',
      staSsid: 'AstroHome',
      wifiRssi: -64,
      networkRecovery: false,
    },
    posts: [],
    reboots: 0,
    selftestApplied: false,
  };
};

const installDroid = async (context, droid) => {
  await context.addInitScript(() => {
    window.EventSource = undefined;
  });
  await context.route((url) => url.pathname.startsWith('/api/'), async (route) => {
    const request = route.request();
    const method = request.method();
    const path = lib.pathOf(request.url());
    if (method === 'GET' && path === '/api/identity') {
      return route.fulfill(json({
        droidName: 'r5unit',
        mdnsUseName: true,
        board: 'artoo_esp32',
        board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
        build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
      }));
    }
    if (method === 'GET' && path === '/api/status') {
      // The six fields data/live_reading.js requires of a frame.
      return route.fulfill(json({
        estop: false,
        sbusHwFailsafe: false,
        sbusSignalLost: false,
        webDriveExpired: false,
        webControlEnabled: false,
        sleepMode: false,
        uptimeMs: 120000,
        wifiConnected: true,
        wifiClientConnected: true,
        wifiRssi: droid.diagnostics.wifiRssi,
      }));
    }
    if (method === 'GET' && path === '/api/config') {
      return route.fulfill(json({
        wifi: wifiView(droid),
        components: {},
        system: { logLevel: 2 },
        guidedSetup: { run: 'completed', visited: [], recorded: true, summaryDone: true },
      }));
    }
    if (method === 'GET' && path === '/api/wifi') return route.fulfill(json(droid.diagnostics));
    if (method === 'POST' && path === '/api/wifi') {
      const form = lib.formOf(request.postData());
      droid.posts.push(form);
      const answer = applyWifi(droid.saved, form);
      if (answer.status !== 200) return route.fulfill(json(answer.body, answer.status));
      droid.saved = answer.working;
      return route.fulfill(json({ ok: true, wifi: wifiView(droid) }));
    }
    if (method === 'POST' && path === '/api/reboot') {
      droid.reboots += 1;
      return route.fulfill(json({ ok: true }));
    }
    // A route the controller does not have.
    return route.fulfill(json({ ok: false, error: 'not found' }, 404));
  });
  if (SELFTEST === 'sends-blank-password') {
    const needle = 'if (nextStaPassword.length > 0) {';
    await context.route((url) => url.pathname === '/wifi.js', async (route) => {
      const response = await route.fetch();
      const source = await response.text();
      droid.selftestApplied = source.includes(needle);
      await route.fulfill({ response, body: source.replace(needle, 'if (true) {') });
    });
  }
};

const isAllowedWrite = (entry) => entry.method === 'POST' && entry.path === '/api/wifi';

// ---------------------------------------------------------------------------
// Reading the page
// ---------------------------------------------------------------------------

// Each id's text against what the page is written to say there: equal to a
// string, or matching a RegExp.
const expectTexts = async (page, want) => {
  const ids = Object.keys(want);
  const got = await page.evaluate((list) => Object.fromEntries(list.map((id) => {
    const node = document.getElementById(id);
    return [id, node ? node.textContent.trim() : null];
  })), ids);
  const wrong = ids.filter((id) => (want[id] instanceof RegExp ? !want[id].test(got[id] ?? '') : got[id] !== want[id]));
  return {
    ok: wrong.length === 0,
    detail: wrong.length ? wrong.map((id) => `#${id} reads "${got[id]}", want "${want[id]}"`).join('; ') : `${ids.length} readouts as written`,
  };
};

const waitWifiRead = (page) =>
  page.waitForFunction(() => document.getElementById('wifi-pending-summary')?.textContent.trim() !== '', null, { timeout: 15000 });

// ---------------------------------------------------------------------------
// The check
// ---------------------------------------------------------------------------

const run = async ({ page, droid, report, pageErrors, consoleErrors }) => {
  await lib.loadSurface(page, 'wifi');
  await waitWifiRead(page);
  await lib.step('WiFi is open on a joined client with a staged AP name.');

  const posture = await expectTexts(page, {
    'wifi-pending-summary': 'Pending apply',
    'wifi-posture-desc': 'Saved WiFi Client Mode settings wait for a reboot.',
    'wifi-provisioning-state': 'Provisioned',
    'wifi-active-mode': 'WiFi Client Mode',
    'wifi-client-state': 'Connected',
    'wifi-signal': 'Excellent \u00b7 -64 dBm',
    'wifi-sta-ip': '10.0.0.22',
    'wifi-ap-ip': '192.168.4.1',
  });
  const postureName = await page.getAttribute('#wifi-posture-card', 'data-posture');
  report.add('a', 'Network posture reads the joined client and the change waiting for a reboot',
    lib.verdict(posture.ok && postureName === 'client'), `${posture.detail}; plate posture "${postureName}"`);

  const compare = await expectTexts(page, {
    'wifi-compare-state': 'saved settings wait for a reboot',
    'wifi-active-summary-mode': 'WiFi Client Mode',
    'wifi-active-summary-network': 'AstroHome',
    'wifi-active-summary-address': 'http://r5unit.local / http://10.0.0.22',
    'wifi-saved-summary-title': 'Saved after reboot',
    'wifi-saved-summary-mode': 'WiFi Client Mode',
    'wifi-saved-summary-sta': 'AstroHome',
    'wifi-saved-summary-ap': 'protoArtoo-AP',
  });
  report.add('b', 'Active against saved shows the joined network beside the saved settings', lib.verdict(compare.ok), compare.detail);

  const hints = await expectTexts(page, {
    'wifi-sta-password-hint': 'Saved password present; blank keeps it.',
    'wifi-ap-password-hint': 'Saved password present; blank keeps it.',
  });
  const form = await page.evaluate(() => ({
    client: document.getElementById('wifi-mode-client').checked,
    staSsid: document.getElementById('wifi-sta-ssid').value,
    apSsid: document.getElementById('wifi-ap-ssid').value,
    passwords: document.getElementById('wifi-sta-password').value + document.getElementById('wifi-ap-password').value,
  }));
  const formOk = form.client && form.staSsid === 'AstroHome' && form.apSsid === 'protoArtoo-AP' && form.passwords === '';
  report.add('c', 'The form holds the saved settings and says a blank password is kept',
    lib.verdict(hints.ok && formOk), `${hints.detail}; form ${JSON.stringify(form)}`);

  const apply = await expectTexts(page, {
    'wifi-apply-state': 'staged, waiting for a reboot',
    'wifi-apply-guidance': 'After the reboot, reach the droid on your network at http://r5unit.local or http://10.0.0.22.',
  });
  const applyLive = await page.isEnabled('#wifi-apply-reboot-button');
  report.add('d', 'Staged network switch says where the droid will be, and Reboot to Apply is live',
    lib.verdict(apply.ok && applyLive), `${apply.detail}; Reboot to Apply ${applyLive ? 'live' : 'refused'}`);
  await page.screenshot({ path: `${ARTIFACTS}/client-mode-read.png`, fullPage: true });

  // e. Save with both password boxes blank.
  await lib.step('Next: Save with both password boxes blank.');
  const postsBefore = droid.posts.length;
  await page.fill('#wifi-sta-password', '');
  await page.fill('#wifi-ap-password', '');
  await page.click('#wifi-save-settings-button');
  await page.waitForFunction(() => /saved|failed|rejected|error/i.test(document.getElementById('wifi-settings-feedback')?.textContent || ''), null, { timeout: 10000 })
    .catch(() => {});
  const saved = droid.posts.slice(postsBefore);
  const sent = saved[0] || {};
  const savedSaid = await expectTexts(page, {
    'wifi-settings-feedback': 'WiFi settings saved. Reboot the Body Controller to apply the staged network switch.',
  });
  const passwordKeys = Object.keys(sent).filter((key) => /password/i.test(key));
  const bodyOk = saved.length === 1 && sent.wifiMode === 'client' && sent.staSsid === 'AstroHome' && sent.apSsid === 'protoArtoo-AP' && passwordKeys.length === 0;
  const kept = droid.saved.staPassword === 'astro-home-pass' && droid.saved.apPassword === 'field-kit-pass';
  report.add('e', 'Save with blank password boxes sends no password, and both saved passwords stand',
    lib.verdict(bodyOk && kept && savedSaid.ok),
    `sent ${saved.length} save(s): ${JSON.stringify(sent)}; password keys sent: ${passwordKeys.join(', ') || 'none'}; saved passwords ${kept ? 'kept' : 'CHANGED'}; ${savedSaid.detail}`);

  // f. The droid refuses an empty client network name.
  await lib.step('Next: empty the client Network name and Save.');
  await page.fill('#wifi-sta-ssid', '');
  await page.click('#wifi-save-settings-button');
  await page.waitForFunction(() => (document.getElementById('wifi-sta-ssid-error')?.textContent || '').trim() !== '', null, { timeout: 10000 })
    .catch(() => {});
  const refusal = await page.evaluate(() => ({
    field: document.getElementById('wifi-sta-ssid-error').textContent.trim(),
    invalid: document.getElementById('wifi-sta-ssid').getAttribute('aria-invalid'),
    feedbackClass: document.getElementById('wifi-settings-feedback').className,
    feedback: document.getElementById('wifi-settings-feedback').textContent.trim(),
  }));
  const refusedAnswer = droid.posts.at(-1) || {};
  report.add('f', 'An emptied client Network name is refused, on the Network name box',
    lib.verdict(refusedAnswer.staSsid === '' && refusal.field !== '' && refusal.invalid === 'true' && /\berror\b/.test(refusal.feedbackClass) && droid.saved.staSsid === 'AstroHome'),
    `box says "${refusal.field}", aria-invalid ${refusal.invalid}; feedback (${refusal.feedbackClass}) "${refusal.feedback}"; saved client network still "${droid.saved.staSsid}"`);

  const width = await page.evaluate(() => ({ window: window.innerWidth, page: document.documentElement.scrollWidth }));
  report.add('g', 'The page is no wider than the window', lib.verdict(width.page <= width.window), `page ${width.page} px, window ${width.window} px`);
  await page.screenshot({ path: `${ARTIFACTS}/client-mode-after-save.png`, fullPage: true });

  report.add('h', 'WiFi threw nothing and logged no error', lib.verdict(pageErrors.length === 0 && consoleErrors.length === 0),
    [...pageErrors.map((line) => `threw: ${line}`), ...consoleErrors.map((line) => `logged: ${line}`)].join(' | ') || 'nothing');

  // i. Configuration, through the nav.
  await lib.step('Next: open Configuration.');
  await lib.openSurface(page, 'configuration');
  const found = await page.evaluate(() => {
    const surface = document.querySelector('#shell-content > .surface[data-surface="configuration"]');
    if (!surface) return null;
    const names = ['wifiMode', 'staSsid', 'staPassword', 'apSsid', 'apPassword'];
    return {
      title: document.getElementById('configuration-title')?.checkVisibility() ? 'Configuration' : 'not Configuration',
      controls: names.filter((name) => surface.querySelector(`[name="${name}"]`)),
    };
  });
  await page.screenshot({ path: `${ARTIFACTS}/client-mode-configuration.png`, fullPage: true });
  report.add('i', 'Configuration carries no WiFi control',
    lib.verdict(found !== null && found.title === 'Configuration' && found.controls.length === 0),
    found === null ? 'the Configuration surface did not mount' : `drawn as ${found.title}; WiFi controls: ${found.controls.join(', ') || 'none'}`);
};

// ---------------------------------------------------------------------------
// Start to close
// ---------------------------------------------------------------------------

const main = async () => {
  if (SELFTEST && !SELFTESTS.includes(SELFTEST)) {
    console.error(`SELFTEST=${SELFTEST} is not one this script knows (${SELFTESTS.join(', ')})`);
    process.exitCode = 2;
    return;
  }
  fs.mkdirSync(ARTIFACTS, { recursive: true });
  const report = lib.createReport(RULE);
  const droid = initialDroid();
  const pageErrors = [];
  const consoleErrors = [];
  let writes = [];
  let context = null;
  const browser = await lib.launchBrowser();
  try {
    context = await browser.newContext({ viewport: lib.VIEWPORT });
    await installDroid(context, droid);
    const page = await context.newPage();
    page.on('pageerror', (error) => pageErrors.push(String(error).split('\n')[0]));
    page.on('console', (message) => {
      if (message.type() === 'error' && !message.text().startsWith('Failed to load resource:')) consoleErrors.push(message.text());
    });
    writes = await lib.installGuard(page, isAllowedWrite);
    // Rows a-h read these as they stand when h is written; Configuration's
    // own loads (i) come after.
    await run({ page, droid, report, pageErrors, consoleErrors });
    if (SELFTEST) {
      report.add('s', `the self-test's break (${SELFTEST}) reached the page`, lib.verdict(droid.selftestApplied),
        droid.selftestApplied ? 'data/wifi.js served broken' : 'the text it breaks is gone from data/wifi.js: rewrite the self-test');
    }
  } catch (error) {
    console.error(`${RULE}: could not complete:`, error);
    report.add('x', 'the script ran to the end', lib.FAIL, String(error.message).split('\n')[0]);
  } finally {
    await lib.closeAll(browser, [context]);
  }

  if (writes.length) {
    console.log('\nWrites the page attempted (all answered by the fake droid or blocked):');
    [...new Set(writes.map(lib.describeWrite))].forEach((line) => console.log(`  ${line}`));
  }
  report.print();
  console.log(`Screenshots under ${ARTIFACTS}`);
  const results = report.rows.map((row) => row.result);
  if (results.includes(lib.FAIL)) process.exitCode = 1;
  else if (!results.includes(lib.PASS)) process.exitCode = 2;
};

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});

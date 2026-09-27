// Network Recovery Mode reads as recovery - never as WiFi Provisioning or a
// client that failed to join - although the droid's settings still read as
// provisioned, and the saved settings recovery left untouched are repaired
// through the ordinary Save and applied by Reboot to Apply: no special
// recovery write.
// History: ADR 0015 (Network Recovery Mode), #344 (WiFi inside the Operator
// Shell), #407 (copy rewritten), 4ccdeac7 (Save and Reboot to Apply reach the
// droid again).
//
// SELF-MOCKED - THE DROID IS FAKE. Every /api/** request is fulfilled inside
// the browser by the fake droid below, modelled on the firmware (named beside
// each answer), and never leaves it: nothing is read from or written to a real
// droid, whatever BASE_URL is. BASE_URL only serves the page files. The write
// guard (_lib/checks.js) lets POST /api/wifi and POST /api/reboot through to
// the fake droid and aborts any other write before it leaves the browser. The
// browser is given no EventSource, so the shell takes its one-poll path
// (data/live_reading.js) and there is no status stream to fake.
//
// PRECONDITION: none - the droid's state is the script's own: booted into
// Network Recovery Mode by the power-cycle gesture, with a mistyped client
// network saved and nothing staged.
//
// WHAT IT PROVES, against the page's own words (data/wifi.js, data/wifi.html):
//   a  the Network posture plate reads Network Recovery Mode, and says the
//      saved settings are untouched and how to fix them
//   b  the saved settings recovery kept are in the form and in Active against
//      saved, beside the recovery network that is actually running
//   c  Staged network switch sends the operator to the recovery network, and
//      Reboot to Apply is refused while nothing is staged
//   d  the repair is the ordinary Save: one POST /api/wifi carrying the fixed
//      network name, and no password, so the saved one is kept
//   e  after the repair the page still reads recovery, says the fixes are
//      saved and that Reboot to Apply returns to WiFi Client Mode, and Reboot
//      to Apply is live
//   f  Reboot to Apply sends one reboot, says so, and is refused again while
//      that reboot is on its way
//   g  the page is no wider than the 1440 px window
//   h  the page threw nothing and logged no error
//
// DROPPED, BY DESIGN: the 820 px and 390 px overflow screenshots the older
// version took. The operator UI is desktop-only (test/playwright/README.md).
// The older version's wording checks ("power-cycle gesture") are replaced by
// the page's current sentences (#407).
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wifi/wifi-network-recovery-page.js
//   BASE_URL=http://<host>   serves the page files (default http://10.0.0.22)
//   HEADLESS=true            no window        STEP=1   wait for Enter between steps
// Offline, against tools/serve_editor_fixture.py on its own port:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     HEADLESS=true BASE_URL=http://127.0.0.1:4194 \
//     node test/playwright/wifi/wifi-network-recovery-page.js
// Self-test: SELFTEST=recovery-unseen serves a data/wifi.js that ignores the
// droid's networkRecovery flag; rows a, b, c and e must FAIL. Safe at any
// BASE_URL, since the droid is always the fake one; FIXTURE=1 is not used by
// this script.
//
// Exit codes: 0 every row PASS, 1 any FAIL or the script could not finish,
// 2 nothing could be assessed.
const fs = require('node:fs');
const lib = require('../_lib/checks.js');

const RULE = 'Network Recovery Mode reads as recovery, and repairs through the ordinary Save';
const ARTIFACTS = 'output/playwright/wifi';
const SELFTESTS = ['recovery-unseen'];
const SELFTEST = process.env.SELFTEST || '';
const DASH = '\u2014';

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

// The droid this script meets: booted into Network Recovery Mode, broadcasting
// the Default AP Credential (WIFI_AP_SSID "protoArtoo", include/config.h) with
// its client off, and a mistyped client network saved and applied.
const initialDroid = () => {
  const applied = {
    provisioned: true,
    mode: 'client',
    staSsid: 'AstroHome-Typo',
    staPassword: 'astro-home-pass',
    apSsid: 'FieldArtoo',
    apPassword: 'field-kit-pass',
  };
  return {
    active: { ...applied },
    saved: { ...applied },
    networkRecovery: true,
    // GET /api/wifi, formatWifiJson() in src/web/api_status_serializers.cpp.
    diagnostics: {
      apSsid: 'protoArtoo',
      apIp: '192.168.4.1',
      staEnabled: false,
      staConnected: false,
      staIp: '',
      staSsid: '',
      wifiRssi: 0,
      networkRecovery: true,
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
        wifiClientConnected: false,
        wifiRssi: 0,
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
  if (SELFTEST === 'recovery-unseen') {
    const needle = 'const networkRecovery = Boolean(diag.networkRecovery);';
    await context.route((url) => url.pathname === '/wifi.js', async (route) => {
      const response = await route.fetch();
      const source = await response.text();
      droid.selftestApplied = source.includes(needle);
      await route.fulfill({ response, body: source.replace(needle, 'const networkRecovery = false;') });
    });
  }
};

const isAllowedWrite = (entry) => entry.method === 'POST' && (entry.path === '/api/wifi' || entry.path === '/api/reboot');

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
  page.waitForFunction(() => document.getElementById('wifi-pending-summary')?.textContent.trim() !== 'finding out', null, { timeout: 15000 });

const RECOVERY_DESC = 'Network Recovery Mode: a power-cycle opened WiFi Provisioning for now. Your saved Device WiFi Settings are untouched; fix them below, save, then reboot.';

// ---------------------------------------------------------------------------
// The check
// ---------------------------------------------------------------------------

const run = async ({ page, droid, report, pageErrors, consoleErrors }) => {
  await lib.loadSurface(page, 'wifi');
  await waitWifiRead(page);
  await lib.step('WiFi is open on a droid in Network Recovery Mode.');

  const posture = await expectTexts(page, {
    'wifi-pending-summary': 'Recovery',
    'wifi-posture-desc': RECOVERY_DESC,
    'wifi-provisioning-state': 'Network Recovery Mode',
    'wifi-active-mode': 'Network Recovery Mode',
    'wifi-client-state': 'Not active',
    'wifi-signal': DASH,
    'wifi-sta-ip': DASH,
    'wifi-ap-ip': '192.168.4.1',
  });
  const postureName = await page.getAttribute('#wifi-posture-card', 'data-posture');
  report.add('a', 'Network posture reads Network Recovery Mode, not provisioning or a failed client',
    lib.verdict(posture.ok && postureName === 'recovery'), `${posture.detail}; plate posture "${postureName}"`);

  const kept = await expectTexts(page, {
    'wifi-compare-state': 'saved settings are the ones running',
    'wifi-active-summary-mode': 'Network Recovery Mode',
    'wifi-active-summary-network': 'protoArtoo',
    'wifi-active-summary-address': 'http://192.168.4.1',
    'wifi-saved-summary-title': 'Saved settings',
    'wifi-saved-summary-mode': 'WiFi Client Mode',
    'wifi-saved-summary-sta': 'AstroHome-Typo',
    'wifi-saved-summary-ap': 'FieldArtoo',
  });
  const form = await page.evaluate(() => ({
    client: document.getElementById('wifi-mode-client').checked,
    staSsid: document.getElementById('wifi-sta-ssid').value,
    editable: !document.getElementById('wifi-sta-ssid').disabled && !document.getElementById('wifi-save-settings-button').disabled,
  }));
  report.add('b', 'The saved settings recovery kept are shown and editable, beside the recovery network',
    lib.verdict(kept.ok && form.client && form.staSsid === 'AstroHome-Typo' && form.editable), `${kept.detail}; form ${JSON.stringify(form)}`);

  const guidance = await expectTexts(page, {
    'wifi-apply-state': 'nothing staged',
    'wifi-apply-guidance': 'Network Recovery Mode is on; saved settings are unchanged. Join protoArtoo, open http://192.168.4.1, fix the settings below, save, then reboot.',
  });
  const refusedBefore = await page.isDisabled('#wifi-apply-reboot-button');
  report.add('c', 'Staged network switch points at the recovery network, and Reboot to Apply is refused',
    lib.verdict(guidance.ok && refusedBefore), `${guidance.detail}; Reboot to Apply ${refusedBefore ? 'refused' : 'LIVE'}`);
  await page.screenshot({ path: `${ARTIFACTS}/recovery-read.png`, fullPage: true });

  // d. The repair, through the ordinary Save.
  await lib.step('Next: fix the client network name and Save.');
  const postsBefore = droid.posts.length;
  await page.fill('#wifi-sta-ssid', 'AstroHome');
  await page.click('#wifi-save-settings-button');
  await page.waitForFunction(() => /saved|failed|rejected|error/i.test(document.getElementById('wifi-settings-feedback')?.textContent || ''), null, { timeout: 10000 })
    .catch(() => {});
  const saves = droid.posts.slice(postsBefore);
  const sent = saves[0] || {};
  const savedSaid = await expectTexts(page, {
    'wifi-settings-feedback': 'WiFi settings saved. Reboot the Body Controller to apply the staged network switch.',
  });
  const passwordKeys = Object.keys(sent).filter((key) => /password/i.test(key));
  report.add('d', 'The repair is one ordinary Save carrying the fixed network name and no password',
    lib.verdict(saves.length === 1 && sent.wifiMode === 'client' && sent.staSsid === 'AstroHome' && passwordKeys.length === 0
      && droid.saved.staSsid === 'AstroHome' && droid.saved.staPassword === 'astro-home-pass' && savedSaid.ok),
    `sent ${saves.length} save(s): ${JSON.stringify(sent)}; saved client network "${droid.saved.staSsid}", password ${droid.saved.staPassword === 'astro-home-pass' ? 'kept' : 'CHANGED'}; ${savedSaid.detail}`);

  const staged = await expectTexts(page, {
    'wifi-pending-summary': 'Recovery',
    'wifi-posture-desc': RECOVERY_DESC,
    'wifi-compare-state': 'saved settings wait for a reboot',
    'wifi-saved-summary-sta': 'AstroHome',
    'wifi-apply-state': 'staged, waiting for a reboot',
    'wifi-apply-guidance': 'Network Recovery Mode is on and your fixes are saved. Join protoArtoo, open http://192.168.4.1, then Reboot to Apply to return to WiFi Client Mode.',
  });
  const liveAfter = await page.isEnabled('#wifi-apply-reboot-button');
  report.add('e', 'After the repair it still reads recovery, says the fixes are saved, and Reboot to Apply is live',
    lib.verdict(staged.ok && liveAfter), `${staged.detail}; Reboot to Apply ${liveAfter ? 'live' : 'refused'}`);

  // f. Reboot to Apply.
  await lib.step('Next: press Reboot to Apply.');
  await page.click('#wifi-apply-reboot-button');
  await page.waitForFunction(() => (document.getElementById('wifi-apply-feedback')?.textContent || '').trim() !== ''
    && !/Sending/.test(document.getElementById('wifi-apply-feedback').textContent), null, { timeout: 10000 })
    .catch(() => {});
  const rebooted = await expectTexts(page, {
    'wifi-apply-feedback': 'Reboot command sent. After it restarts, reconnect using the guidance above.',
    'wifi-apply-state': 'reboot requested',
  });
  const refusedAfter = await page.isDisabled('#wifi-apply-reboot-button');
  report.add('f', 'Reboot to Apply sends one reboot, says so, and is refused while it is on its way',
    lib.verdict(droid.reboots === 1 && rebooted.ok && refusedAfter), `${droid.reboots} reboot(s) sent; ${rebooted.detail}; Reboot to Apply ${refusedAfter ? 'refused' : 'LIVE'}`);

  const width = await page.evaluate(() => ({ window: window.innerWidth, page: document.documentElement.scrollWidth }));
  report.add('g', 'The page is no wider than the window', lib.verdict(width.page <= width.window), `page ${width.page} px, window ${width.window} px`);
  await page.screenshot({ path: `${ARTIFACTS}/recovery-after-reboot.png`, fullPage: true });

  report.add('h', 'WiFi threw nothing and logged no error', lib.verdict(pageErrors.length === 0 && consoleErrors.length === 0),
    [...pageErrors.map((line) => `threw: ${line}`), ...consoleErrors.map((line) => `logged: ${line}`)].join(' | ') || 'nothing');
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

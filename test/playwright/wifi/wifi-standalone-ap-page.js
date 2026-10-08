// bench-auto: droid
// Each network posture reads as itself - Standalone AP Mode, WiFi
// Provisioning, and a client that has not joined its network are three
// different things on the page - and a switch from a joined client to
// Standalone AP Mode is refused box by box until it is whole, then saved and
// applied by Reboot to Apply, with the page saying where the droid will be.
// History: ADR 0015 (Device WiFi Settings and the Staged Network Switch),
// #344 (WiFi inside the Operator Shell), #407 (copy rewritten), 4ccdeac7
// (Save and Reboot to Apply reach the droid again).
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
// PRECONDITION: none - the droid's state is the script's own. It meets four
// droids in turn, each on a fresh page load: one hosting its own network in
// Standalone AP Mode, a new one in WiFi Provisioning, one whose client has not
// joined, and one joined as a client that the operator switches to Standalone
// AP Mode.
//
// WHAT IT PROVES, against the page's own words (data/wifi.js, data/wifi.html):
//   a  Standalone AP Mode reads as the droid's own network, with where to
//      join it and where OTA goes, and nothing staged
//   b  WiFi Provisioning reads as temporary and unsaved, and says to join the
//      droid's network and save settings
//   c  a client that has not joined reads as that, with the way to fix it
//   d  a joined client reads as joined, with its name and address
//   e  picking Standalone AP Mode marks it chosen and sends nothing
//   f  an AP password under 8 characters is refused by the droid, on the AP
//      Password box, and nothing is saved
//   g  an emptied AP network name is refused, on the AP Network name box, and
//      the earlier password refusal is cleared
//   h  a whole switch is one Save carrying the mode, the AP name and password,
//      and no client password, so the saved one is kept
//   i  once saved, the page names the switch as waiting for a reboot and says
//      where the droid will be after it, and Reboot to Apply is live
//   j  Reboot to Apply sends one reboot and says so
//   k  the page is no wider than the 1440 px window
//   l  the page threw nothing and logged no error
//
// DROPPED, BY DESIGN: the 820 px and 390 px overflow screenshots the older
// version took. The operator UI is desktop-only (test/playwright/README.md).
// The older version's "WiFi Client Mode is active" guidance for a client that
// has not joined, and its "waiting for saved Device WiFi Settings" posture
// line, are replaced by the page's current sentences (#407).
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wifi/wifi-standalone-ap-page.js
//   BASE_URL=http://<host>   serves the page files (default http://10.0.0.22)
//   HEADLESS=true            no window        STEP=1   wait for Enter between steps
// Offline, against tools/serve_editor_fixture.py on its own port:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     HEADLESS=true BASE_URL=http://127.0.0.1:4194 \
//     node test/playwright/wifi/wifi-standalone-ap-page.js
// Self-test: SELFTEST=guidance-names-old-network serves a data/wifi.js that
// names the network running now instead of the one saved; row i must FAIL.
// Safe at any BASE_URL, since the droid is always the fake one; FIXTURE=1 is
// not used by this script.
//
// Exit codes: 0 every row PASS, 1 any FAIL or the script could not finish,
// 2 nothing could be assessed.
const fs = require('node:fs');
const lib = require('../_lib/checks.js');

const RULE = 'Each WiFi posture reads as itself, and a switch to Standalone AP is saved and applied';
const ARTIFACTS = 'output/playwright/wifi';
const SELFTESTS = ['guidance-names-old-network'];
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

// The four droids, as the settings they saved and applied and what
// GET /api/wifi reports (formatWifiJson() in
// src/web/api_status_serializers.cpp). WiFi Provisioning broadcasts the Default
// AP Credential (WIFI_AP_SSID "protoR2", include/config.h).
const SAVED_CLIENT = {
  provisioned: true,
  mode: 'client',
  staSsid: 'AstroHome',
  staPassword: 'astro-home-pass',
  apSsid: 'FieldArtoo',
  apPassword: 'field-kit-pass',
};
const NO_CLIENT = { staEnabled: false, staConnected: false, staIp: '', staSsid: '', wifiRssi: 0, networkRecovery: false };
const DROIDS = {
  standaloneAp: {
    settings: { ...SAVED_CLIENT, mode: 'standalone_ap' },
    diagnostics: { apSsid: 'FieldArtoo', apIp: '192.168.4.1', ...NO_CLIENT },
  },
  provisioning: {
    settings: { provisioned: false, mode: 'client', staSsid: '', staPassword: '', apSsid: '', apPassword: '' },
    diagnostics: { apSsid: 'protoR2', apIp: '192.168.4.1', ...NO_CLIENT },
  },
  clientNotJoined: {
    settings: { ...SAVED_CLIENT },
    diagnostics: { apSsid: 'protoR2', apIp: '', ...NO_CLIENT, staEnabled: true },
  },
  clientJoined: {
    settings: { ...SAVED_CLIENT },
    diagnostics: {
      apSsid: 'protoR2',
      apIp: '',
      staEnabled: true,
      staConnected: true,
      staIp: '10.0.0.22',
      staSsid: 'AstroHome',
      wifiRssi: -64,
      networkRecovery: false,
    },
  },
};

const createDroid = () => ({
  active: null,
  saved: null,
  networkRecovery: false,
  diagnostics: null,
  posts: [],
  reboots: 0,
  selftestApplied: false,
  // A droid that booted on these settings: saved and applied are the same.
  become(name) {
    const { settings, diagnostics } = DROIDS[name];
    this.active = { ...settings };
    this.saved = { ...settings };
    this.diagnostics = { ...diagnostics };
  },
});

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
        wifiClientConnected: droid.diagnostics.staConnected,
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
  if (SELFTEST === 'guidance-names-old-network') {
    const needle = 'const apName = wifi.apSsid || diag.apSsid || "the Body Controller AP";';
    await context.route((url) => url.pathname === '/wifi.js', async (route) => {
      const response = await route.fetch();
      const source = await response.text();
      droid.selftestApplied = source.includes(needle);
      await route.fulfill({ response, body: source.replace(needle, 'const apName = diag.apSsid || wifi.apSsid || "the Body Controller AP";') });
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

// A fresh page load of WiFi on the droid as it now is.
const openWifi = async (page) => {
  await page.goto('about:blank');
  await lib.loadSurface(page, 'wifi');
  await page.waitForFunction(() => document.getElementById('wifi-pending-summary')?.textContent.trim() !== '', null, { timeout: 15000 });
};

const posture = (page) => page.getAttribute('#wifi-posture-card', 'data-posture');

// The refusal shown on one box, and the page's feedback line.
const refusalOn = (page, id) =>
  page.evaluate((box) => ({
    says: document.getElementById(`${box}-error`).textContent.trim(),
    invalid: document.getElementById(box).getAttribute('aria-invalid'),
    feedbackClass: document.getElementById('wifi-settings-feedback').className,
    feedback: document.getElementById('wifi-settings-feedback').textContent.trim(),
  }), id);

const pressSave = async (page, droid) => {
  const before = droid.posts.length;
  await page.click('#wifi-save-settings-button');
  await page.waitForFunction(() => /saved|failed|rejected|error|must|required/i.test(document.getElementById('wifi-settings-feedback')?.textContent || ''), null, { timeout: 10000 })
    .catch(() => {});
  return droid.posts.slice(before);
};

// ---------------------------------------------------------------------------
// The check
// ---------------------------------------------------------------------------

const run = async ({ page, droid, report, pageErrors, consoleErrors }) => {
  // a. Standalone AP Mode.
  droid.become('standaloneAp');
  await openWifi(page);
  await lib.step('WiFi is open on a droid hosting its own network.');
  const ap = await expectTexts(page, {
    'wifi-pending-summary': 'Active',
    'wifi-posture-desc': 'Standalone AP Mode settings are running.',
    'wifi-provisioning-state': 'Provisioned',
    'wifi-active-mode': 'Standalone AP Mode',
    'wifi-client-state': 'Not active',
    'wifi-ap-ip': '192.168.4.1',
    'wifi-active-summary-network': 'FieldArtoo',
    'wifi-active-summary-address': 'http://192.168.4.1',
    'wifi-apply-state': 'nothing staged',
    'wifi-apply-guidance': 'Join FieldArtoo and open http://192.168.4.1. OTA goes to 192.168.4.1 while you are on that network.',
  });
  const apChosen = await page.isChecked('#wifi-mode-standalone-ap');
  const apRefused = await page.isDisabled('#wifi-apply-reboot-button');
  const apPosture = await posture(page);
  report.add('a', 'Standalone AP Mode reads as the droid\'s own network, with where to join it',
    lib.verdict(ap.ok && apChosen && apRefused && apPosture === 'standalone-ap'),
    `${ap.detail}; Standalone AP ${apChosen ? 'chosen' : 'NOT chosen'} in the form; Reboot to Apply ${apRefused ? 'refused' : 'LIVE'}; plate posture "${apPosture}"`);
  await page.screenshot({ path: `${ARTIFACTS}/standalone-ap-read.png`, fullPage: true });

  // b. WiFi Provisioning.
  droid.become('provisioning');
  await openWifi(page);
  await lib.step('WiFi is open on a new droid in WiFi Provisioning.');
  const provisioning = await expectTexts(page, {
    'wifi-pending-summary': 'Provisioning',
    'wifi-posture-desc': 'WiFi Provisioning is temporary. The droid waits for saved Device WiFi Settings.',
    'wifi-provisioning-state': 'WiFi Provisioning',
    'wifi-active-mode': 'WiFi Provisioning',
    'wifi-client-state': 'Not active',
    'wifi-compare-state': 'nothing saved yet',
    'wifi-saved-summary-mode': 'Not provisioned',
    'wifi-apply-guidance': 'WiFi Provisioning is temporary, not Standalone AP Mode. Join protoR2, open http://192.168.4.1, save settings below, then reboot.',
  });
  const provisioningPosture = await posture(page);
  report.add('b', 'WiFi Provisioning reads as temporary and unsaved, and says how to save settings',
    lib.verdict(provisioning.ok && provisioningPosture === 'provisioning'), `${provisioning.detail}; plate posture "${provisioningPosture}"`);

  // c. A client that has not joined.
  droid.become('clientNotJoined');
  await openWifi(page);
  await lib.step('WiFi is open on a droid whose client has not joined.');
  const notJoined = await expectTexts(page, {
    'wifi-pending-summary': 'Client not connected',
    'wifi-posture-desc': 'WiFi Client Mode is on, but the droid has not joined the saved network.',
    'wifi-active-mode': 'WiFi Client Mode',
    'wifi-client-state': 'Not connected',
    'wifi-signal': DASH,
    'wifi-sta-ip': DASH,
    'wifi-active-summary-network': 'Client not connected',
    'wifi-apply-guidance': 'Not joined. Check the saved network, or use Network Recovery Mode to fix it.',
  });
  const notJoinedPosture = await posture(page);
  report.add('c', 'A client that has not joined reads as that, with the way to fix it',
    lib.verdict(notJoined.ok && notJoinedPosture === 'client-failure'), `${notJoined.detail}; plate posture "${notJoinedPosture}"`);

  // d. A joined client.
  droid.become('clientJoined');
  await openWifi(page);
  await lib.step('WiFi is open on a droid joined as a client.');
  const joined = await expectTexts(page, {
    'wifi-pending-summary': 'Active',
    'wifi-posture-desc': 'WiFi Client Mode settings are running.',
    'wifi-active-mode': 'WiFi Client Mode',
    'wifi-client-state': 'Connected',
    'wifi-active-summary-network': 'AstroHome',
    'wifi-active-summary-address': 'http://r5unit.local / http://10.0.0.22',
    'wifi-apply-guidance': 'Open http://r5unit.local or http://10.0.0.22.',
  });
  const clientChosen = await page.isChecked('#wifi-mode-client');
  const joinedPosture = await posture(page);
  report.add('d', 'A joined client reads as joined, with its name and address',
    lib.verdict(joined.ok && clientChosen && joinedPosture === 'client'),
    `${joined.detail}; WiFi Client Mode ${clientChosen ? 'chosen' : 'NOT chosen'} in the form; plate posture "${joinedPosture}"`);

  // e. Pick Standalone AP Mode.
  await lib.step('Next: pick Standalone AP Mode.');
  const postsBeforePick = droid.posts.length;
  await page.click('#wifi-mode-standalone-ap');
  const picked = await page.evaluate(() => ({
    checked: document.getElementById('wifi-mode-standalone-ap').checked,
    marked: document.querySelector('label[for="wifi-mode-standalone-ap"]').classList.contains('is-selected'),
    clientMarked: document.querySelector('label[for="wifi-mode-client"]').classList.contains('is-selected'),
  }));
  report.add('e', 'Picking Standalone AP Mode marks it chosen and sends nothing',
    lib.verdict(picked.checked && picked.marked && !picked.clientMarked && droid.posts.length === postsBeforePick),
    `${JSON.stringify(picked)}; ${droid.posts.length - postsBeforePick} save(s) sent`);

  // f. A short AP password.
  await lib.step('Next: give the AP a 5-character password and Save.');
  await page.fill('#wifi-ap-password', 'short');
  const shortSent = await pressSave(page, droid);
  const shortRefusal = await refusalOn(page, 'wifi-ap-password');
  report.add('f', 'An AP password under 8 characters is refused, on the AP Password box, and nothing is saved',
    lib.verdict(shortSent.length === 1 && shortSent[0].apPassword === 'short' && shortRefusal.says !== '' && shortRefusal.invalid === 'true'
      && /\berror\b/.test(shortRefusal.feedbackClass) && droid.saved.mode === 'client' && droid.saved.apPassword === 'field-kit-pass'),
    `box says "${shortRefusal.says}", aria-invalid ${shortRefusal.invalid}; feedback (${shortRefusal.feedbackClass}) "${shortRefusal.feedback}"; saved mode still "${droid.saved.mode}"`);

  // g. An emptied AP network name.
  await lib.step('Next: clear the AP password and the AP network name, and Save.');
  await page.fill('#wifi-ap-password', '');
  await page.fill('#wifi-ap-ssid', '');
  const emptySent = await pressSave(page, droid);
  const nameRefusal = await refusalOn(page, 'wifi-ap-ssid');
  const passwordCleared = await refusalOn(page, 'wifi-ap-password');
  report.add('g', 'An emptied AP network name is refused, on the AP Network name box, and the password refusal clears',
    lib.verdict(emptySent.length === 1 && emptySent[0].apSsid === '' && nameRefusal.says !== '' && nameRefusal.invalid === 'true'
      && passwordCleared.says === '' && passwordCleared.invalid === 'false' && droid.saved.apSsid === 'FieldArtoo'),
    `name box says "${nameRefusal.says}", aria-invalid ${nameRefusal.invalid}; password box now says "${passwordCleared.says}", aria-invalid ${passwordCleared.invalid}; saved AP network still "${droid.saved.apSsid}"`);

  // h. The whole switch.
  await lib.step('Next: name the AP R2-FieldKit with a good password, and Save.');
  await page.fill('#wifi-ap-ssid', 'R2-FieldKit');
  await page.fill('#wifi-ap-password', 'fieldpass1');
  const switchSent = await pressSave(page, droid);
  const sent = switchSent[0] || {};
  const savedSaid = await expectTexts(page, {
    'wifi-settings-feedback': 'WiFi settings saved. Reboot the Body Controller to apply the staged network switch.',
  });
  report.add('h', 'A whole switch is one Save of mode, AP name and password, with no client password',
    lib.verdict(switchSent.length === 1 && sent.wifiMode === 'standalone_ap' && sent.apSsid === 'R2-FieldKit' && sent.apPassword === 'fieldpass1'
      && !('staPassword' in sent) && droid.saved.staPassword === 'astro-home-pass' && savedSaid.ok),
    `sent ${switchSent.length} save(s): ${JSON.stringify(sent)}; saved client password ${droid.saved.staPassword === 'astro-home-pass' ? 'kept' : 'CHANGED'}; ${savedSaid.detail}`);

  // i. Named as waiting for a reboot.
  const staged = await expectTexts(page, {
    'wifi-pending-summary': 'Pending apply',
    'wifi-posture-desc': 'Saved Standalone AP Mode settings wait for a reboot.',
    'wifi-compare-state': 'saved settings wait for a reboot',
    'wifi-saved-summary-title': 'Saved after reboot',
    'wifi-saved-summary-mode': 'Standalone AP Mode',
    'wifi-saved-summary-ap': 'R2-FieldKit',
    'wifi-ap-password-hint': 'Saved password present; blank keeps it.',
    'wifi-apply-state': 'staged, waiting for a reboot',
    'wifi-apply-guidance': 'After the reboot, join R2-FieldKit and open http://192.168.4.1. OTA goes to 192.168.4.1 while you are on that network.',
  });
  const live = await page.isEnabled('#wifi-apply-reboot-button');
  report.add('i', 'Once saved, the switch waits for a reboot, the page says where the droid will be, and Reboot to Apply is live',
    lib.verdict(staged.ok && live), `${staged.detail}; Reboot to Apply ${live ? 'live' : 'refused'}`);
  await page.screenshot({ path: `${ARTIFACTS}/standalone-ap-staged.png`, fullPage: true });

  // j. Reboot to Apply.
  await lib.step('Next: press Reboot to Apply.');
  await page.click('#wifi-apply-reboot-button');
  await page.waitForFunction(() => (document.getElementById('wifi-apply-feedback')?.textContent || '').trim() !== ''
    && !/Sending/.test(document.getElementById('wifi-apply-feedback').textContent), null, { timeout: 10000 })
    .catch(() => {});
  const rebooted = await expectTexts(page, {
    'wifi-apply-feedback': 'Reboot command sent. After it restarts, reconnect using the guidance above.',
  });
  report.add('j', 'Reboot to Apply sends one reboot and says so', lib.verdict(droid.reboots === 1 && rebooted.ok),
    `${droid.reboots} reboot(s) sent; ${rebooted.detail}`);

  const width = await page.evaluate(() => ({ window: window.innerWidth, page: document.documentElement.scrollWidth }));
  report.add('k', 'The page is no wider than the window', lib.verdict(width.page <= width.window), `page ${width.page} px, window ${width.window} px`);

  report.add('l', 'WiFi threw nothing and logged no error', lib.verdict(pageErrors.length === 0 && consoleErrors.length === 0),
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
  const droid = createDroid();
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

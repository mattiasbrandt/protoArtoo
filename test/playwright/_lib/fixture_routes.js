// Browser-side stand-ins for the controller routes tools/serve_editor_fixture.py
// does not answer, for the scripts that run through ./checks.js runCheck().
//
// Loaded ONLY when FIXTURE=1. Against the live controller nothing here runs,
// so a live run is the script and the droid and nothing in between.
//
// Started from test/playwright/shell/_fixture_routes.js (the SSE stand-in and
// the "refuse and keep" write rule are that file's) and widened for what these
// scripts read: the lineup, the photographs, a Light wire, the calibration
// dial's hold and its two firmware bounds, and the estop letting go of every
// Output. That file belongs to other scripts, so this one is a copy rather
// than an edit; the two can be merged once both settle.
//
// Options (install(context, options)): `estop: true` starts the droid latched
// (and its enabled Outputs already let go); `textPlainPhotos: [ids]` answers
// those registry photographs as text/plain, for a self-test.
//
// WHAT IT MODELS, AND FROM WHERE. Each behaviour below is the firmware's, read
// from the source named beside it, so a script that passes here passes for the
// reason it would on the droid:
//   - GET /api/identity/components is parsed out of include/component_registry.inc
//     at install time, not typed here. `included` follows the artoo_esp32 build.
//   - /<id>.webp answers image/webp only for a Component Registry id, and a
//     picture that is not one (mrbaddeley) answers text/plain: the device
//     registers the image/webp handler per registry id and serveStatic() falls
//     back to text/plain for .webp (src/web/web_request_psychic.cpp).
//   - POST /api/servo hold/release: a press takes the Output, `refresh=1` only
//     refreshes a hold that stands and is dropped otherwise, and a hold nobody
//     refreshes for SERVO_HOLD_EXPIRY_MS (3000, include/config.h) goes limp with
//     `limp: "expiry"` (include/servo_hold.h, src/tasks/servo_task.cpp holdArm).
//   - POST /api/estop latches and releases every ENABLED Output with
//     `limp: "estop"` (releaseAllOutputs, include/servo_halt.h). Enabled is the
//     wired tick on a row that carries no light (isArmEnabled's lit mask).
//   - A hold under a latched estop is answered 200 and changes nothing: the
//     route queues it and ServoTask refuses it (src/web/api_servo.cpp).
//   - POST /api/config with `outputs` rows saves a row's ledCount and answers
//     the config (sendConfigSnapshot()).
//   - A client admitted to /api/events is pushed a status, and so is every
//     other open client (src/web/api_events.cpp).
// Shapes follow docs/api.md; they are a plausible artoo with nothing but USB
// connected, not a copy of any one board's answer.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

const REPO = path.resolve(__dirname, '..', '..', '..');
const ASSETS = path.join(REPO, 'data', 'asset-sets', 'default');
const HOLD_EXPIRY_MS = 3000;

// The Component Registry, read from the one file the firmware builds it from.
const readRegistry = () => {
  const source = fs.readFileSync(path.join(REPO, 'include', 'component_registry.inc'), 'utf8');
  const categories = [];
  const categoryId = new Map();
  for (const match of source.matchAll(/^PA_COMPONENT_CATEGORY\((\w+),\s*"([^"]+)",\s*"([^"]+)",\s*(nullptr|"[^"]+")\)/gm)) {
    categoryId.set(match[1], match[2]);
    categories.push({ id: match[2], name: match[3], member_key: match[4] === 'nullptr' ? null : match[4].slice(1, -1) });
  }
  const parts = [];
  const partRe = /^PA_COMPONENT_PART\(\s*(\d+),\s*"([^"]+)",\s*"([^"]+)",\s*(\w+),\s*"([^"]+)",\s*COMPONENT_STATUS_(\w+),([\s\S]*?)\)\s*$/gm;
  for (const match of source.matchAll(partRe)) {
    const status = match[6].toLowerCase();
    const tail = match[7];
    // The last argument is `included`: a literal, the board test, or a
    // capability macro that is 1 on the artoo_esp32 build.
    const includedExpr = tail.slice(tail.lastIndexOf(',') + 1).trim();
    let included;
    if (includedExpr === '1') included = true;
    else if (includedExpr === '0') included = false;
    else if (includedExpr.includes('PA_BOARD_ARTOO_ESP32')) included = true;
    else if (includedExpr.includes('PA_BOARD_FIREBEETLE2')) included = false;
    else if (includedExpr.startsWith('PA_CAP_')) included = true;
    else throw new Error(`component_registry.inc: cannot read "included" for ${match[2]}: ${includedExpr}`);
    const gate = /"(PA_CAP_\w+)"/.exec(tail);
    parts.push({
      id: match[2],
      value: Number(match[1]),
      name: match[3],
      category: categoryId.get(match[4]),
      protocol: match[5],
      status,
      capabilities: 0,
      included,
      board_capability: gate ? gate[1] : null,
    });
  }
  if (parts.length < 20) throw new Error(`component_registry.inc: read only ${parts.length} parts`);
  categories.forEach((category) => {
    const selectable = parts.filter((part) => part.category === category.id && part.status === 'supported' && part.included);
    category.selectable = selectable.length;
    if (category.selectable <= 1) category.member_key = null;
    category.active_member = null;
  });
  const sound = categories.find((category) => category.id === 'sound');
  if (sound) sound.active_member = 'dy_sv5w';
  return { categories, parts };
};

// Five Artoo-style Outputs (include/board_outputs.h ids and labels). ARM1
// carries two body panels and is calibrated; AUX3 carries the CBI and the LED
// strip Light Type, so Lights has a wire with an LED count; ARM2 is wired with
// nothing on it; AUX1 and AUX2 are not wired.
const initialOutputs = () =>
  [
    { n: 1, id: 'arm1', name: 'ARM1', wired: true, parts: ['bodyPanel1', 'bodyPanel2'], calibrated: true },
    { n: 2, id: 'arm2', name: 'ARM2', wired: true, parts: [], calibrated: false },
    { n: 3, id: 'aux1', name: 'AUX1', wired: false, parts: [], calibrated: false },
    { n: 4, id: 'aux2', name: 'AUX2', wired: false, parts: [], calibrated: false },
    { n: 5, id: 'aux3', name: 'AUX3', wired: true, parts: ['cbi'], calibrated: false, light: true },
  ].map((row) => ({
    address: `ledc:${row.n - 1}`,
    name: row.name,
    id: row.id,
    switchable: true,
    wired: row.wired,
    lightCapable: row.n >= 3,
    ledCount: row.n >= 3 ? 16 : null,
    throwMs: 400,
    accelMs: 100,
    ease: 'none',
    boot: 'limp',
    parts: row.parts,
    bandLoUs: 1000,
    bandHiUs: 2000,
    component: row.light ? 'rgb' : 'mg996r',
    openUs: row.calibrated ? 2000 : null,
    centreUs: row.calibrated ? 1500 : null,
    closeUs: row.calibrated ? 1000 : null,
    calibrated: row.calibrated,
    commandedUs: null,
    targetUs: null,
    nudgesDone: 0,
    held: false,
    limp: 'off',
  }));

// Enabled in ServoTask's sense: wired, and not a wire kept for a light.
const isEnabledOutput = (row) => row.wired && row.component !== 'rgb';

const parseForm = (body) => Object.fromEntries(new URLSearchParams(body || ''));

const install = async (context, options = {}) => {
  const registry = readRegistry();
  const registryIds = new Set(registry.parts.map((part) => part.id));

  const state = {
    estop: options.estop === true,
    outputs: initialOutputs(),
    // Wall-clock of the last hold command per address, while a hold stands.
    holds: new Map(),
    writes: [],
    outputsReads: 0,
    config: {
      drive: { speedLimitMax: 40, speedPreset: 'normal', webDriveTimeoutMs: 500, stationary: false },
      rc: { inputMode: 'standard_pwm', activeInputMode: 'standard_pwm', sbusTimeoutMs: 300, sbus: { recvCh2: false }, member: 'rc_radio' },
      activeToggles: ['drive', 'rcCh1', 'rcCh2'],
      components: {
        domeEsc: { enabled: false, label: 'DOME' },
        rcCh1: { enabled: true, label: 'CH1' },
        rcCh2: { enabled: true, label: 'CH2' },
        rcCh3: { enabled: false, label: 'CH3' },
        rcCh4: { enabled: false, label: 'CH4' },
        rcCh5: { enabled: false, label: 'CH5' },
        rcCh6: { enabled: false, label: 'CH6' },
        drive: { enabled: true, label: 'S1' },
        audio: { enabled: false, label: 'S2', member: 'dy_sv5w', activeMember: 'dy_sv5w' },
        protoR2link: { enabled: false, label: 'S3' },
      },
      system: { logLevel: 3 },
      droidBuild: {
        domeDesign: 'mk4',
        domeVariant: 'complex',
        bodyDesign: 'mk4',
        bodyVariant: 'complex',
        fitted: ['bodyPanel1', 'bodyPanel2', 'cbi', 'dataPanel', 'pie1', 'panel1'],
      },
      // A controller configured before guided Setup existed: no record at all.
      guidedSetup: { run: 'not-run', visited: [], recorded: false, summaryDone: false },
      wifi: {
        provisioned: true,
        mode: 'client',
        staSsid: 'bench-ap',
        staPasswordSet: true,
        apSsid: 'protoartoo',
        apPasswordSet: false,
        pendingApply: false,
      },
    },
  };

  const identity = () => ({
    droidName: 'fixture-artoo',
    mdnsUseName: true,
    board: 'artoo_esp32',
    learned_sequence_cap: 5,
    board_capabilities: {
      PA_CAP_NATIVE_WIFI: true,
      PA_CAP_HOSTED_WIFI: false,
      PA_CAP_DRIVE_BACKEND_HOVERBOARD: true,
      PA_CAP_DEDICATED_AUDIO_UART: false,
    },
    board_lanes: {
      drive: { uart: 1, tx: 16, rx: 17 },
      audio: { uart: 2, tx: 26, rx: 35 },
      protor2link: { uart: 2, tx: 33, rx: 34 },
    },
    build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
  });

  const status = () => {
    const frame = {
      estop: state.estop,
      sbusHwFailsafe: false,
      sbusSignalLost: false,
      webDriveExpired: false,
      webControlEnabled: false,
      sleepMode: false,
      drive: { state: 'idle', detail: 'Enabled' },
      speedLimitMax: 40,
      speedPreset: 'normal',
      stationary: false,
      activeMood: 0,
      domeEnabled: false,
      rcCh1: { state: 'ready', detail: 'Standard PWM input enabled' },
      rcCh2: { state: 'ready', detail: 'Standard PWM input enabled' },
      uptimeMs: Date.now() % 100000000,
      firmwareVersion: 'fixture',
      fsVersion: 'fixture',
      resetReason: 'POWERON',
      heapFree: 150000,
      heapMin: 120000,
      heapLargestBlock: 90000,
      wifiConnected: true,
      wifiClientConnected: true,
      wifiRssi: -55,
      littleFsReady: true,
      dome_link: { state: 'disabled' },
      lights: {},
    };
    // An enabled Output is a key of its own (src/web/status_json.cpp), and a
    // lit wire reports under its id in `lights`.
    state.outputs.forEach((row) => {
      if (isEnabledOutput(row)) frame[row.id] = { state: 'ready', detail: 'Servo channel enabled' };
      if (row.wired && row.component === 'rgb') frame.lights[row.id] = { r: 0, g: 0, b: 0, effect: 'off', available: true };
    });
    return frame;
  };

  // The SSE stand-in. Every open client is pushed each change, and a client
  // arriving is itself a change (src/web/api_events.cpp).
  const clients = new Set();
  const frameText = () => `event: status\ndata: ${JSON.stringify(status())}\n\n`;
  const push = () => clients.forEach((res) => res.write(frameText()));
  const server = http.createServer((req, res) => {
    if (!req.url.startsWith('/events')) {
      res.writeHead(404);
      res.end();
      return;
    }
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-store',
      Connection: 'keep-alive',
      'Access-Control-Allow-Origin': '*',
    });
    clients.add(res);
    push();
    const keepalive = setInterval(() => res.write(': keepalive\n\n'), 5000);
    req.on('close', () => {
      clearInterval(keepalive);
      clients.delete(res);
    });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const ssePort = server.address().port;

  // The hold's expiry, checked on a clock the way ServoTask checks it each tick.
  const expiryTimer = setInterval(() => {
    const now = Date.now();
    state.holds.forEach((lastAt, address) => {
      if (now - lastAt <= HOLD_EXPIRY_MS) return;
      state.holds.delete(address);
      const row = state.outputs.find((each) => each.address === address);
      if (!row) return;
      row.held = false;
      row.commandedUs = null;
      row.targetUs = null;
      row.limp = 'expiry';
    });
  }, 100);

  const releaseAll = (reason) => {
    state.outputs.forEach((row) => {
      if (!isEnabledOutput(row)) return;
      state.holds.delete(row.address);
      row.held = false;
      row.commandedUs = null;
      row.targetUs = null;
      row.limp = reason;
    });
  };

  // A droid that starts latched has already been through the latch's edge.
  if (state.estop) releaseAll('estop');

  const json = (route, body, code = 200) =>
    route.fulfill({ status: code, contentType: 'application/json', body: JSON.stringify(body) });

  const refuse = (route, method, apiPath, body) => {
    // A write this fixture was never meant to see. Refused, and kept, so a
    // script that asserts "no writes" has something to read.
    state.writes.push(`${method} ${apiPath} ${String(body || '').slice(0, 80)}`.trim());
    return json(route, { ok: false, error: 'fixture refuses writes' }, 400);
  };

  const servoPost = (route, body) => {
    const form = parseForm(body);
    const row = state.outputs.find((each) => each.name === form.arm);
    if (!row) return json(route, { ok: false, error: `No output called ${form.arm} on this board` }, 400);
    if (form.action === 'hold') {
      const us = Number(form.positionUs);
      if (!Number.isFinite(us) || us < 500 || us > 2500) return json(route, { ok: false, error: 'positionUs must be between 500 and 2500' }, 400);
      // Queued and answered; ServoTask refuses every command under the latch.
      if (state.estop) return json(route, { ok: true });
      const refresh = form.refresh === '1';
      if (refresh && !state.holds.has(row.address)) return json(route, { ok: true });
      state.holds.set(row.address, Date.now());
      row.held = true;
      row.commandedUs = us;
      row.targetUs = us;
      return json(route, { ok: true });
    }
    if (form.action === 'release') {
      if (state.estop) return json(route, { ok: true });
      state.holds.delete(row.address);
      row.held = false;
      row.commandedUs = null;
      row.targetUs = null;
      row.limp = 'pulses-off';
      return json(route, { ok: true });
    }
    return refuse(route, 'POST', '/api/servo', body);
  };

  const configPost = (route, request, body) => {
    const type = request.headers()['content-type'] || '';
    if (!type.includes('application/json')) return refuse(route, 'POST', '/api/config', body);
    let parsed;
    try {
      parsed = JSON.parse(body || '');
    } catch (_error) {
      return json(route, { ok: false, error: 'invalid JSON' }, 400);
    }
    const keys = Object.keys(parsed || {});
    if (keys.length !== 1 || keys[0] !== 'outputs' || !Array.isArray(parsed.outputs)) {
      return refuse(route, 'POST', '/api/config', body);
    }
    for (const sent of parsed.outputs) {
      const row = state.outputs.find((each) => each.address === sent.address);
      if (!row) return json(route, { ok: false, error: `${sent.address} is not an Output`, field: `${sent.address}` }, 400);
      if ('ledCount' in sent) {
        if (!row.lightCapable || !Number.isInteger(sent.ledCount) || sent.ledCount < 1 || sent.ledCount > 255) {
          return json(route, { ok: false, error: 'ledCount must be between 1 and 255', field: `${sent.address}.ledCount` }, 400);
        }
        row.ledCount = sent.ledCount;
      }
    }
    state.writes.push(`POST /api/config ${body.slice(0, 80)}`);
    return json(route, state.config);
  };

  // The photographs: image/webp for a registry id, text/plain for any other
  // file the set carries, as the device answers them.
  await context.route('**/*.webp*', async (route) => {
    const url = new URL(route.request().url());
    const name = path.basename(url.pathname, '.webp');
    const file = path.join(ASSETS, `${name}.webp`);
    if (!fs.existsSync(file)) return route.fulfill({ status: 404, contentType: 'text/plain', body: 'Not found' });
    const body = fs.readFileSync(file);
    // `textPlainPhotos` is a self-test's: registry ids answered the wrong way.
    const typed = registryIds.has(name) && !(options.textPlainPhotos || []).includes(name);
    return route.fulfill({ status: 200, contentType: typed ? 'image/webp' : 'text/plain', body });
  });

  await context.route('**/api/**', async (route) => {
    const request = route.request();
    const url = request.url();
    const apiPath = url.slice(url.indexOf('/api/')).split('?')[0];
    const method = request.method();
    const body = request.postData() || '';

    if (apiPath === '/api/events') {
      await route.continue({ url: `http://127.0.0.1:${ssePort}/events` });
      return;
    }
    if (method === 'GET') {
      if (apiPath === '/api/identity') return json(route, identity());
      if (apiPath === '/api/identity/components') return json(route, registry);
      if (apiPath === '/api/status') return json(route, status());
      if (apiPath === '/api/config') return json(route, state.config);
      if (apiPath === '/api/servo/outputs') {
        state.outputsReads += 1;
        return json(route, { outputs: state.outputs.map((row) => ({ ...row, parts: row.parts.slice() })) });
      }
      if (apiPath === '/api/rc/map') return json(route, { mode: state.config.rc.inputMode, map: [] });
      // Every other read goes to the fixture server, which 404s it the way
      // the controller answers a route it does not have.
      return route.fallback();
    }
    if (method === 'POST' && apiPath === '/api/estop') {
      state.estop = true;
      releaseAll('estop');
      push();
      return json(route, { ok: true });
    }
    if (method === 'POST' && apiPath === '/api/servo') return servoPost(route, body);
    if (method === 'POST' && apiPath === '/api/config') return configPost(route, request, body);
    return refuse(route, method, apiPath, body);
  });

  return {
    state,
    ssePort,
    push,
    // The operator clearing the latch on Foot Drive or the Dashboard, which
    // these scripts never do themselves.
    clearEstop: () => {
      state.estop = false;
      push();
    },
    close: () =>
      new Promise((resolve) => {
        clearInterval(expiryTimer);
        clients.forEach((res) => res.end());
        server.close(() => resolve());
        server.closeAllConnections();
      }),
  };
};

module.exports = { install };

// Browser-side stand-ins for the controller routes tools/serve_editor_fixture.py
// does not answer, for every script under test/playwright/ that takes FIXTURE=1:
// the rule scripts through ./checks.js runCheck(), and the older scripts
// (shell/stop-every-surface.js, shell/status-plate-truth.js,
// parts/parts-surface.js, console-sweep.js) that install it themselves.
//
// Loaded ONLY when FIXTURE=1. Against the live controller nothing here runs,
// so a live run is the script and the droid and nothing in between.
//
// Why a module and not a fixture-server edit: the fixture server is shared by
// every other script and deliberately 404s any /api/* route it does not have
// (its header, #261). These answers are shaped for these scripts' needs, so
// they live beside them and ride on Playwright's routing instead.
//
// THE TWO DROIDS. install(context, { droid }) answers as one of two droids.
// Every route below is answered for both; only the shapes of four answers
// differ, and each script keeps the droid its checks were proved against.
//   'artoo' (the default) - five Artoo-style Outputs from include/board_outputs.h
//       (ARM1 carrying two body panels, calibrated; ARM2 wired with nothing on
//       it; AUX1 and AUX2 not wired; AUX3 the CBI on an LED strip), a
//       configured droid's config (components, Droid Build, a guided Setup
//       record of a droid configured before it existed), and a full identity
//       and status frame.
//   'bench' - five wired ARMs with no light, ARM1 ganged to two body panels and
//       FOLLOWING: its commanded width alternates 1400/1600 on every Outputs
//       read, so a once-a-second follow actually changes a value, which is
//       what makes "only values change" a test rather than a formality. The
//       config is the older-firmware answer with no droidBuild key, so the
//       Parts picture draws every marker; the status frame carries the six
//       fields data/live_reading.js requires and just enough for every Status
//       Plate chip to read a value.
//
// Other options: `estop: true` starts the droid latched (and its enabled
// Outputs already let go); `textPlainPhotos: [ids]` answers those registry
// photographs as text/plain, for a self-test.
//
// FIXTURE_FITTED=<part ids, comma-separated> in the environment fits those
// Parts on whichever droid a script runs, so a script with no option of its
// own can meet a droid that carries a Common Addition - an arm is on the Parts
// picture only once it is fitted. The artoo droid adds them to its Droid
// Build; the bench droid, which has none, gets an MK4 Complex body and dome
// with those and the Parts on its Outputs fitted. Unset, both droids answer exactly as above.
//
// THE STATUS STREAM is the one route page.route cannot fake faithfully:
// route.fulfill() hands over a finished body, so an EventSource reading it
// sees the stream END, errors and reconnects - a flapping link, which is the
// exact thing status-plate-truth.js measures. So /api/events is continued to a
// small real SSE server started here, which holds the connection open.
//
// The same small server also takes POST /api/estop, so a script can latch the
// estop from OUTSIDE the browser the way a second client or the Console would
// on the bench: page.route never sees a request made with Playwright's own
// request API, so that write has to reach something that is not a route.
//
// One droid, many browsers: install() routes the first context and hands back
// addContext(), which routes another context against the SAME state and the
// same stream, so a fresh browser context meets the droid the first one left
// (the estop it latched, the sleep it asked for).
//
// WHAT IT MODELS, AND FROM WHERE. Each behaviour below is the firmware's, read
// from the source named beside it, so a script that passes here passes for the
// reason it would on the droid:
//   - GET /api/identity/components is parsed out of include/component_registry.inc
//     at install time, not typed here. `included` follows the artoo_esp32 build.
//   - /<id>.webp answers image/webp for every picture the set carries whose
//     name has the safe shape (lowercase letters, digits, underscore): one
//     handler claims the shape, not a list of ids, so Droid Build pictures
//     such as mrbaddeley answer image/webp too (include/web_webp.h
//     webPathIsWebpPicture, src/web/web_request_psychic.cpp, #355 finding 4).
//   - POST /api/servo hold/release: a press takes the Output, `refresh=1` only
//     refreshes a hold that stands and is dropped otherwise, and a hold nobody
//     refreshes for SERVO_HOLD_EXPIRY_MS (3000, include/config.h) goes limp with
//     `limp: "expiry"` (include/servo_hold.h, src/tasks/servo_task.cpp holdArm).
//   - POST /api/estop latches and releases every ENABLED Output with
//     `limp: "estop"` (releaseAllOutputs, include/servo_halt.h). Enabled is the
//     wired tick on a row that carries no light (isArmEnabled's lit mask). A
//     status is pushed on the first trigger only (src/failsafe_gate.cpp
//     failsafeTrigger, requestStatusBroadcastNow); a repeat is idempotent.
//   - A hold under a latched estop is answered 200 and changes nothing: the
//     route queues it and ServoTask refuses it (src/web/api_servo.cpp).
//   - POST /api/config with `outputs` rows saves a row's ledCount and answers
//     the config (sendConfigSnapshot()). POST /api/config with nothing but
//     guidedSetupVisited=<step> (data/setup.js saveVisited) is taken and kept
//     in `state.accepted`, so it is never mistaken for nothing having been
//     written.
//   - POST /api/sleep and /api/wake answer {ok, sleepMode, changed}
//     (docs/api.md) and push a status only when the posture changed
//     (src/web/api_system.cpp).
//   - A client admitted to /api/events is pushed a status, and so is every
//     other open client (src/web/api_events.cpp).
//   - GET /api/rc and GET /api/rc/map answer as docs/api.md gives them for a
//     droid with no receiver switched on. Without them the RC surface's
//     sections fail and the bootstrap retries them on its own clock, which is
//     a request nothing on the surface asked for - and exactly what a "leaving
//     a surface stops its polling" count must not be confused by.
//   - GET /api/seq/list and /api/seq/builtins answer an empty JSON array each
//     (docs/api.md): no Learned Sequences, and the factory list left empty
//     rather than invented.
// Any other write is refused with 400 and kept in `state.writes`, so a script
// that asserts "no writes" has something to read. Any other read goes to the
// fixture server, which 404s it the way the controller answers a route it does
// not have.
//
// Shapes follow docs/api.md; they are a plausible droid with nothing but USB
// connected, not a copy of any one board's answer.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const REPO = path.resolve(__dirname, '..', '..', '..');
const ASSETS = path.join(REPO, 'data', 'asset-sets', 'default');
const HOLD_EXPIRY_MS = 3000;
const DROIDS = ['artoo', 'bench'];

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

// One Output row in GET /api/servo/outputs's shape (docs/api.md).
const outputRow = ({ n, id, name, wired, parts, calibrated, lightCapable, component, following = false }) => ({
  address: `ledc:${n - 1}`,
  name,
  id,
  switchable: true,
  wired,
  lightCapable,
  ledCount: lightCapable ? 16 : null,
  throwMs: 400,
  accelMs: 100,
  ease: 'none',
  boot: 'limp',
  parts,
  bandLoUs: 1000,
  bandHiUs: 2000,
  component,
  openUs: calibrated ? 2000 : null,
  centreUs: calibrated ? 1500 : null,
  closeUs: calibrated ? 1000 : null,
  calibrated,
  commandedUs: null,
  targetUs: null,
  nudgesDone: 0,
  held: false,
  limp: 'off',
  // Not a firmware field: which row the 'bench' droid moves on every read.
  // Stripped from every answer.
  following,
});

const initialOutputs = (droid) => {
  if (droid === 'bench') {
    return [1, 2, 3, 4, 5].map((n) =>
      outputRow({
        n,
        id: `arm${n}`,
        name: `ARM${n}`,
        wired: true,
        parts: n === 1 ? ['bodyPanel1', 'bodyPanel2'] : [],
        calibrated: n === 1,
        lightCapable: false,
        component: 'none',
        following: n === 1,
      }),
    );
  }
  // Five Artoo-style Outputs (include/board_outputs.h ids and labels). ARM1
  // carries two body panels and is calibrated; AUX3 carries the CBI and the
  // LED strip Light Type, so Lights has a wire with an LED count; ARM2 is
  // wired with nothing on it; AUX1 and AUX2 are not wired.
  return [
    { n: 1, id: 'arm1', name: 'ARM1', wired: true, parts: ['bodyPanel1', 'bodyPanel2'], calibrated: true },
    { n: 2, id: 'arm2', name: 'ARM2', wired: true, parts: [], calibrated: false },
    { n: 3, id: 'aux1', name: 'AUX1', wired: false, parts: [], calibrated: false },
    { n: 4, id: 'aux2', name: 'AUX2', wired: false, parts: [], calibrated: false },
    { n: 5, id: 'aux3', name: 'AUX3', wired: true, parts: ['cbi'], calibrated: false, light: true },
  ].map((row) => outputRow({ ...row, lightCapable: row.n >= 3, component: row.light ? 'rgb' : 'mg996r' }));
};

const initialConfig = (droid) => {
  // No droidBuild key: the older-firmware answer, so the Parts picture falls
  // back to drawing every marker rather than only the fitted ones.
  if (droid === 'bench') return { system: { logLevel: 3 } };
  return {
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
  };
};

// The FIXTURE_FITTED override (header). An id the catalog (data/droid_parts.js)
// does not carry stops the script: it would otherwise run against a droid
// that fits nothing new and pass for the wrong reason.
const withFitted = (config, outputs, list) => {
  const ids = String(list || '')
    .split(',')
    .map((id) => id.trim())
    .filter(Boolean);
  if (ids.length === 0) return config;
  const catalog = { window: {} };
  vm.runInNewContext(fs.readFileSync(path.join(REPO, 'data', 'droid_parts.js'), 'utf8'), catalog);
  const known = new Set(catalog.window.DroidParts.parts.map((part) => part.id));
  const unknown = ids.filter((id) => !known.has(id));
  if (unknown.length) throw new Error(`fixture_routes: FIXTURE_FITTED names no catalog Part: ${unknown.join(', ')}`);
  // A droid with no Droid Build of its own still carries what its Outputs
  // drive, so those stay fitted and keep offering their acts.
  const build = config.droidBuild || {
    domeDesign: 'mk4',
    domeVariant: 'complex',
    bodyDesign: 'mk4',
    bodyVariant: 'complex',
    fitted: outputs.flatMap((row) => row.parts || []),
  };
  return { ...config, droidBuild: { ...build, fitted: [...new Set([...build.fitted, ...ids])] } };
};

const identityOf = (droid) => {
  if (droid === 'bench') {
    return {
      droidName: 'fixture-artoo',
      mdnsUseName: true,
      board: 'artoo_esp32',
      board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
      build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
    };
  }
  return {
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
  };
};

// Enabled in ServoTask's sense: wired, and not a wire kept for a light.
const isEnabledOutput = (row) => row.wired && row.component !== 'rgb';

const parseForm = (body) => Object.fromEntries(new URLSearchParams(body || ''));

const install = async (context, options = {}) => {
  const droid = options.droid || 'artoo';
  if (!DROIDS.includes(droid)) throw new Error(`fixture_routes: no droid called "${droid}" (${DROIDS.join(', ')})`);
  const registry = readRegistry();
  const identity = identityOf(droid);

  const state = {
    droid,
    estop: options.estop === true,
    sleep: false,
    outputs: initialOutputs(droid),
    // Wall-clock of the last hold command per address, while a hold stands.
    holds: new Map(),
    // Writes refused (kept so "no writes" has something to read), and the
    // guided Setup visit record taken.
    writes: [],
    accepted: [],
    outputsReads: 0,
    config: withFitted(initialConfig(droid), initialOutputs(droid), process.env.FIXTURE_FITTED),
  };

  const status = () => {
    if (droid === 'bench') {
      return {
        // The six fields data/live_reading.js requires of a frame.
        estop: state.estop,
        sbusHwFailsafe: false,
        sbusSignalLost: false,
        webDriveExpired: false,
        webControlEnabled: false,
        sleepMode: state.sleep,
        // Enough beyond them for every Status Plate chip to read a value.
        drive: { backend: 'none' },
        speedLimitMax: 40,
        rcCh1: { state: 'not_seen' },
        stationary: false,
        uptimeMs: Date.now() % 100000000,
      };
    }
    const frame = {
      estop: state.estop,
      sbusHwFailsafe: false,
      sbusSignalLost: false,
      webDriveExpired: false,
      webControlEnabled: false,
      sleepMode: state.sleep,
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

  // The Outputs answer. A following row that the droid still drives moves to
  // its other width on each read.
  const outputsAnswer = () => {
    state.outputsReads += 1;
    state.outputs.forEach((row) => {
      if (!row.following || row.limp !== 'off') return;
      row.commandedUs = state.outputsReads % 2 === 0 ? 1400 : 1600;
      row.targetUs = row.commandedUs;
    });
    return {
      outputs: state.outputs.map(({ following, ...row }) => ({ ...row, parts: row.parts.slice() })),
    };
  };

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

  // The SSE stand-in. Every open client is pushed each change, and a client
  // arriving is itself a change (src/web/api_events.cpp).
  const clients = new Set();
  const frameText = () => `event: status\ndata: ${JSON.stringify(status())}\n\n`;
  const push = () => clients.forEach((res) => res.write(frameText()));

  // The latch, from a page or from outside the browser (header).
  const latch = () => {
    const was = state.estop;
    state.estop = true;
    releaseAll('estop');
    if (!was) push();
  };

  const server = http.createServer((req, res) => {
    // The latch from outside the browser (header). Answered the way
    // src/web/api_estop.cpp answers it.
    if (req.method === 'POST' && req.url.startsWith('/api/estop')) {
      req.resume();
      latch();
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end('{"ok":true}');
      return;
    }
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

  // POST /api/sleep and /api/wake (header).
  const setSleep = (route, sleeping) => {
    const changed = state.sleep !== sleeping;
    state.sleep = sleeping;
    if (changed) push();
    return json(route, { ok: true, sleepMode: sleeping, changed });
  };

  // GET /api/rc for a droid with no receiver switched on (header).
  const rcDiagnostics = () => ({
    mode: state.config.rc?.inputMode || 'standard_pwm',
    updatedMs: Date.now() % 100000000,
    sources: {
      sbus1: { enabled: false, linked: false, ageMs: 0, lostFrames: 0, failsafe: false },
      sbus2: { enabled: false, linked: false, ageMs: 0, lostFrames: 0, failsafe: false },
      pwm: { enabled: false, linked: false, ageMs: 0, lostFrames: 0, failsafe: false },
    },
    channels: [],
    digital: {},
    mappingProfile: { channels: [] },
    raw: {},
  });

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
    // Guided Setup's visit record (data/setup.js saveVisited): taken, and kept.
    if (/^guidedSetupVisited=[^&]*$/.test(body)) {
      state.accepted.push(`POST /api/config ${body}`);
      return json(route, { ok: true });
    }
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

  // The pictures: image/webp for every safely named file the set carries, as
  // the device answers them since #355 finding 4.
  const photoHandler = async (route) => {
    const url = new URL(route.request().url());
    const name = path.basename(url.pathname, '.webp');
    const file = path.join(ASSETS, `${name}.webp`);
    if (!fs.existsSync(file)) return route.fulfill({ status: 404, contentType: 'text/plain', body: 'Not found' });
    const body = fs.readFileSync(file);
    // `textPlainPhotos` is a self-test's: pictures answered the wrong way.
    const safeShape = /^[a-z][a-z0-9_]*$/.test(name);
    const typed = safeShape && !(options.textPlainPhotos || []).includes(name);
    return route.fulfill({ status: 200, contentType: typed ? 'image/webp' : 'text/plain', body });
  };

  const apiHandler = async (route) => {
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
      if (apiPath === '/api/identity') return json(route, identity);
      if (apiPath === '/api/identity/components') return json(route, registry);
      if (apiPath === '/api/status') return json(route, status());
      if (apiPath === '/api/config') return json(route, state.config);
      if (apiPath === '/api/servo/outputs') return json(route, outputsAnswer());
      if (apiPath === '/api/rc') return json(route, rcDiagnostics());
      if (apiPath === '/api/rc/map') {
        return json(route, { mode: state.config.rc?.inputMode || 'standard_pwm', map: [], capacity: { total: 14, used: 0 } });
      }
      if (apiPath === '/api/seq/list' || apiPath === '/api/seq/builtins') return json(route, []);
      // Every other read goes to the fixture server, which 404s it the way
      // the controller answers a route it does not have.
      return route.fallback();
    }
    if (method === 'POST' && apiPath === '/api/estop') {
      latch();
      return json(route, { ok: true });
    }
    if (method === 'POST' && apiPath === '/api/sleep') return setSleep(route, true);
    if (method === 'POST' && apiPath === '/api/wake') return setSleep(route, false);
    if (method === 'POST' && apiPath === '/api/servo') return servoPost(route, body);
    if (method === 'POST' && apiPath === '/api/config') return configPost(route, request, body);
    return refuse(route, method, apiPath, body);
  };

  const routeContext = async (target) => {
    await target.route('**/*.webp*', photoHandler);
    await target.route('**/api/**', apiHandler);
  };
  await routeContext(context);

  return {
    state,
    ssePort,
    // Routes another browser context against this same droid (header).
    addContext: routeContext,
    // Sends every open stream the current status, as a state change on the
    // droid would; a self-test that changes `state` calls it.
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
        // A browser that is gone may leave its socket half-open; do not let
        // it hold the script's exit.
        server.closeAllConnections();
      }),
  };
};

module.exports = { install, DROIDS };

// Browser-side stand-ins for the controller routes tools/serve_editor_fixture.py
// does not answer, for the browser regression scripts in test/playwright/shell/,
// test/playwright/parts/ and test/playwright/console-sweep.js, so each can be
// proved offline before it is pointed at a droid.
//
// Loaded ONLY when FIXTURE=1. Against a live controller nothing here runs, so
// a run on a droid is the script and the droid and nothing in between.
//
// Why a module and not a fixture-server edit: the fixture server is shared by
// every other script and deliberately 404s any /api/* route it does not have
// (its header, #261). These answers are shaped for four scripts' needs, so
// they live beside them and ride on Playwright's routing instead.
//
// The status stream is the one route page.route cannot fake faithfully:
// route.fulfill() hands over a finished body, so an EventSource reading it
// sees the stream END, errors and reconnects - a flapping link, which is the
// exact thing status-plate-truth.js measures. So /api/events is continued to a
// small real SSE server started here, which holds the connection open and
// pushes a status event on connect and on every change, the way
// src/web/api_events.cpp does on admission and on requestStatusBroadcastNow().
// status-plate-truth.js cuts it through a relay of its own, because going
// offline in Chromium was measured NOT to end an open EventSource.
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
// Shapes follow docs/api.md and data/live_reading.js's six core fields; they
// are a plausible Bench-Mode droid, not a copy of any one board's answer.
const http = require('node:http');

const identity = {
  droidName: 'fixture-artoo',
  mdnsUseName: true,
  board: 'artoo_esp32',
  board_capabilities: { PA_CAP_NATIVE_WIFI: true, PA_CAP_HOSTED_WIFI: false },
  build_flags: { PA_HEAP_PROFILE: false, PA_HEAP_TRACING: false, PA_ADMISSION_TRACE: false },
};

// Five Artoo-style Outputs. ARM1 carries a ganged pair so Parts has wired rows
// as well as "not wired" ones; its commanded width alternates per read so a
// once-a-second follow actually changes a value, which is what makes "only
// values change" a test rather than a formality.
const outputsFor = (tick) => ({
  outputs: [1, 2, 3, 4, 5].map((n) => ({
    address: `ledc:${n - 1}`,
    name: `ARM${n}`,
    id: `arm${n}`,
    switchable: true,
    wired: true,
    lightCapable: false,
    throwMs: 400,
    accelMs: 100,
    ease: 'none',
    boot: 'limp',
    parts: n === 1 ? ['bodyPanel1', 'bodyPanel2'] : [],
    bandLoUs: 1000,
    bandHiUs: 2000,
    component: 'none',
    openUs: n === 1 ? 2000 : null,
    centreUs: n === 1 ? 1500 : null,
    closeUs: n === 1 ? 1000 : null,
    calibrated: n === 1,
    commandedUs: n === 1 ? (tick % 2 === 0 ? 1400 : 1600) : null,
    targetUs: n === 1 ? (tick % 2 === 0 ? 1400 : 1600) : null,
    nudgesDone: 0,
    held: false,
    limp: 'off',
  })),
});

const install = async (context) => {
  const state = { estop: false, sleep: false, outputsReads: 0, writes: [], accepted: [] };

  const status = () => ({
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
  });

  // The SSE stand-in. Every open client is pushed each change.
  const clients = new Set();
  const push = () => {
    const frame = `event: status\ndata: ${JSON.stringify(status())}\n\n`;
    clients.forEach((res) => res.write(frame));
  };
  const server = http.createServer((req, res) => {
    // The latch from outside the browser (header). Answered the way
    // src/web/api_estop.cpp answers it, and pushed the way failsafeTrigger()
    // pushes a first trigger (src/failsafe_gate.cpp).
    if (req.method === 'POST' && req.url.startsWith('/api/estop')) {
      req.resume();
      const was = state.estop;
      state.estop = true;
      if (!was) push();
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
    res.write(`event: status\ndata: ${JSON.stringify(status())}\n\n`);
    clients.add(res);
    const keepalive = setInterval(() => res.write(': keepalive\n\n'), 5000);
    req.on('close', () => {
      clearInterval(keepalive);
      clients.delete(res);
    });
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const ssePort = server.address().port;

  const json = (route, body, code = 200) =>
    route.fulfill({ status: code, contentType: 'application/json', body: JSON.stringify(body) });

  // POST /api/sleep and /api/wake answer in the shape docs/api.md gives
  // ({ok, sleepMode, changed}); src/web/api_system.cpp pushes a status only
  // when the posture changed, and so does this.
  const setSleep = (route, sleeping) => {
    const changed = state.sleep !== sleeping;
    state.sleep = sleeping;
    if (changed) push();
    return json(route, { ok: true, sleepMode: sleeping, changed });
  };

  // RC's two reads, shaped as docs/api.md gives GET /api/rc and GET
  // /api/rc/map for a droid with no receiver switched on. Without them the
  // RC surface's sections fail and the bootstrap retries them on its own
  // clock, which is a request nothing on the surface asked for - and exactly
  // what a "leaving a surface stops its polling" count must not be confused by.
  const rcDiagnostics = () => ({
    mode: 'standard_pwm',
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

  const handler = async (route) => {
    const request = route.request();
    const url = request.url();
    const path = url.slice(url.indexOf('/api/')).split('?')[0];
    const method = request.method();

    if (path === '/api/events') {
      await route.continue({ url: `http://127.0.0.1:${ssePort}/events` });
      return;
    }
    if (method === 'GET' && path === '/api/identity') return json(route, identity);
    if (method === 'GET' && path === '/api/status') return json(route, status());
    if (method === 'GET' && path === '/api/servo/outputs') {
      state.outputsReads += 1;
      return json(route, outputsFor(state.outputsReads));
    }
    // No droidBuild key: the older-firmware answer, so the Parts picture falls
    // back to drawing every marker rather than only the fitted ones.
    if (method === 'GET' && path === '/api/config') return json(route, { system: { logLevel: 3 } });
    if (method === 'GET' && path === '/api/rc') return json(route, rcDiagnostics());
    if (method === 'GET' && path === '/api/rc/map') {
      return json(route, { mode: 'standard_pwm', map: [], capacity: { total: 14, used: 0 } });
    }
    if (method === 'POST' && path === '/api/estop') {
      const was = state.estop;
      state.estop = true;
      if (!was) push();
      return json(route, { ok: true });
    }
    if (method === 'POST' && path === '/api/sleep') return setSleep(route, true);
    if (method === 'POST' && path === '/api/wake') return setSleep(route, false);
    // Sequences' two list reads (docs/api.md: a JSON array each), empty: a
    // droid with no Learned Sequences, and the factory list left empty rather
    // than invented.
    if (method === 'GET' && (path === '/api/seq/list' || path === '/api/seq/builtins')) return json(route, []);
    // Guided Setup's visit record (data/setup.js saveVisited), the one write
    // console-sweep.js lets through: taken, and kept in `accepted` so it is
    // never mistaken for nothing having been written.
    if (method === 'POST' && path === '/api/config' && /^guidedSetupVisited=[^&]*$/.test(request.postData() || '')) {
      state.accepted.push(`${method} ${path} ${request.postData()}`);
      return json(route, { ok: true });
    }
    if (method !== 'GET') {
      // A write this fixture was never meant to see. Refused, and kept, so a
      // script that asserts "no writes" has something to read.
      state.writes.push(`${method} ${path}`);
      return json(route, { ok: false, error: 'fixture refuses writes' }, 400);
    }
    // Every other read goes to the fixture server, which 404s it the way the
    // controller answers a route it does not have.
    await route.fallback();
  };
  await context.route('**/api/**', handler);

  return {
    state,
    ssePort,
    // Routes another browser context against this same droid (header).
    addContext: (other) => other.route('**/api/**', handler),
    // Sends every open stream the current status, as a state change on the
    // droid would; a self-test that changes `state` calls it.
    push,
    close: () =>
      new Promise((resolve) => {
        clients.forEach((res) => res.end());
        server.close(() => resolve());
        // A browser that is gone may leave its socket half-open; do not let
        // it hold the script's exit.
        server.closeAllConnections();
      }),
  };
};

module.exports = { install };

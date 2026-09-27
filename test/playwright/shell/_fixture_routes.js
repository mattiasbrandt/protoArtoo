// Browser-side stand-ins for the controller routes tools/serve_editor_fixture.py
// does not answer, for the bench-day scripts in test/playwright/shell/ and
// test/playwright/parts/ (#355 section E).
//
// Loaded ONLY when FIXTURE=1. Against the live controller nothing here runs,
// so the bench path is the script and the droid and nothing in between.
//
// Why a module and not a fixture-server edit: the fixture server is shared by
// every other script and deliberately 404s any /api/* route it does not have
// (its header, #261). These answers are shaped for three scripts' needs, so
// they live beside them and ride on Playwright's routing instead.
//
// The status stream is the one route page.route cannot fake faithfully:
// route.fulfill() hands over a finished body, so an EventSource reading it
// sees the stream END, errors and reconnects - a flapping link, which is the
// exact thing status-plate-truth.js measures. So /api/events is continued to a
// small real SSE server started here, which holds the connection open and
// pushes a status event on connect and on every change, the way
// src/web/api_events.cpp does on admission and on requestStatusBroadcastNow().
// Being a real connection, it is also cut by the browser going offline, which
// is how the live stream is broken in status-plate-truth.js.
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
  const state = { estop: false, outputsReads: 0, writes: [] };

  const status = () => ({
    // The six fields data/live_reading.js requires of a frame.
    estop: state.estop,
    sbusHwFailsafe: false,
    sbusSignalLost: false,
    webDriveExpired: false,
    webControlEnabled: false,
    sleepMode: false,
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

  await context.route('**/api/**', async (route) => {
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
    if (method === 'POST' && path === '/api/estop') {
      state.estop = true;
      push();
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
  });

  return {
    state,
    ssePort,
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

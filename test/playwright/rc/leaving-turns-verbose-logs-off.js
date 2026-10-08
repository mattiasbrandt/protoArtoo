// bench-auto: droid
// Leaving RC Control for another surface turns the droid's verbose RC logs
// back off: the page asks for them on arrival (POST /api/rc/debug
// {"enabled":true}) and must ask for {"enabled":false} when the operator
// leaves, not only when the browser tab is closed. Introduced by #360
// (a surface stops what it started when the operator leaves it).
//
// PRECONDITION: none beyond a droid that answers. Writes nothing: the guard
// RECORDS both requests and blocks them, so the droid's log level is never
// touched by this check.
//
// WHAT IT PROVES. RC is opened through the nav, then the Dashboard; within
// 1.5 s of leaving, the page has tried POST /api/rc/debug with a body whose
// "enabled" is false.
//
// WHY A REAL BROWSER. It is the shell's real navigation, and the real absence
// of an unload, that the rule is about.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/rc/leaving-turns-verbose-logs-off.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=silent drops every {"enabled":false} the page would send
// before it leaves the page; the row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/rc';

lib.runCheck({
  rule: 'Leaving RC turns verbose RC logs off',
  artifactDir: ARTIFACTS,
  selftests: ['silent'],
  run: async ({ page, writes, report, selftest }) => {
    if (selftest === 'silent') {
      await page.addInitScript(() => {
        const send = window.fetch.bind(window);
        window.fetch = (target, options) =>
          String(target).includes('/api/rc/debug') && /"enabled"\s*:\s*false/.test(String(options?.body || ''))
            ? Promise.resolve(new Response('{"ok":true}'))
            : send(target, options);
        navigator.sendBeacon = () => true;
      });
    }
    await lib.loadSurface(page, 'home');
    const since = writes.length;
    await lib.openSurface(page, 'rc');
    const arrived = writes.length;
    await lib.openSurface(page, 'home');
    await page.waitForTimeout(1500);
    const tried = writes.slice(since).filter((entry) => entry.path === '/api/rc/debug');
    const off = writes.slice(arrived).filter((entry) => entry.path === '/api/rc/debug' && /"enabled"\s*:\s*false/.test(entry.body));
    report.add('a', 'After leaving RC, the page asks for verbose logs off', lib.verdict(off.length > 0),
      `POST /api/rc/debug tried: ${tried.map((entry) => entry.body).join(', ') || 'none'}; after leaving: ${off.length ? '{"enabled":false}' : 'nothing'}`);
  },
});

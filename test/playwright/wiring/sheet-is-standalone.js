// Wiring's printable sheet is one file that stands alone: named for the droid
// and the minute it was made, stamped with the same minute in every picture,
// opening with no network request and no stylesheet, and carrying its promise
// and its scope along the foot of every picture. Introduced by #366 (one
// generator, two callers: data/wiring.js sheetStamp, sheetFileName,
// wiringSheetFile). The BETA badge #366 once asked for was removed on #411
// (01ac70b9) and is not part of this rule.
//
// PRECONDITION: none beyond a droid that answers. Writes nothing to the droid:
// the sheet is a download saved on this computer, under the artifacts folder.
//
// WHAT IT PROVES.
//   a  The download is named wiring-<droid>-<YYYY-MM-DD-HHMM>.html, <droid>
//      being the droid's name (GET /api/identity droidName) where it is a
//      valid one.
//   b  Every picture in the file, and every picture on screen, is stamped
//      "made YYYY-MM-DD HH:MM" with the minute the file name carries.
//   c  Opened on its own (a fresh browser context, from disk), the file asks
//      for nothing over the network and carries no stylesheet.
//   d  Every picture's foot carries the promise and the scope statement, as
//      window.PAWiring.PROMISE and .SCOPE say them.
//
// WHY A REAL BROWSER. A real download and a real standalone open.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/wiring/sheet-is-standalone.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)   HEADLESS=true   no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js).
// Self-test: SELFTEST=styled makes the saved file carry a stylesheet link and
// lose its promise; c and d must FAIL.
const fs = require('node:fs');
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/wiring';

lib.runCheck({
  rule: 'Wiring\'s printable sheet stands alone',
  artifactDir: ARTIFACTS,
  selftests: ['styled'],
  run: async ({ page, report, browser, selftest }) => {
    await lib.landNeutral(page);
    const droid = (await lib.readJson(page, '/api/identity')).json?.droidName || '';
    await lib.loadSurface(page, 'wiring');
    await page.waitForSelector('#wiring-save[aria-disabled="false"]', { timeout: 15000 });
    const words = await page.evaluate(() => ({ promise: window.PAWiring.PROMISE, scope: window.PAWiring.SCOPE }));
    if (selftest === 'styled') {
      await page.evaluate(() => {
        const Real = window.Blob;
        window.Blob = function Blob(parts, options) {
          const text = options && options.type === 'text/html' && typeof parts[0] === 'string' ? parts[0] : null;
          return new Real(text
            ? [text.replace('</head>', '<link rel="stylesheet" href="/style.css"></head>').replace(/(class="wd-scope wd-promise"[^>]*>)[^<]*/g, '$1selftest')]
            : parts, options);
        };
        window.Blob.prototype = Real.prototype;
      });
    }
    const downloading = page.waitForEvent('download', { timeout: 15000 });
    await page.click('#wiring-save');
    const download = await downloading;
    const name = download.suggestedFilename();
    const saved = `${ARTIFACTS}/${name}`;
    await download.saveAs(saved);
    const onScreen = await page.evaluate(() => [...document.querySelectorAll('#wiring-wires .wd-stamp')].map((node) => node.textContent));

    const named = /^wiring-(.+)-(\d{4}-\d{2}-\d{2})-(\d{2})(\d{2})\.html$/.exec(name);
    const expectedDroid = /^[a-z0-9-]{1,32}$/.test(droid) ? droid : 'droid';
    report.add('a', 'Named wiring-<droid>-<YYYY-MM-DD-HHMM>.html', lib.verdict(Boolean(named) && named[1] === expectedDroid),
      `"${name}"; the droid is "${expectedDroid}"`);
    const minute = named ? `made ${named[2]} ${named[3]}:${named[4]}` : null;

    const alone = await browser.newContext({ viewport: { width: 1440, height: 900 } });
    try {
      const standalone = await alone.newPage();
      const fileUrl = `file://${fs.realpathSync(saved)}`;
      const fetched = [];
      standalone.on('request', (request) => {
        if (request.url() !== fileUrl) fetched.push(request.url());
      });
      await standalone.goto(fileUrl, { waitUntil: 'load' });
      await standalone.waitForTimeout(1000);
      const file = await standalone.evaluate(() => ({
        sheets: document.styleSheets.length,
        links: document.querySelectorAll('link[rel~="stylesheet"]').length,
        pictures: [...document.querySelectorAll('svg.wd')].map((svg) => ({
          stamp: svg.querySelector('.wd-stamp')?.textContent || '',
          promise: svg.querySelector('.wd-promise')?.textContent || '',
          scope: [...svg.querySelectorAll('.wd-scope:not(.wd-promise)')].map((node) => node.textContent),
        })),
      }));
      await standalone.screenshot({ path: `${ARTIFACTS}/sheet-standalone.png`, fullPage: true });
      const stamps = [...file.pictures.map((picture) => picture.stamp), ...onScreen];
      report.add('b', 'Every picture, in the file and on screen, is stamped with that minute',
        lib.verdict(Boolean(minute) && file.pictures.length > 0 && stamps.every((stamp) => stamp.includes(minute))),
        `${file.pictures.length} in the file, ${onScreen.length} on screen: ${[...new Set(stamps)].join(' | ')}`);
      report.add('c', 'Opened alone: no network request, no stylesheet', lib.verdict(fetched.length === 0 && file.sheets === 0 && file.links === 0),
        `requests: ${fetched.join(', ') || 'none'}; ${file.sheets} stylesheet(s), ${file.links} stylesheet link(s)`);
      const footless = file.pictures.filter((picture) => picture.promise !== words.promise || !picture.scope.includes(words.scope));
      report.add('d', 'Every picture carries the promise and the scope along its foot', lib.verdict(file.pictures.length > 0 && footless.length === 0),
        footless.length ? footless.map((picture) => `promise "${picture.promise}", scope [${picture.scope.join(', ')}]`).join('; ') : `"${words.promise}" / "${words.scope}"`);
    } finally {
      await alone.close();
    }
  },
});

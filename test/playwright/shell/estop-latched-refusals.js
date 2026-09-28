// With the estop latched, a control that would move something is refused,
// says why, and sends nothing: Servos' "find by moving", the Parts picture's
// "Open it" on a body Part and on a dome piece, and a panel press on the
// Dashboard's dome. Introduced by #363 (the shell's Ignored Input Notice) and
// #372 (the droid picture's acts).
//
// PRECONDITION: GET /api/status says the estop is LATCHED (NOT ASSESSED
// otherwise). The script never releases it - a release is a deliberate
// operator act on Foot Drive or the Dashboard (ADR 0048). Writes nothing: the
// guard records every write the page tries and blocks it, so "sends nothing"
// below is read off what the page TRIED.
//
// WHAT IT PROVES.
//   a  Servos: "find by moving" is refused (disabled + aria-disabled,
//      data/servo.js gateActs). Three quick presses on it raise the shell's
//      notice once - "That control is switched off right now. The estop is
//      latched." with its route "Open Foot Drive, where that is changed"
//      (data/shell.js showNotice) - and three more after the 1.5 s burst
//      window raise it once more. No POST /api/servo.
//   b  Parts: a fitted body Part, picked, says "Estop latched. Nothing moves
//      until it is cleared." beside a refused "Open it"; pressing it sends no
//      POST /api/servo or /api/dome/cmd (data/parts.js describePick).
//   c  Parts, Dome (Top) face: a dome piece says and does the same.
//   d  Dashboard: a press on a dome panel says the same sentence and sends no
//      POST /api/dome/cmd (data/dome_control.js estopHold).
// A pick on the picture only selects (data/parts.js "THE VIEW NEVER WRITES").
//
// WHY A REAL BROWSER. The notice hangs off a capture-phase pointerdown that
// lands on whatever is UNDER a refused button (data/style.css gives
// .btn:disabled pointer-events: none): real hit testing and real pointer
// events, which mini_dom does not have.
//
// RUN:
//   NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
//     node test/playwright/shell/estop-latched-refusals.js
//   BASE_URL=http://<board>   (default http://10.0.0.22)
//   STEP=1                    wait for Enter after each check
//   HEADLESS=true             no window
// Offline proof: FIXTURE=1 BASE_URL=http://127.0.0.1:<port> HEADLESS=true
// against tools/serve_editor_fixture.py (routes in ../_lib/fixture_routes.js;
// the fixture's droid starts latched).
// Self-test: SELFTEST=ungate enables "find by moving" and "Open it", blanks
// their reasons and makes each press send; every row must FAIL.
const lib = require('../_lib/checks.js');

const ARTIFACTS = 'output/playwright/shell';
const NOTICE = 'That control is switched off right now. The estop is latched.';
const NOTICE_ROUTE = 'Open Foot Drive, where that is changed';
const HELD = 'Estop latched. Nothing moves until it is cleared.';
const MOVES = ['/api/servo', '/api/dome/cmd', '/api/servo/centre'];

lib.runCheck({
  rule: 'With the estop latched, move controls refuse, say why, and send nothing',
  artifactDir: ARTIFACTS,
  fixture: { estop: true },
  selftests: ['ungate'],
  precondition: lib.estopMustBe(true),
  run: async ({ page, writes, report, selftest }) => {
    const ungate = selftest === 'ungate';
    const moves = (since) => writes.slice(since).filter((entry) => MOVES.includes(entry.path));

    // a: find by moving -------------------------------------------------------
    await lib.loadSurface(page, 'servo');
    await page.waitForFunction(() => document.getElementById('shell-estop-state').textContent === 'Estop: latched', null, { timeout: 15000 });
    // The button itself is the refusal: it is what opens the Parts to find,
    // so while it is refused there is no Part to press.
    const find = page.locator('#outputs-find .parts-find');
    if (ungate) {
      await page.evaluate(() => {
        const button = document.querySelector('#outputs-find .parts-find');
        button.disabled = false;
        button.setAttribute('aria-disabled', 'false');
        button.addEventListener('click', () => fetch('/api/servo', { method: 'POST', body: 'arm=selftest&action=nudge' }).catch(() => {}));
      });
    }
    const refused = await find.evaluate((button) => ({ disabled: button.disabled, aria: button.getAttribute('aria-disabled') }));
    await page.evaluate(() => {
      window.__noticeWrites = 0;
      // One record per write of the notice's text; showNotice writes it once.
      new MutationObserver((records) => { window.__noticeWrites += records.length; })
        .observe(document.getElementById('ignored-input-text'), { childList: true, characterData: true, subtree: true });
    });
    const sinceFind = writes.length;
    const burst = async () => {
      const before = await page.evaluate(() => window.__noticeWrites);
      for (let press = 0; press < 3; press += 1) {
        await lib.pressAt(page, find);
        await page.waitForTimeout(150);
      }
      await page.waitForTimeout(300);
      return (await page.evaluate(() => window.__noticeWrites)) - before;
    };
    const first = await burst();
    const notice = await page.evaluate(() => ({
      shown: document.getElementById('ignored-input-notice').checkVisibility({ checkVisibilityCSS: true }),
      text: document.getElementById('ignored-input-text').textContent,
      route: document.getElementById('ignored-input-route').textContent,
    }));
    await page.screenshot({ path: `${ARTIFACTS}/estop-find-by-moving.png` });
    await page.waitForTimeout(2000);
    const second = await burst();
    const reasonsA = [];
    if (!refused.disabled || refused.aria !== 'true') reasonsA.push(`not refused (disabled ${refused.disabled}, aria-disabled ${refused.aria})`);
    if (!notice.shown || notice.text !== NOTICE) reasonsA.push(`the notice ${notice.shown ? `reads "${notice.text}"` : 'is not on screen'}`);
    if (notice.route !== NOTICE_ROUTE) reasonsA.push(`its route reads "${notice.route}"`);
    if (first !== 1 || second !== 1) reasonsA.push(`notices per burst: ${first}, then ${second} (want 1 and 1)`);
    moves(sinceFind).forEach((entry) => reasonsA.push(`tried ${lib.describeWrite(entry)}`));
    report.add('a', 'Servos: find by moving refused, one notice per burst, nothing sent', lib.verdict(reasonsA.length === 0),
      reasonsA.join('; ') || `refused; "${notice.text}" + "${notice.route}"; one notice per burst of three`);
    await lib.step('Find by moving checked.');

    // b, c: the Parts picture --------------------------------------------------
    await lib.openSurface(page, 'parts');
    // Picks Parts until one says the estop holds it: by the Parts list's named
    // buttons where the face has them (a marker can sit under its neighbour),
    // else by the picture's markers.
    const partsAct = async (id, name, face, byList) => {
      const targets = await page.evaluate(({ selector, list }) => {
        const visible = (node) => node.checkVisibility({ checkVisibilityCSS: true });
        const picks = list ? [...document.querySelectorAll('.bv-list-row:not([hidden]) [data-pick-marker]')].filter(visible).map((node) => `[data-pick-marker="${node.dataset.pickMarker}"]`) : [];
        return picks.length ? picks : [...document.querySelectorAll(`${selector} [data-marker]`)].filter(visible).map((node) => `${selector} [data-marker="${node.dataset.marker}"]`);
      }, { selector: face, list: byList });
      let found = null;
      for (const target of targets.slice(0, 40)) {
        if (!(await page.locator(target).first().click({ timeout: 3000 }).then(() => true, () => false))) continue;
        const panel = await page.evaluate(() => ({ why: document.querySelector('.bodyview-panel-why')?.textContent || '', shown: !document.querySelector('[data-act="toggle"]')?.hidden }));
        if (panel.shown && panel.why === HELD) {
          found = target;
          break;
        }
      }
      if (!found) {
        report.add(id, name, targets.length ? lib.FAIL : lib.NOT_ASSESSED, targets.length ? `none of ${Math.min(targets.length, 40)} Parts said "${HELD}"` : 'nothing to pick on that face');
        return;
      }
      if (ungate) {
        await page.evaluate(() => {
          const toggle = document.querySelector('[data-act="toggle"]');
          toggle.disabled = false;
          toggle.setAttribute('aria-disabled', 'false');
          toggle.addEventListener('click', () => fetch('/api/servo', { method: 'POST', body: 'arm=selftest&action=open' }).catch(() => {}), { once: true });
          document.querySelector('.bodyview-panel-why').textContent = '';
        });
      }
      const toggle = page.locator('[data-act="toggle"]');
      const state = await toggle.evaluate((button) => ({ disabled: button.disabled, label: button.textContent }));
      const since = writes.length;
      await lib.pressAt(page, toggle);
      const why = await page.evaluate(() => document.querySelector('.bodyview-panel-why')?.textContent || '');
      await page.waitForTimeout(1500);
      const reasons = [];
      if (why !== HELD) reasons.push(`the reason reads "${why}"`);
      if (!state.disabled) reasons.push(`"${state.label}" is not refused`);
      moves(since).forEach((entry) => reasons.push(`tried ${lib.describeWrite(entry)}`));
      report.add(id, name, lib.verdict(reasons.length === 0), reasons.join('; ') || `${found}: "${why}"; "${state.label}" refused, pressed, nothing sent`);
    };
    await partsAct('b', 'Parts: a body Part says the estop holds it, and sends nothing', '.bv-face:not([hidden])', true);
    await page.screenshot({ path: `${ARTIFACTS}/estop-parts-body.png` });
    const domeTab = page.locator('[data-face-tab="dome"]');
    if ((await domeTab.count()) > 0) {
      await domeTab.click();
      await page.waitForTimeout(500);
      await partsAct('c', 'Parts: a dome piece says the estop holds it, and sends nothing', '.bv-face[data-face="dome"]', false);
      await page.screenshot({ path: `${ARTIFACTS}/estop-parts-dome.png` });
    } else {
      report.add('c', 'Parts: a dome piece says the estop holds it, and sends nothing', lib.NOT_ASSESSED, 'the picture has no Dome (Top) face');
    }
    await lib.step('Parts checked.');

    // d: the Dashboard's dome ---------------------------------------------------
    await lib.openSurface(page, 'home');
    await page.click('#dome-control-header');
    const panel = '.dome-svg-container svg [data-element-id][data-selectable="true"], .dome-svg-container svg [data-target]';
    if (!(await page.waitForSelector(panel, { timeout: 10000 }).then(() => true, () => false))) {
      const body = (await page.textContent('#dome-control-body').catch(() => '')).trim();
      report.add('d', 'Dashboard dome: a panel press says the estop holds it, and sends nothing', lib.NOT_ASSESSED, `no pressable panel in the dome drawing (${body.slice(0, 120)})`);
      return;
    }
    if (ungate) {
      await page.evaluate(() => document.querySelector('.dome-svg-container svg').addEventListener('click', () => {
        fetch('/api/dome/cmd', { method: 'POST', body: 'cmd=selftest' }).catch(() => {});
      }, { once: true }));
    }
    const since = writes.length;
    await page.locator(panel).first().click();
    await page.waitForTimeout(1000);
    if (ungate) await page.evaluate(() => { document.querySelector('.dome-control-feedback').textContent = ''; });
    const said = (await page.textContent('.dome-control-feedback')).trim();
    const tried = moves(since);
    await page.screenshot({ path: `${ARTIFACTS}/estop-dashboard-dome.png` });
    report.add('d', 'Dashboard dome: a panel press says the estop holds it, and sends nothing', lib.verdict(said === HELD && tried.length === 0),
      `"${said}"${tried.length ? `; tried ${tried.map(lib.describeWrite).join(' | ')}` : '; nothing sent'}`);
    console.log(`\nThe estop line reads "${await page.textContent('#shell-estop-state')}". Nothing was released: clear it on Foot Drive or the Dashboard when you are ready.`);
  },
});

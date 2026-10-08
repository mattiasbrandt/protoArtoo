#!/usr/bin/env node
// Recaptures the README Showcase: a still and a scroll of ten pages, written
// over the twenty files under docs/images/readme/ (#480; the first set, and
// the look this repeats, is #476).
//
//   npm ci && npx playwright install chromium
//   node tools/readme_showcase.mjs
//
// Needs python3 (the fixture server) and an ffmpeg with libx264 and libwebp.
//
// What it does, in order: serves data/ through tools/serve_editor_fixture.py on
// a free port, answers the controller routes with
// test/playwright/_lib/fixture_routes.js plus an overlay that turns the
// fixture's Artoo droid into a FireBeetle 2 one, checks that one scroll step
// moves the picture exactly one pixel, films every page into one 60 fps h264
// master at one pixel per frame, cuts each page out of the master as a WebP
// scroll and a PNG still, checks every file, and only then replaces the twenty
// files. Any failure exits non-zero and leaves docs/images/readme/ as it was.
//
// It writes those twenty paths and nothing else: no versioned copy, no second
// directory (operator decision on #480, 2026-10-06). It never edits the shared
// fixture, README.md or data/.
//
// The droid name `preview`, the `FW: fixture` footer and the fixture's canned
// console log are what the approved pictures show, and stay (operator, #480,
// 2026-10-08).

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { chromium } from 'playwright';

import { install } from '../test/playwright/_lib/fixture_routes.js';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const OUT_DIR = path.join(REPO, 'docs', 'images', 'readme');
const BOARD = 'firebeetle2';

// Shell surface id -> file slug, in filming order.
const PAGES = [
  { surface: 'home', slug: 'dashboard' },
  { surface: 'configuration', slug: 'configuration' },
  { surface: 'wiring', slug: 'wiring' },
  { surface: 'parts', slug: 'parts' },
  { surface: 'lights', slug: 'lights' },
  { surface: 'servo', slug: 'servos' },
  { surface: 'drive', slug: 'foot-drive' },
  { surface: 'dome', slug: 'dome' },
  { surface: 'sound', slug: 'sound' },
  { surface: 'seq', slug: 'sequences' },
];

// The approved motion (#476): 60 fps, one pixel per frame, so 60 px/s.
const VIEWPORT = { width: 1440, height: 900 };
const FPS = 60;
const HOLD_TOP_FRAMES = 72; // 1.2 s on the top of the page
const HOLD_BOTTOM_FRAMES = 54; // 0.9 s on the bottom
const SHOT = { type: 'jpeg', quality: 95, animations: 'allow', caret: 'hide' };

// The one-pixel check: 24 steps from the top, at least 22 of them a shift of
// exactly one pixel, measured on these columns between these rows.
const CHECK_STEPS = 24;
const CHECK_MIN_ONE_PX = 22;
const CHECK_COLUMNS = [420, 640, 860, 1100];
const CHECK_ROW_FIRST = 220;
const CHECK_ROW_LAST = 699;
const CHECK_MAX_SHIFT = 3;

// What a published file is.
const PUBLISH = { width: 720, height: 450, webpFps: 12, webpQuality: 52, webpCompression: 6 };
// A file over 40 MB fails the run and replaces nothing (operator, #480,
// 2026-10-08): under GitHub's 50 MB warning. Decimal megabytes, the stricter
// reading.
const MAX_FILE_BYTES = 40 * 1000 * 1000;

const SETTLE_IMAGES_MS = 2500;
const SETTLE_AFTER_MS = 300;

const fail = (message) => {
  throw new Error(message);
};

const readRepo = (relative) => fs.readFileSync(path.join(REPO, relative), 'utf8');

// ── firmware facts for the overlay ──────────────────────────────────────────
//
// Every value the overlay puts in front of the page is read from the header
// the firmware builds it from, for firebeetle2, so a release never carries a
// hand-copied pin list. A value that is not there fails the run.

// The lines of `source` that sit under a preprocessor branch whose condition
// `selects`, at any depth. Nested branches inside a selected one are kept
// whole; a name defined twice with different values is caught by the caller.
const selectedLines = (source, selects) => {
  const stack = [];
  const kept = [];
  for (const line of source.split('\n')) {
    const directive = /^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$/.exec(line);
    if (directive) {
      const [, word, rest] = directive;
      if (word === 'if' || word === 'ifdef' || word === 'ifndef') {
        stack.push(word === 'if' && selects(rest));
      } else if (word === 'elif') {
        if (stack.length === 0) fail('unbalanced #elif');
        stack[stack.length - 1] = selects(rest);
      } else if (word === 'else') {
        if (stack.length === 0) fail('unbalanced #else');
        stack[stack.length - 1] = false;
      } else {
        if (stack.length === 0) fail('unbalanced #endif');
        stack.pop();
      }
      continue;
    }
    if (stack.includes(true)) kept.push(line);
  }
  return kept.join('\n');
};

// name -> value for every `pattern` match in `text`; a name with two values fails.
const collect = (text, pattern, file) => {
  const values = new Map();
  for (const match of text.matchAll(pattern)) {
    const [, name, value] = match;
    if (values.has(name) && values.get(name) !== value) {
      fail(`${file}: ${name} has two values for ${BOARD} (${values.get(name)}, ${value})`);
    }
    values.set(name, value);
  }
  return values;
};

const need = (values, name, file) => {
  if (!values.has(name)) fail(`${file}: no value for ${name} on ${BOARD}`);
  return values.get(name);
};

const boardBranch = (condition) => /PA_BOARD\s*==\s*PA_BOARD_FIREBEETLE2\b/.test(condition);

const readFirmwareFacts = () => {
  // include/config.h: the capability gates, the UART ports and pins, and the
  // chip target, each from this board's branches only. The artoo_esp32
  // branches define the same names.
  const configText = selectedLines(readRepo('include/config.h'), boardBranch);
  const defines = collect(configText, /^\s*#\s*define\s+(\w+)\s+(\d+)\b/gm, 'include/config.h');
  const constants = collect(configText, /^\s*constexpr\s+uint8_t\s+(\w+)\s*=\s*(\d+)\s*;/gm, 'include/config.h');
  const chipTargets = [...defines.keys()].filter((name) => name.startsWith('PA_CHIP_TARGET_'));
  if (chipTargets.length !== 1) fail(`include/config.h: ${BOARD} maps to ${chipTargets.length} chip targets`);
  const chipTarget = chipTargets[0];

  // GET /api/identity's board_capabilities, in the manifest's order.
  const capNames = [...readRepo('include/board_capabilities.inc').matchAll(/^PA_BOARD_CAPABILITY\((\w+)\)/gm)].map((m) => m[1]);
  if (capNames.length === 0) fail('include/board_capabilities.inc: no capabilities read');
  const capabilities = Object.fromEntries(capNames.map((name) => {
    const value = need(defines, name, 'include/config.h');
    if (value !== '0' && value !== '1') fail(`include/config.h: ${name} is ${value}, not 0 or 1`);
    return [name, value === '1'];
  }));

  // board_lanes: the manifest names each lane's port and pin symbols, the wire
  // header its baud and protocol where the lane owns them.
  const wires = new Map();
  for (const match of readRepo('include/board_lane_wire.h').matchAll(/kBoardLaneWire_(\w+)\s*=\s*\{\s*(\d+)\s*,\s*(nullptr|"[^"]*")\s*\}/g)) {
    wires.set(match[1], match[3] === 'nullptr' ? null : { baud: Number(match[2]), protocol: match[3].slice(1, -1) });
  }
  const lanes = {};
  for (const match of readRepo('include/board_lanes.inc').matchAll(/^PA_BOARD_LANE\((\w+),\s*(\w+),\s*(\w+),\s*(\w+)\)/gm)) {
    const [, name, port, tx, rx] = match;
    if (!wires.has(name)) fail(`include/board_lane_wire.h: no kBoardLaneWire_${name}`);
    lanes[name] = {
      uart: Number(need(constants, port, 'include/config.h')),
      tx: Number(need(constants, tx, 'include/config.h')),
      rx: Number(need(constants, rx, 'include/config.h')),
      ...(wires.get(name) || {}),
    };
  }
  if (Object.keys(lanes).length === 0) fail('include/board_lanes.inc: no lanes read');

  // The Learned Sequence caps: the store cap is a board fact, the per-file cap
  // a chip-target one (`#if defined(PA_CHIP_TARGET_...)`).
  const seqSource = readRepo('include/seq_store_util.h');
  const seqBoard = collect(selectedLines(seqSource, boardBranch), /^\s*#\s*define\s+(\w+)\s+(\d+)\b/gm, 'include/seq_store_util.h');
  const chipBranch = (condition) => new RegExp(`defined\\(\\s*${chipTarget}\\s*\\)`).test(condition);
  const seqChip = collect(selectedLines(seqSource, chipBranch), /^\s*#\s*define\s+(\w+)\s+(\d+)\b/gm, 'include/seq_store_util.h');
  const learnedCap = Number(need(seqBoard, 'PA_SEQ_STORE_CAP', 'include/seq_store_util.h'));
  const learnedMaxBytes = Number(need(seqChip, 'PA_SEQ_FILE_MAX_KB', 'include/seq_store_util.h')) * 1024;

  // Build feature flags: a release image builds with every one off.
  const buildFlags = Object.fromEntries(
    [...readRepo('include/build_flags.inc').matchAll(/^PA_BUILD_FLAG\((\w+)\)/gm)].map((m) => [m[1], false]),
  );

  // Board Component Labels: an Output's label is its name on screen.
  const labels = new Map();
  const labelRe = new RegExp(`^PA_COMPONENT_LABEL\\(${BOARD},\\s*(\\w+),\\s*"([^"]*)"\\)`, 'gm');
  for (const match of readRepo('include/component_labels.inc').matchAll(labelRe)) labels.set(match[1], match[2]);

  return { capabilities, lanes, learnedCap, learnedMaxBytes, buildFlags, labels };
};

// GET /api/identity/components for this board. The parse is the shared
// fixture's readRegistry() (test/playwright/_lib/fixture_routes.js), which that
// module does not export and which this script may not edit, so it is repeated
// here; only the `included` rule differs. That rule follows the firebeetle2
// build: the board test is true for FIREBEETLE2 and false for ARTOO_ESP32, a
// lone capability gate takes the value config.h gives it on this board, a
// literal stays. Anything else fails the run rather than being guessed.
const readRegistry = (capabilities) => {
  const source = readRepo('include/component_registry.inc');
  const categories = [];
  const categoryId = new Map();
  for (const match of source.matchAll(/^PA_COMPONENT_CATEGORY\((\w+),\s*"([^"]+)",\s*"([^"]+)",\s*(nullptr|"[^"]+")\)/gm)) {
    categoryId.set(match[1], match[2]);
    categories.push({ id: match[2], name: match[3], member_key: match[4] === 'nullptr' ? null : match[4].slice(1, -1) });
  }
  const parts = [];
  const partRe = /^PA_COMPONENT_PART\(\s*(\d+),\s*"([^"]+)",\s*"([^"]+)",\s*(\w+),\s*"([^"]+)",\s*COMPONENT_STATUS_(\w+),([\s\S]*?)\)\s*$/gm;
  for (const match of source.matchAll(partRe)) {
    const tail = match[7];
    const includedExpr = tail.slice(tail.lastIndexOf(',') + 1).trim();
    let included;
    if (includedExpr === '1') included = true;
    else if (includedExpr === '0') included = false;
    else if (includedExpr.includes('PA_BOARD_FIREBEETLE2')) included = true;
    else if (includedExpr.includes('PA_BOARD_ARTOO_ESP32')) included = false;
    else if (/^PA_CAP_\w+$/.test(includedExpr) && includedExpr in capabilities) included = capabilities[includedExpr];
    else fail(`include/component_registry.inc: cannot read "included" for ${match[2]} on ${BOARD}: ${includedExpr}`);
    const confirmedWord = tail.slice(0, tail.indexOf(',')).trim();
    if (!['COMPONENT_CONFIRMED_ON_DROID', 'COMPONENT_NOT_CONFIRMED_ON_DROID'].includes(confirmedWord)) {
      fail(`include/component_registry.inc: cannot read "confirmed_on_droid" for ${match[2]}: ${confirmedWord}`);
    }
    const gate = /"(PA_CAP_\w+)"/.exec(tail);
    parts.push({
      id: match[2],
      value: Number(match[1]),
      name: match[3],
      category: categoryId.get(match[4]),
      protocol: match[5],
      status: match[6].toLowerCase(),
      confirmed_on_droid: confirmedWord === 'COMPONENT_CONFIRMED_ON_DROID',
      capabilities: 0,
      included,
      board_capability: gate ? gate[1] : null,
    });
  }
  if (parts.length < 20) fail(`include/component_registry.inc: read only ${parts.length} parts`);
  categories.forEach((category) => {
    category.selectable = parts.filter((part) => part.category === category.id && part.status === 'supported' && part.included).length;
    if (category.selectable <= 1) category.member_key = null;
    category.active_member = null;
  });
  const sound = categories.find((category) => category.id === 'sound');
  if (!sound) fail('include/component_registry.inc: no sound category');
  sound.active_member = 'dy_sv5w';
  return { categories, parts };
};

// The fixture's Output ids and `config.components` keys, each joined to the
// component_labels.inc entry that names it.
const OUTPUT_LABEL = { arm1: 'enable_arm1', arm2: 'enable_arm2', aux1: 'enable_aux1', aux2: 'enable_aux2', aux3: 'enable_aux3' };
const COMPONENT_LABEL = {
  drive: 'enable_drive',
  audio: 'enable_audio',
  domeEsc: 'enable_dome_esc',
  protoR2link: 'enable_protor2link',
  rcCh1: 'enable_rc_ch1',
  rcCh2: 'enable_rc_ch2',
  rcCh3: 'enable_rc_ch3',
  rcCh4: 'enable_rc_ch4',
  rcCh5: 'enable_rc_ch5',
  rcCh6: 'enable_rc_ch6',
};

// The overlay: renames the fixture droid's Outputs and components in the state
// its routes already answer from, and answers the two identity routes as a
// FireBeetle 2. Fitted parts stay the fixture's. Registered after install(),
// so Playwright tries these handlers first.
const applyOverlay = async (context, droid, facts) => {
  const label = (key) => {
    if (!facts.labels.has(key)) fail(`include/component_labels.inc: no ${BOARD} label for ${key}`);
    return facts.labels.get(key);
  };
  droid.state.outputs.forEach((row) => {
    if (!(row.id in OUTPUT_LABEL)) fail(`fixture Output ${row.id} has no component label entry here`);
    row.name = label(OUTPUT_LABEL[row.id]);
  });
  Object.entries(droid.state.config.components).forEach(([key, component]) => {
    if (!(key in COMPONENT_LABEL)) fail(`fixture component ${key} has no component label entry here`);
    component.label = label(COMPONENT_LABEL[key]);
  });

  const identity = {
    droidName: 'preview',
    mdnsUseName: true,
    board: BOARD,
    learned_sequence_cap: facts.learnedCap,
    learned_sequence_max_bytes: facts.learnedMaxBytes,
    board_capabilities: facts.capabilities,
    board_lanes: facts.lanes,
    build_flags: facts.buildFlags,
  };
  const registry = readRegistry(facts.capabilities);
  const answers = { '/api/identity': identity, '/api/identity/components': registry };
  await context.route(
    (url) => url.pathname in answers,
    (route) => {
      if (route.request().method() !== 'GET') return route.fallback();
      const body = answers[new URL(route.request().url()).pathname];
      return route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) });
    },
  );
};

// ── processes ───────────────────────────────────────────────────────────────

const freePort = () =>
  new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
  });

const requireEncoders = () => {
  const result = spawnSync('ffmpeg', ['-hide_banner', '-encoders'], { encoding: 'utf8' });
  if (result.error) fail(`ffmpeg not runnable: ${result.error.message}`);
  if (result.status !== 0) fail(`ffmpeg -encoders exited ${result.status}`);
  for (const encoder of ['libx264', 'libwebp']) {
    if (!new RegExp(`^\\s*\\S+\\s+${encoder}\\s`, 'm').test(result.stdout)) fail(`ffmpeg has no ${encoder} encoder`);
  }
};

// Starts the fixture server and resolves once it answers. The server has no
// flags; the port is PA_FIXTURE_PORT. It logs every request to stderr, so only
// the tail is kept, for the failure message.
const startFixtureServer = async (port) => {
  const child = spawn('python3', [path.join(REPO, 'tools', 'serve_editor_fixture.py')], {
    cwd: REPO,
    env: { ...process.env, PA_FIXTURE_PORT: String(port) },
    stdio: ['ignore', 'ignore', 'pipe'],
  });
  const tail = [];
  child.stderr.on('data', (chunk) => {
    tail.push(...String(chunk).split('\n').filter(Boolean));
    tail.splice(0, Math.max(0, tail.length - 20));
  });
  let exited = null;
  child.on('exit', (code, signal) => {
    exited = signal || code;
  });
  const deadline = Date.now() + 15000;
  for (;;) {
    if (exited !== null) fail(`fixture server exited (${exited}):\n${tail.join('\n')}`);
    try {
      const response = await fetch(`http://127.0.0.1:${port}/dashboard.html`);
      if (response.ok) return child;
    } catch (_notListeningYet) {
      // Connection refused until the server binds; the deadline bounds it.
    }
    if (Date.now() > deadline) {
      child.kill('SIGTERM');
      fail(`fixture server did not answer on ${port} within 15 s:\n${tail.join('\n')}`);
    }
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
};

const stopProcess = (child) =>
  new Promise((resolve) => {
    if (!child || child.exitCode !== null || child.signalCode !== null) return resolve();
    child.once('exit', () => resolve());
    child.kill('SIGTERM');
  });

// Runs ffmpeg to completion; a non-zero exit fails with its stderr tail.
const ffmpeg = (args, input) => {
  const result = spawnSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', ...args], {
    input,
    maxBuffer: 1 << 30,
  });
  if (result.error) fail(`ffmpeg: ${result.error.message}`);
  if (result.status !== 0) fail(`ffmpeg ${args.join(' ')} exited ${result.status}:\n${String(result.stderr).slice(-2000)}`);
  return result.stdout;
};

// ── the browser ─────────────────────────────────────────────────────────────

const STILL_CSS = `
  *, *::before, *::after { animation: none !important; transition: none !important; }
  html { scroll-behavior: auto !important; overflow-anchor: none !important; }
  * { overflow-anchor: none !important; }
  body { -webkit-font-smoothing: antialiased; text-rendering: optimizeLegibility; }
`;

// Scrolls the window to `y` and waits two frames, so the shot that follows is
// of the painted position. Chrome snaps scrollTop to whole pixels; a position
// that does not land exactly is jitter in the clip, so it fails.
const scrollTo = async (page, y) => {
  const landed = await page.evaluate(async (target) => {
    const scroller = document.scrollingElement;
    scroller.scrollTop = target;
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    return scroller.scrollTop;
  }, y);
  if (Math.round(landed) !== y) fail(`scrollTop ${landed} after asking for ${y}`);
};

const maxScroll = (page) =>
  page.evaluate(() => {
    const scroller = document.scrollingElement;
    return Math.max(0, Math.round(scroller.scrollHeight - scroller.clientHeight));
  });

// Waits for a page's markup, then its images (bounded), then a short beat.
// body.dataset.page flips before the markup arrives, so it is not the signal.
const settle = async (page, surface) => {
  if (surface === 'home') {
    await page.waitForSelector('.moving-parts-body', { state: 'attached' });
  } else {
    await page.waitForFunction((name) => {
      const content = document.querySelector(`#shell-content [data-surface="${name}"]`);
      return Boolean(content && content.firstElementChild && content.getBoundingClientRect().height > 80);
    }, surface);
  }
  await page.evaluate(
    (timeout) =>
      Promise.race([
        Promise.all(
          [...document.images].map((img) => (img.complete ? null : new Promise((resolve) => {
            img.addEventListener('load', resolve, { once: true });
            img.addEventListener('error', resolve, { once: true });
          }))),
        ),
        new Promise((resolve) => setTimeout(resolve, timeout)),
      ]),
    SETTLE_IMAGES_MS,
  );
  await page.waitForTimeout(SETTLE_AFTER_MS);
};

// The shell's nav scrolls with the page, so a link below the fold fails a
// visibility wait: back to the top, bring the link into view, click. Sound and
// Dome sit in more than one nav group, hence the first.
const openSurface = async (page, surface) => {
  await scrollTo(page, 0);
  const link = page.locator(`a[data-surface-link="${surface}"]`).first();
  await link.scrollIntoViewIfNeeded();
  await link.click();
  await settle(page, surface);
  await scrollTo(page, 0);
};

// Decodes JPEG shots to 8-bit grey frames, one Buffer per shot.
const toGrey = (jpegs) => {
  const raw = ffmpeg(['-f', 'image2pipe', '-c:v', 'mjpeg', '-i', '-', '-f', 'rawvideo', '-pix_fmt', 'gray', '-'], Buffer.concat(jpegs));
  const size = VIEWPORT.width * VIEWPORT.height;
  if (raw.length !== size * jpegs.length) fail(`decoded ${raw.length} bytes for ${jpegs.length} shots of ${size}`);
  return jpegs.map((_, i) => raw.subarray(i * size, (i + 1) * size));
};

// How far the picture moved up between two frames: the shift in 0..3 whose
// rows line up best on the sampled columns.
const verticalShift = (before, after) => {
  let best = { shift: 0, diff: Infinity };
  for (let shift = 0; shift <= CHECK_MAX_SHIFT; shift += 1) {
    let diff = 0;
    for (let y = CHECK_ROW_FIRST; y + shift <= CHECK_ROW_LAST; y += 1) {
      for (const x of CHECK_COLUMNS) {
        diff += Math.abs(after[y * VIEWPORT.width + x] - before[(y + shift) * VIEWPORT.width + x]);
      }
    }
    const mean = diff / ((CHECK_ROW_LAST - CHECK_ROW_FIRST + 1 - shift) * CHECK_COLUMNS.length);
    if (mean < best.diff) best = { shift, diff: mean };
  }
  return best.shift;
};

// One scroll step must move the picture exactly one pixel, or every clip
// judders. Run from the top of the first page, before anything is filmed.
const onePixelCheck = async (page) => {
  if ((await maxScroll(page)) < CHECK_STEPS) fail(`the first page scrolls less than ${CHECK_STEPS} px; nothing to check against`);
  const shots = [];
  for (let y = 0; y <= CHECK_STEPS; y += 1) {
    await scrollTo(page, y);
    shots.push(await page.screenshot(SHOT));
  }
  const grey = toGrey(shots);
  const shifts = grey.slice(1).map((frame, i) => verticalShift(grey[i], frame));
  const onePx = shifts.filter((shift) => shift === 1).length;
  console.log(`one-pixel check: ${onePx}/${CHECK_STEPS} steps moved 1 px (shifts ${shifts.join(' ')})`);
  if (onePx < CHECK_MIN_ONE_PX) fail(`one-pixel check: ${onePx} of ${CHECK_STEPS} steps moved 1 px, need ${CHECK_MIN_ONE_PX}`);
  await scrollTo(page, 0);
};

// The h264 master every published file is cut from. Frames go in as JPEGs.
const openMaster = (file) => {
  const child = spawn('ffmpeg', [
    '-hide_banner', '-loglevel', 'error', '-y',
    '-f', 'image2pipe', '-c:v', 'mjpeg', '-framerate', String(FPS), '-i', '-',
    '-c:v', 'libx264', '-crf', '16', '-preset', 'fast', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
    file,
  ], { stdio: ['pipe', 'ignore', 'pipe'] });
  let stderr = '';
  child.stderr.on('data', (chunk) => {
    stderr = (stderr + chunk).slice(-4000);
  });
  const done = new Promise((resolve, reject) => {
    child.on('error', reject);
    child.on('exit', (code, signal) =>
      (code === 0 ? resolve() : reject(new Error(`master encoder exited ${signal || code}:\n${stderr}`))));
  });
  // write() and close() await `done` and report its failure; this only keeps
  // a failure nobody is awaiting yet (the encoder killed in cleanup) from
  // being an unhandled rejection.
  done.catch(() => {});
  // A write after the encoder died: kept, and reported by the next write().
  let pipeError = null;
  child.stdin.on('error', (error) => {
    pipeError = error;
  });
  let frames = 0;
  const write = async (jpeg, times = 1) => {
    for (let i = 0; i < times; i += 1) {
      if (pipeError || child.exitCode !== null || child.signalCode !== null) {
        await done;
        fail(`master encoder stopped taking frames${pipeError ? `: ${pipeError.message}` : ''}`);
      }
      if (!child.stdin.write(jpeg)) {
        await Promise.race([new Promise((resolve) => child.stdin.once('drain', resolve)), done]);
      }
      frames += 1;
    }
  };
  const close = async () => {
    child.stdin.end();
    await done;
  };
  return { write, close, frames: () => frames, kill: () => child.kill('SIGKILL') };
};

// Films one page: the top held, one pixel per frame to the bottom, the bottom held.
const filmPage = async (page, master) => {
  const start = master.frames();
  const bottom = await maxScroll(page);
  let shot = await page.screenshot(SHOT);
  await master.write(shot, HOLD_TOP_FRAMES);
  for (let y = 1; y <= bottom; y += 1) {
    await scrollTo(page, y);
    shot = await page.screenshot(SHOT);
    await master.write(shot);
  }
  await master.write(shot, HOLD_BOTTOM_FRAMES);
  return { start, frames: master.frames() - start, scrolled: bottom };
};

// ── encode and check ────────────────────────────────────────────────────────

// Cuts one page out of the master. Seeking before -i is accurate on the h264
// master (ffmpeg decodes from the previous keyframe); never seek a WebP.
const encodePage = (master, cut, workDir, slug) => {
  const seek = ['-ss', (cut.start / FPS).toFixed(6)];
  const scale = `scale=${PUBLISH.width}:${PUBLISH.height}`;
  const webp = path.join(workDir, `${slug}.webp`);
  const png = path.join(workDir, `${slug}.png`);
  ffmpeg([
    ...seek, '-t', (cut.frames / FPS).toFixed(6), '-i', master,
    '-an', '-vf', `fps=${PUBLISH.webpFps},${scale}`, '-fps_mode', 'vfr',
    '-c:v', 'libwebp', '-quality', String(PUBLISH.webpQuality), '-compression_level', String(PUBLISH.webpCompression),
    '-loop', '0',
    webp,
  ]);
  // The first frame of the page is the hold on its top.
  ffmpeg([...seek, '-i', master, '-frames:v', '1', '-vf', scale, '-update', '1', png]);
  return [png, webp];
};

// Reads a PNG's IHDR size.
const pngInfo = (bytes) => {
  if (bytes.length < 24 || bytes.toString('latin1', 1, 4) !== 'PNG' || bytes.toString('latin1', 12, 16) !== 'IHDR') return null;
  return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
};

// Walks a WebP's RIFF chunks: the VP8X canvas, the ANIM loop count, the frames.
const webpInfo = (bytes) => {
  if (bytes.length < 12 || bytes.toString('latin1', 0, 4) !== 'RIFF' || bytes.toString('latin1', 8, 12) !== 'WEBP') return null;
  const info = { width: null, height: null, loop: null, frames: 0 };
  for (let at = 12; at + 8 <= bytes.length;) {
    const fourcc = bytes.toString('latin1', at, at + 4);
    const size = bytes.readUInt32LE(at + 4);
    const body = at + 8;
    if (fourcc === 'VP8X') {
      info.width = bytes.readUIntLE(body + 4, 3) + 1;
      info.height = bytes.readUIntLE(body + 7, 3) + 1;
    } else if (fourcc === 'ANIM') {
      info.loop = bytes.readUInt16LE(body + 4);
    } else if (fourcc === 'ANMF') {
      info.frames += 1;
    }
    at = body + size + (size % 2);
  }
  return info;
};

// Every file 720x450, not empty, under the cap; every WebP animated and
// looping forever. Returns one problem per line, empty when all pass.
const checkFiles = (files) => {
  const problems = [];
  for (const file of files) {
    const name = path.basename(file);
    const bytes = fs.readFileSync(file);
    if (bytes.length === 0) problems.push(`${name}: empty`);
    if (bytes.length > MAX_FILE_BYTES) problems.push(`${name}: ${bytes.length} bytes, over the ${MAX_FILE_BYTES} byte cap`);
    const info = name.endsWith('.png') ? pngInfo(bytes) : webpInfo(bytes);
    if (!info) {
      problems.push(`${name}: not a readable ${path.extname(name).slice(1)}`);
      continue;
    }
    if (info.width !== PUBLISH.width || info.height !== PUBLISH.height) {
      problems.push(`${name}: ${info.width}x${info.height}, not ${PUBLISH.width}x${PUBLISH.height}`);
    }
    if (name.endsWith('.webp')) {
      if (info.loop !== 0) problems.push(`${name}: ANIM loop count ${info.loop}, not 0`);
      if (info.frames < 2) problems.push(`${name}: ${info.frames} animation frames`);
    }
  }
  return problems;
};

// Moves the checked files over the published ones: each lands as a temporary
// name beside its target first, so a copy that fails part-way renames nothing
// and takes its temporaries with it.
const replacePublished = (files) => {
  const staged = [];
  try {
    for (const file of files) {
      const target = path.join(OUT_DIR, path.basename(file));
      const temporary = `${target}.showcase-tmp`;
      staged.push({ temporary, target });
      fs.copyFileSync(file, temporary);
    }
  } catch (error) {
    staged.forEach(({ temporary }) => fs.rmSync(temporary, { force: true }));
    throw error;
  }
  staged.forEach(({ temporary, target }) => fs.renameSync(temporary, target));
};

// ── main ────────────────────────────────────────────────────────────────────

const main = async () => {
  requireEncoders();
  const facts = readFirmwareFacts();
  // Fail on the registry before any browser starts.
  readRegistry(facts.capabilities);

  const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'readme-showcase-'));
  let server = null;
  let browser = null;
  let droid = null;
  let master = null;
  try {
    const port = await freePort();
    server = await startFixtureServer(port);
    browser = await chromium.launch({
      headless: true,
      args: ['--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows'],
    });
    const context = await browser.newContext({ viewport: VIEWPORT, deviceScaleFactor: 1 });
    droid = await install(context, { droid: 'artoo' });
    await applyOverlay(context, droid, facts);

    const page = await context.newPage();
    await page.goto(`http://127.0.0.1:${port}/dashboard.html`);
    await page.waitForSelector('#shell-estop-button');
    await page.addStyleTag({ content: STILL_CSS });
    await settle(page, 'home');
    await scrollTo(page, 0);

    await onePixelCheck(page);

    const masterFile = path.join(workDir, 'master.mp4');
    master = openMaster(masterFile);
    const cuts = [];
    for (const { surface, slug } of PAGES) {
      if (surface !== 'home') await openSurface(page, surface);
      const started = Date.now();
      const cut = await filmPage(page, master);
      cuts.push({ slug, ...cut });
      console.log(`filmed ${slug}: ${cut.scrolled} px, ${(cut.frames / FPS).toFixed(1)} s footage, ${((Date.now() - started) / 1000).toFixed(0)} s wall`);
    }
    await master.close();
    master = null;
    const total = cuts.reduce((sum, cut) => sum + cut.frames, 0);
    console.log(`master: ${total} frames, ${(total / FPS).toFixed(3)} s`);

    const files = cuts.flatMap((cut) => encodePage(masterFile, cut, workDir, cut.slug));
    const problems = checkFiles(files);
    if (problems.length) fail(`checks failed, nothing replaced:\n  ${problems.join('\n  ')}`);

    replacePublished(files);
    console.log('replaced docs/images/readme/:');
    let sum = 0;
    for (const file of files) {
      const bytes = fs.statSync(file).size;
      sum += bytes;
      console.log(`  ${path.basename(file).padEnd(20)} ${String(bytes).padStart(10)}`);
    }
    console.log(`  ${'total'.padEnd(20)} ${String(sum).padStart(10)}`);
  } finally {
    if (master) master.kill();
    if (browser) await browser.close();
    if (droid) await droid.close();
    await stopProcess(server);
    fs.rmSync(workDir, { recursive: true, force: true });
  }
};

main().catch((error) => {
  console.error(`readme_showcase: ${error.message}`);
  process.exit(1);
});

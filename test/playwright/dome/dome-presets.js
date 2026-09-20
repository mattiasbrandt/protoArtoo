const { chromium } = require('playwright');

const TARGET_URL = process.env.TARGET_URL || 'http://127.0.0.1:4173/dome.html';
const HEADLESS = process.env.HEADLESS === 'true';

(async () => {
  const browser = await chromium.launch({ headless: HEADLESS, slowMo: HEADLESS ? 0 : 40 });
  const page = await browser.newPage({ viewport: { width: 1560, height: 920 } });

  await page.addInitScript(() => {
    class MockEventSource {
      constructor(url) {
        this.url = url;
        this.listeners = new Map();
        this.onerror = null;
        window.__mockEventSource = this;
      }

      addEventListener(type, handler) {
        if (!this.listeners.has(type)) this.listeners.set(type, []);
        this.listeners.get(type).push(handler);
      }

      close() {}

      emit(type, payload) {
        const handlers = this.listeners.get(type) || [];
        const data = typeof payload === 'string' ? payload : JSON.stringify(payload);
        handlers.forEach((handler) => handler({ data }));
      }
    }

    window.EventSource = MockEventSource;
    window.__emitMockStatus = (payload) => {
      if (!window.__mockEventSource) return false;
      window.__mockEventSource.emit('status', payload);
      return true;
    };
  });

  try {
    await page.goto(TARGET_URL, { waitUntil: 'networkidle' });
    await page.waitForSelector('.dome-live-track', { timeout: 10000 });
    await page.waitForFunction(() => typeof window.__emitMockStatus === 'function', { timeout: 10000 });

    const emitStatus = async (payload) => {
      const emitted = await page.evaluate((p) => window.__emitMockStatus(p), payload);
      if (!emitted) throw new Error('Mock status emitter unavailable');
      await page.waitForTimeout(120);
    };

    const readState = async () =>
      page.evaluate(() => {
        const liveFill = document.getElementById('dome-live-fill');
        const speedLimit = document.getElementById('dome-speed-limit');
        const reloadButton = document.getElementById('reload-esc-button');
        return {
          hasLegacySlider: Boolean(document.getElementById('dome-slider')),
          webNote: document.getElementById('dome-web-note')?.textContent?.trim() || '',
          hardwareState: document.getElementById('dome-hardware-state')?.textContent?.trim() || '',
          rotationStateClass: document.getElementById('dome-rotation-state')?.className || '',
          liveFillBackground: liveFill ? getComputedStyle(liveFill).backgroundColor : 'missing',
          rotationState: document.getElementById('dome-rotation-state')?.textContent?.trim() || '',
          speedText: document.getElementById('dome-speed-display')?.textContent?.trim() || '',
          feedbackText: document.getElementById('dome-feedback')?.textContent?.trim() || '',
          feedbackClass: document.getElementById('dome-feedback')?.className || '',
          liveFillOpacity: liveFill ? getComputedStyle(liveFill).opacity : 'missing',
          liveFillWidth: liveFill ? getComputedStyle(liveFill).width : 'missing',
          speedLimitDisabled: speedLimit ? speedLimit.disabled : null,
          reloadDisabled: reloadButton ? reloadButton.disabled : null,
        };
      });

    await emitStatus({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: -0.55 });
    const reverse = await readState();

    await emitStatus({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: 0 });
    const idle = await readState();

    await emitStatus({ webControlEnabled: true, domeEnabled: true, domeTargetSpeed: 0.72 });
    const forward = await readState();

    if (reverse.hasLegacySlider) throw new Error('Legacy dome slider still present');
    // Exact strings, not includes(): the three readings are the whole of what
    // these elements say, and a substring match let 'Web control pill did not
    // become enabled' sit here passing against a pill that has never contained
    // the word 'enabled' (#399 slice 3).
    if (reverse.hardwareState !== 'switched on') throw new Error('Dome hardware state not rendered');
    if (!reverse.webNote.startsWith('Web control is on.')) throw new Error('Web control note did not follow the frame');
    if (reverse.rotationState !== 'Reverse' || reverse.speedText !== '-55%') throw new Error('Reverse status not rendered');
    if (idle.rotationState !== 'Idle' || idle.speedText !== '0%') throw new Error('Idle status not rendered');
    if (forward.rotationState !== 'Forward' || forward.speedText !== '72%') throw new Error('Forward status not rendered');
    // A direction is a value, not a symptom: neither the word nor the bar may
    // take a signal color, and the bar is one color whichever way it goes
    // (CONTEXT.md 'Status Color').
    if (reverse.rotationStateClass !== 'dome-rotation-state') throw new Error('Rotation state took a state class');
    if (forward.rotationStateClass !== 'dome-rotation-state') throw new Error('Rotation state took a state class');
    if (reverse.liveFillBackground !== forward.liveFillBackground) throw new Error('The live bar changed color with direction');

    console.log('DOME_PRESETS_START');
    console.log(JSON.stringify({ reverse, idle, forward }, null, 2));
    console.log('DOME_PRESETS_END');

    await page.screenshot({ path: '/tmp/dome-presets-audit.png', fullPage: true });
  } finally {
    await browser.close();
  }
})();

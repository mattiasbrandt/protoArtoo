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
          webPill: document.getElementById('dome-web-pill')?.textContent?.trim() || '',
          hardwarePill: document.getElementById('dome-hardware-pill')?.textContent?.trim() || '',
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
    if (!reverse.webPill.includes('enabled')) throw new Error('Web control pill did not become enabled');
    if (!reverse.rotationState.includes('Reverse')) throw new Error('Reverse status not rendered');
    if (idle.rotationState !== '⏸️ Idle') throw new Error('Idle status not rendered');
    if (!forward.rotationState.includes('Forward')) throw new Error('Forward status not rendered');

    console.log('DOME_PRESETS_START');
    console.log(JSON.stringify({ reverse, idle, forward }, null, 2));
    console.log('DOME_PRESETS_END');

    await page.screenshot({ path: '/tmp/dome-presets-audit.png', fullPage: true });
  } finally {
    await browser.close();
  }
})();

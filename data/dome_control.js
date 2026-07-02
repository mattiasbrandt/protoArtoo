/**
 * data/dome_control.js
 *
 * Live dome control section for the home page.
 * Provides:
 *   - Interactive SVG dome with click-to-toggle panel actuation
 *   - Quick-sequence dropdown + Play/Stop buttons
 *   - Lazy loading on first card expand
 *   - Accessibility: aria-expanded, keyboard support, status announcements
 *
 * Reuses: DomeCommandMap, DomeLayout, DomeLayoutRender, PAApi
 */

(() => {
  'use strict';

  // Only initialize on home page
  if (document.body.dataset.page !== 'home') return;

  // Wait for assets to be loaded
  if (!window.PAAssetsReady) {
    window.addEventListener('pa:assets-ready', initDomeControl);
  } else {
    initDomeControl();
  }

  function initDomeControl() {
    const cardEl = document.getElementById('dome-control-card');
    if (!cardEl) return;

    const headerBtn = cardEl.querySelector('.dome-control-header');
    const bodyEl = cardEl.querySelector('.dome-control-body');
    const feedbackEl = cardEl.querySelector('.dome-control-feedback');

    if (!headerBtn || !bodyEl || !feedbackEl) return;

    // State tracking
    let isExpanded = false;
    let isRendered = false;
    const openPanels = new Set(); // Track which panels are currently open

    // Keyboard and click to toggle expand
    const toggleExpand = async (e) => {
      // Ignore if target is an interactive element inside the body
      if (isExpanded && bodyEl.contains(e.target) && e.target !== headerBtn) {
        return;
      }

      isExpanded = !isExpanded;
      headerBtn.setAttribute('aria-expanded', isExpanded);

      if (isExpanded && !isRendered) {
        // Lazy render on first expand
        await renderDomePanel();
        isRendered = true;
      }

      bodyEl.classList.toggle('hidden', !isExpanded);
    };

    // Header is a native <button>, so Enter/Space already fire a click — a
    // separate keydown handler would double-toggle (Space activates on keyup
    // after the keydown handler already ran). Rely on the click event alone.
    headerBtn.addEventListener('click', toggleExpand);

    let bannerEl = null;
    let pickerContainer = null;

    // Build the picker SVG for the current model. Live/cached tiers have real
    // elements and render through DomeLayoutRender; vendored/unsupported tiers
    // have an empty elements[] (geometry from an unsupported schema is never
    // trusted), so fall back to the vendored MK4 SVG — same fallback seq.js
    // uses for the sequence editor picker.
    function pickerHtmlFor(model) {
      const hasLiveElements = model?.elements?.length > 0;
      if (hasLiveElements && window.DomeLayoutRender?.renderPicker) {
        return window.DomeLayoutRender.renderPicker(model);
      }
      return window.DOME_PANEL_MAP_SVG || '';
    }

    // Re-render the picker + banner in place, re-attach handlers, and restore
    // any panels the operator has toggled open so a layout refresh (dome
    // reconnect) doesn't silently drop visible open state.
    function renderInto(model, source) {
      if (bannerEl) {
        bannerEl.remove();
        bannerEl = null;
      }
      const bannerHtml = renderSourceBanner(source);
      if (bannerHtml) {
        bodyEl.insertAdjacentHTML('afterbegin', bannerHtml);
        bannerEl = bodyEl.firstElementChild;
      }

      pickerContainer.innerHTML = pickerHtmlFor(model);
      attachPanelClickHandlers(pickerContainer, model);

      const svg = pickerContainer.querySelector('svg');
      if (svg) {
        openPanels.forEach((id) => {
          const el =
            svg.querySelector(`[data-element-id="${id}"]`) || svg.querySelector(`[data-target="${id}"]`);
          if (el) el.classList.add('open');
        });
      }
    }

    // Render the dome SVG and attach click handlers
    async function renderDomePanel() {
      try {
        // Load the dome layout if available
        if (window.DomeLayout) {
          await window.DomeLayout.load().catch(() => {
            // Silent fallback to vendored/cached
          });
        }

        pickerContainer = document.createElement('div');
        pickerContainer.className = 'dome-svg-container';
        bodyEl.appendChild(pickerContainer);

        renderInto(window.DomeLayout?.getModel?.(), window.DomeLayout?.getSource?.() || 'vendored');

        // Subscribe to layout changes for live reconnect
        if (window.DomeLayout) {
          window.DomeLayout.onChange(() => {
            renderInto(window.DomeLayout.getModel(), window.DomeLayout.getSource());
          });
        }

        // Render sequence quick controls
        await renderSequenceControls(bodyEl);

        showFeedback('');
      } catch (error) {
        showFeedback('Failed to load dome: ' + PAApi.messageFor(error), 'error');
      }
    }

    function attachPanelClickHandlers(pickerContainer, model) {
      // Event delegation: click on any clickable panel
      const svg = pickerContainer.querySelector('svg');
      if (!svg) return;

      svg.addEventListener('click', async (e) => {
        // Try live picker first (data-element-id)
        let element = e.target.closest('[data-element-id]');
        let elementId = null;

        if (element) {
          elementId = element.dataset.elementId;
          const isSelectable = element.dataset.selectable === 'true';

          if (!isSelectable) {
            // Show advisory for non-selectable panel
            const advisory = buildAdvisory(elementId, model);
            if (advisory) {
              showFeedback(advisory, 'warn');
            }
            return;
          }

          // Selectable: resolve to command and toggle
          await togglePanel(elementId, element);
        } else {
          // Try legacy vendored picker (data-target)
          element = e.target.closest('[data-target]');
          if (element) {
            const target = element.dataset.target;
            await togglePanelVendored(target, element);
          }
        }
      });

      // Keyboard support: Enter/Space on panels
      svg.querySelectorAll('[data-element-id][data-selectable="true"]').forEach((el) => {
        el.addEventListener('keydown', (e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            const elementId = el.dataset.elementId;
            togglePanel(elementId, el);
          }
        });
      });

      // Vendored picker keyboard support
      svg.querySelectorAll('[data-target]').forEach((el) => {
        el.addEventListener('keydown', (e) => {
          if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            const target = el.dataset.target;
            togglePanelVendored(target, el);
          }
        });
      });
    }

    async function togglePanel(elementId, svgElement) {
      try {
        const isOpen = openPanels.has(elementId);
        const capability = isOpen ? 'close' : 'open';
        const cmd = window.DomeCommandMap?.resolvePanelCommand?.(elementId, capability);

        if (!cmd) {
          showFeedback(`Cannot ${capability} ${elementId}`, 'error');
          return;
        }

        // Send command to device
        await PAApi.postForm('/api/dome/cmd', { cmd });

        // Update local state and visual feedback
        if (isOpen) {
          openPanels.delete(elementId);
          svgElement.classList.remove('open');
        } else {
          openPanels.add(elementId);
          svgElement.classList.add('open');
        }

        showFeedback(`${elementId} ${capability}`, 'success');
      } catch (error) {
        showFeedback(`Command failed: ${PAApi.messageFor(error)}`, 'error');
      }
    }

    async function togglePanelVendored(target, svgElement) {
      try {
        const isOpen = openPanels.has(target);
        const action = isOpen ? 'CL' : 'OP';
        const cmd = `:${action}${target}`;

        // Send command to device
        await PAApi.postForm('/api/dome/cmd', { cmd });

        // Update local state (openPanels is the durable source of truth,
        // reapplied by renderInto() after a layout refresh) and visual feedback
        if (isOpen) {
          openPanels.delete(target);
          svgElement.classList.remove('open');
        } else {
          openPanels.add(target);
          svgElement.classList.add('open');
        }

        showFeedback(`${action}${target}`, 'success');
      } catch (error) {
        showFeedback(`Command failed: ${PAApi.messageFor(error)}`, 'error');
      }
    }

    function buildAdvisory(elementId, model) {
      if (!model) return null;

      const elem = model.elements?.find((e) => e.id === elementId);
      if (!elem) return null;

      const severity = elem.severity;
      if (!severity) return null; // Element is available

      let message = '';
      if (severity === 'disabled') {
        const reason = elem.disabled_reason ? ` (${elem.disabled_reason})` : '';
        message = `⚠ ${elementId} is disabled${reason} — dome may ignore this command`;
      } else if (severity === 'inactive') {
        message = `⚠ ${elementId} is not currently active — dome may ignore this command`;
      } else if (severity === 'unverified') {
        message = `⚠ ${elementId} availability unverified — dome state is unknown`;
      } else if (severity === 'unmapped') {
        message = `⚠ ${elementId} is unmapped — cannot actuate`;
      } else {
        message = `⚠ ${elementId} is not available`;
      }

      return message;
    }

    async function renderSequenceControls(container) {
      try {
        // Fetch sequence lists
        const learnedResult = await PAApi.get('/api/seq/list');
        const builtinsResult = await PAApi.get('/api/seq/builtins');

        const learned = learnedResult.data || [];
        const builtins = builtinsResult.data || [];

        // Merge and de-dupe: learned name shadows factory name
        const merged = [];
        const seen = new Set();

        // Add learned first (higher priority)
        learned.forEach((seq) => {
          merged.push(seq);
          seen.add(seq.name);
        });

        // Add builtins not in learned
        builtins.forEach((seq) => {
          if (!seen.has(seq.name)) {
            merged.push(seq);
          }
        });

        // Render controls
        const controlsHtml = `
          <div class="dome-sequence-row">
            <select id="dome-seq-selector" class="dome-seq-select" aria-label="Select sequence">
              <option value="">Choose sequence...</option>
              ${merged.map((seq) => `<option value="${escapeAttr(seq.name)}">${escapeHtml(seq.name)}</option>`).join('')}
            </select>
            <button id="dome-seq-play" class="btn" aria-label="Play selected sequence" title="Send sequence to droid">▶ Play</button>
            <button id="dome-seq-stop" class="btn" aria-label="Stop running sequence" title="Abort sequence">⏹ Stop</button>
          </div>
        `;

        container.insertAdjacentHTML('beforeend', controlsHtml);

        // Attach event listeners
        const selector = document.getElementById('dome-seq-selector');
        const playBtn = document.getElementById('dome-seq-play');
        const stopBtn = document.getElementById('dome-seq-stop');

        if (playBtn && selector) {
          playBtn.addEventListener('click', async () => {
            const seqName = selector.value;
            if (!seqName) {
              showFeedback('Choose a sequence first', 'warn');
              return;
            }

            playBtn.disabled = true;
            showFeedback(`Playing ${seqName}...`, 'info');

            try {
              await PAApi.postJson('/api/seq/test', { name: seqName });
              showFeedback(`${seqName} dispatched`, 'success');
            } catch (error) {
              showFeedback(`Play failed: ${PAApi.messageFor(error)}`, 'error');
            } finally {
              playBtn.disabled = false;
            }
          });
        }

        if (stopBtn) {
          stopBtn.addEventListener('click', async () => {
            stopBtn.disabled = true;
            showFeedback('Stopping sequence...', 'info');

            try {
              await PAApi.postForm('/api/seq/stop', {});
              showFeedback('Sequence stopped', 'success');
            } catch (error) {
              showFeedback(`Stop failed: ${PAApi.messageFor(error)}`, 'error');
            } finally {
              stopBtn.disabled = false;
            }
          });
        }

        // Update selector disabled state based on list
        if (selector) {
          selector.disabled = merged.length === 0;
          selector.addEventListener('change', () => {
            showFeedback('');
          });
        }

        // Disable play if no selection
        if (playBtn && selector) {
          const updatePlayState = () => {
            playBtn.disabled = !selector.value;
          };
          selector.addEventListener('change', updatePlayState);
          updatePlayState();
        }
      } catch (error) {
        showFeedback('Failed to load sequences: ' + PAApi.messageFor(error), 'error');
      }
    }

    function renderSourceBanner(source) {
      let banner = '';
      if (source === 'live') {
        // No banner for live
      } else if (source === 'cached') {
        banner = '<div class="dome-source-banner info">📦 Last known layout (runtime unverified)</div>';
      } else if (source === 'unsupported') {
        banner = '<div class="dome-source-banner warn">⚠ Unsupported layout schema — showing built-in fallback</div>';
      } else if (source === 'vendored') {
        banner = '<div class="dome-source-banner warn">🔌 Dome not reachable — showing MK4 built-in layout</div>';
      }
      return banner;
    }

    function showFeedback(text, level = '') {
      feedbackEl.textContent = text;
      feedbackEl.className = level ? `dome-control-feedback feedback ${level}` : 'dome-control-feedback feedback';
      feedbackEl.classList.toggle('hidden', !text);
    }

    function escapeHtml(str) {
      const div = document.createElement('div');
      div.textContent = str;
      return div.innerHTML;
    }

    function escapeAttr(str) {
      return str.replace(/"/g, '&quot;');
    }
  }
})();

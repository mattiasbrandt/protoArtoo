/**
 * data/dome_control.js
 *
 * The Dashboard's Moving parts, under the three controls in Controls: the
 * droid's two drawings and nothing else (operator, 2026-09-28 on #372; moved
 * into Controls 2026-09-29 on #399).
 *   - The body, Front and Rear (data/body_view.js). A click on a drawn door,
 *     panel or arm opens or closes it - the Parts picture's decision and
 *     request, from data/droid_picture.js, not a copy of them.
 *   - The dome, top-down, from the layout the dome reports or the built-in
 *     map, with click-to-toggle panel actuation, and the Dome Bearing's
 *     marker for where the dome points (data/dome_bearing.js, #445).
 *   - Both drawn once, when the Dashboard mounts: they are not behind a
 *     disclosure any more, so there is no first expand to wait for.
 *   - Accessibility: keyboard support, status announcements.
 * A refused click sends nothing and says why in the drawings' feedback line.
 *
 * Reuses: BodyView, PADroidPicture, DomeCommandMap, DomeLayout,
 * DomeLayoutRender, PADomeBearing, PAApi
 */

(() => {
  'use strict';

  // Only initialize on home page
  if (document.body.dataset.page !== 'home') return;

  // The card is found now, while this script runs, and not when the drawing
  // starts. The Operator Shell runs a surface's scripts with that surface in
  // the document, and only then: on a first load pa:assets-ready waits for
  // every section of the session, so an operator who has moved on by then has
  // the Dashboard detached (ADR 0048), where getElementById finds nothing - and
  // the Dashboard came back with no drawing at all (#472).
  const cardEl = document.getElementById('dome-control-card');

  // The one armed wait for the sections, so a second one replaces it instead
  // of joining it: two waiting would mean two draws when the sections settle.
  // Defensive: the shell mounts a surface once and keeps its nodes (data/
  // shell.js detach()), so this script arms one wait a session today.
  let armedSettle = null;

  // Wait for assets to be loaded. Already loaded means the Operator Shell is
  // mounting this surface after the page's first one (ADR 0048).
  if (!window.PAAssetsReady) {
    window.addEventListener('pa:assets-ready', () => initDomeControl(cardEl, false), { once: true });
  } else {
    initDomeControl(cardEl, true);
  }

  // Runs `draw` once the Dashboard's own sections have settled - done, or
  // failed and waiting to retry. On a first page load this script starts from
  // pa:assets-ready, which fires only after that, so it draws at once. Under
  // the Operator Shell it runs while the surface is still mounting, before
  // app.js has declared its sections, so it waits for the next stable change,
  // the pattern data/wifi.js already uses.
  function afterSectionsSettle(mountedLate, draw) {
    if (!mountedLate || !window.PABootstrap) {
      draw();
      return;
    }
    if (armedSettle) window.removeEventListener('pa:bootstrap-change', armedSettle);
    const onChange = (event) => {
      if (!event.detail?.sectionsStable) return;
      window.removeEventListener('pa:bootstrap-change', onChange);
      armedSettle = null;
      draw();
    };
    armedSettle = onChange;
    window.addEventListener('pa:bootstrap-change', onChange);
  }

  function initDomeControl(cardEl, mountedLate) {
    if (!cardEl) return;

    const feedbackEl = cardEl.querySelector('.dome-control-feedback');
    const bodyDrawingEl = cardEl.querySelector('.moving-parts-body');
    const domeEl = cardEl.querySelector('.moving-parts-dome');

    if (!feedbackEl || !bodyDrawingEl || !domeEl) return;

    const openPanels = new Set(); // Track which panels are currently open

    let bannerEl = null;
    let pickerContainer = null;
    // Releases the one DomeLayout subscription this drawing holds.
    let releaseLayout = null;

    // Build the picker SVG for the current model. Live/cached tiers have real
    // elements and render through DomeLayoutRender; the offline tiers have an
    // empty elements[] (geometry from an unsupported schema is never trusted),
    // so they fall back to the built-in MK4 SVG — same fallback seq.js uses for
    // the sequence editor picker.
    //
    // The built-in drawing is shown only where it IS the dome the builder says
    // they built (ADR 0047): a drawing of somebody else's design presented as
    // theirs is worse than no drawing, because every panel on it is one they
    // would go looking for. `usesVendoredDrawing` is set by tier 3; a model
    // without it is an older shape and keeps the old behaviour.
    function pickerHtmlFor(model) {
      const hasLiveElements = model?.elements?.length > 0;
      if (hasLiveElements && window.DomeLayoutRender?.renderPicker) {
        return window.DomeLayoutRender.renderPicker(model);
      }
      if (model?.usesVendoredDrawing === false) {
        return '';
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
      const bannerHtml = renderSourceBanner(source, model);
      if (bannerHtml) {
        domeEl.insertAdjacentHTML('afterbegin', bannerHtml);
        bannerEl = domeEl.firstElementChild;
      }

      pickerContainer.innerHTML = pickerHtmlFor(model);
      attachPanelClickHandlers(pickerContainer, model);
      // Where the dome points, on whichever drawing this is, in its own frame.
      window.PADomeBearing?.mount(pickerContainer.querySelector('svg'));

      const svg = pickerContainer.querySelector('svg');
      if (svg) {
        openPanels.forEach((openCmd) => {
          let el = null;

          // Try to decode the command to get element id/kind (live tier)
          if (window.DomeCommandMap?.decodeCommandToElement) {
            const decoded = window.DomeCommandMap.decodeCommandToElement(openCmd);
            if (decoded && (decoded.kind === 'ring' || decoded.kind === 'pie')) {
              el = svg.querySelector(`[data-element-id="${decoded.id}"]`);
            }
          }

          // Fall back to vendored tier: extract bare target from :OP prefix
          if (!el) {
            const target = openCmd.replace(/^:OP/, '');
            el = svg.querySelector(`[data-target="${target}"]`);
          }

          if (el) el.classList.add('open');
        });
      }
    }

    // Render the dome drawing and attach its click handlers
    async function renderDome() {
      try {
        // Load the dome layout if available
        if (window.DomeLayout) {
          await window.DomeLayout.load().catch(() => {
            // Silent fallback to vendored/cached
          });
        }

        // One drawing, whatever drew before: a draw replaces the container it
        // finds rather than adding a second beside it (#472, the operator saw
        // the dome twice). Looked up in the card, not held, so a draw that
        // overlapped another's await is replaced too.
        domeEl.querySelectorAll('.dome-svg-container').forEach((stale) => stale.remove());
        pickerContainer = document.createElement('div');
        pickerContainer.className = 'dome-svg-container';
        domeEl.appendChild(pickerContainer);

        renderInto(window.DomeLayout?.getModel?.(), window.DomeLayout?.getSource?.() || 'vendored');

        // Subscribe to layout changes for live reconnect, once: the previous
        // draw's subscription goes before this one is taken.
        if (window.DomeLayout) {
          releaseLayout?.();
          releaseLayout = window.DomeLayout.onChange(() => {
            renderInto(window.DomeLayout.getModel(), window.DomeLayout.getSource());
          });
        }

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
              showFeedback(advisory, 'warning');
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

    // The estop holds every servo move a picture of the droid can start
    // (operator, 2026-09-19, #372): a panel press is refused while the estop is
    // latched, and while the droid has not said whether it is. The hold and its
    // words are data/droid_picture.js's, the same the Parts picture and the
    // body drawing above keep; the answer is the Live Reading's
    // (data/live_reading.js). Returns the sentence to say instead, or null
    // when the press may go.
    function estopHold() {
      return window.PADroidPicture.estopRefusal(window.PALiveReading.current().estop);
    }

    // The body drawing: Front and Rear, the Parts this droid carries, each in
    // the state the droid was last told. A click is the act here, not a pick:
    // it opens or closes the Part when Parts' own decision allows it, and says
    // that decision's reason and sends nothing when it does not. The drawing
    // never writes (data/body_view.js); this is where a click becomes a request.
    function renderBody() {
      if (!window.BodyView || !window.PADroidPicture) {
        // Never swallowed: the card then shows the dome alone, and says why here.
        console.error('[dome-control] /body_view.js or /droid_picture.js did not load; no body drawing');
        return;
      }
      const drawing = window.BodyView.mountDrawing(bodyDrawingEl, {
        parts: window.DroidParts.parts,
        art: window.BodyArt,
        faces: ['front', 'rear'],
        onPick: (markerId) => {
          pressBody(markerId);
        },
      });
      const picture = window.PADroidPicture.caller(drawing);

      const paint = () => {
        const onPicture = picture.pictureFor();
        drawing.update({
          kind: window.BodyView.STATE_KINDS.LIVE,
          marks: picture.marks(),
          shown: onPicture.shown,
          fitted: picture.fittedNow(),
          domePending: onPicture.domePending,
        });
      };

      // openClose() sends only what the decision allows, and otherwise hands
      // back the decision's reason, so this is the one guard, in shared code.
      const pressBody = (markerId) => {
        const decision = picture.decide(markerId, window.PALiveReading.current().estop);
        picture.openClose(decision).then((result) => {
          showFeedback(result.text, result.level);
          paint();
        });
      };

      // Painted from the Outputs the Dashboard already reads (data/app.js) and
      // the Droid Build it already holds; this card asks the droid for nothing
      // on its own clock.
      window.PAOutputs.onChange(paint);
      window.DroidBuild?.onChange?.(paint);
      paint();
    }

    async function togglePanel(elementId, svgElement) {
      const held = estopHold();
      if (held) {
        showFeedback(held, 'error');
        return;
      }
      try {
        // Resolve the open command first to get the canonical key for openPanels
        const openCmd = window.DomeCommandMap?.resolvePanelCommand?.(elementId, 'open');
        if (!openCmd) {
          showFeedback(`Cannot resolve ${elementId}`, 'error');
          return;
        }

        const isOpen = openPanels.has(openCmd);
        const capability = isOpen ? 'close' : 'open';
        const cmd = window.DomeCommandMap?.resolvePanelCommand?.(elementId, capability);

        if (!cmd) {
          showFeedback(`Cannot ${capability} ${elementId}`, 'error');
          return;
        }

        // Send command to device
        await PAApi.postForm('/api/dome/cmd', { cmd });

        // Update local state and visual feedback (using canonical openCmd as key)
        if (isOpen) {
          openPanels.delete(openCmd);
          svgElement.classList.remove('open');
        } else {
          openPanels.add(openCmd);
          svgElement.classList.add('open');
        }

        showFeedback(`${elementId} ${capability}`, 'success');
      } catch (error) {
        showFeedback(`Command failed: ${PAApi.messageFor(error)}`, 'error');
      }
    }

    async function togglePanelVendored(target, svgElement) {
      const held = estopHold();
      if (held) {
        showFeedback(held, 'error');
        return;
      }
      try {
        // Canonical key for openPanels is the open command
        const openCmd = `:OP${target}`;
        const isOpen = openPanels.has(openCmd);
        const action = isOpen ? 'CL' : 'OP';
        const cmd = `:${action}${target}`;

        // Send command to device
        await PAApi.postForm('/api/dome/cmd', { cmd });

        // Update local state (openPanels is the durable source of truth,
        // reapplied by renderInto() after a layout refresh) and visual feedback
        if (isOpen) {
          openPanels.delete(openCmd);
          svgElement.classList.remove('open');
        } else {
          openPanels.add(openCmd);
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

      // The state clause is data/dome_layout.js's, where severity is computed
      // and where the sequence editor reads it too (#348). What this surface
      // adds is its own consequence: the button still sends, so a builder
      // pressing it is owed the fact that the dome may do nothing.
      //
      // No warning glyph in front of the sentence: an operator surface carries
      // no pictograph (ADR 0066), and the amber the feedback line takes is the
      // second reading the sentence already gives on its own.
      const clause = window.DomeLayout?.severityClause?.(elem);
      if (!clause) return null; // Element is available

      const stillSends = elem.severity === 'disabled' || elem.severity === 'inactive';
      return stillSends ? `${clause} The dome may ignore it.` : clause;
    }

    function renderSourceBanner(source, model) {
      // The two treatments are the anatomy's own note voices rather than a
      // second spelling of them: a provenance line takes the plain note, and a
      // dome the builder can go and plug in takes the amber "act on this" one.
      // The cached banner used to be blue, and blue reports no state at all
      // (GLOSSARY.md "Status Color").
      let banner = '';
      if (source === 'live') {
        // No banner for live
      } else if (source === 'cached') {
        banner = '<div class="note dome-source-banner">Last layout the dome sent. It may have changed since.</div>';
      } else if (source === 'unsupported') {
        banner = '<div class="note note-act dome-source-banner">The dome sent a layout this page cannot read. Showing the built-in map.</div>';
      } else if (source === 'stated-design') {
        // Tier 3 with a dome the built-in drawing is not of. Two different
        // jobs for the builder, so two different sentences: one is "we have no
        // picture of your dome", the other is "we do not know what your dome
        // carries at all" — and the second is the one somebody has to go and
        // read out of the design files (ADR 0047).
        banner = model?.complementKnown === false
          ? '<div class="note note-act dome-source-banner">Dome not reachable. This build does not know which panels your dome carries.</div>'
          : '<div class="note note-act dome-source-banner">Dome not reachable. No built-in map for your dome design.</div>';
      } else if (source === 'vendored') {
        banner = '<div class="note note-act dome-source-banner">Dome not reachable. Showing the built-in MK4 map.</div>';
      }
      return banner;
    }

    function showFeedback(text, level = '') {
      feedbackEl.textContent = text;
      feedbackEl.className = level ? `dome-control-feedback feedback ${level}` : 'dome-control-feedback feedback';
      feedbackEl.classList.toggle('hidden', !text);
    }

    // Drawn once, on mount, rather than on a first expand: the drawings sit
    // open in Controls. The body asks the droid for nothing, so it is drawn at
    // once. The dome's layout read first joins the Droid Build this page reads
    // with its log level (data/droid_build.js load()), so it waits for the
    // Dashboard's sections: started before app.js had read the config, it
    // opened a second GET /api/config beside that one, on a controller that
    // sheds connections under load. renderDome() reports its own failure in
    // the feedback line.
    renderBody();
    afterSectionsSettle(mountedLate, renderDome);
  }
})();

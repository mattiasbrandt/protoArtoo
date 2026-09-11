// =============================================================================
// data/dome_layout.js
//
// Browser-side dome layout fetch/cache/fallback view-model for the layout editor.
//
// Implements the 4-tier fallback hierarchy from ADR 0009:
//   1. Live: fetch /api/dome/layout with supported schema -> cached
//   2. Cached-live: fetch fails but localStorage has prior live layout
//   3. Stated design: no cache -> the Dome Design the builder stated, which
//      replaces the hardcoded vendored MK4 here (ADR 0047, #333)
//   4. Unsupported: 200 OK but schema_revision not in SUPPORTED_DOME_LAYOUT_SCHEMAS
//
// Tier 3 asks window.DroidBuild what dome the builder says they built and
// answers with the vendored drawing only where that drawing IS their dome. It
// still carries no geometry of its own: the catalog records bearings whose
// convention is unresolved (docs/droid-parts.yaml), so drawing a complement
// without a vendored picture is not something this tier can do yet - what it
// can do is stop claiming an MK4 dome belongs to a builder who stated
// otherwise, and say which case they are in.
//
// Subscribes to dome connection state changes (dome_link.state) and refetches
// when transitioning INTO connected state. No polling.
//
// Exposes window.DomeLayout with:
//   - load() / refresh(): fetch and resolve the model
//   - getModel(): current normalized model
//   - onChange(cb): register callback fired after each resolve
//   - getSource(): 'live' | 'cached' | 'vendored' | 'stated-design' | 'unsupported'
//     'vendored' and 'stated-design' are both tier 3: the first says the
//     built-in drawing is this builder's dome, the second that it is not.
// =============================================================================

(() => {
  'use strict';

  // ── Constants ──────────────────────────────────────────────────────────

  const SUPPORTED_DOME_LAYOUT_SCHEMAS = new Set([1]);

  // Cache key format: `dome_layout_${templateId}_${templateRevision}_${schemaRevision}`
  // Stores: { rawLayout, savedAt, runtimeStateTsAtSave }
  const cacheKeyPrefix = 'dome_layout_';

  // Fetch timeout: 5s for the /api/dome/layout proxy call
  const FETCH_TIMEOUT_MS = 5000;

  // ── State ──────────────────────────────────────────────────────────────

  let currentModel = null;
  let currentSource = 'vendored';
  let listeners = new Set();
  let lastDomeLinkState = null;
  let statusStreamSubscribed = false;

  // ── Cache Helpers ──────────────────────────────────────────────────────

  /**
   * Compute localStorage cache key for a given layout.
   * @param {string} templateId
   * @param {number} templateRevision
   * @param {number} schemaRevision
   * @returns {string}
   */
  function computeCacheKey(templateId, templateRevision, schemaRevision) {
    return `${cacheKeyPrefix}${templateId}_${templateRevision}_${schemaRevision}`;
  }

  /**
   * Store layout to localStorage cache.
   * @param {string} templateId
   * @param {number} templateRevision
   * @param {number} schemaRevision
   * @param {object} rawLayout - the raw layout from the API
   * @param {number} runtimeStateTsAtSave - runtime_state_ts from the layout
   */
  function setCachedLayout(templateId, templateRevision, schemaRevision, rawLayout, runtimeStateTsAtSave) {
    try {
      const key = computeCacheKey(templateId, templateRevision, schemaRevision);
      const cacheEntry = {
        rawLayout,
        runtimeStateTsAtSave,
        savedAt: Date.now(),
      };
      window.localStorage.setItem(key, JSON.stringify(cacheEntry));
    } catch (_error) {
      // Ignore cache write failures; they are non-fatal
    }
  }

  // ── Normalization & Severity Logic ─────────────────────────────────────

  /**
   * Compute element severity and selectability for a single element.
   * Severity precedence:
   *   1. in_layout === false             -> 'in_layout_false'
   *   2. commandable && !mapped          -> 'unmapped'
   *   3. !runtimeVerified && commandable -> 'unverified'
   *   4. disabled === true               -> 'disabled'
   *   5. commandable && active !== true  -> 'inactive'
   *   6. (otherwise)                     -> null
   *
   * `disabled` and `active` are dome-composed runtime state (ADR 0009: "trust
   * active/disabled only from a live fetch"), so they are only consulted once
   * runtimeVerified is true — a cached-tier element must not assert stale
   * disabled/inactive state as current fact; it reports 'unverified' instead.
   *
   * selectableForNewStep is true ONLY when:
   *   in_layout && commandable && mapped && runtimeVerified && active === true && !disabled
   *
   * @param {object} elem - raw element from layout
   * @param {boolean} runtimeVerified - whether runtime state is trusted (freshly live)
   * @returns {object} { severity, mapped, selectableForNewStep }
   */
  function computeElementSeverity(elem, runtimeVerified) {
    const { commandable, in_layout, disabled, active } = elem;

    // Determine if element is mapped (has a command target)
    const mapped = Boolean(window.DomeCommandMap?.resolvePanelCommand?.(elem.id, 'open'));

    // Severity precedence
    let severity = null;
    if (!in_layout) {
      severity = 'in_layout_false';
    } else if (commandable && !mapped) {
      severity = 'unmapped';
    } else if (!runtimeVerified) {
      if (commandable) {
        severity = 'unverified';
      }
      // Non-commandable (decorative) elements have no v1 availability display
      // when runtime state isn't fresh; they render for spatial context only.
    } else if (disabled) {
      severity = 'disabled';
    } else if (commandable && active !== true) {
      severity = 'inactive';
    }

    // selectableForNewStep: strict eligibility for new authoring
    const selectableForNewStep =
      in_layout &&
      commandable &&
      mapped &&
      runtimeVerified &&
      active === true &&
      !disabled;

    return { severity, mapped, selectableForNewStep };
  }

  /**
   * Normalize raw layout from API into the view-model consumed by the picker.
   * @param {object} rawLayout - raw layout from /api/dome/layout or cache
   * @param {boolean} runtimeVerified - whether active/disabled state is trusted
   * @param {string} source - 'live', 'cached', 'vendored', or 'unsupported'
   * @param {string|null} warning - null or diagnostic message
   * @returns {object} normalized model
   */
  function normalizeLayout(rawLayout, runtimeVerified, source, warning = null) {
    // Extract coordinate space or use fallback
    const viewBox = rawLayout?.coordinate_space?.viewBox || '0 0 480 480';

    // Normalize elements (empty array for vendored)
    let elements = [];
    if (rawLayout?.elements && Array.isArray(rawLayout.elements)) {
      elements = rawLayout.elements.map((elem) => {
        const { severity, mapped, selectableForNewStep } = computeElementSeverity(elem, runtimeVerified);
        return {
          id: elem.id,
          label: elem.label,
          element_type: elem.element_type,
          panel_kind: elem.panel_kind,
          mounted_on: elem.mounted_on,
          geometry: elem.geometry,
          label_anchor: elem.label_anchor,
          callout: elem.callout,
          render_order: elem.render_order,
          aliases: elem.aliases || [],
          commandable: elem.commandable,
          in_layout: elem.in_layout,
          active: elem.active ?? null,
          disabled: elem.disabled,
          mapped,
          severity,
          selectableForNewStep,
        };
      });
    }

    return {
      source,
      runtimeVerified,
      warning,
      viewBox,
      elements,
    };
  }

  /**
   * Tier 3: what the builder says their dome is, and whether the built-in
   * drawing is a drawing of it.
   *
   * The Dome Design is the builder's statement, so this reads it through the
   * one Droid Build seam rather than working out a complement of its own
   * (data/droid_build.js). Three outcomes, and they are different sentences to
   * a builder standing at a bench:
   *
   *   the built-in drawing IS their dome  -> show it, as this tier always has
   *   it is a drawing of another design   -> do not show it as theirs
   *   this build does not record what     -> say so; `mk4/simple` is that case
   *     their design and variant carry       today, and drawing an empty dome
   *                                          would read as "you fitted nothing"
   *
   * A page that has not loaded the Droid Build seam, or a controller too old to
   * answer, leaves the design unstated - and an unstated design keeps exactly
   * the behaviour this tier had before, which is the vendored drawing.
   *
   * Tier 4 - an unsupported schema - resolves the same way and says so on top:
   * its geometry is not trusted either, so what it can show is exactly what
   * this tier can show, plus the schema warning.
   *
   * @param {string} [forcedSource] - tier 4 passes 'unsupported'
   * @param {string} [forcedWarning] - tier 4's schema warning, which wins
   * @returns {object} the normalized model, with tier-3 fields on top
   */
  function statedDesignLayout(forcedSource, forcedWarning) {
    const build = window.DroidBuild?.current?.() || null;
    const designId = build ? build.dome.design : '';
    const variantId = build ? build.dome.variant : '';
    const complement = window.DroidBuild?.complementFor
      ? window.DroidBuild.complementFor(designId, variantId, 'dome')
      : { ids: [], known: false };

    const drawingIsTheirs =
      designId === '' ||
      (designId === window.DOME_PANEL_MAP_DESIGN &&
       variantId === window.DOME_PANEL_MAP_VARIANT);

    let warning = null;
    if (designId !== '' && !complement.known) {
      warning = 'This build does not record which panels that dome design carries';
    } else if (!drawingIsTheirs) {
      warning = 'The built-in dome map is not the design you stated';
    }

    const source = forcedSource || (drawingIsTheirs ? 'vendored' : 'stated-design');
    const model = normalizeLayout({}, false, source, forcedWarning || warning);
    model.domeDesign = designId;
    model.domeVariant = variantId;
    model.complementKnown = complement.known;
    // What the two consumers of this tier actually branch on: may the built-in
    // MK4 drawing be shown as this builder's dome.
    model.usesVendoredDrawing = drawingIsTheirs;
    return model;
  }

  // ── Fetch & Resolve ────────────────────────────────────────────────────

  /**
   * Fetch layout from /api/dome/layout via body proxy.
   * @returns {Promise<object|null>} raw layout object or null on error
   */
  async function fetchLiveLayout() {
    try {
      const result = await window.PAApi.get('/api/dome/layout', {
        timeoutMs: FETCH_TIMEOUT_MS,
        cache: 'no-store',
      });
      if (result.ok && result.data) {
        return result.data;
      }
    } catch (_error) {
      // Fetch failed (timeout, network, 503, bad JSON, etc.) — silent fallback to cache/vendored
    }
    return null;
  }

  /**
   * Resolve a layout using the 4-tier fallback hierarchy.
   * Returns the normalized model and updates currentModel / currentSource.
   * Fires onChange callbacks.
   *
   * @returns {Promise<void>}
   */
  async function resolveLayout() {
    // The boot re-apply of the Droid Build, before anything can need it. It is
    // held by the seam, so every surface on the page that asks joins this one
    // request rather than opening another, and a failure to read it leaves the
    // design unstated - which is the pre-#343 behaviour rather than a broken
    // picker.
    await window.DroidBuild?.load?.();

    const liveLayout = await fetchLiveLayout();

    let model;
    let source;
    let warning = null;

    if (liveLayout) {
      // Tier 1: Live fetch succeeded
      const schemaRev = liveLayout.schema_revision;
      if (SUPPORTED_DOME_LAYOUT_SCHEMAS.has(schemaRev)) {
        // Schema is supported: use it, cache it, mark runtime verified
        model = normalizeLayout(liveLayout, true, 'live');
        source = 'live';
        // Cache the live layout
        setCachedLayout(
          liveLayout.template_id,
          liveLayout.template_revision,
          schemaRev,
          liveLayout,
          liveLayout.runtime_state_ts
        );
      } else {
        // Tier 4: Schema not supported
        warning = `Layout schema ${schemaRev} not supported (supported: ${Array.from(SUPPORTED_DOME_LAYOUT_SCHEMAS).join(', ')})`;
        // Tier 4 is tier 3 plus this warning: an unsupported schema's geometry
        // is not trusted, so what can be shown is what the stated Dome Design
        // allows - including, for a design the built-in drawing is not of, no
        // drawing at all (ADR 0047).
        model = statedDesignLayout('unsupported', warning);
        source = 'unsupported';
      }
    } else if (liveLayout === null) {
      // Live fetch failed. Try Tier 2: cached-live fallback.
      // Scan all cached layouts (one per template_id/revision/schema_revision
      // seen) and keep the most recently saved one with a supported schema.
      let cachedLayout = null;
      try {
        for (let i = 0; i < window.localStorage.length; i++) {
          const key = window.localStorage.key(i);
          if (key && key.startsWith(cacheKeyPrefix)) {
            const stored = window.localStorage.getItem(key);
            if (stored) {
              const parsed = JSON.parse(stored);
              if (parsed.rawLayout) {
                const schemaRev = parsed.rawLayout.schema_revision;
                if (
                  SUPPORTED_DOME_LAYOUT_SCHEMAS.has(schemaRev) &&
                  (!cachedLayout || (parsed.savedAt || 0) > (cachedLayout.savedAt || 0))
                ) {
                  cachedLayout = parsed;
                }
              }
            }
          }
        }
      } catch (_error) {
        // Ignore cache inspection errors
      }

      if (cachedLayout) {
        // Tier 2: Cached-live
        model = normalizeLayout(cachedLayout.rawLayout, false, 'cached');
        source = 'cached';
      } else {
        // Tier 3: the stated Dome Design (ADR 0047)
        model = statedDesignLayout();
        source = model.source;
      }
    }

    currentModel = model;
    currentSource = source;

    // Fire onChange callbacks
    listeners.forEach((cb) => {
      try {
        cb(model);
      } catch (_error) {
        // Swallow listener errors to avoid breaking other subscribers
      }
    });
  }

  // ── Status Stream Subscription ────────────────────────────────────────

  /**
   * Subscribe to dome connection state changes.
   * Refetch layout when transitioning INTO "connected" state.
   */
  function subscribeToStatusStream() {
    if (statusStreamSubscribed || !window.PAStatusStream) {
      return;
    }

    window.PAStatusStream.subscribe((_eventType, payload) => {
      if (payload?.dome_link?.state === undefined) {
        return;
      }

      const newState = payload.dome_link.state;
      // Refetch only when transitioning INTO connected
      if (lastDomeLinkState !== 'connected' && newState === 'connected') {
        resolveLayout();
      }
      lastDomeLinkState = newState;
    });

    statusStreamSubscribed = true;
  }

  // ── Public API ─────────────────────────────────────────────────────────

  /**
   * Load or reload the dome layout.
   * Runs the 4-tier fallback and updates currentModel.
   * Fires onChange callbacks.
   * @returns {Promise<void>}
   */
  async function load() {
    await resolveLayout();
  }

  /**
   * Manually refresh the layout.
   * Alias for load(); provided for symmetry.
   * @returns {Promise<void>}
   */
  async function refresh() {
    await load();
  }

  /**
   * Get the current normalized model.
   * @returns {object|null}
   */
  function getModel() {
    return currentModel;
  }

  /**
   * Register a change listener.
   * Fired after each resolveLayout() completes (on load/refresh/dome-reconnect).
   * @param {Function} cb - callback(normalizedModel)
   */
  function onChange(cb) {
    if (typeof cb === 'function') {
      listeners.add(cb);
    }
  }

  /**
   * Unregister a change listener.
   * @param {Function} cb
   */
  function offChange(cb) {
    listeners.delete(cb);
  }

  /**
   * Get the current source tier.
   * @returns {string} 'live' | 'cached' | 'vendored' | 'stated-design' | 'unsupported'
   */
  function getSource() {
    return currentSource;
  }

  // ── Initialization ────────────────────────────────────────────────────

  // Subscribe to dome connection changes when assets are ready
  if (window.PAAssetsReady === true) {
    subscribeToStatusStream();
  } else {
    window.addEventListener('pa:assets-ready', () => {
      subscribeToStatusStream();
    }, { once: true });
  }

  // ── Export ────────────────────────────────────────────────────────────

  window.DomeLayout = {
    SUPPORTED_DOME_LAYOUT_SCHEMAS,
    load,
    refresh,
    getModel,
    onChange,
    offChange,
    getSource,
  };
})();

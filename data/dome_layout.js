// =============================================================================
// data/dome_layout.js
//
// Browser-side dome layout fetch/cache/fallback view-model for the layout editor.
//
// Implements the 4-tier fallback hierarchy from ADR 0009:
//   1. Live: fetch /api/dome/layout with supported schema -> cached
//   2. Cached-live: fetch fails but localStorage has prior live layout
//   3. Vendored: no cache -> offline MK4 fallback (no geometry)
//   4. Unsupported: 200 OK but schema_revision not in SUPPORTED_DOME_LAYOUT_SCHEMAS
//
// Subscribes to dome connection state changes (dome_link.state) and refetches
// when transitioning INTO connected state. No polling.
//
// Exposes window.DomeLayout with:
//   - load() / refresh(): fetch and resolve the model
//   - getModel(): current normalized model
//   - onChange(cb): register callback fired after each resolve
//   - getSource(): 'live' | 'cached' | 'vendored' | 'unsupported'
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
   * Retrieve cached layout from localStorage.
   * @param {string} templateId
   * @param {number} templateRevision
   * @param {number} schemaRevision
   * @returns {object|null} cached layout or null if not found or corrupted
   */
  function getCachedLayout(templateId, templateRevision, schemaRevision) {
    try {
      const key = computeCacheKey(templateId, templateRevision, schemaRevision);
      const stored = window.localStorage.getItem(key);
      if (!stored) return null;
      const parsed = JSON.parse(stored);
      // Validate that it has the required fields
      if (parsed.rawLayout && parsed.savedAt) {
        return parsed;
      }
    } catch (_error) {
      // Ignore JSON parse or access errors; cache is best-effort
    }
    return null;
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
   *   1. in_layout === false         -> 'in_layout_false'
   *   2. commandable && !mapped      -> 'unmapped'
   *   3. disabled === true           -> 'disabled'
   *   4. commandable && !runtimeVerified -> 'unverified'
   *   5. commandable && active !== true -> 'inactive'
   *   6. (otherwise)                 -> null
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
    } else if (disabled) {
      severity = 'disabled';
    } else if (commandable && !runtimeVerified) {
      severity = 'unverified';
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
   * Normalize raw layout from API into the Slice D contract model.
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
        // Emit warning and fall back to vendored
        warning = `Layout schema ${schemaRev} not supported (supported: ${Array.from(SUPPORTED_DOME_LAYOUT_SCHEMAS).join(', ')})`;
        model = normalizeLayout({}, false, 'unsupported', warning);
        source = 'unsupported';
      }
    } else if (liveLayout === null) {
      // Live fetch failed. Try Tier 2: cached-live fallback
      // Heuristic: use the most recently cached live layout (not the unsupported ones)
      let cachedLayout = null;
      try {
        // Iterate localStorage looking for any cached live layouts
        for (let i = 0; i < window.localStorage.length; i++) {
          const key = window.localStorage.key(i);
          if (key && key.startsWith(cacheKeyPrefix)) {
            const stored = window.localStorage.getItem(key);
            if (stored) {
              const parsed = JSON.parse(stored);
              if (parsed.rawLayout) {
                const schemaRev = parsed.rawLayout.schema_revision;
                // Only consider layouts with supported schemas
                if (SUPPORTED_DOME_LAYOUT_SCHEMAS.has(schemaRev)) {
                  // Found a usable cached layout (we could rank by savedAt if multiple exist)
                  cachedLayout = parsed;
                  break;
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
        // Tier 3: Vendored fallback
        model = normalizeLayout({}, false, 'vendored');
        source = 'vendored';
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
   * @returns {string} 'live' | 'cached' | 'vendored' | 'unsupported'
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

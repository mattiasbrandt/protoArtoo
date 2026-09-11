/**
 * data/droid_build.js
 *
 * The one apply seam for the Droid Build (ADR 0047, #333, #343).
 *
 * A Droid Build is what protoArtoo knows about which droid it is bolted into:
 * a Dome Design and a Body Design, each at a Design Variant, together with the
 * Fitted Parts they seeded and any Common Addition the builder added.
 *
 * ONE FUNCTION APPLIES IT, AND EVERY SURFACE HANGS OFF THAT.
 * `applyDroidBuild()` is called by the Droid Build step in Setup, by the
 * Component Picker, by the wiring sheet, by the body views and by the dome
 * map's tier-3 fallback. None of them re-derives which Parts a design carries
 * or which half of the droid a Part is on; they ask here, or they read what
 * this published. It is re-applied at page boot, so a surface that renders
 * before the device has answered still renders from one place.
 *
 * A DESIGN SEEDS, IT NEVER FENCES. `applyDroidBuild()` adds a design's
 * complement to the Fitted Parts; it has no branch that removes one. A builder
 * who fitted the gripper arm their design never carried keeps it when they
 * change design, and a Part outside the set is still legal to author, save and
 * wire - the firmware's Part vocabulary is the whole catalog whatever is
 * stored here. Adding a `delete` branch is the one change to this file that
 * would undo the decision it implements.
 *
 * A COMPLEMENT NOBODY HAS READ IS NOT AN EMPTY ONE. A variant whose seeds the
 * catalog records as unknown carries `seeds: null`, never `[]`; `own`
 * legitimately carries `[]`. Seeding nothing silently from the first is the
 * one outcome that is wrong, so this module reports it as `unknownComplement`
 * and fits nothing, rather than quietly producing an empty droid. `mk4/simple`
 * is that case today.
 */

(function () {
  'use strict';

  // The two halves of the answer, and the key each half's fields go over the
  // wire under. POST /api/config takes a half as a PAIR - a variant means
  // nothing without the design it belongs to.
  const HALVES = {
    dome: { designField: 'domeDesign', variantField: 'domeVariant' },
    body: { designField: 'bodyDesign', variantField: 'bodyVariant' },
  };

  const PERSIST_TIMEOUT_MS = 5000;

  // The applied Droid Build, or null before the first apply. Surfaces read it
  // through current(); nothing outside this file writes it.
  let applied = null;
  const listeners = new Set();

  // ── The catalog ────────────────────────────────────────────────────────

  function parts() {
    return (window.DroidParts && window.DroidParts.parts) || [];
  }

  function designs() {
    return (window.DroidParts && window.DroidParts.designs) || [];
  }

  function designById(id) {
    return designs().find((design) => design.id === id) || null;
  }

  /**
   * Which half of the droid a Part is on, from the catalog rather than from
   * its id. `half` is absent on the escape-hatch slots, which belong to no
   * design and so are seeded by none.
   * @param {string} id
   * @returns {string|null} 'dome', 'body', or null
   */
  function halfOf(id) {
    const part = parts().find((row) => row.id === id);
    return (part && part.half) || null;
  }

  /**
   * The Part ids a design seeds into one half of the droid.
   *
   * `known` is the whole point of the return shape: false says this catalog
   * does not record what that design and variant carry, which is a different
   * answer from an empty complement and must not be drawn as one.
   *
   * @param {string} designId
   * @param {string} variantId - empty for a design that declares no variants
   * @param {string} half - 'dome' or 'body'
   * @returns {{ids: string[], known: boolean}}
   */
  function complementFor(designId, variantId, half) {
    const design = designById(designId);
    if (!design) {
      return { ids: [], known: false };
    }
    let seeds;
    if (Array.isArray(design.variants)) {
      const variant = design.variants.find((row) => row.id === variantId);
      seeds = variant ? variant.seeds : undefined;
    } else {
      seeds = design.seeds;
    }
    // null is the catalog's declared unknown and undefined is a variant this
    // design does not publish; neither is an empty complement.
    if (!Array.isArray(seeds)) {
      return { ids: [], known: false };
    }
    return { ids: seeds.filter((id) => halfOf(id) === half), known: true };
  }

  // ── The applied build ──────────────────────────────────────────────────

  function emptyBuild() {
    return {
      dome: { design: '', variant: '' },
      body: { design: '', variant: '' },
      fitted: [],
    };
  }

  function snapshot(build) {
    const fitted = Array.from(build.fitted);
    return Object.freeze({
      dome: Object.freeze({ ...build.dome }),
      body: Object.freeze({ ...build.body }),
      fitted: Object.freeze(fitted),
    });
  }

  function publish(build) {
    applied = snapshot(build);
    listeners.forEach((listener) => {
      try {
        listener(applied);
      } catch (_error) {
        // One surface throwing must not stop the others being told.
      }
    });
  }

  /**
   * Apply a Droid Build, and tell every surface.
   *
   * @param {object} next - any of domeDesign, domeVariant, bodyDesign,
   *   bodyVariant, and `fitted` (an array of Part ids that REPLACES the set).
   *   A half that is not named is left exactly as it stood.
   * @param {object} [options]
   * @param {boolean} [options.seed=true] - fit the complement of a half whose
   *   design or variant this call changes. False is the boot re-apply: the
   *   device already holds the Fitted Parts, and re-seeding there would put
   *   back a Part the builder had dropped.
   * @param {boolean} [options.persist=true] - write the result to the device.
   * @returns {Promise<{build: object, seeded: string[], unknownComplement: string[], persisted: boolean}>}
   */
  function applyDroidBuild(next, options) {
    const request = next || {};
    const opts = options || {};
    const seed = opts.seed !== false;
    const persist = opts.persist !== false;

    const build = applied
      ? { dome: { ...applied.dome }, body: { ...applied.body }, fitted: Array.from(applied.fitted) }
      : emptyBuild();

    if (Array.isArray(request.fitted)) {
      build.fitted = Array.from(request.fitted);
    }
    const fitted = new Set(build.fitted);

    const changedHalves = [];
    Object.keys(HALVES).forEach((half) => {
      const design = request[HALVES[half].designField];
      const variant = request[HALVES[half].variantField];
      if (design === undefined && variant === undefined) {
        return;
      }
      const nextDesign = design === undefined ? build[half].design : String(design);
      const nextVariant = variant === undefined ? build[half].variant : String(variant);
      const moved = nextDesign !== build[half].design || nextVariant !== build[half].variant;
      build[half] = { design: nextDesign, variant: nextVariant };
      if (moved) {
        changedHalves.push(half);
      }
    });

    const seeded = [];
    const unknownComplement = [];
    if (seed) {
      changedHalves.forEach((half) => {
        const complement = complementFor(build[half].design, build[half].variant, half);
        if (!complement.known) {
          // Reported, never drawn as empty: this catalog does not record what
          // that design and variant carry, and seeding nothing silently is the
          // one outcome the decision behind this file rules out.
          unknownComplement.push(half);
          return;
        }
        complement.ids.forEach((id) => {
          // Seeds, never fences: this only ever adds. There is deliberately no
          // branch here that removes a Part the previous design carried.
          if (!fitted.has(id)) {
            fitted.add(id);
            seeded.push(id);
          }
        });
      });
    }

    build.fitted = parts()
      .map((part) => part.id)
      .filter((id) => fitted.has(id));

    publish(build);

    if (!persist) {
      return Promise.resolve({ build: applied, seeded, unknownComplement, persisted: false });
    }
    return persistBuild(build, changedHalves, Array.isArray(request.fitted) || seeded.length > 0)
      .then((persisted) => ({ build: applied, seeded, unknownComplement, persisted }));
  }

  /**
   * Write the applied build to the device.
   *
   * Only the halves this call moved are sent: a builder changing their Dome
   * Design is saying nothing about their body, and the device leaves a half it
   * was not told about alone. The Fitted Parts go whole, because they are a
   * set and there is no partial form of one.
   */
  function persistBuild(build, changedHalves, fittedMoved) {
    const form = {};
    changedHalves.forEach((half) => {
      form[HALVES[half].designField] = build[half].design;
      form[HALVES[half].variantField] = build[half].variant;
    });
    if (fittedMoved) {
      form.fittedParts = build.fitted.join(',');
    }
    if (Object.keys(form).length === 0) {
      return Promise.resolve(false);
    }
    if (!window.PAApi || !window.PAApi.postForm) {
      return Promise.resolve(false);
    }
    return window.PAApi.postForm('/api/config', form, { timeoutMs: PERSIST_TIMEOUT_MS })
      .then((result) => Boolean(result && result.ok))
      .catch(() => false);
  }

  /**
   * The Droid Build carried by a GET /api/config payload, in the shape
   * applyDroidBuild() takes. Returns null when the payload carries none, which
   * is an older firmware rather than an empty droid.
   */
  function fromConfig(config) {
    const stored = config && config.droidBuild;
    if (!stored) {
      return null;
    }
    return {
      domeDesign: stored.domeDesign || '',
      domeVariant: stored.domeVariant || '',
      bodyDesign: stored.bodyDesign || '',
      bodyVariant: stored.bodyVariant || '',
      fitted: Array.isArray(stored.fitted) ? stored.fitted : [],
    };
  }

  /**
   * The boot re-apply: adopt the answer the device holds.
   *
   * Deliberately does NOT seed. The device's answer already includes whatever
   * its design seeded - a fresh controller comes up on the pre-selected design
   * with that design's complement fitted - so seeding again here would put
   * back every Part the builder had dropped since.
   */
  function adopt(config) {
    const stored = fromConfig(config);
    if (!stored) {
      return null;
    }
    applyDroidBuild(stored, { seed: false, persist: false });
    return applied;
  }

  // The boot re-apply, held so a page pays for it once. Several surfaces want
  // the Droid Build and each of them calls load(); the device sheds
  // connections under load, so the second caller joins the first request
  // rather than opening another.
  let loading = null;

  /**
   * Fetch the Droid Build and re-apply it: the boot re-apply.
   *
   * A page holding a config payload already calls adopt() with it instead and
   * spends no request at all.
   *
   * @param {object} [options]
   * @param {boolean} [options.refresh=false] - read the device again even if
   *   this page has already asked once.
   * @returns {Promise<object|null>} the applied build, or null if it could not
   *   be read - a surface renders from whatever it had rather than blocking.
   */
  function load(options) {
    if (loading && !(options && options.refresh)) {
      return loading;
    }
    if (!window.PAApi || !window.PAApi.get) {
      return Promise.resolve(null);
    }
    loading = window.PAApi.get('/api/config', { timeoutMs: PERSIST_TIMEOUT_MS })
      .then((result) => (result && result.ok ? adopt(result.data) : null))
      .catch(() => null);
    return loading;
  }

  function current() {
    return applied;
  }

  function isFitted(id) {
    return Boolean(applied && applied.fitted.indexOf(id) !== -1);
  }

  function onChange(listener) {
    if (typeof listener !== 'function') {
      return () => {};
    }
    listeners.add(listener);
    return () => listeners.delete(listener);
  }

  window.DroidBuild = {
    applyDroidBuild,
    adopt,
    load,
    current,
    isFitted,
    onChange,
    complementFor,
    halfOf,
    fromConfig,
  };
})();

// =============================================================================
// data/feature_availability.js
//
// Feature Availability: how a surface reads the identity manifest to say
// whether a component, or a panel that exists only in some builds, can run on
// this controller.
//
// Two surfaces ask it: Configuration, for every component row, and
// Maintenance, for the Memory Profiler. That is why it is a file of its own
// rather than the top of either one - the two used to share it by sitting in
// one page, and a second copy of the resolver would be a second answer to the
// same question to keep in step (#404).
//
// It owns the feed as well as the resolver. The shell fetches the manifest once
// per session, caches it in window.PAIdentity and replays its outcome to every
// surface it mounts, so listening here, once, is what keeps the two surfaces
// from each writing the same state. The shell loads each script once per
// session, so this runs once however many surfaces load it.
// =============================================================================
(() => {
  const createFeatureAvailability = () => {
    const listeners = new Set();
    let phase = "loading";
    // Identity manifest is fetched once by shell.js at page load and cached in window.PAIdentity.
    // Do not restore per-card endpoint probing; the resolve() function reads this cache only.
    let identity = null;
    let identityErrorReason = null;  // "incompatible" or "no-response" when phase === "error"
    const STATE_LABELS = Object.freeze({
      on: "On",
      off: "Off",
      "not-in-this-build": "Not included",
      "not-on-this-board": "Not on this board",
      checking: "Checking",
      "identity-unavailable": "Availability unknown",
      "included": "Included",
    });

    // Resolve the compile-time manifest tiers first (board capability, build flag),
    // then the optional runtime toggle (Component Toggle). Compile tiers resolve
    // before runtime state to preserve the reason: "not in this build" or "not on
    // this board" takes precedence over "off". Component rows and build-conditional
    // panels call this same seam.
    //
    // Layer 2 validation: per-key completeness uses Object.hasOwn — not optional
    // chaining, not the `in` operator — so a MISSING key is distinguished from a
    // false value. The resolver already knows the key it was asked for, so there is
    // no JavaScript mirror of the .inc manifests to drift out of date.
    //
    // A key missing from an already-validated manifest is TERMINALLY unknown: the
    // fetch has completed and no later request will supply it. That is phase="failed"
    // with state="identity-unavailable", which renders as "Availability unknown" —
    // never phase="checking", which would tell the operator the page is still working
    // on an answer that will never arrive.
    const resolve = ({ boardCapability = "", buildFlag = "", enabled = true, hasToggle = true } = {}) => {
      const needsManifest = Boolean(boardCapability || buildFlag);
      if (needsManifest && phase !== "ready") {
        return phase === "error"
          ? { phase: "failed", state: "identity-unavailable" }
          : { phase: "checking", state: "checking" };
      }
      if (boardCapability) {
        if (!identity?.board_capabilities || !Object.hasOwn(identity.board_capabilities, boardCapability)) {
          // Missing key in board_capabilities is terminally unknown (will not arrive in future fetch)
          return { phase: "failed", state: "identity-unavailable" };
        }
        if (identity.board_capabilities[boardCapability] !== true) {
          return { phase: "ready", state: "not-on-this-board" };
        }
      }
      if (buildFlag) {
        if (!identity?.build_flags || !Object.hasOwn(identity.build_flags, buildFlag)) {
          // Missing key in build_flags is terminally unknown (will not arrive in future fetch)
          return { phase: "failed", state: "identity-unavailable" };
        }
        if (identity.build_flags[buildFlag] !== true) {
          return { phase: "ready", state: "not-in-this-build" };
        }
      }
      if (!hasToggle && enabled) {
        return { phase: "ready", state: "included" };
      }
      return enabled
        ? { phase: "ready", state: "on" }
        : { phase: "ready", state: "off" };
    };

    // Helper to derive control availability from resolved state.
    // Control is interactable when the manifest is ready and the feature is not gated.
    const isFeatureAvailable = (result) => {
      return result.phase === "ready" && result.state !== "not-on-this-board" && result.state !== "not-in-this-build";
    };


    const labelFor = (state) => STATE_LABELS[state] || "Availability unknown";

    // Where a builder goes about a "no" (#348). One entry per state that HAS a
    // next move, and nothing for the states that do not: an Availability Family
    // decides whether there is a destination at all, so the absence here is the
    // answer rather than a gap (CONTEXT.md "Availability Family").
    //
    //   off                 change it here - the Component Toggle is a control
    //                       the builder owns, on Configuration
    //   not-in-this-build   change it elsewhere - a different image carries it,
    //                       and Firmware is where one is uploaded
    //
    // A SETTLED NO HAS NO ROW. `not-on-this-board` is the board the builder
    // already owns and this image already runs on; a manifest this page cannot
    // read will not read differently on a retry. Neither carries a date, a
    // version or another product to go and buy - pointing a builder at hardware
    // is a Builder Recommendation, and a card reporting this droid's state does
    // not make one. `checking` has no row either: it is still finding out, and
    // asks nothing of anybody.
    const ROUTES = Object.freeze({
      off: Object.freeze({ href: "#configuration", label: "Switch it on in Configuration" }),
      // The same words the Component Picker's own other-board card already
      // uses for this destination (data/component_picker.js OTHER_BOARD), so
      // one place to go is named one way wherever a surface points at it.
      "not-in-this-build": Object.freeze({ href: "#firmware", label: "Open Firmware" }),
    });

    /**
     * The route a resolved state offers, or null where the family has none.
     *
     * @returns {{href: string, label: string}|null}
     */
    const routeFor = (state) => ROUTES[state] || null;

    // Turn a resolved state into the maker-facing explanation shown below a
    // feature. Component and profiler renderers share this copy policy.
    //
    // EVERY "NO" HERE NAMES AN ACT or says plainly that there is nothing to do.
    // The sentence and the route are separate, and this seam composes no
    // markup: it answers with vocabulary, and the surface that owns the element
    // appends the link (PAUtils.appendRoute, data/web_api.js). That split is
    // what lets a reason be read by a page, a pill or a test without any of
    // them needing a document - and it is the shape the reference uses for its
    // own findings, which carry fields and compose their sentence at render
    // (r2d2-astromech-simulator v1.79.0, lint.js:116).
    const reasonFor = (state, featureName, { on = "", notInThisBuild = "" } = {}) => {
      if (state === "on" || state === "included") return on;
      // The one state the builder chose and can unchoose. It had no branch at
      // all until #348, so a component switched off explained itself with
      // silence wherever a surface did not hand-write its own sentence.
      if (state === "off") return `${featureName} is switched off.`;
      if (state === "not-on-this-board") return `This Body Controller cannot run ${featureName}.`;
      if (state === "not-in-this-build") return notInThisBuild || `This firmware was loaded without ${featureName}.`;
      if (state === "checking") return `Checking ${featureName}…`;
      if (state === "identity-unavailable") {
        // Two different failures read as identity-unavailable; differ by reason:
        // - "no-response": transport failure, retryable, genuinely reconnecting
        // - "incompatible": validation failure, terminal, no reconnection coming
        if (identityErrorReason === "incompatible") {
          return `Could not check ${featureName}.`;
        }
        return `Could not check ${featureName}. Reconnecting…`;
      }
      return "";
    };

    /**
     * The same explanation as one line of plain text, with the route named in
     * words. For a sink that can only hold text - a feedback pill, a title
     * attribute - where a link cannot be appended. Where the destination can
     * actually be clicked, take reasonFor() and routeFor() and append a link.
     */
    const reasonLine = (state, featureName, options) => {
      const text = reasonFor(state, featureName, options);
      const route = text ? routeFor(state) : null;
      return route ? `${text} ${route.label}.` : text;
    };

    // The Availability Family a resolved state is painted in (CONTEXT.md
    // "Availability Family"), where the state class alone cannot say it. An
    // identity that could not be read is two different answers: a controller
    // that did not respond is retryable and still being found out, and one
    // that answered with a manifest this page cannot read is terminal - no
    // later request will change it - so it is settled, and drawn as settled.
    // So is a manifest that arrived without the key asked for (resolve()
    // above). The copy already told them apart (reasonFor); this is the same
    // split for the paint (#341, #369). Every other state's family is carried
    // by its own class, so this names none.
    const FAMILY_CLASSES = Object.freeze(["availability-finding-out", "availability-settled-no"]);
    const familyClassFor = (state) => {
      if (state === "checking") return "availability-finding-out";
      if (state === "identity-unavailable") {
        const terminal = phase === "ready" || identityErrorReason === "incompatible";
        return terminal ? "availability-settled-no" : "availability-finding-out";
      }
      return "";
    };

    const notify = () => listeners.forEach((listener) => listener());

    const setIdentity = (nextIdentity) => {
      identity = nextIdentity || null;
      phase = "ready";
      notify();
    };

    const setIdentityError = (reason = "no-response") => {
      identity = null;
      phase = "error";
      identityErrorReason = reason;
      notify();
    };

    const subscribe = (listener) => {
      listeners.add(listener);
      listener();
      return () => listeners.delete(listener);
    };

    return {
      resolve,
      isFeatureAvailable,
      labelFor,
      reasonFor,
      routeFor,
      reasonLine,
      familyClassFor,
      FAMILY_CLASSES,
      setIdentity,
      setIdentityError,
      subscribe,
    };
  };

  if (typeof window !== "undefined") {
    const availability = createFeatureAvailability();
    window.PAFeatureAvailability = availability;
    window.addEventListener("pa:identity-available", (event) => {
      availability.setIdentity(event.detail);
    });
    window.addEventListener("pa:identity-unavailable", (event) => {
      availability.setIdentityError(event.detail?.reason || "no-response");
    });
    // The manifest may have settled before this file ran; the cache is the
    // shell's answer to exactly that ordering.
    if (window.PAIdentity) availability.setIdentity(window.PAIdentity);
  }

  // A surface run on its own in a test gets a fresh one of these by require,
  // the way the Dashboard's tests take data/health_signals.js.
  if (typeof module !== "undefined" && module.exports) {
    module.exports = { createFeatureAvailability };
  }
})();

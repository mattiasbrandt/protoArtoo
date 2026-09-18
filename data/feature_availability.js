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
      checking: "Checking controller",
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

    // Turn a resolved state into the maker-facing explanation shown below a
    // feature. Component and profiler renderers share this copy policy.
    const reasonFor = (state, featureName, { on = "", notInThisBuild = "" } = {}) => {
      if (state === "on" || state === "included") return on;
      if (state === "not-on-this-board") return `This controller board cannot run ${featureName}.`;
      if (state === "not-in-this-build") return notInThisBuild || `This controller was loaded without ${featureName}.`;
      if (state === "checking") return `Checking whether this controller can run ${featureName}…`;
      if (state === "identity-unavailable") {
        // Two different failures read as identity-unavailable; differ by reason:
        // - "no-response": transport failure, retryable, genuinely reconnecting
        // - "incompatible": validation failure, terminal, no reconnection coming
        if (identityErrorReason === "incompatible") {
          return `Could not check ${featureName}. The controller did not report its features.`;
        }
        return `Could not check ${featureName}. Reconnecting to the controller…`;
      }
      return "";
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

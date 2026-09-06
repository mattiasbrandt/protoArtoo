// =============================================================================
// data/shell.js
//
// Shared topbar/nav/status shell renderer for all web pages.
// Page selects shell configuration via <body data-page="...">.
// The status bar carries firmware/filesystem versions only; footer.js fills it.
// =============================================================================
(() => {
  const page = document.body?.dataset?.page || "home";

  const PAGE_CONFIG = {
    home: { title: "{name} - Dashboard" },
    drive: { title: "Drive - {name}" },
    dome: { title: "Dome - {name}" },
    sound: { title: "{name} - Sound" },
    servo: { title: "Servos - {name}" },
    rc: { title: "RC Control - {name}" },
    setup: { title: "Setup - {name}" },
    wifi: { title: "WiFi - {name}" },
    firmware: { title: "Firmware - {name}" },
    seq: { title: "Sequences - {name}" },
  };

  const NAV = [
    { key: "home", href: "/", label: "🏠 Home" },
    { key: "drive", href: "/drive.html", label: "🏎️ Drive" },
    { key: "dome", href: "/dome.html", label: "🔄 Dome" },
    { key: "sound", href: "/sound.html", label: "🔊 Sound" },
    { key: "servo", href: "/servo.html", label: "🦾 Servos" },
    { key: "seq", href: "/seq.html", label: "🎬 Sequences" },
    { key: "rc", href: "/rc.html", label: "🕹️ RC" },
    { key: "setup", href: "/setup.html", label: "⚙️ Setup" },
    { key: "wifi", href: "/wifi.html", label: "📶 WiFi" },
    { key: "firmware", href: "/firmware.html", label: "💾 Firmware" },
  ];

  const cfg = PAGE_CONFIG[page] || PAGE_CONFIG.home;
  const applyIdentityName = (name) => {
    const droidName = String(name || "protoartoo");
    document.title = cfg.title.replace("{name}", droidName);
    document.querySelectorAll("[data-identity-name]").forEach((el) => {
      el.textContent = droidName;
    });
  };
  applyIdentityName("protoartoo");

  // Layer 1 validation: ensure the identity manifest conforms to the expected shape
  // before it reaches feature availability resolvers. Protects against null, non-objects,
  // and responses missing required structure. Returns null if validation fails, otherwise
  // returns the validated identity.
  const validateIdentityShape = (identity) => {
    // Identity must be an object
    if (!identity || typeof identity !== "object" || Array.isArray(identity)) {
      return null;
    }
    // board must be a string
    if (typeof identity.board !== "string") {
      return null;
    }
    // board_capabilities and build_flags must be objects if present
    if (!identity.board_capabilities || (typeof identity.board_capabilities !== "object" || Array.isArray(identity.board_capabilities) || identity.board_capabilities === null)) {
      return null;
    }
    if (!identity.build_flags || (typeof identity.build_flags !== "object" || Array.isArray(identity.build_flags) || identity.build_flags === null)) {
      return null;
    }
    // Every present value in board_capabilities and build_flags must be a boolean
    for (const value of Object.values(identity.board_capabilities)) {
      if (typeof value !== "boolean") {
        return null;
      }
    }
    for (const value of Object.values(identity.build_flags)) {
      if (typeof value !== "boolean") {
        return null;
      }
    }
    return identity;
  };

  // Publish the shell's once-per-page identity result for feature consumers.
  // Identity is fetched once at page load and cached in window.PAIdentity.
  // Feature availability resolution reads this cache only and never probes endpoints
  // to discover capabilities — the manifest is authoritative and must not be rediscovered.
  // Setup listens to the event, while the cache closes late-load ordering gaps.
  // Layer 1 validation ensures the manifest conforms to the expected shape before
  // pa:identity-available is published; invalid manifests are treated as unavailable.
  const publishIdentity = (identity) => {
    const validatedIdentity = validateIdentityShape(identity);
    window.PAIdentity = validatedIdentity;
    if (typeof window.dispatchEvent === "function") {
      if (validatedIdentity) {
        window.dispatchEvent(new CustomEvent("pa:identity-available", { detail: validatedIdentity }));
      } else {
        // Invalid manifest shape is treated as unavailable
        window.dispatchEvent(new CustomEvent("pa:identity-unavailable", { detail: { error: "invalid manifest", reason: "incompatible" } }));
      }
    }
    return validatedIdentity;
  };


  window.PAUi = window.PAUi || {};
  if (typeof window.PAUi.setupActionText !== "function") {
    window.PAUi.setupActionText = (action) => `${action} in Setup`;
  }
  if (typeof window.PAUi.setupActionHtml !== "function") {
    window.PAUi.setupActionHtml = (action) => `${action} in <a class="setup-link" href="/setup.html">Setup</a>`;
  }
  const shellTop = document.getElementById("shell-top");
  if (shellTop) {
    const navHtml = NAV.map((item) =>
      `<a href="${item.href}"${item.key === page ? ' class="active"' : ""}>${item.label}</a>`
    ).join("");
    const topbarActionsTemplate = document.getElementById("topbar-actions-template");
    const topbarActionsHtml = topbarActionsTemplate?.innerHTML?.trim() || "";

    shellTop.innerHTML = `
      <div class="topbar">
        <a href="/" class="topbar-brand">
          <img src="/r2d2body.svg" alt="R2-D2 body icon" class="topbar-logo">
          <div>
            <h1 data-identity-name>protoartoo</h1>
            <div class="subtitle">R2-D2 Body Controller</div>
          </div>
        </a>
        <div class="topbar-actions" id="shell-top-actions">${topbarActionsHtml}</div>
      </div>
      <nav>
        ${navHtml}
      </nav>
    `;
  }

  const shellStatus = document.getElementById("shell-status");
  if (shellStatus) {
    shellStatus.innerHTML = `
      <div class="status-bar" id="conn-status">
        <div class="status-subline" id="fw-meta">Loading firmware info...</div>
      </div>
    `;
  }

  const loadIdentity = async ({ handle = null } = {}) => {
    let result;
    try {
      const api = handle || window.PAApi;
      result = api
        ? await api.get("/api/identity")
        : { data: await fetch("/api/identity", { cache: "no-store" }).then((r) => r.json()) };
    } catch (error) {
      // Transport failure: retryable. Unchanged behaviour.
      console.warn("[shell] identity unavailable:", error);
      if (typeof window.dispatchEvent === "function") {
        window.dispatchEvent(new CustomEvent("pa:identity-unavailable", { detail: { error, reason: "no-response" } }));
      }
      throw error;
    }

    applyIdentityName(result.data?.droidName);

    // publishIdentity is the single validation boundary and has already
    // dispatched pa:identity-unavailable if the manifest is unusable.
    if (!publishIdentity(result.data)) {
      const error = new Error("identity manifest failed validation");
      error.kind = "incompatible";
      error.status = 200;   // the response was a valid 2xx; its content was not
      throw error;          // terminal: the bootstrap maps this to failed-terminal
    }
  };

  window.addEventListener("pa:identity-updated", (event) => {
    applyIdentityName(event.detail?.droidName);
    publishIdentity(event.detail);
  });

  // Register identity load with the bootstrap if available; otherwise run it directly.
  // This ensures the identity request is routed through the bootstrap's single-slot
  // recovery mechanism rather than competing with other startup GETs.
  if (window.PABootstrap) {
    window.PABootstrap.registerSection("shell-identity", loadIdentity, {
      label: "droid identity",
    });
  } else {
    loadIdentity();
  }
})();

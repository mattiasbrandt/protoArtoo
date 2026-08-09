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

  const loadIdentity = async ({ signal = null } = {}) => {
    try {
      const result = window.PAApi
        ? await window.PAApi.get("/api/identity", { timeoutMs: 3000, signal })
        : { data: await fetch("/api/identity", { cache: "no-store", signal }).then((r) => r.json()) };
      applyIdentityName(result.data?.droidName);
    } catch (error) {
      console.warn("[shell] identity unavailable:", error);
      // Rethrow so the bootstrap can show recovery and retry the request
      throw error;
    }
  };

  window.addEventListener("pa:identity-updated", (event) => {
    applyIdentityName(event.detail?.droidName);
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

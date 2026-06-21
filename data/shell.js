// =============================================================================
// data/shell.js
//
// Shared topbar/nav/status shell renderer for all web pages.
// Page selects shell configuration via <body data-page="...">.
// =============================================================================
(() => {
  const page = document.body?.dataset?.page || "home";

  const PAGE_CONFIG = {
    home: {
      title: "{name} - Dashboard",
      statusDot: "yellow",
      statusText: "",
      connectedText: "",
    },
    drive: {
      title: "Drive - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Drive page connected",
    },
    dome: {
      title: "Dome - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Dome page connected",
    },
    sound: {
      title: "{name} - Sound",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Sound page connected",
    },
    servo: {
      title: "Servos - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Servos page connected",
    },
    rc: {
      title: "RC Control - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "RC page connected",
    },
    setup: {
      title: "Setup - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Setup page connected",
    },
    wifi: {
      title: "WiFi - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "WiFi page connected",
    },
    firmware: {
      title: "Firmware - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Firmware page connected",
    },
    seq: {
      title: "Sequences - {name}",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Sequences page connected",
    },
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
        <div><span class="dot ${cfg.statusDot}"></span>${cfg.statusText}</div>
        <div class="status-subline" id="fw-meta">Loading firmware info...</div>
        ${cfg.extraStatus || ""}
      </div>
    `;
  }

  if (window.PAStatusStream?.isSupported()) {
    const connStatus = document.getElementById('conn-status');
    const textEl = connStatus?.querySelector('div');

    window.PAStatusStream.subscribe((eventType) => {
      if (!connStatus) return;
      if (eventType === 'status') {
        if (textEl) { textEl.innerHTML = `<span class="dot green"></span>${cfg.connectedText || cfg.statusText}`; }
      } else if (eventType === 'stream_error') {
        if (textEl) { textEl.innerHTML = `<span class="dot warn"></span>Connection lost — retrying…`; }
      }
    });
  }

  const loadIdentity = async () => {
    try {
      const result = window.PAApi
        ? await window.PAApi.get("/api/identity", { timeoutMs: 3000 })
        : { data: await fetch("/api/identity", { cache: "no-store" }).then((r) => r.json()) };
      applyIdentityName(result.data?.droidName);
    } catch (error) {
      console.warn("[shell] identity unavailable:", error);
    }
  };

  window.addEventListener("pa:identity-updated", (event) => {
    applyIdentityName(event.detail?.droidName);
  });
  loadIdentity();
})();

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
      title: "protoArtoo - Dashboard",
      statusDot: "yellow",
      statusText: "Dashboard connected",
    },
    drive: {
      title: "Drive - protoArtoo",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Drive page connected",
    dome: {
      title: "Dome - protoArtoo",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Dome page connected",
    sound: {
      title: "protoArtoo - Sound",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Sound page connected",
    servo: {
      title: "Servos - protoArtoo",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Servos page connected",
    rc: {
      title: "RC Control - protoArtoo",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "RC page connected",
    setup: {
      title: "Setup - protoArtoo",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Setup page connected",
    wifi: {
      title: "WiFi - protoArtoo",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "WiFi page connected",
    firmware: {
      title: "Firmware - protoArtoo",
      statusDot: "yellow",
      statusText: "Connecting…",
      connectedText: "Firmware page connected",
  };

  const NAV = [
    { key: "home", href: "/", label: "🏠 Home" },
    { key: "drive", href: "/drive.html", label: "🏎️ Drive" },
    { key: "dome", href: "/dome.html", label: "🔄 Dome" },
    { key: "sound", href: "/sound.html", label: "🔊 Sound" },
    { key: "servo", href: "/servo.html", label: "🦾 Servos" },
    { key: "rc", href: "/rc.html", label: "🕹️ RC" },
    { key: "setup", href: "/setup.html", label: "⚙️ Setup" },
    { key: "wifi", href: "/wifi.html", label: "📶 WiFi" },
    { key: "firmware", href: "/firmware.html", label: "💾 Firmware" },
  ];

  const cfg = PAGE_CONFIG[page] || PAGE_CONFIG.home;
  document.title = cfg.title;

  const shellTop = document.getElementById("shell-top");
  if (shellTop) {
    const navHtml = NAV.map((item) =>
      `<a href="${item.href}"${item.key === page ? ' class="active"' : ""}>${item.label}</a>`
    ).join("");

    shellTop.innerHTML = `
      <div class="topbar">
        <a href="/" class="topbar-brand">
          <img src="/r2d2body.svg" alt="R2-D2 body icon" class="topbar-logo">
          <div>
            <h1>protoArtoo</h1>
            <div class="subtitle">R2-D2 Body Controller</div>
          </div>
        </a>
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
    const dotEl = connStatus?.querySelector('.dot');
    const textEl = connStatus?.querySelector('div');

    window.PAStatusStream.subscribe((eventType) => {
      if (!connStatus) return;
      if (eventType === 'status') {
        if (dotEl) { dotEl.className = 'dot green'; }
        if (textEl) { textEl.innerHTML = `<span class="dot green"></span>${cfg.connectedText || cfg.statusText}`; }
      } else if (eventType === 'stream_error') {
        if (textEl) { textEl.innerHTML = `<span class="dot warn"></span>Connection lost — retrying…`; }
      }
    });
  }
})();

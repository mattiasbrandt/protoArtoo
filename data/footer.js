(() => {
  const footer = document.getElementById("fw-meta");

  if (!footer) {
    return;
  }

  const formatUptime = (uptimeMs) => {
    const totalSeconds = Math.floor(Number(uptimeMs || 0) / 1000);
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    return `${hours}h ${minutes}m ${seconds}s`;
  };

  const loadFooter = async () => {
    try {
      const response = await fetch("/api/status", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const payload = await response.json();
      footer.innerHTML = `Build: <span class="mono">${payload.firmwareVersion}</span><br>Uptime: ${formatUptime(payload.uptimeMs)}`;
    } catch (_error) {
      footer.textContent = "Firmware info unavailable";
    }
  };

  loadFooter();
})();

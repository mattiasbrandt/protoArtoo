(() => {
  const liveTransport = document.getElementById("live-transport");
  const reloadButton = document.getElementById("reload-serial-button");
  const feedback = document.getElementById("serial-feedback");

  if (!liveTransport || !reloadButton || !feedback) {
    return;
  }

  const renderSection = (title, payload) => {
    const stateLabel = payload.active ? "Active" : "Not active";
    const requirement = payload.hardwareRequired ? "Full hardware required" : "Available now";
    const metrics = [];

    if (typeof payload.heartbeatRx === "number") {
      metrics.push(`Heartbeat RX: ${payload.heartbeatRx}`);
    }
    if (typeof payload.heartbeatTx === "number") {
      metrics.push(`Heartbeat TX: ${payload.heartbeatTx}`);
    }

    return `
      <div class="status-item">
        <dt>${title}</dt>
        <dd>${stateLabel}</dd>
        <div class="list-note">${requirement}</div>
        <div class="list-note">${payload.note}</div>
        ${metrics.map((item) => `<div class="list-note">${item}</div>`).join("")}
      </div>
    `;
  };

  const renderTransport = (payload) => {
    liveTransport.innerHTML = [
      renderSection(`${payload.debug.label} - ${payload.debug.name}`, payload.debug),
      renderSection(`${payload.hoverboard.label} - ${payload.hoverboard.name}`, payload.hoverboard),
      renderSection(`${payload.sound.label} - ${payload.sound.name}`, payload.sound),
      renderSection(`${payload.dome.label} - ${payload.dome.name}`, payload.dome),
    ].join("");
    feedback.textContent = `Loaded at ${new Date().toLocaleTimeString()}`;
  };

  const loadTransport = async () => {
    feedback.textContent = "Loading live transport status…";

    try {
      const response = await fetch("/api/serial", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      renderTransport(await response.json());
    } catch (_error) {
      feedback.textContent = "Failed to load live transport status";
    }
  };

  reloadButton.addEventListener("click", () => {
    loadTransport();
  });

  loadTransport();
})();

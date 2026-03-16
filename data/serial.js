(() => {
  const reloadButton = document.getElementById("reload-serial-button");
  const feedback = document.getElementById("serial-feedback");
  const s1Status = document.getElementById("s1-status");
  const s2Status = document.getElementById("s2-status");
  const s3Status = document.getElementById("s3-status");

  const loadSerialStatus = async () => {
    if (feedback) {
      feedback.textContent = "Loading connection status...";
      feedback.className = "feedback";
    }

    try {
      const response = await fetch("/api/status", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }

      const data = await response.json();
      
      if (s1Status) {
        s1Status.textContent = data.s1Hoverboard?.state === "connected" ? "✅ Connected" : "⏸️ Disconnected";
      }
      if (s2Status) {
        s2Status.textContent = data.s2Sound?.state === "connected" ? "✅ Connected" : "⏸️ Disconnected";
      }
      if (s3Status) {
        s3Status.textContent = data.s3DomeCtrl?.state === "connected" ? "✅ Connected" : "⏸️ Disconnected";
      }
      
      if (feedback) {
        feedback.textContent = `Status loaded at ${new Date().toLocaleTimeString()}`;
        feedback.className = "feedback success";
      }
    } catch (error) {
      if (feedback) {
        feedback.textContent = error instanceof Error ? error.message : "Failed to load status";
        feedback.className = "feedback error";
      }
    }
  };

  if (reloadButton) {
    reloadButton.addEventListener("click", loadSerialStatus);
  }

  loadSerialStatus();
})();

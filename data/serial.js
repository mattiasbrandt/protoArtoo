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
      
      // S1 — hoverboard: present in status when enabled; state is "idle"/"commanding"
      if (s1Status) {
        if (!data.s1Hoverboard) {
          s1Status.textContent = "⏸️ Disabled";
        } else {
          s1Status.textContent = data.s1Hoverboard.state === "commanding"
            ? "✅ Active" : "✅ Enabled / Idle";
        }
      }

      // S2 — audio: present when enabled; state is "idle" or "playing"
      if (s2Status) {
        if (!data.s2Sound) {
          s2Status.textContent = "⏸️ Disabled";
        } else {
          s2Status.textContent = data.s2Sound.state === "playing"
            ? "🔊 Playing" : "✅ Enabled / Idle";
        }
      }

      // S3 — dome link: use top-level dome_link block (always present)
      if (s3Status) {
        const dl = data.dome_link;
        if (!dl || dl.state === "disabled") {
          s3Status.textContent = "⏸️ Disabled";
        } else if (dl.state === "connected") {
          s3Status.textContent = `✅ Connected (hb rx ${dl.hb_rx} / tx ${dl.hb_tx})`;
        } else if (dl.state === "lost") {
          s3Status.textContent = `❌ Lost — last seen ${dl.last_rx_ms} ms ago`;
        } else {
          s3Status.textContent = "⚠️ Not seen — waiting for dome heartbeat";
        }
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

(() => {
  const fileInput = document.getElementById("fw-file");
  const uploadButton = document.getElementById("upload-fw-button");
  const rebootButton = document.getElementById("reboot-button");
  const progressWrap = document.getElementById("fw-progress");
  const progressBar = document.getElementById("fw-bar");
  const progressStatus = document.getElementById("fw-status");
  const feedback = document.getElementById("fw-feedback");

  if (!fileInput || !uploadButton || !rebootButton || !progressWrap || !progressBar || !progressStatus || !feedback) {
    return;
  }

  let uploadInProgress = false;

  const setUploadBusy = (busy) => {
    uploadInProgress = busy;
    uploadButton.disabled = busy;
    rebootButton.disabled = busy;
    if (uploadFsButton) uploadFsButton.disabled = busy;
    if (uploadWmButton) uploadWmButton.disabled = busy || (uploadWmButton.dataset.wmLocked === "1");
  };

  const flashSuccess = sessionStorage.getItem('ota_flash_success');
  if (flashSuccess) {
    sessionStorage.removeItem('ota_flash_success');
    const label = flashSuccess === 'filesystem' ? 'Filesystem' : 'Firmware';
    feedback.textContent = `${label} updated successfully.`;
    feedback.classList.add('success');
  }

  const waitForReconnect = (feedbackEl, statusEl, flashType, onTimeout) => {
    let attempts = 0;
    const MAX_ATTEMPTS = 30;
    const POLL_MS = 2000;
    const INITIAL_DELAY_MS = 4000;

    const poll = () => {
      attempts++;
      if (statusEl) statusEl.textContent = `Reconnecting… (${MAX_ATTEMPTS - attempts} attempts left)`;

      const controller = new AbortController();
      const timeoutId = window.setTimeout(() => controller.abort(), 2000);

      fetch("/api/status", { method: "GET", signal: controller.signal })
        .then((r) => {
          window.clearTimeout(timeoutId);
          if (r.ok) {
            if (statusEl) statusEl.textContent = "Device is back online.";
            if (feedbackEl) feedbackEl.textContent = "Upload complete — reloading page.";
            sessionStorage.setItem('ota_flash_success', flashType || 'firmware');
            window.setTimeout(() => window.location.reload(), 1200);
          } else {
            schedule();
          }
        })
        .catch(() => {
          window.clearTimeout(timeoutId);
          schedule();
        });
    };

    const schedule = () => {
      if (attempts >= MAX_ATTEMPTS) {
        if (statusEl) statusEl.textContent = "Device did not reconnect. Refresh manually.";
        if (typeof onTimeout === "function") onTimeout();
        return;
      }
      window.setTimeout(poll, POLL_MS);
    };

    if (statusEl) statusEl.textContent = "Waiting for device to reboot…";
    window.setTimeout(poll, INITIAL_DELAY_MS);
  };

  const postReboot = async () => {
    if (!window.PAApi) {
      feedback.textContent = "API helper unavailable";
      return;
    }
    if (uploadInProgress) {
      feedback.textContent = "Upload in progress — reboot is temporarily blocked.";
      return;
    }
    feedback.textContent = "Requesting reboot...";
    try {
      await window.PAApi.postForm("/api/reboot", {});
      feedback.textContent = "Reboot requested.";
    } catch (error) {
      feedback.textContent = `Reboot failed: ${window.PAApi.messageFor(error)}`;
    }
  };

  // Shared upload helper using fetch().
  // Note: Fetch does not provide upload progress tracking. To show progress,
  // we would need ReadableStream / Blob.stream() which adds complexity.
  // Instead, we show an indeterminate busy state and report completion/failure.
  const doUpload = async (url, formData, onSuccess, onFailure) => {
    try {
      const response = await fetch(url, {
        method: "POST",
        body: formData,
      });

      const contentType = response.headers.get("content-type") || "";
      let errorMessage = null;

      if (contentType.includes("application/json")) {
        try {
          const jsonData = await response.json();
          errorMessage = jsonData.error || null;
        } catch {
          // ignore json parse errors
        }
      }

      if (!response.ok) {
        errorMessage = errorMessage || `HTTP ${response.status}`;
        onFailure(errorMessage);
        return;
      }

      onSuccess();
    } catch (error) {
      onFailure(error.message || "Upload error");
    }
  };

  const uploadFirmware = () => {
    const file = fileInput.files && fileInput.files[0];
    if (!file) {
      feedback.textContent = "Select a .bin file first.";
      return;
    }
    if (uploadInProgress) {
      feedback.textContent = "Another upload is already in progress.";
      return;
    }

    const formData = new FormData();
    formData.append("firmware", file, file.name);
    if (!confirm("Upload firmware? Keep power connected during the update.")) {
      feedback.textContent = "Firmware upload canceled.";
      return;
    }

    progressWrap.classList.remove("hidden");
    progressBar.style.width = "50%";
    progressStatus.textContent = "Uploading...";
    feedback.className = "feedback mt-12";
    feedback.textContent = `Uploading ${file.name}...`;

    setUploadBusy(true);
    doUpload("/upload/firmware", formData, () => {
      progressBar.style.width = "100%";
      waitForReconnect(feedback, progressStatus, 'firmware', () => setUploadBusy(false));
    }, (errorMessage) => {
      setUploadBusy(false);
      progressStatus.textContent = "Upload failed";
      feedback.textContent = errorMessage || "Upload failed.";
      progressBar.style.width = "0%";
      progressWrap.classList.add("hidden");
    });
  };

  // Filesystem upload
  const fsFile = document.getElementById("fs-file");
  const uploadFsButton = document.getElementById("upload-fs-button");
  const uploadWmButton = document.getElementById("upload-wm-button");
  const fsProgressWrap = document.getElementById("fs-progress");
  const fsProgressBar = document.getElementById("fs-bar");
  const fsProgressStatus = document.getElementById("fs-status");

  const uploadFilesystem = () => {
    const file = fsFile && fsFile.files && fsFile.files[0];
    if (!file) {
      feedback.textContent = "Select a filesystem .bin file first.";
      return;
    }
    if (uploadInProgress) {
      feedback.textContent = "Another upload is already in progress.";
      return;
    }

    const formData = new FormData();
    formData.append("filesystem", file, file.name);
    if (!confirm("Upload filesystem? Keep power connected during the update.")) {
      feedback.textContent = "Filesystem upload canceled.";
      return;
    }

    if (fsProgressWrap) fsProgressWrap.classList.remove("hidden");
    if (fsProgressBar) fsProgressBar.style.width = "50%";
    if (fsProgressStatus) fsProgressStatus.textContent = "Uploading...";
    feedback.className = "feedback mt-12";
    feedback.textContent = `Uploading filesystem ${file.name}...`;

    setUploadBusy(true);
    doUpload("/upload/filesystem", formData, () => {
      if (fsProgressBar) fsProgressBar.style.width = "100%";
      waitForReconnect(feedback, fsProgressStatus, 'filesystem', () => setUploadBusy(false));
    }, (errorMessage) => {
      setUploadBusy(false);
      if (fsProgressStatus) fsProgressStatus.textContent = "Upload failed";
      feedback.textContent = errorMessage || "Filesystem upload error.";
      if (fsProgressBar) fsProgressBar.style.width = "0%";
      if (fsProgressWrap) fsProgressWrap.classList.add("hidden");
    });
  };

  uploadButton.addEventListener("click", uploadFirmware);
  rebootButton.addEventListener("click", postReboot);
  if (uploadFsButton) uploadFsButton.addEventListener("click", uploadFilesystem);

  // WiFi Module Update (#241). Feature Availability is PA_CAP_HOSTED_WIFI
  // from identity; updateSupport is discovered on /api/status.
  const wmCard = document.getElementById("wifi-module-card");
  const wmStatus = document.getElementById("wm-availability-status");
  const wmLamp = document.getElementById("wm-availability-lamp");
  const wmReason = document.getElementById("wm-availability-reason");
  const wmContent = document.getElementById("wm-content");
  const wmSupportLine = document.getElementById("wm-support-line");
  const wmVersionLine = document.getElementById("wm-version-line");
  const wmFile = document.getElementById("wm-file");
  const wmProgressWrap = document.getElementById("wm-progress");
  const wmProgressBar = document.getElementById("wm-bar");
  const wmProgressStatus = document.getElementById("wm-status");
  const wmFeedback = document.getElementById("wm-feedback");

  const FEATURE_STATE_CLASSES = [
    "feature-state-on",
    "feature-state-included",
    "feature-state-not-on-this-board",
    "feature-state-not-in-this-build",
    "feature-state-checking",
    "feature-state-identity-unavailable",
  ];

  const applyFeatureState = (el, state) => {
    if (!el) return;
    FEATURE_STATE_CLASSES.forEach((name) => el.classList.remove(name));
    el.classList.add(`feature-state-${state}`);
  };

  const setWmLocked = (locked) => {
    if (!uploadWmButton) return;
    uploadWmButton.dataset.wmLocked = locked ? "1" : "0";
    uploadWmButton.disabled = uploadInProgress || locked;
  };

  const makerErrorFor = (token) => {
    if (token === "wifi-module-link-not-ready") {
      return "The WiFi module is not on the bus. Nothing was written.";
    }
    if (token === "wifi-module-unknown") {
      return "The module was never asked — it is not on the bus. Nothing was written.";
    }
    if (token === "wifi-module-not-supported") {
      return "This module cannot take an update over the air. Nothing was written. A wired rewrite is the only route.";
    }
    if (token === "wifi-module-already-current") {
      return "The module already matches this controller's wireless software. Nothing was written.";
    }
    if (token === "wifi-module-begin-failed" || token === "wifi-module-write-failed" ||
        token === "wifi-module-end-failed" || token === "wifi-module-update-failed") {
      return "The update did not finish. The module is still running what it had.";
    }
    return token || "The WiFi module update did not finish.";
  };

  const renderSupport = (wifiModule) => {
    if (!wmSupportLine) return;
    if (!wifiModule || typeof wifiModule !== "object") {
      wmSupportLine.textContent = "Waiting for the module to report in.";
      if (wmVersionLine) wmVersionLine.textContent = "";
      setWmLocked(true);
      return;
    }
    const support = wifiModule.updateSupport;
    if (support === "unknown") {
      wmSupportLine.textContent =
        "The WiFi module is not answering. Check it is fitted and powered - this is not a cannot-update reading.";
      if (wmVersionLine) wmVersionLine.textContent = "";
      setWmLocked(true);
      return;
    }
    if (support === "not_supported") {
      wmSupportLine.textContent =
        "This module's software cannot take an update over the air. The only route is a wired rewrite.";
      if (wmVersionLine) wmVersionLine.textContent = "";
      setWmLocked(true);
      return;
    }
    if (support === "supported") {
      wmSupportLine.textContent = "This module can take new software over the air.";
      const version = wifiModule.version;
      const hostVersion = wifiModule.hostVersion;
      if (wmVersionLine) {
        const bits = [];
        if (typeof version === "string" && version.length > 0) {
          bits.push(`Module software ${version}.`);
        }
        if (typeof hostVersion === "string" && hostVersion.length > 0) {
          bits.push(`Target ${hostVersion}.`);
        }
        wmVersionLine.textContent = bits.join(" ");
      }
      setWmLocked(false);
      return;
    }
    wmSupportLine.textContent = "Waiting for the module to report in.";
    setWmLocked(true);
  };

  const renderAvailability = (identity) => {
    if (!wmCard) return;
    const caps = identity && identity.board_capabilities;
    let state = "checking";
    let reason = "Checking whether this controller has a WiFi module…";
    let available = false;
    if (!identity) {
      state = "identity-unavailable";
      reason = "Could not check WiFi module. Reconnecting to the controller…";
    } else if (!caps || !Object.hasOwn(caps, "PA_CAP_HOSTED_WIFI")) {
      state = "identity-unavailable";
      reason = "Could not check WiFi module. The controller did not report its features.";
    } else if (caps.PA_CAP_HOSTED_WIFI !== true) {
      state = "not-on-this-board";
      reason = "This controller board cannot run a WiFi module.";
    } else {
      state = "included";
      reason = "This board has a WiFi module. Whether it can take new software is a fact about the module, not the board.";
      available = true;
    }

    wmCard.hidden = false;
    applyFeatureState(wmCard, state);
    if (wmStatus) {
      wmStatus.textContent =
        state === "not-on-this-board" ? "Not on this board"
        : state === "included" ? "Included"
        : state === "checking" ? "Checking controller"
        : "Availability unknown";
      applyFeatureState(wmStatus, state);
    }
    if (wmLamp) applyFeatureState(wmLamp, state);
    if (wmReason) wmReason.textContent = reason;
    if (wmContent) {
      wmContent.inert = !available;
      wmContent.setAttribute("aria-hidden", available ? "false" : "true");
    }
    if (!available) {
      setWmLocked(true);
    } else if (typeof fetch === "function") {
      fetch("/api/status", { method: "GET", cache: "no-store" })
        .then((r) => (r.ok ? r.json() : null))
        .then((body) => {
          if (body) renderSupport(body.wifiModule);
        })
        .catch(() => {});
    }
  };

  const waitForModuleBack = (onDone) => {
    let attempts = 0;
    const MAX_ATTEMPTS = 30;
    const POLL_MS = 2000;
    const poll = () => {
      attempts += 1;
      if (wmProgressStatus) {
        wmProgressStatus.textContent =
          `Waiting for the WiFi module to come back… (${MAX_ATTEMPTS - attempts} left)`;
      }
      const controller = new AbortController();
      const timeoutId = window.setTimeout(() => controller.abort(), 2000);
      fetch("/api/status", { method: "GET", signal: controller.signal })
        .then((r) => {
          window.clearTimeout(timeoutId);
          if (!r.ok) {
            schedule();
            return null;
          }
          return r.json();
        })
        .then((body) => {
          if (!body) return;
          const wm = body.wifiModule;
          if (wm && (wm.updateSupport === "supported" || wm.updateSupport === "not_supported")) {
            if (wmProgressStatus) wmProgressStatus.textContent = "WiFi module is back.";
            renderSupport(wm);
            if (wmFeedback) {
              wmFeedback.textContent = "WiFi module software is in place. The controller stayed up.";
              wmFeedback.className = "feedback mt-12 success";
            }
            onDone();
            return;
          }
          schedule();
        })
        .catch(() => {
          window.clearTimeout(timeoutId);
          schedule();
        });
    };
    const schedule = () => {
      if (attempts >= MAX_ATTEMPTS) {
        if (wmProgressStatus) {
          wmProgressStatus.textContent =
            "The wireless link has not come back yet. Refresh this page — do not re-send unless you mean to.";
        }
        onDone();
        return;
      }
      window.setTimeout(poll, POLL_MS);
    };
    if (wmProgressStatus) {
      wmProgressStatus.textContent =
        "New software is on the module. The wireless link will drop while it restarts — this page stays here.";
    }
    window.setTimeout(poll, 4000);
  };

  const uploadWifiModule = () => {
    const file = wmFile && wmFile.files && wmFile.files[0];
    if (!file) {
      if (wmFeedback) wmFeedback.textContent = "Select a WiFi module .bin file first.";
      return;
    }
    if (uploadInProgress) {
      if (wmFeedback) wmFeedback.textContent = "Another upload is already in progress.";
      return;
    }
    const formData = new FormData();
    formData.append("wifiModule", file, file.name);
    if (!confirm("Send new software to the WiFi module? Keep power connected. The droid stays running; the wireless link will drop while the module restarts.")) {
      if (wmFeedback) wmFeedback.textContent = "WiFi module update canceled.";
      return;
    }
    if (wmProgressWrap) wmProgressWrap.classList.remove("hidden");
    if (wmProgressBar) wmProgressBar.style.width = "50%";
    if (wmProgressStatus) wmProgressStatus.textContent = "Sending…";
    if (wmFeedback) {
      wmFeedback.className = "feedback mt-12";
      wmFeedback.textContent = `Sending ${file.name} to the WiFi module…`;
    }
    setUploadBusy(true);
    doUpload("/upload/wifi-module", formData, () => {
      if (wmProgressBar) wmProgressBar.style.width = "100%";
      waitForModuleBack(() => setUploadBusy(false));
    }, (errorMessage) => {
      setUploadBusy(false);
      if (wmProgressStatus) wmProgressStatus.textContent = "Update failed";
      if (wmFeedback) wmFeedback.textContent = makerErrorFor(errorMessage);
      if (wmProgressBar) wmProgressBar.style.width = "0%";
      if (wmProgressWrap) wmProgressWrap.classList.add("hidden");
    });
  };

  if (wmCard) {
    renderAvailability(window.PAIdentity || null);
    window.addEventListener("pa:identity-available", (event) => {
      renderAvailability(event.detail || window.PAIdentity || null);
    });
    window.addEventListener("pa:identity-unavailable", () => {
      renderAvailability(null);
    });
    if (window.PAStatusStream && typeof window.PAStatusStream.subscribe === "function") {
      window.PAStatusStream.subscribe((eventType, payload) => {
        if (eventType === "status") renderSupport(payload && payload.wifiModule);
      });
    }
    if (uploadWmButton) uploadWmButton.addEventListener("click", uploadWifiModule);
  }
})();

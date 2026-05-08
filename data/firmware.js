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
})();

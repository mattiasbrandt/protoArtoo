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

  const postReboot = async () => {
    feedback.textContent = "Requesting reboot...";
    try {
      const response = await fetch("/api/reboot", { method: "POST" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      feedback.textContent = "Reboot requested.";
    } catch (_error) {
      feedback.textContent = "Failed to request reboot.";
    }
  };

  const uploadFirmware = () => {
    const file = fileInput.files && fileInput.files[0];
    if (!file) {
      feedback.textContent = "Select a .bin file first.";
      return;
    }

    progressWrap.style.display = "block";
    progressBar.style.width = "0%";
    progressStatus.textContent = "Uploading...";
    feedback.textContent = `Uploading ${file.name}...`;

    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/upload/firmware");

    xhr.upload.onprogress = (event) => {
      if (!event.lengthComputable) {
        return;
      }
      const pct = Math.min(99, Math.round((event.loaded / event.total) * 100));
      progressBar.style.width = `${pct}%`;
      progressStatus.textContent = `Uploading... ${pct}%`;
    };

    xhr.onload = () => {
      if (xhr.status === 200) {
        progressBar.style.width = "100%";
        progressStatus.textContent = "Upload complete. Waiting for reboot...";
        feedback.textContent = "Firmware uploaded. Controller should reboot shortly.";
      } else {
        progressStatus.textContent = "Upload failed";
        feedback.textContent = xhr.responseText || `Upload failed (HTTP ${xhr.status}).`;
      }
    };

    xhr.onerror = () => {
      progressStatus.textContent = "Upload failed";
      feedback.textContent = "Upload error.";
    };

    const formData = new FormData();
    formData.append("firmware", file, file.name);
    xhr.send(formData);
  };

  uploadButton.addEventListener("click", uploadFirmware);
  rebootButton.addEventListener("click", postReboot);
})();

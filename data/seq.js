// =============================================================================
// data/seq.js
//
// Learned Sequence web editor for protoArtoo.
// Manages list view, modals (clone, import, memory wipe), and editor view.
// =============================================================================

(() => {
  // =========================================================================
  // State & DOM References
  // =========================================================================
  let sequences = []; // Current list of learned sequences
  let builtins = []; // Factory sequences (cached after first load)
  let currentEditingSeq = null; // The sequence being edited (or null)

  // Editor state tracking (Slice B onwards)
  let editorState = {
    original: null,   // snapshot at open time (for Revert)
    current: null,    // live edited copy
    isNew: false,     // true for blank/clone/duplicate (unsaved)
  };

  const els = {
    // List view
    mainCard: document.getElementById("seq-main-card"),
    emptyState: document.getElementById("seq-empty-state"),
    populatedState: document.getElementById("seq-populated-state"),
    capacityDisplay: document.getElementById("seq-capacity-display"),
    cardsContainer: document.getElementById("seq-cards-container"),

    // Top buttons
    btnCloneFactory: document.getElementById("seq-btn-clone-factory"),
    btnImport: document.getElementById("seq-btn-import"),
    emptyClone: document.getElementById("seq-empty-clone"),
    emptyImport: document.getElementById("seq-empty-import"),

    // Clone factory modal
    modalClone: document.getElementById("seq-modal-clone-factory"),
    cloneSearch: document.getElementById("seq-clone-search"),
    cloneResultsInfo: document.getElementById("seq-clone-results-info"),
    cloneBuiltinsList: document.getElementById("seq-clone-builtins-list"),
    modalCloneClose: document.getElementById("seq-modal-clone-close"),

    // Import modal
    modalImport: document.getElementById("seq-modal-import"),
    importFileInput: document.getElementById("seq-import-file-input"),
    importTextarea: document.getElementById("seq-import-textarea"),
    importFeedback: document.getElementById("seq-import-feedback"),
    modalImportCancel: document.getElementById("seq-modal-import-cancel"),
    modalImportConfirm: document.getElementById("seq-modal-import-confirm"),
    modalImportClose: document.getElementById("seq-modal-import-close"),

    // Editor view
    editorView: document.getElementById("seq-editor-view"),

    // Memory wipe modal
    modalWipe: document.getElementById("seq-modal-memory-wipe"),
    wipeSeqName: document.getElementById("seq-wipe-seq-name"),
    wipeConfirmInput: document.getElementById("seq-wipe-confirm-input"),
    wipeDanglingInfo: document.getElementById("seq-wipe-dangling-info"),
    modalWipeCancel: document.getElementById("seq-modal-wipe-cancel"),
    modalWipeConfirm: document.getElementById("seq-modal-wipe-confirm"),
  };

  // =========================================================================
  // Helper: Toggle modal visibility
  // =========================================================================
  const showModal = (modal) => {
    if (modal) {
      modal.classList.remove("hidden");
      // Focus trap: focus first focusable element
      const focusable = modal.querySelector("button, input, [tabindex]");
      if (focusable) focusable.focus();
    }
  };

  const hideModal = (modal) => {
    if (modal) modal.classList.add("hidden");
  };

  // =========================================================================
  // Slice A: Load & Render List View
  // =========================================================================

  const loadSequenceList = async () => {
    try {
      const result = await PAApi.get("/api/seq/list");
      if (!result.ok) {
        // Show error in console; don't destroy the page
        console.error("Failed to load sequences:", result);
        // In a real scenario, the device API would return data
        // For now, show empty state as safe default
        sequences = [];
        renderListView();
        return;
      }
      sequences = result.data || [];
      renderListView();
    } catch (error) {
      console.error("Error loading sequences:", error);
      // Safe default: show empty state
      sequences = [];
      renderListView();
    }
  };

  const renderListView = () => {
    // Update capacity
    els.capacityDisplay.textContent = `${sequences.length} / 16 sequences`;

    if (sequences.length === 0) {
      // Show empty state
      els.emptyState.classList.remove("hidden");
      els.populatedState.classList.add("hidden");
      els.cardsContainer.innerHTML = "";
    } else {
      // Show populated state
      els.emptyState.classList.add("hidden");
      els.populatedState.classList.remove("hidden");

      // Render seq-cards
      els.cardsContainer.innerHTML = sequences
        .map((seq) => renderSeqCard(seq))
        .join("");

      // Attach event listeners to action buttons
      els.cardsContainer.querySelectorAll(".seq-card-actions button").forEach((btn) => {
        const action = btn.dataset.action;
        const seqName = btn.dataset.seqName;
        const cardEl = btn.closest(".seq-card");
        btn.addEventListener("click", () => handleSeqAction(action, seqName, cardEl));
      });
    }
  };

  const renderSeqCard = (seq) => {
    const badges = [];
    if (seq.retrained) {
      badges.push(
        `<span class="seq-badge seq-badge-retrained" title="This sequence shadows the factory ${seq.name}">Retrained</span>`
      );
    }
    if (seq.source && seq.source !== "user") {
      badges.push(
        `<span class="seq-badge seq-badge-guild" title="Imported from ${seq.source}">Guild</span>`
      );
    }

    const stepCount = seq.stepCount || 0;
    const modifiedDate = seq.modified ? new Date(seq.modified).toLocaleString() : "Unknown";

    return `
      <div class="seq-card">
        <div class="seq-card-header">
          <h4>${escapeHtml(seq.name)}</h4>
          <div class="seq-badges">${badges.join("")}</div>
        </div>
        <div class="seq-card-meta">
          <span class="seq-meta-item">Toggle: ${escapeHtml(seq.toggleGroup || "none")}</span>
          <span class="seq-meta-item">Suppress: ${seq.suppressMs}ms</span>
          <span class="seq-meta-item">Steps: ${stepCount}</span>
          <span class="seq-meta-item">Modified: ${escapeHtml(modifiedDate)}</span>
        </div>
        <div class="seq-card-actions">
          <button class="btn btn-sm btn-action" data-action="edit" data-seq-name="${escapeAttr(seq.name)}">Edit</button>
          <button class="btn btn-sm btn-action" data-action="test" data-seq-name="${escapeAttr(seq.name)}">Test</button>
          <button class="btn btn-sm btn-action" data-action="duplicate" data-seq-name="${escapeAttr(seq.name)}">Duplicate</button>
          <button class="btn btn-sm btn-action" data-action="memory-wipe" data-seq-name="${escapeAttr(seq.name)}">Memory Wipe</button>
          <button class="btn btn-sm btn-action" data-action="export" data-seq-name="${escapeAttr(seq.name)}">Export</button>
        </div>
        <div class="seq-card-test-feedback feedback hidden"></div>
      </div>
    `;
  };

  const escapeHtml = (str) => {
    const div = document.createElement("div");
    div.textContent = str;
    return div.innerHTML;
  };

  const escapeAttr = (str) => {
    return str.replace(/"/g, "&quot;");
  };

  // =========================================================================
  // Clone Factory Modal
  // =========================================================================

  const loadBuiltins = async () => {
    if (builtins.length > 0) return; // Already cached
    try {
      const result = await PAApi.get("/api/seq/builtins");
      if (!result.ok) {
        console.error("Failed to load builtins");
        return;
      }
      builtins = result.data || [];
    } catch (error) {
      console.error("Error loading builtins:", error);
    }
  };

  const showCloneFactoryModal = async () => {
    await loadBuiltins();
    els.cloneSearch.value = "";
    renderCloneFactoryList();
    showModal(els.modalClone);
  };

  const renderCloneFactoryList = () => {
    const query = els.cloneSearch.value.toLowerCase();
    const filtered = builtins.filter((b) =>
      (b.name || "").toLowerCase().includes(query)
    );

    const total = builtins.length;
    const shown = filtered.length;
    els.cloneResultsInfo.textContent = `${shown} of ${total} shown`;

    els.cloneBuiltinsList.innerHTML = filtered
      .map((builtin) => renderBuiltinRow(builtin))
      .join("");

    // Attach clone buttons
    els.cloneBuiltinsList.querySelectorAll(".builtin-clone-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        const name = btn.dataset.builtinName;
        handleCloneBuiltin(name);
      });
    });
  };

  const renderBuiltinRow = (builtin) => {
    const stepCount = (builtin.steps || []).length;
    const toggleGroup = builtin.toggleGroup || "none";
    const suppressMs = builtin.suppressMs || 0;
    const notes = builtin.meta?.notes || "";

    return `
      <div class="builtin-row">
        <div class="builtin-info">
          <h5>${escapeHtml(builtin.name)}</h5>
          <div class="builtin-meta">
            <span>${stepCount} steps</span>
            <span>Toggle: ${escapeHtml(toggleGroup)}</span>
            <span>${suppressMs}ms suppress</span>
          </div>
          ${notes ? `<div class="builtin-notes">${escapeHtml(notes)}</div>` : ""}
        </div>
        <button class="btn btn-sm builtin-clone-btn" data-builtin-name="${escapeAttr(builtin.name)}">Clone</button>
      </div>
    `;
  };

  const handleCloneBuiltin = (builtinName) => {
    const builtin = builtins.find((b) => b.name === builtinName);
    if (!builtin) return;

    // Store for editor to load
    currentEditingSeq = JSON.parse(JSON.stringify(builtin));
    editorState.isNew = true; // Cloning is treated as new sequence
    hideModal(els.modalClone);

    // Hide list, show editor
    els.emptyState.classList.add("hidden");
    els.populatedState.classList.add("hidden");
    els.editorView.classList.remove("hidden");

    // Render editor with this builtin
    renderEditorView(currentEditingSeq);
  };

  // =========================================================================
  // Slice B: Full Editor View
  // =========================================================================

  const renderStepRow = (step, idx) => {
    const stepTypeButtons = ["audio", "dome", "loop", "random", "audioCat", "end"]
      .map((type) =>
        `<button class="step-type-chip ${step.type === type ? "active" : ""}" data-type="${type}" aria-pressed="${step.type === type ? "true" : "false"}">${type}</button>`
      )
      .join("");

    return `
      <div class="step-row" data-step-index="${idx}" data-step-type="${step.type}" draggable="true">
        <span class="step-handle" title="Drag to reorder steps">⋯</span>
        <input class="step-t" type="number" value="${step.t || 0}" min="0" max="120000" aria-label="Step time offset (ms)" placeholder="t (ms)">
        <div class="step-type-chips" role="group" aria-label="Step type">
          ${stepTypeButtons}
        </div>
        <div class="step-fields" data-fields-for-type="${step.type}">
          <!-- Conditional fields populated by renderStepFields -->
        </div>
        <button class="step-remove" aria-label="Remove this step" data-step-index="${idx}">×</button>
      </div>
    `;
  };

  const renderStepFields = (step, fieldsContainer) => {
    let html = "";
    switch (step.type) {
      case "audio":
        html = `<input class="step-field step-field-cmd" type="text" data-field="cmd" value="${escapeHtml(step.cmd || "")}" placeholder="$H, $N, $D, $A..." aria-label="Audio command">`;
        break;
      case "dome":
        html = `<input class="step-field step-field-cmd" type="text" data-field="cmd" value="${escapeHtml(step.cmd || "")}" placeholder=":SM0,2200,150" aria-label="Dome command">`;
        break;
      case "loop":
        html = `
          <input class="step-field step-field-body" type="number" data-field="body" value="${step.body || 1}" min="1" max="96" aria-label="Loop body step count" placeholder="body">
          <input class="step-field step-field-periodMs" type="number" data-field="periodMs" value="${step.periodMs || 1000}" min="100" max="60000" aria-label="Loop period (ms)" placeholder="periodMs">
          <input class="step-field step-field-durationMs" type="number" data-field="durationMs" value="${step.durationMs || 10000}" min="100" max="120000" aria-label="Loop duration (ms)" placeholder="durationMs">
        `;
        break;
      case "random":
        html = `
          <select class="step-field step-field-set" data-field="set" aria-label="Random set">
            <option value="ring" ${step.set === "ring" ? "selected" : ""}>ring</option>
            <option value="pie" ${step.set === "pie" ? "selected" : ""}>pie</option>
          </select>
          <input class="step-field step-field-pulseMin" type="number" data-field="pulseMin" value="${step.pulseMin || 1150}" min="800" max="2200" aria-label="Min pulse" placeholder="pulseMin">
          <input class="step-field step-field-pulseMax" type="number" data-field="pulseMax" value="${step.pulseMax || 1500}" min="800" max="2200" aria-label="Max pulse" placeholder="pulseMax">
          <input class="step-field step-field-moveMs" type="number" data-field="moveMs" value="${step.moveMs || 300}" min="50" max="5000" aria-label="Move time (ms)" placeholder="moveMs">
          <input class="step-field step-field-jitterMs" type="number" data-field="jitterMs" value="${step.jitterMs || 0}" min="0" max="2000" aria-label="Jitter (ms)" placeholder="jitterMs">
          <label class="step-field-checkbox"><input type="checkbox" data-field="distinct" ${step.distinct ? "checked" : ""} aria-label="Distinct"> Distinct</label>
        `;
        break;
      case "audioCat":
        const audioCategories = ["alert", "chatty", "general", "happy", "humming", "processing", "sad", "sentimental", "scream", "surprised", "whistle"];
        html = `
          <select class="step-field step-field-category" data-field="category" aria-label="Audio category">
            ${audioCategories.map((cat) => `<option value="${cat}" ${step.category === cat ? "selected" : ""}>${cat}</option>`).join("")}
          </select>
          <input class="step-field step-field-fallback" type="text" data-field="fallback" value="${escapeHtml(step.fallback || "")}" placeholder="$H (fallback track)" aria-label="Fallback track">
        `;
        break;
      case "end":
        html = `<span class="step-field-empty">(terminal step)</span>`;
        break;
      default:
        html = "";
    }
    fieldsContainer.innerHTML = html;
  };

  const renderEditorView = (seq) => {
    // Initialize editor state
    editorState.original = JSON.parse(JSON.stringify(seq));
    editorState.current = JSON.parse(JSON.stringify(seq));
    editorState.isNew = !seq.name || seq.name.startsWith("_"); // Mark as new if name is missing or placeholder

    const stepRows = (seq.steps || [])
      .map((step, idx) => renderStepRow(step, idx))
      .join("");

    els.editorView.innerHTML = `
      <div class="card">
        <h3>Edit — ${escapeHtml(seq.name || "New Sequence")}</h3>

        <div class="seq-editor-metadata">
          <div class="seq-editor-field">
            <label for="seq-editor-name">Name</label>
            <input id="seq-editor-name" type="text" value="${escapeHtml(seq.name || "DM:")}" placeholder="DM:MYSEQ" aria-label="Sequence name (DM:XXXX format)" maxlength="20">
            <div class="seq-editor-error-text" id="seq-editor-name-error" aria-live="polite"></div>
          </div>

          <div class="seq-editor-field">
            <label for="seq-editor-suppress">Suppress Window (ms)</label>
            <div class="seq-editor-slider-row">
              <input id="seq-editor-suppress" type="range" class="seq-editor-slider" value="${seq.suppressMs || 8000}" min="1000" max="120000" step="100" aria-label="Suppress window milliseconds">
              <span class="seq-editor-slider-value">${seq.suppressMs || 8000}</span>
            </div>
            <div class="seq-editor-error-text" id="seq-editor-suppress-error" aria-live="polite"></div>
          </div>

          <div class="seq-editor-field">
            <label for="seq-editor-toggle">Toggle Group</label>
            <select id="seq-editor-toggle" aria-label="Toggle group for conflict management">
              <option value="none" ${(seq.toggleGroup || "none") === "none" ? "selected" : ""}>none</option>
              <option value="pies" ${seq.toggleGroup === "pies" ? "selected" : ""}>pies</option>
              <option value="low" ${seq.toggleGroup === "low" ? "selected" : ""}>low</option>
              <option value="all" ${seq.toggleGroup === "all" ? "selected" : ""}>all</option>
            </select>
          </div>

          <div class="seq-editor-field">
            <label for="seq-editor-notes">Notes (optional)</label>
            <textarea id="seq-editor-notes" placeholder="Add any notes about this sequence..." aria-label="Optional notes about the sequence">${escapeHtml((seq.meta?.notes || ""))}</textarea>
          </div>
        </div>

        <div class="seq-editor-validation-summary" id="seq-editor-validation-summary" aria-live="polite" aria-label="Validation status">
          <!-- Populated by updateValidationSummary() -->
        </div>

        <div class="seq-editor-steps">
          <h4>Steps</h4>
          <div class="seq-editor-step-table" id="seq-editor-step-table">
            ${stepRows}
          </div>
          <button id="seq-editor-add-step" class="btn btn-sm btn-secondary" aria-label="Add a new step">+ Add Step</button>
        </div>

        <div class="seq-editor-footer">
          <button id="seq-editor-test" class="btn btn-primary" aria-label="Test sequence on droid">Test on Droid</button>
          <button id="seq-editor-save" class="btn btn-primary" aria-label="Save sequence">Save</button>
          <button id="seq-editor-revert" class="btn btn-secondary" aria-label="Discard unsaved changes">Revert</button>
          <button id="seq-editor-cancel" class="btn btn-secondary" aria-label="Cancel editing">Cancel</button>
        </div>

        <div class="seq-editor-feedback" id="seq-editor-feedback" aria-live="polite" aria-label="Editor feedback">
          <!-- Feedback messages shown here -->
        </div>
      </div>
    `;

    // Populate conditional fields for each step
    document.querySelectorAll(".step-fields").forEach((container, idx) => {
      renderStepFields(editorState.current.steps[idx], container);
    });

    // Attach event listeners
    attachEditorEventListeners();
    updateValidationSummary();
  };

  const updateValidationSummary = () => {
    const validation = SeqProtocolCheck.validateSequence(editorState.current);
    const summaryEl = document.getElementById("seq-editor-validation-summary");
    if (!summaryEl) return;

    const icon = validation.ok ? "✓" : "⚠";
    const status = validation.ok ? "valid" : "error";
    summaryEl.innerHTML = `
      <div class="seq-validation-status seq-validation-${status}">
        ${icon} ${validation.ok ? "Sequence is valid" : validation.error || "Validation error"}
      </div>
    `;

    // Disable save button if invalid
    const saveBtn = document.getElementById("seq-editor-save");
    if (saveBtn) saveBtn.disabled = !validation.ok;
  };

  const validateAndUpdateStep = (stepIdx) => {
    const row = document.querySelector(`[data-step-index="${stepIdx}"]`);
    if (!row) return;

    // Read DOM values
    const tInput = row.querySelector(".step-t");
    const typeButtons = row.querySelectorAll(".step-type-chip");
    const fieldsContainer = row.querySelector(".step-fields");

    const step = {
      t: parseInt(tInput.value || 0, 10),
      type: Array.from(typeButtons).find((btn) => btn.classList.contains("active"))?.dataset.type || "audio",
    };

    // Collect conditional fields
    const fieldInputs = fieldsContainer.querySelectorAll("[data-field]");
    fieldInputs.forEach((input) => {
      const field = input.dataset.field;
      let value = input.value;
      if (input.type === "checkbox") {
        value = input.checked;
      } else if (field === "t" || field === "body" || field === "periodMs" || field === "durationMs" || field === "pulseMin" || field === "pulseMax" || field === "moveMs" || field === "jitterMs") {
        value = parseInt(value, 10);
      }
      step[field] = value;
    });

    // Validate
    const validation = SeqProtocolCheck.validateStep(step, stepIdx, editorState.current.steps);
    const errorDiv = row.querySelector(".step-row-error") || document.createElement("div");
    if (!row.querySelector(".step-row-error")) {
      errorDiv.className = "step-row-error";
      row.appendChild(errorDiv);
    }

    if (!validation.ok) {
      row.classList.add("step-row-error-state");
      errorDiv.textContent = validation.error || "Validation error";
      if (validation.field) {
        const fieldEl = row.querySelector(`[data-field="${validation.field}"]`);
        if (fieldEl) fieldEl.classList.add("field-error");
      }
    } else {
      row.classList.remove("step-row-error-state");
      errorDiv.textContent = "";
      row.querySelectorAll(".field-error").forEach((el) => el.classList.remove("field-error"));
    }

    // Update editor state
    editorState.current.steps[stepIdx] = step;
    updateValidationSummary();
  };

  const attachEditorEventListeners = () => {
    // Metadata field changes
    const nameInput = document.getElementById("seq-editor-name");
    const suppressInput = document.getElementById("seq-editor-suppress");
    const suppressValue = document.querySelector(".seq-editor-slider-value");
    const toggleSelect = document.getElementById("seq-editor-toggle");
    const notesInput = document.getElementById("seq-editor-notes");

    if (nameInput) {
      nameInput.addEventListener("input", () => {
        editorState.current.name = nameInput.value;
        updateValidationSummary();
      });
    }

    if (suppressInput) {
      suppressInput.addEventListener("input", () => {
        const val = parseInt(suppressInput.value, 10);
        editorState.current.suppressMs = val;
        if (suppressValue) suppressValue.textContent = val;
        updateValidationSummary();
      });
    }

    if (toggleSelect) {
      toggleSelect.addEventListener("change", () => {
        editorState.current.toggleGroup = toggleSelect.value;
        updateValidationSummary();
      });
    }

    if (notesInput) {
      notesInput.addEventListener("input", () => {
        if (!editorState.current.meta) editorState.current.meta = {};
        editorState.current.meta.notes = notesInput.value;
      });
    }

    // Step type chip selection
    document.querySelectorAll(".step-type-chip").forEach((chip) => {
      chip.addEventListener("click", (e) => {
        e.preventDefault();
        const row = chip.closest(".step-row");
        if (!row) return;
        const stepIdx = parseInt(row.dataset.stepIndex, 10);

        // Update active chip
        row.querySelectorAll(".step-type-chip").forEach((c) => {
          c.classList.toggle("active", c === chip);
          c.setAttribute("aria-pressed", c === chip ? "true" : "false");
        });

        // Change step type and re-render fields
        const newType = chip.dataset.type;
        editorState.current.steps[stepIdx].type = newType;

        // Create default fields for new type
        const defaults = {
          audio: { cmd: "$H" },
          dome: { cmd: ":SM0,2200,150" },
          loop: { body: 2, periodMs: 1846, durationMs: 14000 },
          random: { set: "ring", pulseMin: 1150, pulseMax: 1500, moveMs: 300, jitterMs: 500, distinct: true },
          audioCat: { category: "alert", fallback: "$H" },
          end: {},
        };

        Object.assign(editorState.current.steps[stepIdx], defaults[newType] || {});

        // Re-render fields
        const fieldsContainer = row.querySelector(".step-fields");
        renderStepFields(editorState.current.steps[stepIdx], fieldsContainer);

        // Re-attach input listeners to new fields
        fieldsContainer.querySelectorAll("[data-field]").forEach((input) => {
          input.addEventListener("input", () => validateAndUpdateStep(stepIdx));
          input.addEventListener("change", () => validateAndUpdateStep(stepIdx));
        });

        validateAndUpdateStep(stepIdx);
      });
    });

    // Step field inputs
    document.querySelectorAll(".step-fields [data-field]").forEach((input) => {
      const row = input.closest(".step-row");
      const stepIdx = parseInt(row.dataset.stepIndex, 10);
      input.addEventListener("input", () => validateAndUpdateStep(stepIdx));
      input.addEventListener("change", () => validateAndUpdateStep(stepIdx));
    });

    // Step remove buttons
    document.querySelectorAll(".step-remove").forEach((btn) => {
      btn.addEventListener("click", () => {
        const stepIdx = parseInt(btn.dataset.stepIndex, 10);
        editorState.current.steps.splice(stepIdx, 1);
        rerenderStepTable();
        updateValidationSummary();
      });
    });

    // Add step button
    const addStepBtn = document.getElementById("seq-editor-add-step");
    if (addStepBtn) {
      addStepBtn.addEventListener("click", () => {
        const newStep = { t: 0, type: "audio", cmd: "$H" };
        editorState.current.steps.push(newStep);
        rerenderStepTable();
        updateValidationSummary();
      });
    }

    // Footer buttons
    const testBtn = document.getElementById("seq-editor-test");
    const saveBtn = document.getElementById("seq-editor-save");
    const revertBtn = document.getElementById("seq-editor-revert");
    const cancelBtn = document.getElementById("seq-editor-cancel");

    if (testBtn) {
      testBtn.addEventListener("click", handleTestOnDroid);
    }

    if (saveBtn) {
      saveBtn.addEventListener("click", handleSave);
    }

    if (revertBtn) {
      revertBtn.addEventListener("click", () => {
        editorState.current = JSON.parse(JSON.stringify(editorState.original));
        renderEditorView(editorState.original);
      });
    }

    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => {
        els.editorView.classList.add("hidden");
        currentEditingSeq = null;
        editorState = { original: null, current: null, isNew: false };
        loadSequenceList();
      });
    }

    // Drag-and-drop reordering
    let draggedIndex = null;
    document.querySelectorAll(".step-row").forEach((row, idx) => {
      row.addEventListener("dragstart", (e) => {
        draggedIndex = idx;
        row.classList.add("dragging");
        e.dataTransfer.effectAllowed = "move";
      });

      row.addEventListener("dragend", () => {
        row.classList.remove("dragging");
        draggedIndex = null;
      });

      row.addEventListener("dragover", (e) => {
        e.preventDefault();
        e.dataTransfer.dropEffect = "move";
        const rect = row.getBoundingClientRect();
        const midpoint = rect.top + rect.height / 2;
        if (e.clientY < midpoint) {
          row.classList.add("drop-above");
          row.classList.remove("drop-below");
        } else {
          row.classList.add("drop-below");
          row.classList.remove("drop-above");
        }
      });

      row.addEventListener("dragleave", () => {
        row.classList.remove("drop-above", "drop-below");
      });

      row.addEventListener("drop", (e) => {
        e.preventDefault();
        const dropIdx = idx;
        if (draggedIndex !== null && draggedIndex !== dropIdx) {
          const [movedStep] = editorState.current.steps.splice(draggedIndex, 1);
          const insertIdx = draggedIndex < dropIdx ? dropIdx - 1 : dropIdx;
          editorState.current.steps.splice(insertIdx, 0, movedStep);
          rerenderStepTable();
        }
        row.classList.remove("drop-above", "drop-below");
      });
    });
  };

  const rerenderStepTable = () => {
    const table = document.getElementById("seq-editor-step-table");
    if (!table) return;
    const stepRows = editorState.current.steps
      .map((step, idx) => renderStepRow(step, idx))
      .join("");
    table.innerHTML = stepRows;

    // Populate conditional fields
    document.querySelectorAll(".step-fields").forEach((container, idx) => {
      renderStepFields(editorState.current.steps[idx], container);
    });

    // Re-attach all listeners
    attachEditorEventListeners();
  };

  const showEditorFeedback = (message, kind = "info") => {
    const feedbackEl = document.getElementById("seq-editor-feedback");
    if (!feedbackEl) return;
    feedbackEl.innerHTML = `<div class="feedback feedback-${kind}">${escapeHtml(message)}</div>`;
    feedbackEl.classList.remove("hidden");
  };

  const handleSave = async () => {
    const validation = SeqProtocolCheck.validateSequence(editorState.current);
    if (!validation.ok) {
      showEditorFeedback(validation.error || "Fix validation errors before saving.", "error");
      return;
    }
    if (editorState.isNew && sequences.length >= 16) {
      showEditorFeedback("Capacity limit: 16 sequences on device. Delete one first.", "error");
      return;
    }
    const saveBtn = document.getElementById("seq-editor-save");
    if (saveBtn) saveBtn.disabled = true;
    showEditorFeedback("Saving...", "info");
    try {
      await PAApi.postJson("/api/seq", editorState.current);
      showEditorFeedback("Saved.", "ok");
      editorState.isNew = false;
      editorState.original = JSON.parse(JSON.stringify(editorState.current));
      await loadSequenceList();
    } catch (error) {
      showEditorFeedback("Save failed: " + PAApi.messageFor(error), "error");
    } finally {
      if (saveBtn) saveBtn.disabled = false;
    }
  };

  const handleTestOnDroid = async () => {
    const seqName = editorState.current?.name;
    if (!seqName) return;
    const testBtn = document.getElementById("seq-editor-test");
    if (testBtn) testBtn.disabled = true;
    showEditorFeedback(`Sending ${escapeHtml(seqName)} to droid...`, "info");
    try {
      await PAApi.postJson("/api/seq/test", { name: seqName });
      showEditorFeedback(`${escapeHtml(seqName)} dispatched.`, "ok");
    } catch (error) {
      showEditorFeedback("Test failed: " + PAApi.messageFor(error), "error");
    } finally {
      if (testBtn) testBtn.disabled = false;
    }
  };

  // =========================================================================
  // Action Handlers (Edit, Test, Duplicate, Memory Wipe, Export)
  // =========================================================================

  const handleSeqAction = async (action, seqName, cardEl) => {
    switch (action) {
      case "edit":
        await handleEditSequence(seqName);
        break;
      case "test":
        await handleTestSequence(seqName, cardEl);
        break;
      case "duplicate":
        await handleDuplicateSequence(seqName);
        break;
      case "memory-wipe":
        handleMemoryWipePrompt(seqName);
        break;
      case "export":
        await handleExportSequence(seqName);
        break;
    }
  };

  const handleEditSequence = async (seqName) => {
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      if (!result.ok) {
        console.error("Failed to load sequence for editing");
        return;
      }

      currentEditingSeq = result.data;
      editorState.isNew = false; // Editing existing sequence

      // Hide list, show editor
      els.emptyState.classList.add("hidden");
      els.populatedState.classList.add("hidden");
      els.editorView.classList.remove("hidden");

      renderEditorView(currentEditingSeq);
    } catch (error) {
      console.error("Error loading sequence:", error);
    }
  };

  const handleTestSequence = async (seqName, cardEl) => {
    const feedbackEl = cardEl?.querySelector(".seq-card-test-feedback");
    if (feedbackEl) {
      feedbackEl.textContent = "Running...";
      feedbackEl.className = "seq-card-test-feedback feedback info";
      feedbackEl.classList.remove("hidden");
    }
    try {
      await PAApi.postJson("/api/seq/test", { name: seqName });
      if (feedbackEl) {
        feedbackEl.textContent = "Dispatched.";
        feedbackEl.className = "seq-card-test-feedback feedback ok";
      }
    } catch (error) {
      if (feedbackEl) {
        feedbackEl.textContent = PAApi.messageFor(error);
        feedbackEl.className = "seq-card-test-feedback feedback error";
      }
    }
  };

  const handleDuplicateSequence = async (seqName) => {
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      if (!result.ok) {
        console.error("Failed to load sequence for duplication");
        return;
      }

      const original = result.data;
      // Auto-rename to NAME_copy (avoid _copy_copy by removing existing suffix)
      const baseName = seqName.replace(/_copy(\d*)$/, "");
      original.name = `${baseName}_copy`;

      // Open editor with copy
      currentEditingSeq = original;
      editorState.isNew = true; // Duplicate is a new sequence

      els.emptyState.classList.add("hidden");
      els.populatedState.classList.add("hidden");
      els.editorView.classList.remove("hidden");

      renderEditorView(currentEditingSeq);
    } catch (error) {
      console.error("Error duplicating sequence:", error);
    }
  };

  const handleMemoryWipePrompt = (seqName) => {
    els.wipeSeqName.textContent = `Delete sequence: ${escapeHtml(seqName)}`;
    els.wipeConfirmInput.value = "";
    els.wipeConfirmInput.placeholder = seqName;
    els.wipeDanglingInfo.classList.add("hidden");
    els.modalWipeConfirm.disabled = true;

    // Update confirm button state as user types
    const updateWipeButton = () => {
      const matches = els.wipeConfirmInput.value === seqName;
      els.modalWipeConfirm.disabled = !matches;
    };

    els.wipeConfirmInput.addEventListener("input", updateWipeButton);

    showModal(els.modalWipe);
  };

  const handleMemoryWipeConfirm = async () => {
    const seqName = els.wipeConfirmInput.placeholder;
    try {
      const result = await PAApi.request(`/api/seq?name=${encodeURIComponent(seqName)}`, {
        method: "DELETE",
      });

      if (!result.ok) {
        alert("Failed to delete sequence: " + PAApi.messageFor(new PAApi.ApiError("Delete failed", { status: result.status })));
        return;
      }

      // Show dangling bindings if any
      const response = result.data;
      if (response.danglingBindings && response.danglingBindings.length > 0) {
        let html = "<p><strong>Dangling bindings (now inert):</strong></p><ul>";
        response.danglingBindings.forEach((binding) => {
          html += `<li>${escapeHtml(binding.source)} CH${binding.channel}</li>`;
        });
        html += "</ul>";
        els.wipeDanglingInfo.innerHTML = html;
        els.wipeDanglingInfo.classList.remove("hidden");
      }

      hideModal(els.modalWipe);
      loadSequenceList(); // Refresh list
    } catch (error) {
      alert("Error deleting sequence: " + PAApi.messageFor(error));
    }
  };

  const handleExportSequence = async (seqName) => {
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      if (!result.ok) {
        alert("Failed to load sequence for export");
        return;
      }

      const seqJson = result.data;
      const blob = new Blob([JSON.stringify(seqJson, null, 2)], {
        type: "application/json",
      });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `${seqJson.name.replace(/:/g, "_")}.json`;
      a.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      alert("Error exporting sequence: " + PAApi.messageFor(error));
    }
  };

  // =========================================================================
  // Import Modal (Placeholder for Slice F)
  // =========================================================================

  const handleImportConfirm = async () => {
    let parsed;
    try {
      if (els.importFileInput.files.length > 0) {
        const fileText = await els.importFileInput.files[0].text();
        parsed = JSON.parse(fileText);
      } else {
        const text = els.importTextarea.value.trim();
        parsed = JSON.parse(text);
      }
    } catch {
      PAUtils.showFeedback(els.importFeedback, "Invalid JSON — check the format.", "error");
      els.importFeedback.classList.remove("hidden");
      return;
    }

    const validation = SeqProtocolCheck.validateSequence(parsed);
    if (!validation.ok) {
      PAUtils.showFeedback(els.importFeedback, validation.error || "Sequence failed validation.", "error");
      els.importFeedback.classList.remove("hidden");
      return;
    }

    hideModal(els.modalImport);
    editorState.isNew = true;
    editorState.original = JSON.parse(JSON.stringify(parsed));
    editorState.current = JSON.parse(JSON.stringify(parsed));
    currentEditingSeq = editorState.current;
    els.emptyState.classList.add("hidden");
    els.populatedState.classList.add("hidden");
    els.editorView.classList.remove("hidden");
    renderEditorView(editorState.current);
  };

  const showImportModal = () => {
    els.importFileInput.value = "";
    els.importTextarea.value = "";
    els.importFeedback.classList.add("hidden");
    els.modalImportConfirm.disabled = true;
    showModal(els.modalImport);
  };

  // =========================================================================
  // Event Listeners
  // =========================================================================

  const attachEventListeners = () => {
    // Top buttons
    els.btnCloneFactory.addEventListener("click", showCloneFactoryModal);
    els.btnImport.addEventListener("click", showImportModal);

    // Empty state buttons
    els.emptyClone.addEventListener("click", showCloneFactoryModal);
    els.emptyImport.addEventListener("click", showImportModal);

    // Clone modal
    els.modalCloneClose.addEventListener("click", () => hideModal(els.modalClone));
    els.cloneSearch.addEventListener("input", renderCloneFactoryList);

    // Import modal
    els.modalImportCancel.addEventListener("click", () => hideModal(els.modalImport));
    els.modalImportClose.addEventListener("click", () => hideModal(els.modalImport));
    els.modalImportConfirm.addEventListener("click", handleImportConfirm);

    // Enable/disable import confirm button reactively
    const updateImportConfirmButton = () => {
      let isValidJson = false;
      if (els.importFileInput.files.length > 0) {
        // File is selected; we'll parse it on confirm
        isValidJson = true;
      } else {
        // Try to parse textarea
        const text = els.importTextarea.value.trim();
        if (text) {
          try {
            JSON.parse(text);
            isValidJson = true;
          } catch {
            isValidJson = false;
          }
        }
      }
      els.modalImportConfirm.disabled = !isValidJson;
    };

    els.importFileInput.addEventListener("change", updateImportConfirmButton);
    els.importTextarea.addEventListener("input", updateImportConfirmButton);

    // Memory wipe modal
    els.modalWipeCancel.addEventListener("click", () => hideModal(els.modalWipe));
    els.modalWipeConfirm.addEventListener("click", handleMemoryWipeConfirm);

    // Modal overlays close on click
    document.querySelectorAll(".seq-modal-overlay").forEach((overlay) => {
      overlay.addEventListener("click", (e) => {
        if (e.target === overlay) {
          const modal = overlay.closest(".seq-modal");
          hideModal(modal);
        }
      });
    });

    // Escape key closes modals
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        [els.modalClone, els.modalImport, els.modalWipe].forEach((modal) => {
          if (modal && !modal.classList.contains("hidden")) {
            hideModal(modal);
          }
        });
      }
    });
  };

  // =========================================================================
  // Initialize on Page Load
  // =========================================================================

  const init = async () => {
    attachEventListeners();
    await loadSequenceList();
  };

  // Wait for shell and status_stream to be ready
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }

  // Expose for testing
  window.__seqEditorForTesting = { renderEditorView, editorState };
})();

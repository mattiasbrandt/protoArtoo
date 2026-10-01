// =============================================================================
// data/seq.js
//
// The Sequences surface: the list of what is on the droid, and the workspace
// a sequence is edited in (#441, variant C) - a strip, the stage with the
// timeline and the droid, and one drawer under it. Also the dialogs (restore,
// memory wipe, discard) and a Factory sequence's read-only stage.
// =============================================================================

(() => {
  // =========================================================================
  // State & DOM References
  // =========================================================================
  let sequences = []; // Current list of learned sequences
  let builtins = []; // Factory sequences
  // Whether each list has answered at least once. Until it has, its place on
  // the page waits rather than reading as empty: an unanswered list is not a
  // list with nothing on it. A later re-read keeps the last answer on screen.
  let learnedAnswered = false;
  let factoryAnswered = false;
  let currentEditingSeq = null; // The sequence being edited (or null)
  let timeline = null; // a Factory sequence's read-only timeline (data/seq_timeline.js), or null
  let sessionTimeline = null; // the timeline on the workspace's stage, over the sequence being edited, or null
  let pickedBlocks = []; // the blocks picked on that timeline, as it last said them
  let pickedShown = null; // the Picked block tab's markup as last written
  let domeLayoutChangeSubscribed = false; // guards a single DomeLayout.onChange registration

  // Editor state tracking. One object for the life of the page: it is reset in
  // place, never replaced, so the set of expanded steps is always there and
  // the test seam at the foot of this file holds the object the editor reads.
  const editorState = {
    original: null,   // snapshot at open time (for Revert)
    current: null,    // live edited copy
    isNew: false,     // true for blank/clone/duplicate (unsaved)
    tuningFactory: null, // Factory sequence name when opened via Tune (e.g. "DM:VADER"), or null
    expanded: new Set(), // Set of step indices that are expanded (presentation-only)
    view: "timeline", // which reading of the routine is on the stage: "timeline" or "steps"
    tab: "block",     // which drawer tab is forward: "block", "parts", "sequence" or "rehearsal"
    saved: false,     // whether this session has saved, for the strip's state word
  };

  // ---------------------------------------------------------------------------
  // Undo and redo (ADR 0057): every edit to the routine, one at a time, from
  // either view, on one stack.
  //
  // An entry is a copy of everything a builder authors - the steps, the close
  // half, the interrupt group and the tempo, which re-times every step on a
  // beat - so no edit needs an undo of its own kind. It is bracketed two ways
  // (the pattern is r2d2-astromech-simulator's blockHistPush / blockHistCommit):
  //   historyPush()          BEFORE an edit made in one act that always
  //                          changes something: add, remove, reorder
  //   historyCommit(before)  AFTER an edit made over time - a drag, a run of
  //                          typing - or one that may turn out to change
  //                          nothing, given the copy from historyBegin(). An
  //                          edit that changed nothing records nothing, so a
  //                          press that only selects costs no entry.
  // Revert is not on the stack: it discards the whole session, history and all.
  // ---------------------------------------------------------------------------
  const HISTORY_DEPTH = 100;
  const HISTORY_FIELDS = ["steps", "closeSteps", "toggleGroup", "tempo"];
  // `base` is the routine as the last finished edit left it, which is what a
  // typing run starts from: the dome fields write the step before they read
  // the form back, so the run cannot take its own copy when it begins. `run`
  // is that copy while a run is open, and `runStep` the step being typed in.
  const history = { undo: [], redo: [], base: null, run: null, runStep: null };

  const historyCapture = () =>
    JSON.stringify(Object.fromEntries(HISTORY_FIELDS.map((key) => [key, editorState.current[key]])));

  const historyRecord = (snapshot) => {
    history.undo.push(snapshot);
    if (history.undo.length > HISTORY_DEPTH) history.undo.shift();
    history.redo.length = 0;
  };

  const historyCommit = (before) => {
    if (historyCapture() !== before) historyRecord(before);
  };

  // A run of typing ends: at the field's change, and before anything else
  // reads or writes the stack.
  const historySettle = () => {
    if (history.run !== null) historyCommit(history.run);
    history.run = null;
    history.runStep = null;
    history.base = historyCapture();
  };

  const historyPush = () => {
    historySettle();
    historyRecord(history.base);
  };

  // The copy an edit made over time hands back to historyCommit().
  const historyBegin = () => {
    historySettle();
    return history.base;
  };

  const historyReset = () => {
    Object.assign(history, { undo: [], redo: [], run: null, runStep: null });
    history.base = editorState.current ? historyCapture() : null;
  };

  // A run still open is an edit not yet on the stack.
  const runChanged = () => history.run !== null && historyCapture() !== history.run;

  // How many Learned Sequences this droid lets a builder save. It is a board
  // fact the droid reports - five on the artoo-esp32, ten elsewhere (ADR 0065,
  // amended 2026-09-25) - as learned_sequence_cap in GET /api/identity, which
  // the shell fetches once per session and publishes as window.PAIdentity. The
  // page keeps no number of its own, so it can never promise ten to a droid
  // that stores five. Null until the droid has said, and then the page names
  // no cap at all rather than guessing one; the firmware refuses the save
  // either way.
  const learnedSequenceCap = () => {
    const cap = window.PAIdentity?.learned_sequence_cap;
    return Number.isInteger(cap) && cap > 0 ? cap : null;
  };

  // What the Rehearsal reads off the droid in front of the author: its Servo
  // Output rows and its config (data/outputs.js reads both, GET
  // /api/servo/outputs and GET /api/config), for the body and switched-off
  // rules and the line beside Test on the droid. Null until read. A read that
  // fails leaves them null: the rules that need them stay silent, the Gap lines
  // say what went unchecked, and nothing waits on them.
  const rehearsalFacts = { outputs: null, config: null };
  let lastRunBadge = null; // the badge beside the last run, repainted when facts land
  let saidSaved = false; // whether the strip's feedback line is the receipt of a save

  // The droid's per-file byte cap, the size figure's denominator (GET
  // /api/identity learned_sequence_max_bytes), or null until it has said.
  const learnedSequenceMaxBytes = () => {
    const bytes = window.PAIdentity?.learned_sequence_max_bytes;
    return Number.isInteger(bytes) && bytes > 0 ? bytes : null;
  };

  // A track the builder dropped in to be analyzed, while the editor is open:
  // its fingerprint, which the Rehearsal compares with the one the tempo was
  // measured from, and its analysis, until the builder takes or leaves it.
  // The browser never holds the droid's audio (ADR 0046), so this is the only
  // moment a stored fingerprint can be checked.
  let droppedTrack = null;

  const rehearsalContext = () => ({
    outputs: rehearsalFacts.outputs,
    config: rehearsalFacts.config,
    layout: window.DomeLayout?.getModel?.() || null,
    maxBytes: learnedSequenceMaxBytes(),
    trackHash: droppedTrack ? droppedTrack.hash : null,
  });

  let _pendingWipeSeqName = null; // sequence name pending deletion (avoids placeholder coupling)
  let _wipeInputListener = null;  // stored to enable removeEventListener on modal reopen

  // Audio fallback slots — the named clips usable as an audioCat "fallback"
  // (played when the chosen category has no available track). VALUES must match
  // the server slot table in src/seq_json.cpp (slotToString/slotFromString) and
  // the client validator set in seq_protocol_check.js. A slot that is a sound
  // action's track is named by that track's label in the one words table
  // (data/web_api.js), so this editor and the Sound page call it one thing;
  // "none" and the Happy category are no track.
  const trackSlot = (value) => ({ value, label: window.PAApi.labelOf(value) });
  const AUDIO_FALLBACK_SLOTS = [
    { value: "none", label: "None" },
    ...["scream", "faint", "leia", "cantina_s", "sw_theme", "imp_march", "cantina_l", "startup", "disco"]
      .map(trackSlot),
    { value: "happy", label: "Happy" },
  ];
  const audioFallbackLabel = (value) =>
    (AUDIO_FALLBACK_SLOTS.find((s) => s.value === value) || {}).label || value || "None";

  // Map DV: preset names to friendly operator labels
  const dvPresetLabel = (name) => {
    const labels = {
      "ROCKMARCH": "Rock March",
      "VADER": "Vader",
      "ALARM": "Alarm",
      "LEIA": "Leia",
      "HEART": "Heart",
      "CANTINA": "Cantina",
      "SCREAM": "Scream",
      "OVERLOAD": "Overload",
      "HELLO": "Hello",
      "RESET_VISUALS": "Reset Visuals",
    };
    return labels[name] || name || "Unknown";
  };

  // Map DL: targets to friendly operator labels
  const dlTargetLabel = (target) => {
    const labels = {
      "FLD": "Front logic",
      "RLD": "Rear logic",
      "LOGIC": "Both logic",
      "FPSI": "Front PSI",
      "RPSI": "Rear PSI",
      "PSI": "Both PSI",
      "ALL": "All logic + PSI",
    };
    return labels[target] || target || "Unknown";
  };

  // Map DL: modes to friendly operator labels
  const dlModeLabel = (mode) => {
    const labels = {
      "NORMAL": "Normal",
      "ALARM": "Alarm",
      "FAILURE": "Failure",
      "LEIA": "Leia",
      "MARCH": "March",
      "FLASHCOLOR": "Flash Color",
      "REDALERT": "Red Alert",
      "RAINBOW": "Rainbow",
      "LIGHTSOUT": "Lights Out",
    };
    return labels[mode] || mode || "Unknown";
  };

  // Map DL: colors to friendly operator labels
  const dlColorLabel = (color) => {
    const labels = {
      "DEFAULT": "Default",
      "RED": "Red",
      "BLUE": "Blue",
      "GREEN": "Green",
      "WHITE": "White",
      "YELLOW": "Yellow",
      "ORANGE": "Orange",
      "PURPLE": "Purple",
    };
    return labels[color] || color || "Default";
  };

  // Map DT: targets to friendly operator labels
  const dtTargetLabel = (target) => {
    const labels = {
      "FLD": "Front display",
      "RLD": "Rear display",
      "LOGIC": "Both displays",
    };
    return labels[target] || target || "Unknown";
  };

  // Map DH: targets to friendly operator labels
  const dhTargetLabel = (target) => {
    const labels = {
      "F": "Front holo",
      "R": "Rear holo",
      "T": "Top holo",
      "A": "All holos",
    };
    return labels[target] || target || "Unknown";
  };

  // Map DH: effects to friendly operator labels
  const dhEffectLabel = (effect) => {
    const labels = {
      "OFF": "Off",
      "ON": "On",
      "RESET": "Reset",
      "RANDOM": "Random",
      "WAG": "Wag",
      "NOD": "Nod",
      "PULSE": "Pulse",
      "RAINBOW": "Rainbow",
      "FLASH": "Flash",
      "SHORTCIRCUIT": "Short Circuit",
      "SOLID": "Solid",
    };
    return labels[effect] || effect || "Unknown";
  };

  // Map DH: colors to friendly operator labels (same as DL:)
  const dhColorLabel = (color) => {
    const labels = {
      "DEFAULT": "Default",
      "RED": "Red",
      "BLUE": "Blue",
      "GREEN": "Green",
      "WHITE": "White",
      "YELLOW": "Yellow",
      "ORANGE": "Orange",
      "PURPLE": "Purple",
      "RANDOM": "Random",
    };
    return labels[color] || color || "Default";
  };

  const els = {
    // List view
    title: document.getElementById("seq-title"),
    mainCard: document.getElementById("seq-main-card"),
    emptyState: document.getElementById("seq-empty-state"),
    populatedState: document.getElementById("seq-populated-state"),
    countDisplay: document.getElementById("seq-count-display"),
    filter: document.getElementById("seq-filter"),
    cardsContainer: document.getElementById("seq-cards-container"),

    // Top buttons
    btnImport: document.getElementById("seq-btn-import"),
    emptyImport: document.getElementById("seq-empty-import"),

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

    // Timeline view
    timelineView: document.getElementById("seq-timeline-view"),

    // Memory wipe modal
    modalWipe: document.getElementById("seq-modal-memory-wipe"),
    wipeSeqName: document.getElementById("seq-wipe-seq-name"),
    wipeConfirmInput: document.getElementById("seq-wipe-confirm-input"),
    wipeDanglingInfo: document.getElementById("seq-wipe-dangling-info"),
    modalWipeCancel: document.getElementById("seq-modal-wipe-cancel"),
    modalWipeConfirm: document.getElementById("seq-modal-wipe-confirm"),

    // Discard dialog
    modalDiscard: document.getElementById("seq-modal-discard"),
    discardWhat: document.getElementById("seq-discard-what"),
    modalDiscardKeep: document.getElementById("seq-modal-discard-keep"),
    modalDiscardConfirm: document.getElementById("seq-modal-discard-confirm"),
  };

  // =========================================================================
  // Helper: Toggle modal visibility
  //
  // A dialog covers the surface it belongs to and never the chrome around it.
  // The Operator Shell renders the Latching Estop on that chrome and keeps it
  // live for exactly the moments a dialog is up (ADR 0048, #359), so these
  // dialogs carry no aria-modal: that attribute is the claim that everything
  // outside the dialog is inert, and STOP is outside it. What is really inert
  // while a dialog is open is the surface, and that is what is marked here --
  // the same move the Page Recovery View makes (data/page_bootstrap.js,
  // holdSurfacesInert).
  //
  // The surface's OTHER top-level nodes rather than the surface itself: the
  // dialogs are children of it, and inert is inherited, so a descendant
  // cannot opt back in.
  // =========================================================================
  const topLevelOf = (modal) => {
    const surface = modal?.parentElement;
    return surface && surface.children ? [...surface.children] : [];
  };

  const surfaceBehind = (modal) =>
    topLevelOf(modal).filter((node) => !node.classList.contains("seq-modal"));

  const anotherDialogIsOpen = (modal) =>
    topLevelOf(modal).some(
      (node) =>
        node !== modal &&
        node.classList.contains("seq-modal") &&
        !node.classList.contains("hidden")
    );

  const showModal = (modal) => {
    if (modal) {
      modal.classList.remove("hidden");
      surfaceBehind(modal).forEach((node) => {
        node.inert = true;
      });
      // Focus the first control in the dialog. Not a trap: Tab leaves it for
      // the chrome, which is where STOP is.
      const focusable = modal.querySelector("button, input, [tabindex]");
      if (focusable) focusable.focus();
    }
  };

  const hideModal = (modal) => {
    if (!modal) return;
    modal.classList.add("hidden");
    // Another dialog may still be up; the surface comes back only when the
    // last one closes.
    if (anotherDialogIsOpen(modal)) return;
    surfaceBehind(modal).forEach((node) => {
      node.inert = false;
    });
  };

  // =========================================================================
  // Load & Render List View
  // =========================================================================

  // The two lists are two page sections (registered at the bottom of this
  // file). Each loads through the section's handle, so its deadline and
  // cancellation are the run's, and each lets a failure through to the Page
  // Recovery View, which waits out a busy controller, retries and says what is
  // missing. Neither catches: a read swallowed here is a list that never comes
  // back until a reload, which is how the factory list went missing after an
  // upload (#434). Each paints on its own answer, so one list never waits on,
  // or is wiped by, the other.
  const LEARNED_SECTION = "seq-learned";
  const FACTORY_SECTION = "seq-factory";

  const loadLearned = async ({ handle = null } = {}) => {
    const answer = await (handle || window.PAApi).get("/api/seq/list");
    sequences = answer.data || [];
    learnedAnswered = true;
    renderListView();
  };

  const loadFactory = async ({ handle = null } = {}) => {
    const answer = await (handle || window.PAApi).get("/api/seq/builtins");
    builtins = answer.data || [];
    factoryAnswered = true;
    renderListView();
  };

  // After a save, a wipe or a cancel the Learned list is read again through its
  // section, so a failed re-read is the Page Recovery View's as well.
  const refreshLearned = () => {
    if (window.PABootstrap) {
      window.PABootstrap.refreshSections([LEARNED_SECTION]);
      return;
    }
    loadLearned().catch((error) => console.warn("[seq] your sequences unavailable:", error));
  };

  // A slot that has not had its answer yet: written empty with the waiting
  // class, which data/style.css draws as the three moving dots.
  const WAITING_SLOT = '<p class="hint waiting seq-section-waiting" role="status"></p>';

  const countOf = (n, one, many) => `${n} ${n === 1 ? one : many}`;
  const chevron = '<svg class="i chev" aria-hidden="true" focusable="false"><use href="#i-chevron-right"/></svg>';

  // Which of the two groups the list shows - "all", "yours" or "factory" - and
  // the rows whose More is open, by name. Both are kept here, so reading the
  // list again neither resets the filter nor folds a row shut.
  let listShow = "all";
  const moreOpen = new Set();

  // Name, What it does, Steps, Runs, and the row's acts.
  const LIST_COLUMNS = 5;

  // A group's own row - its name, its count - and under it the one line the
  // group has to say: that it is waiting, empty, or over the droid's cap.
  const groupHtml = (name, count, said = "") => `
      <tbody class="seq-group">
        <tr><th colspan="${LIST_COLUMNS}" scope="colgroup">${name}${count}</th></tr>
        ${said ? `<tr class="seq-group-said"><td colspan="${LIST_COLUMNS}">${said}</td></tr>` : ""}
      </tbody>`;

  const renderListView = () => {
    // The list is its own view: it gives way, title and all, to whatever is
    // open - the workspace, or a Factory sequence's stage - and comes back
    // when that closes. Without this a save, which reads the list again, drew
    // it back in above the editor.
    const open = editorState.current !== null || timeline !== null;
    els.title.classList.toggle("hidden", open);
    els.mainCard.classList.toggle("hidden", open);

    // Compute untuned Factory sequences (those without a Learned override)
    const learnedNames = new Set(sequences.map(s => s.name));
    const untunedFactory = builtins.filter(b => !learnedNames.has(b.name));

    // How many sequences the list holds. Empty until both lists have answered,
    // so the slot's waiting class shows the dots instead of a count of half.
    els.countDisplay.textContent = learnedAnswered && factoryAnswered
      ? countOf(sequences.length + untunedFactory.length, "sequence", "sequences")
      : "";

    // The empty state is a finding, so it needs both answers: nothing learned
    // AND nothing from the factory.
    if (learnedAnswered && factoryAnswered && sequences.length === 0 && untunedFactory.length === 0) {
      els.emptyState.classList.remove("hidden");
      els.populatedState.classList.add("hidden");
      els.cardsContainer.innerHTML = "";
      return;
    }
    els.emptyState.classList.add("hidden");
    els.populatedState.classList.remove("hidden");
    els.filter.querySelectorAll("button").forEach((button) =>
      button.setAttribute("aria-pressed", String(button.dataset.value === listShow)));

    const cap = learnedSequenceCap();
    let yours = "";
    if (listShow !== "factory") {
      let said = "";
      if (!learnedAnswered) {
        said = WAITING_SLOT;
      } else if (sequences.length === 0) {
        // "below" only where the Factory group is on screen under this one.
        const where = listShow === "yours" ? "" : " below";
        said = `<p class="hint seq-section-empty"><b>Nothing of your own yet.</b> Tune a factory sequence${where} and it lands here.</p>`;
      } else if (cap !== null && sequences.length > cap) {
        // A firmware-only update can leave a droid holding more than it now
        // stores. Everything it holds still lists and plays; only a new save
        // waits until the builder is under the cap (ADR 0065).
        said = `<p class="hint seq-over-cap"><b>This droid stores ${cap}.</b> Delete down to ${cap - 1} to save a new one.</p>`;
      }
      yours = groupHtml("Yours", '<span class="seq-group-count" id="seq-capacity-display" role="status"></span>', said)
        + (learnedAnswered ? sequences.map((seq) => renderSeqRow(seq)).join("") : "");
    }

    let factory = "";
    if (listShow !== "yours" && !factoryAnswered) {
      factory = groupHtml("Factory", "", WAITING_SLOT);
    } else if (listShow !== "yours" && untunedFactory.length > 0) {
      factory = groupHtml("Factory", `<span class="seq-group-count">${untunedFactory.length} built in</span>`)
        + untunedFactory.map((builtin) => renderFactoryRow(builtin)).join("");
    }

    els.cardsContainer.innerHTML = `
      <table class="seq-table">
        <thead>
          <tr>
            <th scope="col">Name</th>
            <th scope="col">What it does</th>
            <th scope="col" class="seq-count-cell">Steps</th>
            <th scope="col" class="seq-count-cell">Runs</th>
            <th scope="col"></th>
          </tr>
        </thead>
        ${yours}
        ${factory}
      </table>`;

    // How many of the builder's own the droid holds, against the cap it
    // reports (Learned sequences only). Empty while the list has not answered.
    const capacityEl = document.getElementById("seq-capacity-display");
    if (capacityEl) {
      capacityEl.textContent = !learnedAnswered ? ""
        : cap === null ? `${sequences.length} saved` : `${sequences.length} / ${cap} saved`;
    }

    // Every act on a row carries data-action, and nothing else in the table does.
    els.cardsContainer.querySelectorAll("[data-action]").forEach((btn) => {
      btn.addEventListener("click", () => handleSeqAction(btn));
    });
  };

  // When the droid says a sequence was last saved, as the list words it:
  // "saved 30 Sep 19:42". Nothing when the droid gave no time it can be read as.
  const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
  const savedWords = (modified) => {
    const at = modified ? new Date(modified) : null;
    if (!at || Number.isNaN(at.getTime())) return "";
    const two = (n) => String(n).padStart(2, "0");
    return `saved ${at.getDate()} ${MONTHS[at.getMonth()]} ${two(at.getHours())}:${two(at.getMinutes())}`;
  };

  // What a list row says about its sequence in the Steps column and beside
  // its purpose: only what the droid reported. GET /api/seq/builtins sends a
  // Factory sequence's purpose and step count; GET /api/seq/list sends neither
  // for a Learned one (src/web/api_seq.cpp), and nothing sends how long a
  // sequence runs, so those cells stay empty rather than read 0.
  const reportedSteps = (entry) => (Number.isInteger(entry.stepCount) ? entry.stepCount : "");
  const purposeHtml = (entry) =>
    (entry.purpose ? `<span class="seq-purpose">${window.PAUtils.escapeHtml(entry.purpose)}</span> ` : "");

  // What a row says back - a test's outcome, its Rehearsal - on a line of its
  // own under it.
  const rowSaidHtml = (rehearsal) => `
        <tr class="seq-item-said">
          <td colspan="${LIST_COLUMNS}">
            <div class="seq-item-feedback feedback hidden"></div>
            ${rehearsal ? '<div class="seq-item-rehearsal"></div>' : ""}
          </td>
        </tr>`;

  const renderSeqRow = (seq) => {
    const name = window.PAUtils.escapeAttr(seq.name);
    const badges = [];
    if (seq.retrained) {
      badges.push(
        `<span class="seq-badge seq-badge-retrained" title="This sequence shadows the factory ${name}">Retrained</span>`
      );
    }
    if (seq.valid === false) {
      badges.push(
        `<span class="seq-badge seq-badge-invalid" title="This sequence fails Protocol Check and cannot be run until repaired">Invalid</span>`
      );
    }
    const saved = savedWords(seq.modified);

    // Share to project: only the operator's own custom sequences (not factory-derived).
    const isCustom = !seq.source || seq.source === "user";
    const shareBtn = isCustom
      ? `<button type="button" class="seq-act" data-action="share" data-seq-name="${name}" title="Open a pre-filled GitHub issue to share this sequence with the project">Share to project</button>`
      : "";

    const testBtnDisabled = seq.valid === false ? 'disabled title="Invalid sequence cannot be run — edit to repair"' : `data-seq-name="${name}"`;
    const more = moreOpen.has(seq.name);

    return `
      <tbody class="seq-item" data-seq-name="${name}">
        <tr>
          <th scope="row"><span class="seq-name">${window.PAUtils.escapeHtml(seq.name)}</span>${badges.join("")}</th>
          <td>${purposeHtml(seq)}${saved ? `<span class="seq-meta">${saved}</span>` : ""}</td>
          <td class="seq-count-cell">${reportedSteps(seq)}</td>
          <td class="seq-count-cell"></td>
          <td class="seq-item-acts">
            <span class="seq-acts">
              <button type="button" class="seq-act is-strong" data-action="edit" data-seq-name="${name}">Edit</button>
              <button type="button" class="seq-act" data-action="test" ${testBtnDisabled}>Test</button>
              <button type="button" class="seq-act seq-disclose" data-action="more" data-seq-name="${name}" aria-expanded="${more}">${chevron}More</button>
              <span class="seq-item-more${more ? "" : " hidden"}">
                <button type="button" class="seq-act" data-action="duplicate" data-seq-name="${name}">Duplicate</button>
                <button type="button" class="seq-act" data-action="export" data-seq-name="${name}">Export</button>
                ${shareBtn}
                <button type="button" class="seq-act seq-act-danger" data-action="memory-wipe" data-seq-name="${name}">Memory Wipe</button>
              </span>
            </span>
          </td>
        </tr>
        ${rowSaidHtml(true)}
      </tbody>
    `;
  };

  const renderFactoryRow = (builtin) => {
    const name = window.PAUtils.escapeAttr(builtin.name);
    const group = builtin.toggleGroup && builtin.toggleGroup !== "none"
      ? `<span class="seq-meta">interrupt group ${window.PAUtils.escapeHtml(builtin.toggleGroup[0].toUpperCase() + builtin.toggleGroup.slice(1))}</span>`
      : "";

    return `
      <tbody class="seq-item seq-item-factory" data-seq-name="${name}">
        <tr>
          <th scope="row"><span class="seq-name">${window.PAUtils.escapeHtml(builtin.name)}</span></th>
          <td>${purposeHtml(builtin)}${group}</td>
          <td class="seq-count-cell">${reportedSteps(builtin)}</td>
          <td class="seq-count-cell"></td>
          <td class="seq-item-acts">
            <span class="seq-acts">
              <button type="button" class="seq-act is-strong" data-action="tune" data-builtin-name="${name}" title="Open to edit. Save under the same name to retrain it.">Tune</button>
              <button type="button" class="seq-act" data-action="timeline" data-builtin-name="${name}">Timeline</button>
            </span>
          </td>
        </tr>
        ${rowSaidHtml(false)}
      </tbody>
    `;
  };

  // =========================================================================
  // Tune a Factory Sequence
  // =========================================================================

  const handleCloneBuiltin = async (builtinName) => {
    // The builtins list carries metadata only; fetch the one factory
    // sequence's full step data on demand (keeps the catalog response small).
    let full = null;
    try {
      const result = await PAApi.get(
        `/api/seq/builtins?name=${encodeURIComponent(builtinName)}`
      );
      full = result.data;
    } catch (error) {
      console.error("Error loading factory sequence:", error);
      return;
    }
    if (!full) return;

    // Store for editor to load
    currentEditingSeq = JSON.parse(JSON.stringify(full));
    editorState.isNew = true; // Cloning is treated as new sequence
    editorState.tuningFactory = full.name; // Mark that we're tuning this Factory sequence

    // Render editor with this builtin
    renderEditorView(currentEditingSeq);

    // The clone is where a factory routine's defects pass into a builder's own
    // work, so what the Rehearsal finds in the Factory sequence as it ships is
    // read here (#287): its tab comes forward when it has something to say.
    const report = window.SeqRehearsal?.rehearse(full, rehearsalContext());
    if (report && report.findings.length > 0) showTab("rehearsal");
  };

  // =========================================================================
  // The stage (#440, ADR 0062; #441, ADR 0057)
  //
  // A sequence read as time, two ways through one view (data/seq_timeline.js),
  // on one stage: the routine's lanes, and the droid beside them at the
  // marker's moment.
  //
  // A Factory sequence opens read-only, from GET /api/seq/builtins: the builder
  // has not made it theirs, and Tune is the act that does.
  //
  // The builder's own opens in the workspace: one sequence object,
  // editorState.current, behind the timeline and the step list alike, so an
  // edit in one is there in the other and Save sends it either way
  // (showSessionView() below).
  //
  // Neither moves the droid except on the pose press, and that poses what the
  // droid has stored under the name - never the edits on screen.
  // =========================================================================

  // The stage's markup, for both. `ids` names the three places the timeline
  // draws into, for the workspace, which finds them by id; `above`, `views`
  // and `below` are what the workspace adds round the routine.
  const stageHtml = ({ ids = false, above = "", views = "", below = "" } = {}) => `
      <div class="seq-stage">
        <div class="seq-stage-main">
          ${above}
          <div class="seq-stagebar">
            ${views}
            <div class="tl-bar"${ids ? ' id="seq-editor-tlbar"' : ""}></div>
          </div>
          <div class="seq-lanes"${ids ? ' id="seq-editor-timeline"' : ""}></div>
          ${below}
        </div>
        <aside class="seq-stage-side"${ids ? ' id="seq-editor-droid"' : ""} aria-label="The droid at the marker"></aside>
      </div>`;

  // What opens in place of the list starts at its strip, however far down
  // the list the row that opened it was.
  const showFromTheTop = () => window.scrollTo?.(0, 0);

  // The way back to the list, as the strip's first act.
  const backHtml = (attrs) =>
    `<button type="button" class="seq-act seq-back" ${attrs}><svg class="i" aria-hidden="true" focusable="false"><use href="#i-arrow-left"/></svg>All sequences</button>`;

  const closeTimeline = () => {
    if (timeline) {
      timeline.destroy();
      timeline = null;
    }
    els.timelineView.innerHTML = "";
    els.timelineView.classList.add("hidden");
  };

  // The one request the pose press sends: the sequence's name and the instant.
  // What the droid does at that instant, and how far apart, is the firmware's
  // to work out from what it stores (include/sequence_pose.h); a latched estop
  // or Sleep Mode refuses it there, in words this shows.
  const poseOnDroid = (name, atMs, which = "") =>
    PAApi.postJson("/api/seq/pose", { name, t: atMs })
      .then(() => ({ text: `Moving the droid to ${(atMs / 1000).toFixed(2)} s${which}, one part at a time.`, level: "ok" }))
      .catch((error) => ({ text: PAApi.messageFor(error), level: "error" }));

  // A Factory sequence's stage: the strip carries the way back and Tune, and
  // nothing on it edits - the timeline is handed no `edit`, and there is no
  // drawer.
  const handleOpenTimeline = async (builtinName, rowEl = null) => {
    const sayOnRow = rowSayer(builtinName, rowEl);
    if (!window.SeqTimeline) {
      sayOnRow("The timeline did not load. Reload the page to try again.");
      return;
    }
    let seq = null;
    try {
      const result = await PAApi.get(`/api/seq/builtins?name=${encodeURIComponent(builtinName)}`);
      seq = result.data;
    } catch (error) {
      sayOnRow(`Could not read ${builtinName}: ${PAApi.messageFor(error)}`);
      return;
    }
    if (!seq || !Array.isArray(seq.steps)) {
      sayOnRow(`The droid sent ${builtinName} back with no steps.`);
      return;
    }
    closeTimeline();
    els.timelineView.innerHTML = `
      <div class="seq-work">
        <div class="seq-strip">
          ${backHtml('data-stage-act="back"')}
          <span class="seq-name">${window.PAUtils.escapeHtml(seq.name || builtinName)}</span>
          <span class="seq-badge" title="Built-in Factory sequence">Factory</span>
          <span class="seq-gap"></span>
          <button type="button" class="seq-act is-strong" data-stage-act="tune" title="Open to edit. Save under the same name to retrain it.">Tune</button>
        </div>
        ${stageHtml()}
      </div>`;
    els.timelineView.classList.remove("hidden");
    const within = (selector) => els.timelineView.querySelector(selector);
    within(".seq-strip").addEventListener("click", (event) => {
      const act = event.target?.closest?.("[data-stage-act]")?.dataset.stageAct;
      if (!act) return;
      closeTimeline();
      renderListView();
      if (act === "tune") handleCloneBuiltin(builtinName);
    });
    timeline = window.SeqTimeline.mount(
      { bar: within(".tl-bar"), lanes: within(".seq-lanes"), side: within(".seq-stage-side") },
      seq,
      {
        context: rehearsalContext(),
        describe: stepPreview,
        onPose: (atMs) => poseOnDroid(builtinName, atMs),
      },
    );
    renderListView();
    showFromTheTop();
  };

  // A list row's own feedback line, for an open that failed.
  const rowSayer = (name, rowEl) => (message) => {
    console.error(`[seq] opening ${name}: ${message}`);
    const feedbackEl = rowEl?.querySelector(".seq-item-feedback");
    if (!feedbackEl) return;
    feedbackEl.textContent = message;
    feedbackEl.className = "seq-item-feedback feedback error";
  };

  // The steps of a sequence as the units Protocol Check orders: a step, or a
  // loop with the steps it repeats, which travel with it.
  const stepUnits = (steps) => {
    const units = [];
    for (let at = 0; at < steps.length; ) {
      const step = steps[at] || {};
      const body = step.type === "loop" && step.body > 0 ? Math.min(step.body, steps.length - at - 1) : 0;
      units.push({ at, size: body + 1, t: Number(step.t) || 0 });
      at += body + 1;
    }
    return units;
  };

  // Protocol Check refuses a step timed before the one above it, so a block
  // dragged past its neighbour changes places with it in the step list. The
  // end step, and anything written after it, stays where it is.
  const orderSteps = () => {
    const steps = editorState.current.steps;
    const units = stepUnits(steps);
    const endAt = steps.findIndex((step) => step && step.type === "end");
    const tail = endAt === -1 ? units.length : units.findIndex((unit) => unit.at + unit.size > endAt);
    const order = [...units.slice(0, tail).sort((a, b) => a.t - b.t), ...units.slice(tail)]
      .flatMap((unit) => Array.from({ length: unit.size }, (_, k) => unit.at + k));
    if (order.every((from, to) => from === to)) return;
    editorState.current.steps = order.map((from) => steps[from]);
    editorState.expanded = new Set([...editorState.expanded].map((from) => order.indexOf(from)));
  };

  // Remove steps by index: the one removal, for the step list and the timeline
  // alike. A loop is one object: removing it takes the steps it repeats,
  // removing one of those shortens it, and a loop left repeating nothing goes
  // too - otherwise it would reach for the step after it.
  const removeSteps = (indices) => {
    const steps = editorState.current.steps;
    const gone = new Set(indices);
    const shorter = [];
    stepUnits(steps).forEach((unit) => {
      if (unit.size === 1) return;
      const members = Array.from({ length: unit.size - 1 }, (_, k) => unit.at + 1 + k);
      if (gone.has(unit.at)) {
        members.forEach((member) => gone.add(member));
        return;
      }
      const left = members.filter((member) => !gone.has(member)).length;
      if (left === 0) gone.add(unit.at);
      else if (left < members.length) shorter.push([steps[unit.at], left]);
    });
    if (gone.size === 0) return;
    historyPush();
    shorter.forEach(([loop, left]) => {
      loop.body = left;
    });
    editorState.current.steps = steps.filter((_, index) => !gone.has(index));
    // A card still there stays open, at the place it has moved up to.
    const removed = [...gone];
    editorState.expanded = new Set(
      [...editorState.expanded]
        .filter((index) => !gone.has(index))
        .map((index) => index - removed.filter((at) => at < index).length));
    rerenderStepTable();
    edited();
  };

  const closeSessionTimeline = () => {
    if (sessionTimeline) {
      sessionTimeline.destroy();
      sessionTimeline = null;
    }
  };

  // Put the timeline of the routine being edited on the workspace's stage. It
  // is handed a way to read editorState.current, never a copy of it, and the
  // three things an edit there needs from the editor: the history's two
  // brackets, and removal, which changes which steps there are. It stays
  // mounted while the step list is shown in its place, because the droid
  // beside it is its to draw.
  const mountSessionTimeline = () => {
    closeSessionTimeline();
    if (!window.SeqTimeline) return;
    const hosts = {
      bar: document.getElementById("seq-editor-tlbar"),
      lanes: document.getElementById("seq-editor-timeline"),
      side: document.getElementById("seq-editor-droid"),
    };
    sessionTimeline = window.SeqTimeline.mount(hosts, () => editorState.current, {
      context: rehearsalContext(),
      describe: stepPreview,
      onPose: (atMs) => {
        // The droid poses what it stores, so a sequence never saved has
        // nothing to pose, and unsaved edits are not in the pose.
        const name = editorState.tuningFactory || (editorState.isNew ? null : editorState.original?.name);
        if (!name) return Promise.resolve({ text: "Save it first, so the droid has the routine to move to.", level: "error" });
        return poseOnDroid(name, atMs, sessionDirty() ? " of the routine as last saved" : "");
      },
      edit: {
        begin: historyBegin,
        commit: (before) => {
          orderSteps();
          historyCommit(before);
          rerenderStepTable();
          edited();
        },
        remove: removeSteps,
      },
      onPicked: showPicked,
    });
  };

  // Show the routine being edited as its timeline or as its step list, in the
  // stage's one column. The droid beside it and the drawer under it stay.
  const showSessionView = (view) => {
    let shown = view === "steps" ? "steps" : "timeline";
    if (shown === "timeline" && !sessionTimeline) {
      showEditorFeedback("The timeline did not load. Reload the page to try again.", "error");
      shown = "steps";
    }
    editorState.view = shown;
    const onTimeline = shown === "timeline";
    document.getElementById("seq-editor-steps")?.classList.toggle("hidden", onTimeline);
    document.getElementById("seq-editor-timeline")?.classList.toggle("hidden", !onTimeline);
    ["steps", "timeline"].forEach((name) =>
      document.getElementById(`seq-editor-show-${name}`)?.setAttribute("aria-pressed", String(name === shown)));
    // The lanes go out of sight with the timeline still mounted, so a block
    // held at that moment is let go where the press found it, and the bar
    // stops offering how a loop is drawn.
    document.getElementById("seq-editor-tlbar")?.classList.toggle("is-steps", !onTimeline);
    if (!onTimeline) sessionTimeline?.cancel();
  };

  // -------------------------------------------------------------------------
  // The drawer: Picked block, Parts, Sequence, Rehearsal. Every pane is drawn
  // once when a sequence opens and only shown or hidden after, so the fields
  // in them keep the listeners bound at open.
  // -------------------------------------------------------------------------
  const DRAWER_TABS = ["block", "parts", "sequence", "rehearsal"];

  const showTab = (tab) => {
    editorState.tab = DRAWER_TABS.includes(tab) ? tab : "block";
    DRAWER_TABS.forEach((name) => {
      document.getElementById(`seq-pane-${name}`)?.classList.toggle("hidden", name !== editorState.tab);
      document.getElementById(`seq-editor-tab-${name}`)?.setAttribute("aria-pressed", String(name === editorState.tab));
    });
  };

  // What kind of step this is, as the step list names it.
  const stepKindName = (step) =>
    (step.type === "dome" ? domeSubmodeLabel(step.cmd).name : stepTypeName[step.type] || step.type || "Step");

  // The Picked block tab: what the timeline says is picked. One block shows
  // where it starts, which can be typed; several can be moved together and
  // removed. What a block of each kind does is edited in the step list until
  // the inspector's rows for it land here (#441).
  const pickedHtml = (blocks) => {
    const esc = window.PAUtils.escapeHtml;
    const head = (name, sub) =>
      `<div class="sect seq-picked-head"><h3>${esc(name)}</h3><span class="sub">${esc(sub)}</span></div>`;
    if (blocks.length === 0) {
      return head("Nothing picked", "no block")
        + '<p class="hint">Press a block to change it. Shift-press adds another.</p>';
    }
    const stepCount = new Set(blocks.flatMap((block) => block.steps)).size;
    const remove = `<div class="seq-picked-acts"><button type="button" class="seq-act" data-picked="remove">${stepCount > 1 ? `Remove ${stepCount} steps` : "Remove"}</button></div>`;
    if (blocks.length > 1) {
      const nudge = window.SeqTimeline;
      return head(`${blocks.length} blocks`, countOf(stepCount, "step", "steps"))
        + `<p class="hint">Drag one and they all move. Arrow keys nudge ${nudge.NUDGE_MS} ms, Shift ${nudge.NUDGE_BIG_MS} ms.</p>`
        + remove;
    }
    const block = blocks[0];
    const at = block.steps[0];
    const step = editorState.current.steps[at] || {};
    const beat = beatWords(step);
    return head(block.name || block.words || stepKindName(step), `${stepKindName(step)} · step ${at + 1}`)
      + `<div class="setting-rows seq-picked-rows">
          <div class="setting-row">
            <span class="setting-name">Starts at</span>
            <span class="seq-row-ctl">
              <span class="setting-number">
                <input class="number-cell" type="number" min="0" max="120000" step="10" value="${Math.round(block.t0)}" data-picked="start" aria-label="Starts at, in milliseconds">
                <span class="setting-unit">ms</span>
              </span>
              ${beat ? `<span class="seq-unit">${esc(beat)}</span><button type="button" class="seq-act" data-picked="off-beat">Off the beat</button>` : ""}
            </span>
            <span class="setting-value"></span>
          </div>
        </div>`
      + remove;
  };

  // Called by the timeline whenever it draws the routine: the pane is written
  // again only when what it says has changed, so a number half typed into it
  // is not wiped by a redraw. The press that picked a block brings it forward.
  const showPicked = (blocks, pressed = false) => {
    pickedBlocks = blocks;
    const html = pickedHtml(blocks);
    const pane = document.getElementById("seq-pane-block");
    if (pane && html !== pickedShown) {
      pane.innerHTML = html;
      pickedShown = html;
    }
    if (pressed) showTab("block");
  };

  // The Parts tab: every Part a routine can name, as one flat list, dome and
  // body together, the ones nothing on this droid can move greyed and counted.
  // Read-only: a Part is put on the timeline by a step that names it, until
  // dropping one from here lands (#441). The escape-hatch Output slots and the
  // dome's buttons are not Parts a routine moves.
  const UNLISTED_SECTIONS = ["other_slots", "dome_fixtures"];

  const paintParts = () => {
    const list = document.getElementById("seq-editor-parts");
    const sub = document.getElementById("seq-editor-parts-sub");
    if (!list || !sub) return;
    const esc = window.PAUtils.escapeHtml;
    const context = rehearsalContext();
    const parts = (window.DroidParts?.parts || []).filter((part) => !UNLISTED_SECTIONS.includes(part.section));
    // The timeline's own rule for a lane it dims (data/seq_timeline.js
    // notWired()), which says nothing of a half the droid has not reported.
    const off = (part) => Boolean(window.SeqTimeline?.notWired(part.id, part.half, context));
    const reported = context.outputs !== null;
    sub.textContent = countOf(parts.length, "part", "parts")
      + (reported ? ` · ${parts.filter(off).length} not wired` : "");
    list.innerHTML = parts
      .map((part) =>
        `<span class="seq-part${off(part) ? " is-off" : ""}">${part.shorthand ? `<span class="seq-part-short">${esc(part.shorthand)}</span>` : ""}${esc(part.name)}</span>`)
      .join("");
  };

  // =========================================================================
  // Leaving an edit (#441, operator decision 2026-09-30)
  //
  // An unsaved edit is not kept, and it is never dropped without being asked
  // about: leaving the editor, opening another sequence, or leaving the
  // Sequences surface with edits unsaved asks once, and nothing is written to
  // browser storage. The question is a dialog of this surface, like the other
  // two, so the estop on the chrome stays live behind it (ADR 0048); a browser
  // confirm() would freeze the whole page, STOP included.
  // =========================================================================
  // Key order is not a difference: an undo can put a field back in another
  // place than the one it was read in.
  const canonical = (value) =>
    JSON.stringify(value, (_key, part) =>
      part && typeof part === "object" && !Array.isArray(part)
        ? Object.fromEntries(Object.keys(part).sort().map((key) => [key, part[key]]))
        : part);

  const sessionDirty = () =>
    editorState.current !== null && canonical(editorState.current) !== canonical(editorState.original);

  const closeSession = () => {
    closeSessionTimeline();
    els.editorView.classList.add("hidden");
    currentEditingSeq = null;
    Object.assign(editorState, {
      original: null, current: null, isNew: false, tuningFactory: null, expanded: new Set(),
      view: "timeline", tab: "block", saved: false,
    });
    historyReset();
    renderListView();
    refreshLearned();
  };

  // What the builder was on the way to when asked: `go` once the edit is
  // discarded, `stay` if they keep editing.
  let asked = null;

  const askDiscard = (go, stay = null) => {
    asked = { go, stay };
    // Named by what the droid holds, not by a name typed since: the saved
    // sequence under the name it was saved as, a Factory sequence being tuned
    // under its own, and one never saved under the name it has now.
    const typedName = editorState.current.name || "This sequence";
    if (editorState.tuningFactory) {
      els.discardWhat.textContent = `${editorState.tuningFactory} stays the factory sequence.`;
    } else if (editorState.isNew) {
      els.discardWhat.textContent = `${typedName} was never saved. Discard it and it is gone.`;
    } else {
      els.discardWhat.textContent = `${editorState.original.name || typedName} goes back to how it was last saved.`;
    }
    showModal(els.modalDiscard);
  };

  const answerDiscard = (discard) => {
    const answered = asked;
    asked = null;
    hideModal(els.modalDiscard);
    if (!answered) return;
    if (discard) {
      closeSession();
      answered.go();
    } else if (answered.stay) {
      answered.stay();
    }
  };

  // Close the edit that is open, asking first when it has unsaved edits, and
  // then do `then`. With no edit open it is just `then`.
  const leaveSession = (then) => {
    if (editorState.current === null) {
      then();
      return;
    }
    if (sessionDirty()) {
      askDiscard(then);
      return;
    }
    closeSession();
    then();
  };

  // Every way a dialog closes without a verb being pressed - the overlay,
  // Escape - is an answer too: for the discard question it is "keep editing".
  const dismissModal = (modal) => {
    if (modal === els.modalDiscard) answerDiscard(false);
    else hideModal(modal);
  };

  // =========================================================================
  // Full Editor View
  // =========================================================================

  // Validate a step and return validation result
  const validateStepForCard = (step, stepIdx) => {
    return SeqProtocolCheck.validateStep(step, stepIdx, editorState.current.steps);
  };

  // Plain-English preview text for each step type
  const stepPreview = (step) => {
    switch (step.type) {
      case "audio":
        return `Play sound (${step.cmd || "$H"})`;
      case "dome": {
        const cmd = step.cmd || "";
        // Visual preset mode
        if (cmd.startsWith("DV:")) {
          const presetName = cmd.slice(3);
          return `Visual preset: ${dvPresetLabel(presetName)}`;
        }
        // Logic/PSI mode
        if (cmd.startsWith("DL:")) {
          const parts = cmd.split(":");
          if (parts.length >= 3) {
            const target = parts[1];
            const mode = parts[2];
            const color = parts[3] || "";
            const duration = parts[4] || "";
            let preview = `${dlTargetLabel(target)}: ${dlModeLabel(mode)}`;
            if (color && color !== "DEFAULT") {
              preview += `, ${dlColorLabel(color)}`;
            }
            if (duration) {
              preview += `, ${duration}s`;
            }
            return preview;
          }
          return `Logic/PSI: ${cmd.slice(3)}`;
        }
        // Logic Text mode
        if (cmd.startsWith("DT:")) {
          const parts = cmd.split(":");
          if (parts.length >= 5) {
            const target = parts[1];
            const color = parts[2];
            const duration = parts[3];
            const speed = parts[4];
            const encodedText = parts.slice(5).join(":");
            // Decode percent-encoded text
            let decodedText = "";
            try {
              decodedText = decodeURIComponent(encodedText);
            } catch (e) {
              decodedText = encodedText;
            }
            // Render newline visibly for preview
            const displayText = decodedText.replace(/\n/g, " / ");
            return `${dtTargetLabel(target)} text: "${displayText}"`;
          }
          return `Logic text: ${cmd.slice(3)}`;
        }
        // Holo Effect mode
        if (cmd.startsWith("DH:")) {
          const parts = cmd.split(":");
          if (parts.length >= 3) {
            const target = parts[1];
            const effect = parts[2];
            const color = parts[3] || "";
            const durationOrCount = parts[4] || "";
            let preview = `${dhTargetLabel(target)}: ${dhEffectLabel(effect)}`;
            if (color && color !== "DEFAULT") {
              preview += `, ${dhColorLabel(color)}`;
            }
            if (durationOrCount) {
              preview += `, ${durationOrCount}`;
            }
            return preview;
          }
          return `Holo: ${cmd.slice(3)}`;
        }
        // Panel intent mode: parse action and target
        if (/^(:|)(OP|CL|OF)/.test(cmd)) {
          const match = cmd.match(/^:?(OP|CL|OF)(.+)$/);
          if (match) {
            const action = match[1];
            const target = match[2];
            const actionLabel = action === "OP" ? "Open" : action === "CL" ? "Close" : "Flutter";
            const targetLabel = target === "00" ? "all panels" : target === "14" ? "top group" : target === "15" ? "bottom group" : target.startsWith("P") ? `pie ${target}` : `ring ${target}`;
            return `${actionLabel} ${targetLabel} (:${action}${target})`;
          }
        }
        // Advanced mode
        return `Dome command ${cmd || "@0T6"}`;
      }
      case "domeRotate": {
        const speedPct = step.speedPct ?? 0;
        const durationMs = step.durationMs ?? 0;
        if (speedPct === 0) {
          return "Stop dome (neutral)";
        }
        const direction = speedPct < 0 ? "left" : "right";
        const speed = Math.abs(speedPct);
        return `Rotate ${direction} at ${speed}% for ${durationMs}ms`;
      }
      case "loop": {
        const body = step.body || 1;
        const periodMs = step.periodMs || 1000;
        const durationMs = step.durationMs || 10000;
        return `Repeat next ${body} steps every ${periodMs}ms for ${durationMs}ms`;
      }
      case "random": {
        const setMap = { ring: "ring panels", pie: "pie panels", all: "all panels", hold: "hold" };
        const set = step.set || "ring";
        const moveMs = step.moveMs ?? 300;
        const setLabel = setMap[set] || set;
        return `Random flutter on ${setLabel} (move ${moveMs}ms)`;
      }
      case "audioCat": {
        const category = step.category || "alert";
        return `Play a ${category} sound (fallback ${audioFallbackLabel(step.fallback)})`;
      }
      case "sequence":
        return phraseName(step);
      case "gesture": {
        const G = window.SeqGesture;
        const set = G && step.set ? G.setOf(step.set)?.label || step.set : `${(step.parts || []).length} parts`;
        const spread = G?.SPREADS.find((x) => x.id === (step.spread || "together"))?.id || step.spread || "together";
        return `${set}: ${step.shape || "open"}, ${spread}`;
      }
      case "body": {
        // A Body Step names a Part and a Move Shape (ADR 0049). A light Part
        // hears the same three stored words as on, off and flash, so it reads
        // that way here (CONTEXT.md "Move Shape").
        const part = (window.DroidParts?.parts || []).find((entry) => entry.id === step.part);
        const words = window.DroidPartKind?.isLight(part)
          ? { open: "On", close: "Off", flutter: "Flash" }
          : { open: "Open", close: "Close", flutter: "Flutter" };
        const shape = step.shape || "open";
        const howFar = step.howFar ? `, ${step.howFar}%` : "";
        return `${words[shape] || shape} ${part ? part.name : step.part || "a part"}${howFar}`;
      }
      case "end":
        return "End of sequence";
      default:
        return "Unknown step";
    }
  };

  // Plain-English type names. There was a parallel map of emoji beside this
  // one, drawn in the step card and in the picker immediately next to the name
  // it stood for, and it is gone: an operator surface carries no pictograph
  // (ADR 0066) and the word was already doing the whole job.
  const stepTypeName = {
    audio: "Sound",
    dome: "Panel Action",
    domeRotate: "Spin Dome",
    loop: "Servo Loop",
    random: "Random Flutter",
    audioCat: "Sound Category",
    gesture: "Gesture",
    sequence: "Sequence",
    body: "Body Step",
    end: "Sequence End",
  };

  // Helper: which dome sub-mode a step's cmd is, by name, for the collapsed card
  const domeSubmodeLabel = (cmd) => {
    if ((cmd || "").startsWith("DV:")) return { name: "Visual Preset" };
    if ((cmd || "").startsWith("DL:")) return { name: "Logic / PSI Mode" };
    if ((cmd || "").startsWith("DT:")) return { name: "Logic Text" };
    if ((cmd || "").startsWith("DH:")) return { name: "Holo Effect" };
    return { name: "Panel Action" };
  };

  // Step type descriptions for reference panel
  const stepTypeDescriptions = {
    audio: "Play a sound or cue",
    dome: "Open, close, apply visual presets, or control logic/PSI mood on dome panels",
    domeRotate: "Rotate the dome left or right",
    loop: "Repeat a group of steps at an interval",
    random: "Randomized panel motion",
    audioCat: "Play a random sound from a category",
    gesture: "One move across a set of parts, in order round the droid",
    sequence: "Another sequence, as one step, kept linked",
    end: "Mark the end of the sequence",
  };

  // Render the reference panel (What Each Step Type Does)
  const renderStepTypeReference = () => {
    return `
      <div class="step-type-reference">
        <button class="step-type-reference-toggle" type="button" aria-expanded="false" aria-controls="step-type-reference-panel">
          <svg class="i chev" aria-hidden="true" focusable="false"><use href="#i-chevron-right"/></svg>What does each step type do?
        </button>
        <div id="step-type-reference-panel" class="step-type-reference-panel hidden">
          <div class="step-type-reference-list">
            ${["audio", "dome", "domeRotate", "gesture", "sequence", "loop", "random", "audioCat", "end"]
              .map(
                (type) =>
                  `<div class="step-type-reference-item">
                    <span class="step-type-reference-name">${window.PAUtils.escapeHtml(stepTypeName[type])}</span>
                    <span class="step-type-reference-desc">${window.PAUtils.escapeHtml(stepTypeDescriptions[type])}</span>
                  </div>`
              )
              .join("")}
          </div>
        </div>
      </div>
    `;
  };

  const renderStepRow = (step, idx) => {
    const isExpanded = editorState.expanded.has(idx);
    let typeName = stepTypeName[step.type] || step.type;
    // For dome steps, derive identity from cmd sub-mode (DV:, DL:)
    if (step.type === "dome") {
      typeName = domeSubmodeLabel(step.cmd).name;
    }
    const preview = stepPreview(step);

    // Get validation state for this step
    const validation = validateStepForCard(step, idx);
    const isInvalid = !validation.ok;
    const errorId = `step-card-error-${idx}`;

    // Collapsed header (always visible)
    const headerHtml = `
      <div class="step-card-header" role="button" aria-expanded="${isExpanded}" tabindex="0" ${isInvalid ? `aria-invalid="true" aria-describedby="${errorId}"` : ""}>
        <span class="step-handle" title="Drag to reorder steps">⋯</span>
        <span class="step-number-label">Step ${idx + 1}</span>
        <span class="step-time-label">t=${step.t || 0}ms${beatLabel(step)}</span>
        <span class="step-card-type">${window.PAUtils.escapeHtml(typeName)}</span>
        <span class="step-card-preview">${window.PAUtils.escapeHtml(preview)}</span>
        ${isInvalid ? `<span class="step-card-error-badge" aria-hidden="true">!</span>` : ""}
        <button class="step-card-toggle" aria-label="${isExpanded ? "Collapse" : "Expand"} step" type="button" tabindex="-1">
          <svg class="i chev" aria-hidden="true" focusable="false"><use href="#i-chevron-right"/></svg>
        </button>
        <button class="step-remove" type="button" tabindex="-1">Remove</button>
      </div>
    `;

    // Expanded content (shown only when expanded)
    const expandedHtml = isExpanded ? `
      <div class="step-card-expanded">
        ${isInvalid ? `<div class="step-card-error-message" id="${errorId}" role="alert" aria-live="polite">
          <span class="step-card-error-icon">!</span>
          <span class="step-card-error-text">${window.PAUtils.escapeHtml(validation.error || "Invalid step")}</span>
        </div>` : ""}
        <div class="step-card-expanded-content">
          <div class="setting-rows step-rows">
            <div class="setting-row">
              <span class="setting-name">Starts at</span>
              <span class="setting-number">
                <input class="step-t" type="number" value="${step.t || 0}" min="0" max="120000" aria-label="Step time offset (ms)" placeholder="t (ms)" ${isInvalid && validation.field === "t" ? `aria-invalid="true"` : ""}>
                <span class="setting-unit">ms</span>
              </span>
              <span class="setting-value"></span>
            </div>
          </div>
          ${renderBeatPicker(step, idx)}

          <!-- What kind of step it is: one row of pills, the dome's four kinds
               of command among them. -->
          <div class="setting-rows step-rows">
            <div class="setting-row">
              <span class="setting-name">Kind</span>
              <span class="step-type-picker seq-pills" role="radiogroup" aria-label="Kind of step">
                ${["audio", "domeRotate", "dome"]
                  .map(
                    (type) =>
                      `<button class="step-type-chip step-type-card ${step.type === type && !(type === "dome" && /^(DL|DT|DH):/.test(step.cmd || "")) ? "active" : ""}" data-type="${type}" aria-pressed="${step.type === type ? "true" : "false"}"><span class="step-type-card-name">${window.PAUtils.escapeHtml(stepTypeName[type])}</span></button>`
                  )
                  .join("")}
                <button class="step-type-chip step-type-card step-type-dome-sub ${step.type === "dome" && (step.cmd || "").startsWith("DL:") ? "active" : ""}" data-type="dome" data-dome-mode="logic" aria-pressed="${step.type === "dome" && (step.cmd || "").startsWith("DL:") ? "true" : "false"}"><span class="step-type-card-name">Logic / PSI Mode</span></button>
                <button class="step-type-chip step-type-card step-type-dome-sub ${step.type === "dome" && (step.cmd || "").startsWith("DT:") ? "active" : ""}" data-type="dome" data-dome-mode="text" aria-pressed="${step.type === "dome" && (step.cmd || "").startsWith("DT:") ? "true" : "false"}"><span class="step-type-card-name">Logic Text</span></button>
                <button class="step-type-chip step-type-card step-type-dome-sub ${step.type === "dome" && (step.cmd || "").startsWith("DH:") ? "active" : ""}" data-type="dome" data-dome-mode="holo" aria-pressed="${step.type === "dome" && (step.cmd || "").startsWith("DH:") ? "true" : "false"}"><span class="step-type-card-name">Holo Effect</span></button>
                ${["gesture", "sequence", "loop", "random", "audioCat", "end"]
                  .map(
                    (type) =>
                      `<button class="step-type-chip step-type-card ${step.type === type ? "active" : ""}" data-type="${type}" aria-pressed="${step.type === type ? "true" : "false"}"><span class="step-type-card-name">${window.PAUtils.escapeHtml(stepTypeName[type])}</span></button>`
                  )
                  .join("")}
              </span>
              <span class="setting-value"></span>
            </div>
          </div>

          ${renderStepTypeReference()}

          <div class="step-fields" data-fields-for-type="${step.type}">
            <!-- Conditional fields populated by renderStepFields -->
          </div>
        </div>
      </div>
    ` : "";

    return `
      <div class="step-card ${isInvalid ? "step-card-invalid" : ""}" data-step-index="${idx}" data-step-type="${step.type}" draggable="true" ${isInvalid ? `aria-invalid="true"` : ""}>
        ${headerHtml}
        ${expandedHtml}
      </div>
    `;
  };

  // ---------------------------------------------------------------------------
  // Putting a step on a beat (ADR 0060): the builder picks the beat, from a
  // list of every beat with its bar number, rather than dragging a time until
  // it lands near one. The list and the span sit outside .step-fields, because
  // the form rebuilds a step from its [data-field] inputs and a beat is not a
  // form value: it is set on the step directly and the time resolved from it.
  // ---------------------------------------------------------------------------
  const tempoOf = () => {
    const tempo = editorState.current?.tempo;
    return tempo && SeqProtocolCheck.validateTempo(tempo).ok ? tempo : null;
  };

  // How far a routine reaches, for how long the beat list runs.
  const routineReachMs = () =>
    Math.max(...(editorState.current?.steps || []).map((step) => Number(step?.t) || 0), 0);

  // The beat a step is placed on, in words, or "" when it is on none.
  const beatWords = (step) => {
    const tempo = tempoOf();
    if (!tempo || !Number.isInteger(step.beat) || !window.SeqTempo) return "";
    const name = window.SeqTempo.beatName(tempo, step.beat);
    return name.bar === 0 ? `pickup beat ${name.beat}` : `bar ${name.bar}, beat ${name.beat}`;
  };

  const beatLabel = (step) => {
    const words = beatWords(step);
    return words ? ` &middot; ${words}` : "";
  };

  // Steps whose duration can be a span of beats: a turn that moves, and a
  // body flutter (src/seq_json.cpp parseStepBeats()).
  const spansBeats = (step) =>
    (step.type === "domeRotate" && Number(step.speedPct) !== 0) || (step.type === "body" && step.shape === "flutter");

  // ---------------------------------------------------------------------------
  // A sequence inside a sequence (ADR 0046). A step names its phrase by the
  // phrase's stable reference - a saved sequence's `id`, or a factory
  // sequence's name, which never changes - and shows it by its CURRENT name,
  // so a rename orphans nothing. A saved sequence with no id yet cannot be
  // picked until it is saved again, which mints one.
  // ---------------------------------------------------------------------------
  const phraseChoices = () => [
    ...sequences
      .filter((x) => x.id && x.name !== editorState.current?.name && x.toggleGroup === "none")
      .map((x) => ({ id: x.id, label: x.name })),
    ...builtins
      .filter((x) => (x.toggleGroup || "none") === "none" && x.name !== editorState.current?.name)
      .map((x) => ({ id: x.name, label: x.name })),
  ];

  const phraseName = (step) => {
    const ref = step?.ref;
    const learned = sequences.find((x) => x.id && x.id === ref);
    if (learned) return learned.name;
    if (typeof ref === "string" && ref.startsWith("DM:")) return ref;
    return step?.name ? `${step.name} (not on this droid)` : "Pick a sequence";
  };

  // A stable id for a sequence being saved that has none: eight lowercase hex
  // digits, never changed after (protocolCheckSeqIdValid()).
  const mintSequenceId = () => {
    const bytes = new Uint8Array(4);
    (window.crypto || globalThis.crypto).getRandomValues(bytes);
    return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
  };

  // The Gesture's form values: numbers that are optional, and words that are
  // optional (an unset word is its default).
  const GESTURE_NUMBER_FIELDS = ["howFar", "stepMs", "repeatMs", "extentMs", "speedMs"];
  const GESTURE_WORD_FIELDS = ["set", "shape", "spread", "direction", "start", "easing"];

  const renderBeatPicker = (step, idx) => {
    const tempo = tempoOf();
    if (!tempo || !window.SeqTempo) return "";
    if (SeqProtocolCheck.loopBodySteps(editorState.current.steps).has(idx)) return "";
    // Each bar is its number and its beats as one joined bar, the first beat
    // weighted; the bars sit side by side and wrap.
    const bars = window.SeqTempo.bars(tempo, Math.max(routineReachMs(), Number(step.t) || 0))
      .map((bar) => {
        const name = bar.bar === 0 ? "Pickup" : `Bar ${bar.bar}`;
        const beats = bar.beats
          .map(
            (b) =>
              `<button type="button" class="step-beat-pick${b.strong ? " strong" : ""}" data-beat="${b.index}" aria-pressed="${step.beat === b.index ? "true" : "false"}" aria-label="${name}, beat ${b.beat}">${b.beat}</button>`,
          )
          .join("");
        return `<span class="seq-bar"><span class="seq-bar-num" aria-hidden="true">${bar.bar === 0 ? "-" : bar.bar}</span><span class="seg seg-sm seq-beats" role="group" aria-label="${name}">${beats}</span></span>`;
      })
      .join("");
    const clear = Number.isInteger(step.beat)
      ? `<button type="button" class="seq-act step-beat-clear">Off the beat</button>`
      : "";
    const span = spansBeats(step)
      ? `<span class="seq-row-label">Lasts</span>
         <div class="seq-row-ctl">
           <input class="seq-num step-beat-span" type="number" min="1" max="1200" step="1" value="${Number.isInteger(step.spanBeats) ? step.spanBeats : ""}" placeholder="-" aria-label="How many beats it lasts">
           <span class="seq-unit">beats</span>
         </div>`
      : "";
    return `
      <div class="seq-rows">
        <span class="seq-row-label">Beat</span>
        <div class="seq-row-ctl"><div class="seq-bars">${bars}</div>${clear}</div>
        ${span}
      </div>`;
  };

  // Put the step at `stepIdx` on beat `beat` (null takes it off), or give its
  // duration a span of `spanBeats` beats (null clears it). The time and the
  // duration are resolved from the tempo, as the droid will.
  const setStepBeat = (stepIdx, patch) => {
    const step = { ...editorState.current.steps[stepIdx] };
    if ("beat" in patch) {
      if (patch.beat === null) delete step.beat;
      else step.beat = patch.beat;
    }
    ["spanBeats", "stepBeats", "repeatBeats", "extentBeats"].forEach((key) => {
      if (!(key in patch)) return;
      if (patch[key] === null) delete step[key];
      else step[key] = patch[key];
    });
    // The steps picked on the timeline, by index: this edit writes new steps
    // in their place and changes no step's place in the routine, so the same
    // indices are picked again once it is drawn.
    const pickedSteps = [...new Set(pickedBlocks.flatMap((block) => block.steps))];
    const before = historyBegin();
    editorState.current.steps[stepIdx] = step;
    editorState.current = SeqProtocolCheck.resolveBeats(editorState.current, { written: true });
    historyCommit(before);
    rerenderStepTable();
    edited();
    if (pickedSteps.length > 0) sessionTimeline?.pick(pickedSteps);
  };

  // Retime to the grid (ADR 0060), with its receipt: how many steps actually
  // landed on a beat. It is one entry in the editor's history, so the one
  // Undo takes it back, and the receipt goes with the next edit.
  const showRetime = (receipt) => {
    const receiptEl = document.getElementById("seq-editor-retime-receipt");
    const retimeBtn = document.getElementById("seq-editor-retime");
    if (retimeBtn) retimeBtn.classList.toggle("hidden", !tempoOf());
    if (receiptEl) receiptEl.textContent = receipt || "";
  };

  const retimeToGrid = () => {
    const result = window.SeqTempo?.retime(editorState.current);
    if (!result) return;
    const before = historyBegin();
    editorState.current = result.seq;
    historyCommit(before);
    rerenderStepTable();
    edited(`${result.landed} of ${result.total} steps landed on a beat.`);
  };

  // Helper to render a grouped field section with optional label
  const renderFieldGroup = (label, fieldsHtml) => {
    if (!fieldsHtml || fieldsHtml.trim() === "") return "";
    return `
      <div class="step-field-group">
        <div class="step-field-group-label">${window.PAUtils.escapeHtml(label)}</div>
        <div class="step-field-group-content">
          ${fieldsHtml}
        </div>
      </div>
    `;
  };

  // Helper to generate a contextual help line for a step
  const stepHelpLine = (step) => {
    switch (step.type) {
      case "audio":
        return "Plays a sound";
      case "dome": {
        const cmd = step.cmd || "";
        if (cmd.startsWith("DL:")) {
          const parts = cmd.split(":");
          if (parts.length >= 3) {
            const target = dlTargetLabel(parts[1]);
            const mode = dlModeLabel(parts[2]);
            return `Sets ${target} to ${mode}`;
          }
          return "Sets logic/PSI mood";
        }
        if (/^(:|)(OP|CL|OF)/.test(cmd)) {
          return "Operates dome panels";
        }
        return "Dome command";
      }
      case "visualPreset":
        return "Applies a dome visual preset";
      case "domeRotate": {
        const speedPct = step.speedPct ?? 0;
        const durationMs = step.durationMs ?? 0;
        if (speedPct === 0) {
          return "Stops dome rotation";
        }
        const direction = speedPct < 0 ? "left" : "right";
        const speed = Math.abs(speedPct);
        return `Rotates ${direction} at ${speed}% speed for ${durationMs}ms total`;
      }
      case "loop": {
        const body = step.body || 1;
        const periodMs = step.periodMs || 1000;
        const totalMs = (periodMs * (step.durationMs || 10000)) / periodMs || step.durationMs || 10000;
        return `Repeats ${body} step(s) every ${periodMs}ms for ~${totalMs}ms total`;
      }
      case "random": {
        const set = step.set || "ring";
        const moveMs = step.moveMs ?? 300;
        return `Randomly moves ${set} panels with ${moveMs}ms move time`;
      }
      case "audioCat": {
        const category = step.category || "alert";
        return `Plays a ${category} sound`;
      }
      case "end":
        return "Marks the end of the sequence";
      default:
        return "";
    }
  };

  const renderStepFields = (step, fieldsContainer) => {
    let behaviorHtml = "";
    let targetHtml = "";
    let timingHtml = "";

    switch (step.type) {
      case "audio":
        behaviorHtml = `<input class="step-field step-field-cmd" type="text" data-field="cmd" value="${window.PAUtils.escapeHtml(step.cmd || "")}" placeholder="$H, $N, $D, $A..." aria-label="Sound command">`;
        break;

      case "dome": {
        // Detect mode from step.cmd:
        // - Visual preset if starts with DV: → preset picker
        // - Logic/PSI if starts with DL: → structured DL: controls
        // - Panel intent if starts with :OP, :CL, :OF → panel action UI
        // - Otherwise advanced mode → raw text input
        const domeCmd = step.cmd || "";
        // Check for forced mode attribute (used during toggle)
        const forcedMode = fieldsContainer.dataset.domeMode;
        let domeMode; // "panel", "preset", "logic", or "advanced"
        if (forcedMode) {
          domeMode = forcedMode; // panel, preset, logic, or advanced
          // Clear the forced mode after use
          delete fieldsContainer.dataset.domeMode;
        } else {
          if (domeCmd.startsWith("DV:")) {
            domeMode = "preset";
          } else if (domeCmd.startsWith("DL:")) {
            domeMode = "logic";
          } else if (/^(:|)(OP|CL|OF)/.test(domeCmd)) {
            domeMode = "panel";
          } else {
            domeMode = "advanced";
          }
        }

        if (domeMode === "preset") {
          // Visual preset mode: dropdown of DV_PRESETS names
          const presetName = (step.cmd || "").slice(3); // Extract from "DV:NAME"
          behaviorHtml = `
            <select class="step-field step-field-preset" data-field="preset" aria-label="Visual preset">
              ${["ROCKMARCH", "VADER", "ALARM", "LEIA", "HEART", "CANTINA", "SCREAM", "OVERLOAD", "HELLO", "RESET_VISUALS"]
                .map(
                  (preset) =>
                    `<option value="${preset}" ${presetName === preset ? "selected" : ""}>${window.PAUtils.escapeHtml(dvPresetLabel(preset))}</option>`
                )
                .join("")}
            </select>
            <input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(step.cmd || "DV:ROCKMARCH")}">
            <button type="button" class="dome-mode-toggle seq-act" aria-label="Switch to advanced mode">Advanced</button>
          `;
        } else if (domeMode === "logic") {
          // Logic/PSI Mode (DL:) structured step
          // Grammar: DL:<target>:<mode>[:<color>[:<durationSec>]]
          const cmd = step.cmd || "DL:LOGIC:NORMAL";
          const parts = cmd.split(":");
          const target = parts[1] || "LOGIC";
          const mode = parts[2] || "NORMAL";
          const color = parts[3] || "DEFAULT";
          const duration = parts[4] || "";

          targetHtml = `
            <select class="step-field dl-target-select" data-field="target" aria-label="Target">
              <option value="FLD" ${target === "FLD" ? "selected" : ""}>Front logic (FLD)</option>
              <option value="RLD" ${target === "RLD" ? "selected" : ""}>Rear logic (RLD)</option>
              <option value="LOGIC" ${target === "LOGIC" ? "selected" : ""}>Both logic (LOGIC)</option>
              <option value="FPSI" ${target === "FPSI" ? "selected" : ""}>Front PSI (FPSI)</option>
              <option value="RPSI" ${target === "RPSI" ? "selected" : ""}>Rear PSI (RPSI)</option>
              <option value="PSI" ${target === "PSI" ? "selected" : ""}>Both PSI (PSI)</option>
              <option value="ALL" ${target === "ALL" ? "selected" : ""}>All logic + PSI (ALL)</option>
            </select>
          `;

          behaviorHtml = `
            <select class="step-field dl-mode-select" data-field="mode" aria-label="Mode">
              <option value="NORMAL" ${mode === "NORMAL" ? "selected" : ""}>Normal</option>
              <option value="ALARM" ${mode === "ALARM" ? "selected" : ""}>Alarm</option>
              <option value="FAILURE" ${mode === "FAILURE" ? "selected" : ""}>Failure</option>
              <option value="LEIA" ${mode === "LEIA" ? "selected" : ""}>Leia</option>
              <option value="MARCH" ${mode === "MARCH" ? "selected" : ""}>March</option>
              <option value="FLASHCOLOR" ${mode === "FLASHCOLOR" ? "selected" : ""}>Flash Color</option>
              <option value="REDALERT" ${mode === "REDALERT" ? "selected" : ""}>Red Alert</option>
              <option value="RAINBOW" ${mode === "RAINBOW" ? "selected" : ""}>Rainbow</option>
              <option value="LIGHTSOUT" ${mode === "LIGHTSOUT" ? "selected" : ""}>Lights Out</option>
            </select>
            <select class="step-field dl-color-select" data-field="color" aria-label="Color">
              <option value="DEFAULT" ${color === "DEFAULT" ? "selected" : ""}>Default</option>
              <option value="RED" ${color === "RED" ? "selected" : ""}>Red</option>
              <option value="BLUE" ${color === "BLUE" ? "selected" : ""}>Blue</option>
              <option value="GREEN" ${color === "GREEN" ? "selected" : ""}>Green</option>
              <option value="WHITE" ${color === "WHITE" ? "selected" : ""}>White</option>
              <option value="YELLOW" ${color === "YELLOW" ? "selected" : ""}>Yellow</option>
              <option value="ORANGE" ${color === "ORANGE" ? "selected" : ""}>Orange</option>
              <option value="PURPLE" ${color === "PURPLE" ? "selected" : ""}>Purple</option>
            </select>
          `;

          timingHtml = `
            <input class="step-field dl-duration-input" type="number" data-field="duration" value="${duration}" min="0" max="99" aria-label="Duration (seconds)" placeholder="duration (0-99s)">
            <span class="dome-rotate-label">s</span>
          `;

          // Store hidden cmd field for serialization
          behaviorHtml += `<input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(cmd)}">`;
        } else if (domeMode === "text") {
          // Logic Text Mode (DT:) structured step
          // Grammar: DT:<target>:<color>:<durationSec>:<speed>:<encodedText>
          const cmd = step.cmd || "DT:LOGIC:DEFAULT:5:0:";
          const parts = cmd.split(":");
          const target = parts[1] || "LOGIC";
          const color = parts[2] || "DEFAULT";
          const duration = parts[3] || "5";
          const speed = parts[4] || "0";
          const encodedText = parts.slice(5).join(":") || "";
          // Decode text for display
          let decodedText = "";
          try {
            decodedText = decodeURIComponent(encodedText);
          } catch (e) {
            decodedText = encodedText;
          }

          targetHtml = `
            <select class="step-field dt-target-select" data-field="target" aria-label="Target">
              <option value="FLD" ${target === "FLD" ? "selected" : ""}>Front display (FLD)</option>
              <option value="RLD" ${target === "RLD" ? "selected" : ""}>Rear display (RLD)</option>
              <option value="LOGIC" ${target === "LOGIC" ? "selected" : ""}>Both displays (LOGIC)</option>
            </select>
          `;

          behaviorHtml = `
            <select class="step-field dt-color-select" data-field="color" aria-label="Color">
              <option value="DEFAULT" ${color === "DEFAULT" ? "selected" : ""}>Default</option>
              <option value="RED" ${color === "RED" ? "selected" : ""}>Red</option>
              <option value="BLUE" ${color === "BLUE" ? "selected" : ""}>Blue</option>
              <option value="GREEN" ${color === "GREEN" ? "selected" : ""}>Green</option>
              <option value="WHITE" ${color === "WHITE" ? "selected" : ""}>White</option>
              <option value="YELLOW" ${color === "YELLOW" ? "selected" : ""}>Yellow</option>
              <option value="ORANGE" ${color === "ORANGE" ? "selected" : ""}>Orange</option>
              <option value="PURPLE" ${color === "PURPLE" ? "selected" : ""}>Purple</option>
            </select>
            <textarea class="step-field dt-text-input" data-field="text" placeholder="Enter text (max 32 chars, one line break allowed)" aria-label="Display text">${window.PAUtils.escapeHtml(decodedText)}</textarea>
          `;

          timingHtml = `
            <input class="step-field dt-duration-input" type="number" data-field="duration" value="${duration}" min="0" max="99" aria-label="Duration (seconds)" placeholder="0-99s">
            <span class="dome-rotate-label">s</span>
            <input class="step-field dt-speed-input" type="number" data-field="speed" value="${speed}" min="0" max="9" aria-label="Scroll speed (0-9)" placeholder="0-9">
            <span class="dome-rotate-label">speed</span>
          `;

          // Store hidden cmd field for serialization
          behaviorHtml += `<input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(cmd)}">`;
        } else if (domeMode === "holo") {
          // Holo Effect Mode (DH:) structured step
          // Grammar: DH:<target>:<effect>[:<color>[:<durationOrCount>]]
          const cmd = step.cmd || "DH:A:FLASH";
          const parts = cmd.split(":");
          const target = parts[1] || "A";
          const effect = parts[2] || "FLASH";
          const color = parts[3] || "DEFAULT";
          const durationOrCount = parts[4] || "";

          targetHtml = `
            <select class="step-field dh-target-select" data-field="target" aria-label="Target">
              <option value="F" ${target === "F" ? "selected" : ""}>Front holo (F)</option>
              <option value="R" ${target === "R" ? "selected" : ""}>Rear holo (R)</option>
              <option value="T" ${target === "T" ? "selected" : ""}>Top holo (T)</option>
              <option value="A" ${target === "A" ? "selected" : ""}>All holos (A)</option>
            </select>
          `;

          behaviorHtml = `
            <select class="step-field dh-effect-select" data-field="effect" aria-label="Effect">
              <option value="OFF" ${effect === "OFF" ? "selected" : ""}>Off</option>
              <option value="ON" ${effect === "ON" ? "selected" : ""}>On</option>
              <option value="RESET" ${effect === "RESET" ? "selected" : ""}>Reset</option>
              <option value="RANDOM" ${effect === "RANDOM" ? "selected" : ""}>Random</option>
              <option value="WAG" ${effect === "WAG" ? "selected" : ""}>Wag</option>
              <option value="NOD" ${effect === "NOD" ? "selected" : ""}>Nod</option>
              <option value="PULSE" ${effect === "PULSE" ? "selected" : ""}>Pulse</option>
              <option value="RAINBOW" ${effect === "RAINBOW" ? "selected" : ""}>Rainbow</option>
              <option value="FLASH" ${effect === "FLASH" ? "selected" : ""}>Flash</option>
              <option value="SHORTCIRCUIT" ${effect === "SHORTCIRCUIT" ? "selected" : ""}>Short Circuit</option>
              <option value="SOLID" ${effect === "SOLID" ? "selected" : ""}>Solid</option>
            </select>
            <select class="step-field dh-color-select" data-field="color" aria-label="Color">
              <option value="DEFAULT" ${color === "DEFAULT" ? "selected" : ""}>Default</option>
              <option value="RED" ${color === "RED" ? "selected" : ""}>Red</option>
              <option value="BLUE" ${color === "BLUE" ? "selected" : ""}>Blue</option>
              <option value="GREEN" ${color === "GREEN" ? "selected" : ""}>Green</option>
              <option value="WHITE" ${color === "WHITE" ? "selected" : ""}>White</option>
              <option value="YELLOW" ${color === "YELLOW" ? "selected" : ""}>Yellow</option>
              <option value="ORANGE" ${color === "ORANGE" ? "selected" : ""}>Orange</option>
              <option value="PURPLE" ${color === "PURPLE" ? "selected" : ""}>Purple</option>
              <option value="RANDOM" ${color === "RANDOM" ? "selected" : ""}>Random</option>
            </select>
          `;

          timingHtml = `
            <input class="step-field dh-duration-input" type="number" data-field="durationOrCount" value="${durationOrCount}" min="0" max="99" aria-label="Duration / count (0-99)" placeholder="duration/count (0-99)">
          `;

          // Store hidden cmd field for serialization
          behaviorHtml += `<input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(cmd)}">`;
        } else if (domeMode === "panel") {
          // Render the live picker from DomeLayout when the dome is answering; the
          // offline tiers fall back to the built-in drawing, and only where that
          // drawing is the dome this builder stated (see the else branch below).
          // Parse action and target from cmd, e.g., ":OP01" -> action="OP", target="01"
          let action = "";
          let target = "";
          const match = domeCmd.match(/^:?(OP|CL|OF)(.+)$/);
          if (match) {
            action = match[1];
            target = match[2];
          }

          // Get the layout model to determine picker source (live, cached, vendored, unsupported)
          const domeLayout = window.DomeLayout?.getModel?.();
          const layoutSource = domeLayout?.source || 'vendored';
          const hasLiveElements = domeLayout?.elements?.length > 0;

          // Build the SVG picker: use live layout if available, otherwise fallback to vendored
          let svgPickerHtml = "";
          let sourceNotice = "";

          if (hasLiveElements && window.DomeLayoutRender?.renderPicker) {
            // Render the live picker from dome-served layout
            const pickerSvg = window.DomeLayoutRender.renderPicker(domeLayout);
            svgPickerHtml = `
              <div class="dome-svg-picker-container">
                ${pickerSvg}
              </div>
            `;

            // Add source banner above the picker.
            // 'live' shows no banner (implicit success).
            if (layoutSource === 'cached') {
              sourceNotice = `<div class="dome-layout-notice dome-layout-cached">Showing the last dome layout seen. The dome has not confirmed it.</div>`;
            }
          } else {
            // No live or cached elements: either the dome is unreachable, or it is
            // reachable but on an unsupported schema (whose geometry we deliberately
            // do not trust, so elements is empty). Both land on the same question.
            //
            // The built-in drawing is a drawing of ONE design and declares which
            // (data/dome_panel_model.js). Tier 3 of the Layout Fallback Hierarchy has
            // already asked the Droid Build seam whether that design is the one this
            // builder stated (ADR 0047, #343) - DomeLayout.load() above awaits
            // DroidBuild.load() before it resolves - and `usesVendoredDrawing` is its
            // answer. This reads that answer rather than working out a second one. A
            // model from before tier 3 consulted the design does not carry the field,
            // and keeps the behaviour it had.
            //
            // A null model is the hierarchy not having answered YET: the first editor
            // open of a page session, while /api/dome/layout is still outstanding.
            // That is not a statement that the drawing is theirs, so it is not drawn
            // as one - a builder on their own design would otherwise spend the whole
            // fetch timeout looking at somebody else's dome. DomeLayout.onChange()
            // re-renders this picker through rerenderPanelIntentPickers() as soon as
            // tier 3 does answer.
            const layoutAnswered = Boolean(domeLayout);
            const showsBuiltIn = layoutAnswered && domeLayout.usesVendoredDrawing !== false;

            // The container ships even when it holds no drawing: it is the hook
            // rerenderPanelIntentPickers() finds this picker by, and a step rendered
            // without one would never pick up the live layout on a dome reconnect.
            svgPickerHtml = `
              <div class="dome-picker-container">
                ${showsBuiltIn ? window.DOME_PANEL_MAP_SVG : ""}
              </div>
            `;

            // Distinguish the cases: an unsupported-schema dome IS reachable (wrong
            // version), so "not reachable" would send the operator chasing the wrong
            // problem. Show the schema warning for that case.
            if (!layoutAnswered) {
              sourceNotice = `<div class="dome-layout-notice dome-layout-pending">Checking which dome you built. The panel map follows.</div>`;
            } else if (layoutSource === 'unsupported') {
              const schemaWarning = window.PAUtils.escapeHtml(domeLayout.warning || "Dome layout schema not supported");
              sourceNotice = showsBuiltIn
                ? `<div class="dome-layout-notice dome-layout-error">${schemaWarning}. Showing the built-in MK4 map.</div>`
                : `<div class="dome-layout-notice dome-layout-error">${schemaWarning}. No built-in map for your dome design.</div>`;
            } else if (!showsBuiltIn) {
              // Two different jobs for the builder, so two different sentences - the
              // same two the dashboard's dome card gives (data/dome_control.js): one
              // is "we have no picture of your dome", the other "we do not know what
              // your dome carries at all", and only the second sends somebody to the
              // design files.
              sourceNotice = domeLayout.complementKnown === false
                ? `<div class="dome-layout-notice dome-layout-vendored">Dome not reachable. This build does not know which panels your dome carries.</div>`
                : `<div class="dome-layout-notice dome-layout-vendored">Dome not reachable. No built-in map for your dome design.</div>`;
            } else {
              sourceNotice = `<div class="dome-layout-notice dome-layout-vendored">Dome not reachable. Showing the built-in MK4 map.</div>`;
            }
          }

          // Build the target dropdown, populating from live layout if available
          let targetOptions = "";
          if (hasLiveElements) {
            // Populate from live layout: groups first, then commandable panels
            targetOptions = `
              <optgroup label="Groups">
                <option value="00" ${target === "00" ? "selected" : ""}>All panels (00)</option>
                <option value="14" ${target === "14" ? "selected" : ""}>Pie / top group (14)</option>
                <option value="15" ${target === "15" ? "selected" : ""}>Ring / bottom group (15)</option>
              </optgroup>
            `;

            // Extract commandable panels from layout, grouped by panel_kind
            const commandableRings = domeLayout.elements.filter(
              (e) => e.element_type === "panel" && e.panel_kind === "ring" && e.in_layout && e.commandable && e.mapped
            );
            const commandablePies = domeLayout.elements.filter(
              (e) => e.element_type === "panel" && e.panel_kind === "pie" && e.in_layout && e.commandable && e.mapped
            );

            if (commandableRings.length > 0) {
              targetOptions += "<optgroup label='Ring panels'>";
              commandableRings.forEach((e) => {
                const ringTarget = window.DomeCommandMap?.PANEL_COMMAND_TARGETS?.ring?.[e.id] || e.id;
                targetOptions += `<option value="${ringTarget}" ${target === ringTarget ? "selected" : ""}>${window.PAUtils.escapeHtml(e.label)} → ${ringTarget}</option>`;
              });
              targetOptions += "</optgroup>";
            }

            if (commandablePies.length > 0) {
              targetOptions += "<optgroup label='Pie / top panels'>";
              commandablePies.forEach((e) => {
                const pieTarget = window.DomeCommandMap?.PANEL_COMMAND_TARGETS?.pie?.[e.id] || e.id;
                targetOptions += `<option value="${pieTarget}" ${target === pieTarget ? "selected" : ""}>${window.PAUtils.escapeHtml(e.label)} → ${pieTarget}</option>`;
              });
              targetOptions += "</optgroup>";
            }
          } else {
            // Fallback: use legacy static list
            targetOptions = `
              <optgroup label="Groups">
                <option value="00" ${target === "00" ? "selected" : ""}>All panels (00)</option>
                <option value="14" ${target === "14" ? "selected" : ""}>Pie / top group (14)</option>
                <option value="15" ${target === "15" ? "selected" : ""}>Ring / bottom group (15)</option>
              </optgroup>
              <optgroup label="Ring panels">
                <option value="01" ${target === "01" ? "selected" : ""}>P1 → 01</option>
                <option value="02" ${target === "02" ? "selected" : ""}>P2 → 02</option>
                <option value="03" ${target === "03" ? "selected" : ""}>P3 → 03</option>
                <option value="04" ${target === "04" ? "selected" : ""}>P4 → 04</option>
                <option value="07" ${target === "07" ? "selected" : ""}>P7 → 07</option>
                <option value="11" ${target === "11" ? "selected" : ""}>P11 → 11</option>
                <option value="13" ${target === "13" ? "selected" : ""}>P13 → 13</option>
              </optgroup>
              <optgroup label="Pie / top panels">
                <option value="P1" ${target === "P1" ? "selected" : ""}>PP1 → P1</option>
                <option value="P2" ${target === "P2" ? "selected" : ""}>PP2 → P2</option>
                <option value="P3" ${target === "P3" ? "selected" : ""}>PP3 → P3</option>
                <option value="P4" ${target === "P4" ? "selected" : ""}>PP4 → P4</option>
                <option value="P5" ${target === "P5" ? "selected" : ""}>PP5 → P5</option>
                <option value="P6" ${target === "P6" ? "selected" : ""}>PP6 → P6</option>
              </optgroup>
            `;
          }

          targetHtml = `
            <div class="dome-target-wrapper">
              ${sourceNotice}
              ${svgPickerHtml}
              <select class="step-field dome-target-select" aria-label="Target dropdown (alternative to SVG picker)">
                ${targetOptions}
              </select>
            </div>
          `;

          behaviorHtml = `
            <select class="step-field dome-action-select" aria-label="Action">
              <option value="OP" ${action === "OP" ? "selected" : ""}>Open (:OP)</option>
              <option value="CL" ${action === "CL" ? "selected" : ""}>Close (:CL)</option>
              <option value="OF" ${action === "OF" ? "selected" : ""}>Flutter (:OF)</option>
            </select>
            <span class="dome-cmd-preview">:${action}${target}</span>
            <button type="button" class="dome-mode-toggle seq-act" aria-label="Switch to visual presets">Presets</button>
            <input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(domeCmd)}">
            ${action === "OF" ? "" : `<span class="seq-row-ctl"><span class="seq-unit">How far</span><input class="seq-num" type="number" data-field="howFar" value="${step.howFar ?? ""}" min="1" max="100" placeholder="100" aria-label="How far, percent of the panel's throw"><span class="seq-unit">%</span></span>`}
            <div class="dome-panel-advisory hidden"></div>
          `;
        } else {
          // Advanced mode: raw text input
          behaviorHtml = `
            <input class="step-field step-field-cmd" type="text" data-field="cmd" value="${window.PAUtils.escapeHtml(domeCmd)}" placeholder="@0T6, *HP0, :SE07" aria-label="Dome command (advanced)">
            <button type="button" class="dome-mode-toggle seq-act" aria-label="Switch to panel mode">Panel</button>
          `;
        }
        break;
      }

      case "domeRotate": {
        // Ergonomic operator UI for dome rotation: direction (Left/Right/Stop) + speed + duration
        // Internal storage: speedPct (signed -100..100), durationMs
        // Direction is derived from speedPct sign: negative=left, positive=right, 0=stop
        const rotateSpeedPct = step.speedPct ?? 0;
        const rotateDurationMs = step.durationMs ?? 0;

        // Determine direction from speedPct
        let direction = "stop";
        if (rotateSpeedPct < 0) direction = "left";
        else if (rotateSpeedPct > 0) direction = "right";

        behaviorHtml = `
          <select class="step-field step-field-direction" data-field="direction" aria-label="Direction">
            <option value="stop" ${direction === "stop" ? "selected" : ""}>Stop (neutral)</option>
            <option value="left" ${direction === "left" ? "selected" : ""}>Left (reverse)</option>
            <option value="right" ${direction === "right" ? "selected" : ""}>Right (forward)</option>
          </select>
          <input class="step-field step-field-speed" type="number" data-field="speed" value="${Math.abs(rotateSpeedPct)}" min="0" max="100" step="1" aria-label="Speed (0-100%)" placeholder="0-100">
          <span class="dome-rotate-label">%</span>
        `;

        timingHtml = `
          <input class="step-field step-field-durationMs" type="number" data-field="durationMs" value="${rotateDurationMs}" min="0" max="120000" step="1" aria-label="Run for (ms)" placeholder="duration ms">
          <span class="dome-rotate-label">ms</span>
        `;
        break;
      }

      case "loop": {
        const body = step.body || 1;
        behaviorHtml = `<input class="step-field step-field-body" type="number" data-field="body" value="${body}" min="1" max="96" aria-label="Steps to repeat" placeholder="body">`;

        timingHtml = `
          <input class="step-field step-field-periodMs" type="number" data-field="periodMs" value="${step.periodMs || 1000}" min="100" max="60000" aria-label="Every (ms)" placeholder="periodMs">
          <span class="dome-rotate-label">ms</span>
          <input class="step-field step-field-durationMs" type="number" data-field="durationMs" value="${step.durationMs || 10000}" min="100" max="120000" aria-label="For (ms)" placeholder="durationMs">
          <span class="dome-rotate-label">ms total</span>
        `;
        break;
      }

      case "random": {
        targetHtml = `<select class="step-field step-field-set" data-field="set" aria-label="Target">
          <option value="ring" ${step.set === "ring" ? "selected" : ""}>ring</option>
          <option value="pie" ${step.set === "pie" ? "selected" : ""}>pie</option>
          <option value="all" ${step.set === "all" ? "selected" : ""}>all</option>
          <option value="hold" ${step.set === "hold" ? "selected" : ""}>hold</option>
        </select>`;

        behaviorHtml = `
          <select class="step-field step-field-mode" data-field="mode" aria-label="Action">
            <option value="flutter" ${(step.mode || "flutter") === "flutter" ? "selected" : ""}>flutter</option>
            <option value="open" ${step.mode === "open" ? "selected" : ""}>open</option>
            <option value="close" ${step.mode === "close" ? "selected" : ""}>close</option>
          </select>
          <label class="step-field-checkbox"><input type="checkbox" data-field="distinct" ${step.distinct ? "checked" : ""} aria-label="Distinct"> Distinct</label>
        `;

        timingHtml = `
          <input class="step-field step-field-moveMs" type="number" data-field="moveMs" value="${step.moveMs ?? 300}" min="0" max="5000" aria-label="Move time (ms)" placeholder="moveMs">
          <span class="dome-rotate-label">ms</span>
          <input class="step-field step-field-jitterMs" type="number" data-field="jitterMs" value="${step.jitterMs ?? 0}" min="0" max="2000" aria-label="Jitter (ms)" placeholder="jitterMs">
          <span class="dome-rotate-label">ms</span>
        `;
        break;
      }

      case "audioCat":
        const audioCategories = ["alert", "chatty", "general", "happy", "humming", "processing", "sad", "sentimental", "scream", "surprised", "whistle"];
        behaviorHtml = `
          <select class="step-field step-field-category" data-field="category" aria-label="Category">
            ${audioCategories.map((cat) => `<option value="${cat}" ${step.category === cat ? "selected" : ""}>${cat}</option>`).join("")}
          </select>
          <select class="step-field step-field-fallback" data-field="fallback" aria-label="Fallback sound">
            ${AUDIO_FALLBACK_SLOTS.map((s) => `<option value="${s.value}" ${(step.fallback || "none") === s.value ? "selected" : ""}>${s.label}</option>`).join("")}
          </select>
        `;
        break;

      case "gesture": {
        // One grid of rows, each choice drawn as what it is: a joined bar for
        // a few peers, wrapping pills for a longer set. Each writes a hidden
        // form value, so the step still reads back through its [data-field]
        // inputs like every other type. Pace and repeat are rarely set and
        // fold away.
        const G = window.SeqGesture;
        if (!G) break;
        const esc = window.PAUtils.escapeHtml;
        const hidden = (field, current) => `<input type="hidden" data-field="${field}" value="${esc(current || "")}">`;
        const bar = (field, options, current) => `
          <span class="seg seg-sm" role="group" aria-label="${field}">
            ${options
              .map(
                (o) =>
                  `<button type="button" class="gesture-pick" data-pick="${field}" data-value="${esc(o.id)}" aria-pressed="${o.id === current ? "true" : "false"}">${esc(o.label)}</button>`,
              )
              .join("")}
          </span>${hidden(field, current)}`;
        const pills = (field, options, current) => `
          <span class="seq-pills" role="radiogroup" aria-label="${field}">
            ${options
              .map(
                (o) =>
                  `<button type="button" class="seq-pill gesture-pick" role="radio" data-pick="${field}" data-value="${esc(o.id)}" aria-checked="${o.id === current ? "true" : "false"}">${esc(o.label)}</button>`,
              )
              .join("")}
          </span>${hidden(field, current)}`;
        const num = (field, value, placeholder, label, extra = "") =>
          `<input class="seq-num" type="number" ${extra} value="${value ?? ""}" placeholder="${placeholder}" aria-label="${label}">`;
        const sets = (window.DroidParts?.sets || []).map((x) => ({ id: x.id, label: x.label }));
        const shapes = G.SHAPES.map((x) => ({ id: x, label: x[0].toUpperCase() + x.slice(1) }));
        const tempo = tempoOf();
        const every = tempo
          ? `${num("stepBeats", step.stepBeats, "1", "Beats between parts", 'min="1" max="1200" data-beats="stepBeats"').replace('class="seq-num"', 'class="seq-num gesture-beats"')}<span class="seq-unit">beats</span>`
          : `${num("stepMs", step.stepMs, G.STEP_DEFAULT_MS, "Milliseconds between parts", 'min="50" max="60000" data-field="stepMs"')}<span class="seq-unit">ms</span>`;
        const again = tempo
          ? `${num("repeatBeats", step.repeatBeats, "-", "Repeat every beats", 'min="1" max="1200" data-beats="repeatBeats"').replace('class="seq-num"', 'class="seq-num gesture-beats"')}<span class="seq-unit">beats</span>`
          : `${num("repeatMs", step.repeatMs, "-", "Repeat every milliseconds", 'min="100" max="60000" data-field="repeatMs"')}<span class="seq-unit">ms</span>`;
        fieldsContainer.innerHTML = `
          <div class="seq-rows">
            <span class="seq-row-label">Parts</span>
            <div class="seq-row-ctl">${pills("set", sets, step.set || "")}</div>
            <span class="seq-row-label">Move</span>
            <div class="seq-row-ctl">${bar("shape", shapes, step.shape || "open")}</div>
            <span class="seq-row-label">Travels</span>
            <div class="seq-row-ctl">${pills("spread", G.SPREADS, step.spread || "together")}</div>
            <span class="seq-row-label">Order</span>
            <div class="seq-row-ctl">${bar("direction", G.DIRECTIONS, step.direction || "cw")}${bar("start", G.STARTS, step.start || "front")}</div>
            <span class="seq-row-label">How far</span>
            <div class="seq-row-ctl">${num("howFar", step.howFar, "100", "How far, percent of each part's throw", 'min="1" max="100" data-field="howFar"')}<span class="seq-unit">%</span></div>
          </div>
          <details class="seq-more"${step.stepBeats || step.repeatBeats || step.stepMs || step.repeatMs || step.extentMs || step.speedMs || step.easing ? " open" : ""}>
            <summary><svg class="i chev" aria-hidden="true" focusable="false"><use href="#i-chevron-right"/></svg>Pace, repeat and feel</summary>
            <div class="seq-rows">
              <span class="seq-row-label">Every</span>
              <div class="seq-row-ctl">${every}</div>
              <span class="seq-row-label">Again</span>
              <div class="seq-row-ctl">${again}</div>
              <span class="seq-row-label">For</span>
              <div class="seq-row-ctl">${num("extentMs", step.extentMs, "end", "Repeat for milliseconds, to the end when empty", 'min="0" max="120000" data-field="extentMs"')}<span class="seq-unit">ms</span></div>
              <span class="seq-row-label">Full throw</span>
              <div class="seq-row-ctl">${num("speedMs", step.speedMs, "own", "Full throw time for each part, the part's own when empty", 'min="50" max="5000" data-field="speedMs"')}<span class="seq-unit">ms</span></div>
              <span class="seq-row-label">Easing</span>
              <div class="seq-row-ctl">${bar("easing", [{ id: "", label: "Own" }, ...G.EASINGS.map((x) => ({ id: x, label: x[0].toUpperCase() + x.slice(1) }))], step.easing || "")}</div>
            </div>
          </details>`;
        return;
      }

      case "sequence": {
        // The phrase as wrapping pills of names over a hidden reference; the
        // step's `name` is the label a reader of the file sees.
        const esc = window.PAUtils.escapeHtml;
        const choices = phraseChoices();
        fieldsContainer.innerHTML = `
          <div class="seq-rows">
            <span class="seq-row-label">Sequence</span>
            <div class="seq-row-ctl">
              <span class="seq-pills" role="radiogroup" aria-label="sequence">
                ${choices
                  .map(
                    (o) =>
                      `<button type="button" class="seq-pill gesture-pick" role="radio" data-pick="ref" data-value="${esc(o.id)}" aria-checked="${o.id === step.ref ? "true" : "false"}">${esc(o.label)}</button>`,
                  )
                  .join("")}
              </span>
              ${choices.length ? "" : `<span class="seq-unit">Save a sequence first.</span>`}
              <input type="hidden" data-field="ref" value="${esc(step.ref || "")}">
            </div>
          </div>`;
        return;
      }

      case "end":
        html = `<span class="step-field-empty">(terminal step)</span>`;
        fieldsContainer.innerHTML = html;
        return; // No groups for end step
    }

    // Assemble the grouped HTML structure
    let groupedHtml = "";
    if (targetHtml) groupedHtml += renderFieldGroup("Target", targetHtml);
    if (behaviorHtml) groupedHtml += renderFieldGroup("Behavior", behaviorHtml);
    if (timingHtml) groupedHtml += renderFieldGroup("Timing", timingHtml);

    // Add the help line
    const helpLine = stepHelpLine(step);
    const helpHtml = helpLine ? `<div class="step-help-line">${window.PAUtils.escapeHtml(helpLine)}</div>` : "";

    fieldsContainer.innerHTML = groupedHtml + helpHtml;
  };

  // Where the tempo came from, as the field beside it says it (ADR 0058).
  const TEMPO_SOURCE_LABELS = { typed: "Typed", tapped: "Tapped", analysed: "Analyzed" };
  const tempoSourceLabel = (tempo) => (tempo ? TEMPO_SOURCE_LABELS[tempo.source] || "" : "");

  // A typed BPM is stored as typed, whatever it replaced: the number no longer
  // came from the taps or the analyzer, so their confidence and the analyzed
  // track's fingerprint go with them. Where beat 1 sits and the bar the
  // builder set stay. An empty field is no tempo; a step still on a beat then
  // fails the check until it is placed again.
  const typedTempo = (value) => {
    const kept = editorState.current.tempo;
    if (value === "" || value === null) return undefined;
    const bpm = Math.round(Number(value) * 10) / 10;
    if (!Number.isFinite(bpm)) return kept;
    return {
      bpm,
      phase: kept?.phase ?? 0,
      barLen: kept?.barLen ?? 4,
      barPhase: kept?.barPhase ?? 0,
      ...(kept?.duration ? { duration: kept.duration } : {}),
      source: "typed",
      confidence: 1,
    };
  };

  // Tapping along (ADR 0058). Times are from the start edge of the track on
  // the droid when the builder pressed Play on the droid, else from the first
  // tap - so the first tap is the downbeat either way (ADR 0060).
  const tapState = { taps: [], startedAt: null };
  const nowMs = () => (window.performance?.now ? window.performance.now() : Date.now());

  const resetTaps = () => {
    tapState.taps = [];
    tapState.startedAt = null;
  };

  const tempoFeedback = (message) => {
    const el = document.getElementById("seq-editor-tempo-feedback");
    if (el) el.textContent = message || "";
  };

  // A tempo from any route replaces the one there, and every step on a beat
  // moves to where its beat now falls; the field and the source say so.
  const setTempo = (tempo) => {
    // Every step on a beat moves to where its beat now falls; a step placed in
    // milliseconds stays put (ADR 0058).
    const before = historyBegin();
    const next = { ...editorState.current, tempo };
    if (tempo === undefined) delete next.tempo;
    editorState.current = SeqProtocolCheck.resolveBeats(next, { written: true });
    historyCommit(before);
    paintAuthoredHeader();
    rerenderStepTable();
    edited();
  };

  // The controls that show something the history holds: the tempo on the
  // ruler and the interrupt group in the drawer. Painted by the edit that
  // changes one and by an undo.
  const paintAuthoredHeader = () => {
    const tempo = editorState.current.tempo;
    const bpmInput = document.getElementById("seq-editor-bpm");
    if (bpmInput) bpmInput.value = tempo ? String(tempo.bpm) : "";
    const sourceEl = document.getElementById("seq-editor-tempo-source");
    if (sourceEl) sourceEl.textContent = tempoSourceLabel(tempo);
    document.getElementById("seq-editor-downbeat")?.classList.toggle("hidden", !tempo);
    const group = editorState.current.toggleGroup || "none";
    document.getElementById("seq-editor-toggle")?.querySelectorAll("button").forEach((button) =>
      button.setAttribute("aria-pressed", button.dataset.value === group ? "true" : "false"));
  };

  // ---------------------------------------------------------------------------
  // The one door an edit leaves by, from either view: the verdict, the
  // Rehearsal, the history buttons and the timeline are all read again from
  // the routine as it now is, so no view can drift from it. An undo leaves by
  // the same door.
  // ---------------------------------------------------------------------------
  const paintHistory = () => {
    const undoBtn = document.getElementById("seq-editor-undo");
    const redoBtn = document.getElementById("seq-editor-redo");
    if (undoBtn) undoBtn.disabled = history.undo.length === 0 && !runChanged();
    if (redoBtn) redoBtn.disabled = history.redo.length === 0 || runChanged();
  };

  const edited = (receipt = "") => {
    if (history.run === null) history.base = historyCapture();
    showRetime(receipt);
    updateValidationSummary();
    paintHistory();
    if (sessionTimeline) sessionTimeline.refresh(rehearsalContext());
  };

  const historyRestore = (snapshot) => {
    const kept = JSON.parse(snapshot);
    HISTORY_FIELDS.forEach((key) => {
      if (kept[key] === undefined) delete editorState.current[key];
      else editorState.current[key] = kept[key];
    });
    paintAuthoredHeader();
    rerenderStepTable();
    edited();
  };

  // Neither runs while a block is being dragged on the timeline: the drag's
  // writes are in the routine but not yet an entry, and restoring a copy
  // would replace the very steps the drag is holding.
  const historyBusy = () => !editorState.current || Boolean(sessionTimeline?.dragging());

  const undo = () => {
    if (historyBusy()) return;
    historySettle();
    if (history.undo.length === 0) return;
    history.redo.push(history.base);
    historyRestore(history.undo.pop());
  };

  const redo = () => {
    if (historyBusy()) return;
    historySettle();
    if (history.redo.length === 0) return;
    history.undo.push(history.base);
    historyRestore(history.redo.pop());
  };

  // A run of typing in one step is one edit. It starts at the first keystroke
  // and ends at the field's change, or when the builder moves to another step.
  const openRun = (stepIdx) => {
    if (history.run !== null && history.runStep !== stepIdx) historySettle();
    if (history.run === null) {
      history.run = history.base;
      history.runStep = stepIdx;
    }
  };

  const typed = (stepIdx) => {
    openRun(stepIdx);
    validateAndUpdateStep(stepIdx);
  };

  // A picker writes its step itself and then reads the form back. The run has
  // to be this step's BEFORE that write: a run still open on another step is
  // settled against the routine as it stands, and after the write that would
  // file this step's change under the other step's entry, for one Undo to
  // take back both.
  const picked = (stepIdx, write) => {
    openRun(stepIdx);
    write();
    validateAndUpdateStep(stepIdx);
  };

  // The same, for a picker that is one act and has no field whose `change`
  // would end the run: a panel pressed on the dome map, a panel or an action
  // chosen from the list. Each press is its own entry.
  const pickedOnce = (stepIdx, write) => {
    picked(stepIdx, write);
    historySettle();
    paintHistory();
  };

  const bindStepField = (input, stepIdx) => {
    input.addEventListener("input", () => typed(stepIdx));
    input.addEventListener("change", () => {
      typed(stepIdx);
      historySettle();
      paintHistory();
    });
  };

  const tapCountText = () => {
    const n = tapState.taps.length;
    const result = window.SeqTempo?.tap(tapState.taps);
    return result ? `${n} taps, ${result.bpm} BPM` : `${n} ${n === 1 ? "tap" : "taps"}`;
  };

  // Open `seq` in the editor: a fresh edit, with a fresh history. Revert comes
  // through here too, which is what makes it the whole-session discard.
  const renderEditorView = (seq) => {
    // isNew must be set by the caller before calling renderEditorView
    closeTimeline();
    closeSessionTimeline();
    editorState.original = JSON.parse(JSON.stringify(seq));
    editorState.current = JSON.parse(JSON.stringify(seq));
    editorState.saved = false;
    historyReset();
    droppedTrack = null;

    // Load DomeLayout if available so the live picker in panel-intent steps can
    // render from the connected dome's layout, with automatic refresh on dome
    // reconnect. Subscribe once per page load (renderEditorView runs on every
    // editor open) so repeated opens don't stack duplicate onChange listeners.
    if (window.DomeLayout) {
      window.DomeLayout.load().catch(() => {
        // Silent fallback: if layout fetch fails, the picker will use vendored/cached
      });
      if (!domeLayoutChangeSubscribed) {
        window.DomeLayout.onChange(() => {
          rerenderPanelIntentPickers();
        });
        domeLayoutChangeSubscribed = true;
      }
    }

    const stepRows = (seq.steps || [])
      .map((step, idx) => renderStepRow(step, idx))
      .join("");

    const esc = window.PAUtils.escapeHtml;
    const tuneNotice = editorState.tuningFactory
      ? `<div class="note note-act seq-tuning" role="note">
           Tuning <b>${esc(editorState.tuningFactory)}</b>: save under the same name and your version replaces the factory one. <em>Memory Wipe</em> brings the original back.
         </div>`
      : "";

    // The strip: the way back, which sequence this is and whether it has
    // unsaved edits, the run, Protocol Check's verdict, and the acts that keep
    // or drop the edit. Save is the one filled act on the page.
    const strip = `
        <div class="seq-strip">
          ${backHtml('id="seq-editor-cancel"')}
          <span class="seq-name" id="seq-editor-sub">${esc(seq.name || "a new sequence")}</span>
          <span class="seq-state" id="seq-editor-state" role="status"></span>
          <span class="seq-gap"></span>
          <span class="seq-run">
            <button id="seq-editor-test" class="btn btn-sm" type="button">Test on the droid</button>
            <span class="hint hidden" id="seq-editor-test-hint">Runs the last saved copy.</span>
          </span>
          <span class="seq-strip-seam" aria-hidden="true"></span>
          <span class="seq-verdict" id="seq-editor-validation-summary" aria-live="polite" aria-label="Validation status">
            <!-- Populated by updateValidationSummary() -->
          </span>
          <span class="seq-keep">
            <button id="seq-editor-save" class="btn btn-sm accent" type="button">Save</button>
            <button id="seq-editor-undo" class="seq-act" type="button" disabled>Undo</button>
            <button id="seq-editor-redo" class="seq-act" type="button" disabled>Redo</button>
            <button id="seq-editor-revert" class="seq-act" type="button" aria-label="Discard unsaved changes">Revert</button>
          </span>
          <p class="hint seq-editor-prerun hidden" id="seq-editor-prerun" aria-live="polite"></p>
          <div class="seq-editor-feedback" id="seq-editor-feedback" aria-live="polite" aria-label="Editor feedback"></div>
        </div>`;

    // The tempo, riding on the ruler: it is what the bars under the seconds
    // are counted from, so it sits with the timeline and not in the drawer.
    const tempoRows = `
          <div class="setting-rows seq-tempo">
            <div class="setting-row">
              <span class="setting-name">Tempo</span>
              <span class="seq-row-ctl">
                <span class="setting-number">
                  <input id="seq-editor-bpm" class="number-cell" type="number" min="1" max="600" step="0.1" value="${seq.tempo ? esc(seq.tempo.bpm) : ""}" placeholder="none" aria-label="Tempo in beats per minute">
                  <span class="setting-unit">BPM</span>
                </span>
                <button id="seq-editor-tap-open" class="seq-act" type="button">Tap along</button>
                <label class="seq-act" for="seq-editor-track">Analyze a track</label>
                <input id="seq-editor-track" class="hidden" type="file" accept="audio/*" aria-label="Your copy of the track">
                <button id="seq-editor-retime" class="seq-act${seq.tempo ? "" : " hidden"}" type="button">Retime to the grid</button>
              </span>
              <span class="setting-value seq-tempo-source" id="seq-editor-tempo-source">${tempoSourceLabel(seq.tempo)}</span>
            </div>
            <p class="hint seq-receipt" id="seq-editor-retime-receipt" role="status"></p>
            <div id="seq-editor-tap" class="setting-row hidden">
              <span class="setting-name">Tap on the beat</span>
              <span class="seq-row-ctl">
                <button id="seq-editor-tap-play" class="seq-act" type="button">Play on the droid</button>
                <button id="seq-editor-tap-beat" class="btn btn-sm" type="button">Tap</button>
                <span class="setting-unit" id="seq-editor-tap-count" role="status">0 taps</span>
                <button id="seq-editor-tap-use" class="seq-act" type="button" disabled>Use</button>
              </span>
              <span class="setting-value"></span>
            </div>
            <div class="seq-editor-error-text" id="seq-editor-tempo-feedback" aria-live="polite"></div>
          </div>`;

    // The routine, read two ways over the one sequence. The step list stays
    // reachable until the timeline can author every kind of step (ADR 0057).
    const views = `
            <span class="seg seg-sm" role="group" aria-label="How the routine is shown">
              <button id="seq-editor-show-steps" type="button" aria-pressed="false">Steps</button>
              <button id="seq-editor-show-timeline" type="button" aria-pressed="true">Timeline</button>
            </span>`;
    const stepList = `
          <div class="seq-editor-steps hidden" id="seq-editor-steps">
            <p class="hint">Every step starts collapsed. Press one to open it.</p>
            <div class="seq-editor-step-table" id="seq-editor-step-table">
              ${stepRows}
            </div>
            <div class="seq-row-ctl">
              <button id="seq-editor-add-step" class="seq-act" type="button">Add a step</button>
            </div>
          </div>`;

    // The drawer's panes. The sequence's own settings are setting rows, the
    // family Foot Drive's settings are drawn in: a name, the control, what it
    // is on. What is rarely set is folded away.
    const tab = (name, label) =>
      `<button id="seq-editor-tab-${name}" type="button" data-tab="${name}" aria-pressed="${name === editorState.tab}">${label}</button>`;
    const pane = (name, inner) =>
      `<div class="seq-pane${name === editorState.tab ? "" : " hidden"}" id="seq-pane-${name}">${inner}</div>`;
    const drawer = `
        <div class="seq-drawer">
          <div class="seq-tabs">
            <span class="seg seg-sm" role="group" aria-label="What the drawer shows">
              ${tab("block", "Picked block")}${tab("parts", "Parts")}${tab("sequence", "Sequence")}${tab("rehearsal", "Rehearsal")}
            </span>
            <span class="seq-gap"></span>
            <span class="seq-meta" id="seq-editor-routine-sub"></span>
          </div>
          ${pane("block", "")}
          ${pane("parts", `
            <div class="sect"><h3>Parts</h3><span class="sub" id="seq-editor-parts-sub"></span></div>
            <div class="seq-parts" id="seq-editor-parts"></div>`)}
          ${pane("sequence", `
            <div class="sect"><h3>Sequence</h3><span class="sub" id="seq-editor-saved-sub"></span></div>
            <div class="setting-rows seq-settings">
              <label class="setting-row">
                <span class="setting-name">Name</span>
                <input id="seq-editor-name" class="number-cell text-cell" type="text" value="${esc(seq.name || "DM:")}" placeholder="DM:MYSEQ" aria-label="Sequence name (DM:XXXX format)" maxlength="21">
                <span class="setting-value"></span>
              </label>
              <div class="setting-row">
                <span class="setting-name">Interrupt group</span>
                <span id="seq-editor-toggle" class="seg seg-sm" role="group" aria-label="Interrupt group for conflict management">
                  ${["none", "pies", "low", "all"]
                    .map(
                      (g) =>
                        `<button type="button" data-value="${g}" aria-pressed="${(seq.toggleGroup || "none") === g ? "true" : "false"}">${g[0].toUpperCase() + g.slice(1)}</button>`,
                    )
                    .join("")}
                </span>
                <span class="setting-value"></span>
              </div>
            </div>
            <details class="seq-more seq-settings-more">
              <summary>${chevron}More settings</summary>
              <div class="setting-rows seq-settings">
                <label class="setting-row">
                  <span class="setting-name">Purpose</span>
                  <input id="seq-editor-purpose" class="number-cell text-cell text-cell-wide" type="text" value="${esc(seq.meta?.purpose || "")}" placeholder="optional" aria-label="Purpose of the sequence">
                  <span class="setting-value"></span>
                </label>
                <label class="setting-row">
                  <span class="setting-name">Mute period</span>
                  <input id="seq-editor-suppress" type="range" class="fader" value="${seq.suppressMs || 8000}" min="1000" max="120000" step="100" aria-label="Mute period">
                  <span class="setting-value seq-editor-slider-value">${mutePeriodWords(seq.suppressMs || 8000)}</span>
                </label>
                <label id="seq-editor-downbeat" class="setting-row${seq.tempo ? "" : " hidden"}">
                  <span class="setting-name">Bar 1 starts on beat</span>
                  <input id="seq-editor-downbeat-beat" class="number-cell" type="number" min="1" step="1" value="1" aria-label="Move bar 1 to this beat">
                  <span class="setting-value"></span>
                </label>
                <label class="setting-row">
                  <span class="setting-name">Notes</span>
                  <textarea id="seq-editor-notes" class="number-cell text-cell text-cell-wide seq-notes" placeholder="optional" aria-label="Optional notes about the sequence">${esc(seq.meta?.notes || "")}</textarea>
                  <span class="setting-value"></span>
                </label>
              </div>
            </details>`)}
          ${pane("rehearsal", `
            <div class="sect"><h3>Rehearsal</h3><span class="sub" id="seq-editor-figures"></span></div>
            <div class="seq-editor-rehearsal" id="seq-editor-rehearsal" aria-live="polite" aria-label="Rehearsal">
              <!-- Populated by updateValidationSummary() -->
            </div>`)}
        </div>`;

    els.editorView.innerHTML = `
      <div class="seq-work">
        ${strip}
        ${tuneNotice}
        ${stageHtml({ ids: true, above: tempoRows, views, below: stepList })}
        ${drawer}
      </div>
    `;

    // Populate conditional fields for each step. Only expanded cards have a
    // .step-fields container, so derive the real step index from the card's
    // data-step-index instead of the enumeration order (which is expanded-rank).
    document.querySelectorAll(".step-fields").forEach((container) => {
      const card = container.closest(".step-card");
      if (!card) return;
      const stepIdx = parseInt(card.dataset.stepIndex, 10);
      renderStepFields(editorState.current.steps[stepIdx], container);
    });

    // The Picked block pane is the timeline's to fill, as it mounts.
    pickedBlocks = [];
    pickedShown = null;
    showPicked([]);
    mountSessionTimeline();
    paintParts();

    // Attach event listeners (metadata/footer once; step rows on every rerender)
    attachMetadataListeners();
    attachStepListeners();
    updateValidationSummary();
    paintHistory();
    showSessionView(editorState.view);
    els.editorView.classList.remove("hidden");
    renderListView();
    showFromTheTop();
  };

  // How long a sequence mutes the droid's own chatter after it runs, in the
  // seconds a builder thinks in; it is stored in milliseconds.
  const mutePeriodWords = (ms) => `${(ms / 1000).toFixed(1)} s`;

  // What the strip and the Sequence tab say about the session: whether it has
  // edits the droid has not received, and that a run uses the saved copy.
  const paintSession = () => {
    const dirty = sessionDirty();
    // "Saved." is true of the routine it was said about: the next edit takes
    // it down, so it never stands beside "Unsaved edits".
    if (dirty && saidSaved) showEditorFeedback("");
    const stateEl = document.getElementById("seq-editor-state");
    if (stateEl) stateEl.textContent = dirty ? "Unsaved edits" : editorState.saved ? "Saved" : "No edits";
    document.getElementById("seq-editor-test-hint")?.classList.toggle("hidden", !dirty);
    const savedEl = document.getElementById("seq-editor-saved-sub");
    if (savedEl) savedEl.textContent = dirty ? "unsaved edits" : "as saved";
  };

  const updateValidationSummary = () => {
    const validation = SeqProtocolCheck.validateSequence(editorState.current);
    const summaryEl = document.getElementById("seq-editor-validation-summary");
    if (!summaryEl) return;

    // Read off the routine as it is now: its name on the strip, and how many
    // steps it has beside the drawer's tabs.
    const steps = editorState.current.steps || [];
    const nameEl = document.getElementById("seq-editor-sub");
    if (nameEl) nameEl.textContent = editorState.current.name || "a new sequence";
    const routineEl = document.getElementById("seq-editor-routine-sub");
    if (routineEl) routineEl.textContent = countOf(steps.length, "step", "steps");

    // Protocol Check's two outcomes take the signal colors their own meanings
    // already have - a green lamp for a sequence the droid will accept, red
    // for one it would refuse - and the sentence says which on its own
    // (CONTEXT.md "Status Color", ADR 0044).
    const status = validation.ok ? "valid" : "error";
    summaryEl.innerHTML = `
      <span class="indicator ${validation.ok ? "ok" : "fail"}" aria-hidden="true"></span>
      <span class="seq-validation-status seq-validation-${status}">${window.PAUtils.escapeHtml(validation.ok ? "Sequence is valid" : validation.error || "Validation error")}</span>
    `;

    // Disable save button if invalid
    const saveBtn = document.getElementById("seq-editor-save");
    if (saveBtn) saveBtn.disabled = !validation.ok;

    // The Rehearsal's tab: its counts, what it found and the fix for each, and
    // what the routine weighs. It never feeds the verdict: the save button
    // above answers to Protocol Check alone (ADR 0044). The tab itself says
    // when there is a warning to read.
    const rehearsalEl = document.getElementById("seq-editor-rehearsal");
    if (rehearsalEl && window.SeqRehearsal) {
      const report = window.SeqRehearsal.rehearse(editorState.current, rehearsalContext());
      rehearsalEl.innerHTML = window.SeqRehearsal.countsHtml(report) + window.SeqRehearsal.listHtml(report);
      const figuresEl = document.getElementById("seq-editor-figures");
      if (figuresEl) figuresEl.textContent = window.SeqRehearsal.figuresText(report);
      const tabEl = document.getElementById("seq-editor-tab-rehearsal");
      if (tabEl) {
        tabEl.textContent = report.counts.warning > 0
          ? `Rehearsal · ${countOf(report.counts.warning, "warning", "warnings")}`
          : "Rehearsal";
      }
    }
    paintSession();
    updatePrerun();
  };

  // Beside Test on the droid, before the press: the uncalibrated Servo Outputs
  // this routine moves, whose first move jumps rather than ramps. It never
  // disables or delays the run and asks nothing (#287 specific 6); it is
  // absent when there is nothing to name.
  const updatePrerun = () => {
    const prerunEl = document.getElementById("seq-editor-prerun");
    if (!prerunEl || !window.SeqRehearsal || !editorState.current) return;
    const html = window.SeqRehearsal.unmeasuredHtml(
      window.SeqRehearsal.unmeasuredOutputs(editorState.current, rehearsalContext())
    );
    prerunEl.innerHTML = html;
    prerunEl.classList.toggle("hidden", html === "");
  };

  // Read the droid's Outputs and config for the Rehearsal. Not a page section:
  // a droid that cannot answer costs the Rehearsal its reach, never the page.
  const loadRehearsalFacts = async () => {
    if (!window.PAOutputs) return;
    try {
      const { config, outputs } = await window.PAOutputs.load();
      rehearsalFacts.outputs = outputs;
      rehearsalFacts.config = config;
    } catch (error) {
      console.warn("[seq] the Rehearsal could not read the droid's outputs:", error);
      return;
    }
    if (editorState.current && !els.editorView.classList.contains("hidden")) {
      updateValidationSummary();
      paintParts();
    }
    if (lastRunBadge) showRunBadge(...lastRunBadge);
    if (timeline) timeline.refresh(rehearsalContext());
    if (sessionTimeline) sessionTimeline.refresh(rehearsalContext());
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
      } else if (field === "t" || field === "body" || field === "periodMs" || field === "durationMs" || field === "moveMs" || field === "jitterMs" || field === "speed") {
        value = parseInt(value, 10);
      } else if (GESTURE_NUMBER_FIELDS.includes(field)) {
        // A Gesture's optional numbers: an empty field is the default, which
        // is stored as absence.
        if (value === "") return;
        value = parseInt(value, 10);
      } else if (GESTURE_WORD_FIELDS.includes(field) && value === "") {
        return;
      }
      step[field] = value;
    });

    // Convert domeRotate UI fields (direction + speed) to signed speedPct
    if (step.type === "domeRotate") {
      const direction = step.direction || "stop";
      const speed = step.speed ?? 0;
      const absSpeed = Math.abs(speed) || 0;

      // Compute signed speedPct from direction and speed
      if (direction === "stop") {
        step.speedPct = 0;
        step.durationMs = 0;  // Stop always has 0 duration
      } else if (direction === "left") {
        step.speedPct = -absSpeed;
      } else if (direction === "right") {
        step.speedPct = absSpeed;
      }

      // Clean up UI-only fields
      delete step.direction;
      delete step.speed;
    }

    // The step is rebuilt from the form, which has no field for a beat, so a
    // beat-placed step keeps its beat through an edit of anything else. Typing
    // a new time is choosing a millisecond instead, and the beat goes; a span
    // in beats goes the same way when its duration is typed over (ADR 0058).
    const prev = editorState.current.steps[stepIdx] || {};
    if (prev.beat !== undefined && step.t === prev.t) step.beat = prev.beat;
    // A Gesture's times in beats have no form field either, so they ride
    // along the same way (their inputs set them directly, setStepBeat()); and
    // a Gesture over a listed set of parts, which the pickers do not offer,
    // keeps its list.
    // A phrase step keeps a label for a reader of the file: its current name.
    if (step.type === "sequence") {
      if (step.ref === "") delete step.ref;
      const label = step.ref ? phraseName(step) : "";
      if (label && !label.endsWith("(not on this droid)")) step.name = label;
      else if (prev.name && prev.ref === step.ref) step.name = prev.name;
    }
    if (step.type === "gesture" && prev.type === "gesture") {
      ["stepBeats", "repeatBeats", "extentBeats"].forEach((key) => {
        if (prev[key] !== undefined) step[key] = prev[key];
      });
      if (step.set === undefined && Array.isArray(prev.parts)) step.parts = prev.parts;
    }
    if (prev.spanBeats !== undefined && step.type === prev.type && step.durationMs === prev.durationMs) {
      step.spanBeats = prev.spanBeats;
    }

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
    edited();
  };

  // Called once from renderEditorView — persistent metadata + footer elements only.
  // These elements are NOT re-created on rerenderStepTable, so listeners must not accumulate.
  const attachMetadataListeners = () => {
    const nameInput = document.getElementById("seq-editor-name");
    const purposeInput = document.getElementById("seq-editor-purpose");
    const suppressInput = document.getElementById("seq-editor-suppress");
    const suppressValue = document.querySelector(".seq-editor-slider-value");
    const toggleSelect = document.getElementById("seq-editor-toggle");
    const notesInput = document.getElementById("seq-editor-notes");
    const advancedToggle = document.getElementById("seq-editor-advanced-toggle");
    const advancedFields = document.getElementById("seq-editor-advanced-fields");

    if (nameInput) {
      nameInput.addEventListener("input", () => {
        editorState.current.name = nameInput.value;
        updateValidationSummary();
      });
    }

    if (purposeInput) {
      purposeInput.addEventListener("input", () => {
        if (!editorState.current.meta) editorState.current.meta = {};
        editorState.current.meta.purpose = purposeInput.value;
        paintSession();
      });
    }

    if (suppressInput) {
      suppressInput.addEventListener("input", () => {
        const val = parseInt(suppressInput.value, 10);
        editorState.current.suppressMs = val;
        if (suppressValue) suppressValue.textContent = mutePeriodWords(val);
        updateValidationSummary();
      });
    }

    if (toggleSelect) {
      // A joined bar of the four groups: the pressed one is the group.
      toggleSelect.querySelectorAll("button").forEach((btn) => {
        btn.addEventListener("click", () => {
          const before = historyBegin();
          editorState.current.toggleGroup = btn.dataset.value;
          historyCommit(before);
          paintAuthoredHeader();
          edited();
        });
      });
    }

    const tapPanel = document.getElementById("seq-editor-tap");
    const tapOpen = document.getElementById("seq-editor-tap-open");
    const tapPlay = document.getElementById("seq-editor-tap-play");
    const tapBeat = document.getElementById("seq-editor-tap-beat");
    const tapUse = document.getElementById("seq-editor-tap-use");
    const tapCount = document.getElementById("seq-editor-tap-count");
    const showTaps = () => {
      if (tapCount) tapCount.textContent = tapCountText();
      if (tapUse) tapUse.disabled = !window.SeqTempo?.tap(tapState.taps);
    };
    resetTaps();
    if (tapOpen && tapPanel) {
      tapOpen.addEventListener("click", () => {
        resetTaps();
        showTaps();
        tempoFeedback("");
        tapPanel.classList.toggle("hidden");
      });
    }
    if (tapPlay) {
      tapPlay.addEventListener("click", async () => {
        // The droid plays what is saved under this name, so a sequence never
        // saved has nothing on the droid to play yet.
        if (editorState.isNew || !editorState.original?.name) {
          tempoFeedback("Save it first, so the droid has the track to play.");
          return;
        }
        tapPlay.disabled = true;
        try {
          await PAApi.postJson("/api/seq/test", { name: editorState.original.name });
          resetTaps();
          tapState.startedAt = nowMs();
          showTaps();
          tempoFeedback("");
        } catch (error) {
          tempoFeedback("The droid did not play it: " + PAApi.messageFor(error));
        } finally {
          tapPlay.disabled = false;
        }
      });
    }
    if (tapBeat) {
      tapBeat.addEventListener("click", () => {
        const now = nowMs();
        if (tapState.startedAt === null) tapState.startedAt = now;
        tapState.taps.push(now - tapState.startedAt);
        showTaps();
      });
    }
    if (tapUse) {
      tapUse.addEventListener("click", () => {
        const result = window.SeqTempo?.tap(tapState.taps);
        if (!result) return;
        setTempo(window.SeqTempo.tappedTempo(result));
        resetTaps();
        tapPanel?.classList.add("hidden");
      });
    }
    const trackInput = document.getElementById("seq-editor-track");
    if (trackInput) {
      trackInput.addEventListener("change", async () => {
        const file = trackInput.files && trackInput.files[0];
        trackInput.value = "";
        if (!file || !window.SeqTempo) return;
        tempoFeedback("Reading the track...");
        let result;
        try {
          result = await window.SeqTempo.analyzeFile(file);
        } catch (error) {
          tempoFeedback("This file could not be read as audio.");
          return;
        }
        if (!result.bpm) {
          tempoFeedback("No steady beat in this track. Type the tempo, or tap along.");
          return;
        }
        const offeredBefore = droppedTrack && droppedTrack.hash === result.hash;
        droppedTrack = result;
        const held = editorState.current.tempo;
        // A tempo already measured from a different file is not replaced
        // behind the builder's back: the Rehearsal says the two differ, and
        // dropping the same file in a second time takes its tempo (ADR 0058).
        if (held && held.source === "analysed" && held.hash && held.hash !== result.hash && !offeredBefore) {
          tempoFeedback(`Not the track this tempo was measured from. It reads ${result.bpm} BPM; drop it in again to use it.`);
          updateValidationSummary();
          return;
        }
        if (held && held.hash === result.hash) {
          tempoFeedback("This is the track the tempo was measured from.");
          updateValidationSummary();
          return;
        }
        tempoFeedback("");
        setTempo(window.SeqTempo.analyzedTempo(result));
      });
    }

    const downbeatInput = document.getElementById("seq-editor-downbeat-beat");
    if (downbeatInput) {
      downbeatInput.addEventListener("change", () => {
        const moved = window.SeqTempo?.moveDownbeat(editorState.current.tempo, parseInt(downbeatInput.value, 10));
        if (!moved) {
          tempoFeedback("That beat is past where a sequence can reach.");
          return;
        }
        tempoFeedback("");
        downbeatInput.value = "1";
        setTempo(moved);
      });
    }

    document.getElementById("seq-editor-retime")?.addEventListener("click", retimeToGrid);
    document.getElementById("seq-editor-undo")?.addEventListener("click", undo);
    document.getElementById("seq-editor-redo")?.addEventListener("click", redo);
    ["steps", "timeline"].forEach((view) =>
      document.getElementById(`seq-editor-show-${view}`)?.addEventListener("click", () => showSessionView(view)));
    DRAWER_TABS.forEach((tab) =>
      document.getElementById(`seq-editor-tab-${tab}`)?.addEventListener("click", () => showTab(tab)));

    // The Picked block pane is written again whenever the selection changes,
    // so its controls are heard on the pane itself.
    const pickedPane = document.getElementById("seq-pane-block");
    if (pickedPane) {
      pickedPane.addEventListener("click", (event) => {
        const act = event.target?.closest?.("[data-picked]")?.dataset.picked;
        if (act === "remove") sessionTimeline?.removePicked();
        if (act === "off-beat" && pickedBlocks.length === 1) setStepBeat(pickedBlocks[0].steps[0], { beat: null });
      });
      pickedPane.addEventListener("change", (event) => {
        const input = event.target;
        if (input?.dataset?.picked !== "start" || input.value === "" || !sessionTimeline) return;
        sessionTimeline.movePickedTo(Number(input.value));
        // A start the block could not take - it is held at its limit - leaves
        // the routine as it was, so nothing else draws the pane again: draw
        // it from where the block is, never leave the number that was typed.
        pickedShown = null;
        showPicked(sessionTimeline.picked());
      });
    }

    const bpmInput = document.getElementById("seq-editor-bpm");
    if (bpmInput) {
      bpmInput.addEventListener("change", () => {
        setTempo(typedTempo(bpmInput.value));
      });
    }

    if (notesInput) {
      notesInput.addEventListener("input", () => {
        if (!editorState.current.meta) editorState.current.meta = {};
        editorState.current.meta.notes = notesInput.value;
        paintSession();
      });
    }

    // Advanced Settings collapse toggle
    if (advancedToggle && advancedFields) {
      advancedToggle.addEventListener("click", () => {
        const isExpanded = advancedToggle.getAttribute("aria-expanded") === "true";
        advancedToggle.setAttribute("aria-expanded", !isExpanded);
        advancedFields.classList.toggle("hidden");
      });

      // Keyboard support: Space and Enter to toggle
      advancedToggle.addEventListener("keydown", (e) => {
        if (e.key === " " || e.key === "Enter") {
          e.preventDefault();
          advancedToggle.click();
        }
      });
    }

    const addStepBtn = document.getElementById("seq-editor-add-step");
    if (addStepBtn) {
      addStepBtn.addEventListener("click", () => {
        const newStep = { t: 0, type: "audio", cmd: "$H" };
        const steps = editorState.current.steps;
        const terminalIdx = steps.findIndex((step) => step.type === "end");
        historyPush();
        if (terminalIdx >= 0) {
          const terminalT = steps[terminalIdx].t || 0;
          newStep.t = terminalT;
          steps.splice(terminalIdx, 0, newStep);
        } else {
          steps.push(newStep);
        }
        rerenderStepTable();
        edited();
      });
    }

    const testBtn = document.getElementById("seq-editor-test");
    const saveBtn = document.getElementById("seq-editor-save");
    const revertBtn = document.getElementById("seq-editor-revert");
    // All sequences: the way back to the list, which asks before it drops
    // unsaved edits.
    const cancelBtn = document.getElementById("seq-editor-cancel");

    if (testBtn) testBtn.addEventListener("click", handleTestOnDroid);
    if (saveBtn) saveBtn.addEventListener("click", handleSave);

    if (revertBtn) {
      revertBtn.addEventListener("click", () => renderEditorView(editorState.original));
    }

    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => leaveSession(() => {}));
    }
  };

  // =========================================================================
  // Panel-Intent Availability Advisory System
  // =========================================================================
  // Passive inline advisory for dome panel-intent steps: shown when a saved step
  // targets an unavailable panel (disabled, inactive, excluded, unmapped, unverified).
  // Reusable for both passive (on expand) and click-time advisory updates.

  // Helper: Build an advisory message for an element, or null if available
  // The words are the Rehearsal's (data/seq_rehearsal.js unavailableMessage()),
  // so the message beside a step and the finding in the Rehearsal's list are
  // one sentence from one place; this is only where it is shown (#287, #439).
  const buildAdvisoryMessage = (elementId) =>
    window.SeqRehearsal?.unavailableMessage?.(elementId, window.DomeLayout?.getModel?.()) || null;

  // Helper: Update the advisory element in a fields container
  // Call this after rendering (passive) or after click/keyboard on non-selectable panel
  const updatePanelAdvisory = (fieldsContainer, elementId) => {
    const advisoryEl = fieldsContainer?.querySelector?.(".dome-panel-advisory");
    if (!advisoryEl) return; // Advisory element not present; skip

    const message = buildAdvisoryMessage(elementId);
    if (message) {
      advisoryEl.textContent = message;
      advisoryEl.classList.remove("hidden");
    } else {
      advisoryEl.textContent = "";
      advisoryEl.classList.add("hidden");
    }
  };

  // Called from renderEditorView (initial) and rerenderStepTable (after any step change).
  // Step rows are re-created on every rerender, so fresh listeners are needed each time.
  // Helper: Attach dome-related listeners (panel, preset, advanced) to a specific fields container
  const attachDomePanelIntentListeners = (fieldsContainer, stepIdx) => {
    // Helper: Update the command, target select, and preview from a new target value
    const setTarget = (newTarget) => {
      const actionSelect = fieldsContainer.querySelector(".dome-action-select");
      const targetSelect = fieldsContainer.querySelector(".dome-target-select");
      const hiddenInput = fieldsContainer.querySelector('input[data-field="cmd"]');
      const preview = fieldsContainer.querySelector(".dome-cmd-preview");

      if (actionSelect && hiddenInput) {
        const action = actionSelect.value;
        const cmd = `:${action}${newTarget}`;
        hiddenInput.value = cmd;
        if (targetSelect) targetSelect.value = newTarget;
        if (preview) preview.textContent = cmd;
        pickedOnce(stepIdx, () => {
          editorState.current.steps[stepIdx].cmd = cmd;
        });
      }
    };

    // Live picker: DomeCommandMap.resolvePanelCommand() returns a COMPLETE command
    // string (e.g. ":OP07"), so assign it directly. Do NOT route it through
    // setTarget(), which prepends ":<action>" and would double the prefix
    // (":OP:OP07"). The legacy picker path keeps setTarget() (bare target + prefix).
    const setCommand = (fullCmd) => {
      const targetSelect = fieldsContainer.querySelector(".dome-target-select");
      const hiddenInput = fieldsContainer.querySelector('input[data-field="cmd"]');
      const preview = fieldsContainer.querySelector(".dome-cmd-preview");
      if (!hiddenInput) return;
      hiddenInput.value = fullCmd;
      if (preview) preview.textContent = fullCmd;
      // Keep the target dropdown in sync by recovering the bare command target
      // from the full command (":OP07" -> "07", ":OPP1" -> "P1").
      if (targetSelect) targetSelect.value = fullCmd.replace(/^:(OP|CL|OF)/, "");
      pickedOnce(stepIdx, () => {
        editorState.current.steps[stepIdx].cmd = fullCmd;
      });
    };

    // Handle both live (data-element-id) and legacy (data-target) pickers
    // SVG panel clicks — use event delegation on the SVG
    const svg = fieldsContainer.querySelector(".dome-svg-picker");
    const hasLiveLayout = svg && svg.closest(".dome-svg-picker-container");

    if (svg) {
      svg.addEventListener("click", (e) => {
        // Try to find element in live picker (data-element-id)
        let element = e.target.closest("[data-element-id]");
        let target = null;
        let elementId = null;

        if (element && hasLiveLayout) {
          // Live picker: check selectability and show advisory if needed
          elementId = element.dataset.elementId;
          const isSelectable = element.dataset.selectable === "true";

          if (!isSelectable) {
            // Non-actionable: show advisory inline instead of alert modal
            updatePanelAdvisory(fieldsContainer, elementId);
            return;
          }

          // Selectable: resolve to command via DomeCommandMap
          const actionSelect = fieldsContainer.querySelector(".dome-action-select");
          if (actionSelect && window.DomeCommandMap?.resolvePanelCommand) {
            const action = actionSelect.value;
            const capabilityMap = { "OP": "open", "CL": "close", "OF": "flutter" };
            const capability = capabilityMap[action] || "open";
            const fullCmd = window.DomeCommandMap.resolvePanelCommand(elementId, capability);

            if (fullCmd) {
              setCommand(fullCmd);
              highlightSelectedPanel(elementId, "live");
              // Clear advisory since this panel is selectable (no issues)
              updatePanelAdvisory(fieldsContainer, elementId);
            }
          }
          return;
        }

        // Legacy picker: try data-target
        element = e.target.closest("[data-target]");
        if (!element) return;

        e.preventDefault();
        target = element.dataset.target;
        if (!target) return;

        // Update target and highlight (ring and pie both directly selectable)
        setTarget(target);
        highlightSelectedPanel(target, "legacy");
      });

      svg.addEventListener("keydown", (e) => {
        if (e.key !== "Enter" && e.key !== " ") return;

        // Try live picker (data-element-id)
        let element = e.target.closest("[data-element-id]");
        if (element && hasLiveLayout) {
          const elementId = element.dataset.elementId;
          const isSelectable = element.dataset.selectable === "true";

          if (!isSelectable) {
            e.preventDefault();
            // Non-actionable: show advisory inline instead of alert modal
            updatePanelAdvisory(fieldsContainer, elementId);
            return;
          }

          const actionSelect = fieldsContainer.querySelector(".dome-action-select");
          if (actionSelect && window.DomeCommandMap?.resolvePanelCommand) {
            const action = actionSelect.value;
            const capabilityMap = { "OP": "open", "CL": "close", "OF": "flutter" };
            const capability = capabilityMap[action] || "open";
            const fullCmd = window.DomeCommandMap.resolvePanelCommand(elementId, capability);

            if (fullCmd) {
              e.preventDefault();
              setCommand(fullCmd);
              highlightSelectedPanel(elementId, "live");
              // Clear advisory since this panel is selectable (no issues)
              updatePanelAdvisory(fieldsContainer, elementId);
            }
          }
          return;
        }

        // Legacy picker (data-target)
        element = e.target.closest("[data-target]");
        if (!element) return;

        e.preventDefault();
        const target = element.dataset.target;
        if (!target) return;

        setTarget(target);
        highlightSelectedPanel(target, "legacy");
      });
    }

    // Helper: Highlight the selected panel in the SVG (both live and legacy)
    // mode = "live" (data-element-id) or "legacy" (data-target)
    const highlightSelectedPanel = (target, mode = "legacy") => {
      const svg = fieldsContainer.querySelector(".dome-svg-picker");
      if (!svg) return;

      if (mode === "live") {
        // Live picker: highlight by data-element-id
        svg.querySelectorAll("[data-element-id]").forEach((p) => {
          p.classList.remove("selected");
        });
        const selectedElement = svg.querySelector(`[data-element-id="${target}"]`);
        if (selectedElement) {
          selectedElement.classList.add("selected");
        }
      } else {
        // Legacy picker: highlight by data-target
        svg.querySelectorAll("[data-target]").forEach((p) => {
          p.classList.remove("selected");
        });
        const selectedElement = svg.querySelector(`[data-target="${target}"]`);
        if (selectedElement) {
          selectedElement.classList.add("selected");
        }
      }
    };

    // Highlight the initial target on render: decode existing cmd and highlight
    const targetSelect = fieldsContainer.querySelector(".dome-target-select");
    if (targetSelect && editorState.current.steps[stepIdx]) {
      const step = editorState.current.steps[stepIdx];
      const cmd = step.cmd || "";

      if (hasLiveLayout && window.DomeCommandMap?.decodeCommandToElement) {
        // Try to decode as a panel command
        const decoded = window.DomeCommandMap.decodeCommandToElement(cmd);
        // decoded is null for group steps (:OP14/:OP15/:OP00) or non-panel commands (advanced mode).
        // Only highlight if decoded is a real panel (ring or pie).
        if (decoded && (decoded.kind === "ring" || decoded.kind === "pie")) {
          highlightSelectedPanel(decoded.id, "live");
          // Show passive advisory if this panel has availability issues
          updatePanelAdvisory(fieldsContainer, decoded.id);
        } else {
          // Not a panel command or decode failed; no highlight (groups, advanced, non-panel)
          // Clear advisory for non-panel commands
          updatePanelAdvisory(fieldsContainer, null);
        }
      } else {
        // Legacy picker: extract target from cmd
        const match = cmd.match(/^:?(OP|CL|OF)(.+)$/);
        if (match) {
          const targetValue = match[2];
          highlightSelectedPanel(targetValue, "legacy");
        }
        // Clear advisory for legacy picker (no live availability data)
        updatePanelAdvisory(fieldsContainer, null);
      }
    }

    // Dome panel intent action/target selects update the hidden cmd field
    fieldsContainer.querySelectorAll(".dome-action-select, .dome-target-select").forEach((select) => {
      select.addEventListener("change", () => {
        const newTarget = fieldsContainer.querySelector(".dome-target-select").value;
        setTarget(newTarget);
        // Update advisory and highlight for the newly selected target
        if (hasLiveLayout && window.DomeCommandMap?.decodeCommandToElement) {
          // Decode the command that was just built by setTarget to get the element ID
          const cmd = editorState.current.steps[stepIdx].cmd;
          const decoded = window.DomeCommandMap.decodeCommandToElement(cmd);
          // Only highlight and advise if decoded is a real panel (ring or pie)
          if (decoded && (decoded.kind === "ring" || decoded.kind === "pie")) {
            highlightSelectedPanel(decoded.id, "live");
            updatePanelAdvisory(fieldsContainer, decoded.id);
          } else {
            // Not a panel command or decode failed; clear advisory (groups, advanced, non-panel)
            updatePanelAdvisory(fieldsContainer, null);
          }
        } else {
          // Legacy picker: use the bare target value
          highlightSelectedPanel(newTarget, "legacy");
          updatePanelAdvisory(fieldsContainer, null);
        }
      });
    });


    // Dome visual preset selector updates the hidden cmd field
    const presetSelect = fieldsContainer.querySelector(".step-field-preset");
    if (presetSelect) {
      // On input as well as change: the select is a form field too, and its
      // run ends at its change, so the command has to be written before that
      // or one choice would leave two entries behind.
      const choosePreset = () => {
        const preset = presetSelect.value;
        const cmd = `DV:${preset}`;
        const hiddenInput = fieldsContainer.querySelector('input[data-field="cmd"]');
        if (hiddenInput) {
          hiddenInput.value = cmd;
        }
        picked(stepIdx, () => {
          editorState.current.steps[stepIdx].cmd = cmd;
        });
      };
      presetSelect.addEventListener("input", choosePreset);
      presetSelect.addEventListener("change", choosePreset);
    }

    // Dome mode toggle, one cycle: panel -> preset -> advanced -> panel. Each
    // mode's button is labelled with the mode it goes to next.
    const toggleBtn = fieldsContainer.querySelector(".dome-mode-toggle");
    if (toggleBtn) {
      toggleBtn.addEventListener("click", (e) => {
        e.preventDefault();

        // Determine current mode by checking what UI is visible
        let currentMode = "advanced";
        if (fieldsContainer.querySelector(".dome-action-select")) {
          currentMode = "panel";
        } else if (fieldsContainer.querySelector(".step-field-preset")) {
          currentMode = "preset";
        }

        // Toggle to the next mode in the cycle: panel → preset → advanced → panel
        let nextMode = "panel";
        if (currentMode === "panel") {
          nextMode = "preset";
        } else if (currentMode === "preset") {
          nextMode = "advanced";
        } else {
          nextMode = "panel";
        }

        const before = historyBegin();
        // If toggling to panel from preset/advanced, ensure a valid panel cmd
        if (nextMode === "panel") {
          const hiddenInput = fieldsContainer.querySelector('input[data-field="cmd"]');
          const currentCmd = hiddenInput ? hiddenInput.value : "";
          const match = currentCmd.match(/^:?(OP|CL|OF)(.+)$/);
          if (!match) {
            // Not a panel intent; default to :OP00
            editorState.current.steps[stepIdx].cmd = ":OP00";
          }
        }
        // If toggling to preset from panel/advanced, ensure a valid DV: cmd
        else if (nextMode === "preset") {
          const hiddenInput = fieldsContainer.querySelector('input[data-field="cmd"]');
          const currentCmd = hiddenInput ? hiddenInput.value : "";
          if (!currentCmd.startsWith("DV:")) {
            // Not a preset; default to ROCKMARCH
            editorState.current.steps[stepIdx].cmd = "DV:ROCKMARCH";
          }
        }

        fieldsContainer.dataset.domeMode = nextMode;
        renderStepFields(editorState.current.steps[stepIdx], fieldsContainer);

        // Re-attach listeners for the newly rendered fields
        fieldsContainer.querySelectorAll("[data-field]").forEach((input) => bindStepField(input, stepIdx));

        attachDomePanelIntentListeners(fieldsContainer, stepIdx);
        validateAndUpdateStep(stepIdx);
        historyCommit(before);
        paintHistory();
      });
    }
  };

  // Helper: Attach Logic/PSI mode listeners to a specific fields container
  const attachDomeLogicListeners = (fieldsContainer, stepIdx) => {
    const targetSelect = fieldsContainer.querySelector(".dl-target-select");
    const modeSelect = fieldsContainer.querySelector(".dl-mode-select");
    const colorSelect = fieldsContainer.querySelector(".dl-color-select");
    const durationInput = fieldsContainer.querySelector(".dl-duration-input");
    const hiddenCmd = fieldsContainer.querySelector('input[data-field="cmd"]');

    const updateCmd = () => {
      if (!targetSelect || !modeSelect || !hiddenCmd) return;
      let cmd = `DL:${targetSelect.value}:${modeSelect.value}`;
      if (colorSelect && colorSelect.value !== "DEFAULT") {
        cmd += `:${colorSelect.value}`;
        if (durationInput && durationInput.value) {
          cmd += `:${durationInput.value}`;
        }
      } else if (durationInput && durationInput.value) {
        // If duration is set but color is DEFAULT, we still need to include DEFAULT
        cmd += `:DEFAULT:${durationInput.value}`;
      }
      hiddenCmd.value = cmd;
      picked(stepIdx, () => {
        editorState.current.steps[stepIdx].cmd = cmd;
      });
    };

    [targetSelect, modeSelect, colorSelect, durationInput].forEach((el) => {
      if (el) {
        el.addEventListener("change", updateCmd);
        el.addEventListener("input", updateCmd);
      }
    });
  };

  // Helper: Attach Logic Text listeners to a specific fields container
  const attachDomeTextListeners = (fieldsContainer, stepIdx) => {
    const targetSelect = fieldsContainer.querySelector(".dt-target-select");
    const colorSelect = fieldsContainer.querySelector(".dt-color-select");
    const textInput = fieldsContainer.querySelector(".dt-text-input");
    const durationInput = fieldsContainer.querySelector(".dt-duration-input");
    const speedInput = fieldsContainer.querySelector(".dt-speed-input");
    const hiddenCmd = fieldsContainer.querySelector('input[data-field="cmd"]');

    const updateCmd = () => {
      if (!targetSelect || !colorSelect || !hiddenCmd) return;
      const plainText = textInput ? textInput.value : "";
      let encodedText = "";
      try {
        // Percent-encode the text: newline=%0A, %=%25, :=%3A, space stays literal
        encodedText = encodeURIComponent(plainText)
          .replace(/%20/g, " ");  // Keep spaces literal
      } catch (e) {
        encodedText = plainText;
      }
      const duration = durationInput ? durationInput.value : "5";
      const speed = speedInput ? speedInput.value : "0";
      const cmd = `DT:${targetSelect.value}:${colorSelect.value}:${duration}:${speed}:${encodedText}`;
      hiddenCmd.value = cmd;
      picked(stepIdx, () => {
        editorState.current.steps[stepIdx].cmd = cmd;
      });
    };

    [targetSelect, colorSelect, textInput, durationInput, speedInput].forEach((el) => {
      if (el) {
        el.addEventListener("change", updateCmd);
        el.addEventListener("input", updateCmd);
      }
    });
  };

  // Helper: Attach Holo Effect listeners to a specific fields container
  const attachDomeHoloListeners = (fieldsContainer, stepIdx) => {
    const targetSelect = fieldsContainer.querySelector(".dh-target-select");
    const effectSelect = fieldsContainer.querySelector(".dh-effect-select");
    const colorSelect = fieldsContainer.querySelector(".dh-color-select");
    const durationInput = fieldsContainer.querySelector(".dh-duration-input");
    const hiddenCmd = fieldsContainer.querySelector('input[data-field="cmd"]');

    const updateCmd = () => {
      if (!targetSelect || !effectSelect || !hiddenCmd) return;
      let cmd = `DH:${targetSelect.value}:${effectSelect.value}`;
      if (colorSelect && colorSelect.value !== "DEFAULT") {
        cmd += `:${colorSelect.value}`;
        if (durationInput && durationInput.value) {
          cmd += `:${durationInput.value}`;
        }
      } else if (durationInput && durationInput.value) {
        // If durationOrCount is set but color is DEFAULT, we still need to include DEFAULT
        cmd += `:DEFAULT:${durationInput.value}`;
      }
      hiddenCmd.value = cmd;
      picked(stepIdx, () => {
        editorState.current.steps[stepIdx].cmd = cmd;
      });
    };

    [targetSelect, effectSelect, colorSelect, durationInput].forEach((el) => {
      if (el) {
        el.addEventListener("change", updateCmd);
        el.addEventListener("input", updateCmd);
      }
    });
  };

  const attachStepListeners = () => {
    // The beat list and a span in beats (renderBeatPicker()).
    const stepIndexOf = (el) => parseInt(el.closest(".step-card")?.dataset.stepIndex, 10);
    document.querySelectorAll(".step-beat-pick").forEach((pill) => {
      pill.addEventListener("click", () => {
        const stepIdx = stepIndexOf(pill);
        if (Number.isInteger(stepIdx)) setStepBeat(stepIdx, { beat: parseInt(pill.dataset.beat, 10) });
      });
    });
    document.querySelectorAll(".step-beat-clear").forEach((btn) => {
      btn.addEventListener("click", () => {
        const stepIdx = stepIndexOf(btn);
        if (Number.isInteger(stepIdx)) setStepBeat(stepIdx, { beat: null });
      });
    });
    // A Gesture's pickers write their hidden form value and read the step back.
    document.querySelectorAll(".gesture-pick").forEach((pill) => {
      pill.addEventListener("click", () => {
        const stepIdx = stepIndexOf(pill);
        const fields = pill.closest(".step-fields");
        const hidden = fields?.querySelector(`input[data-field="${pill.dataset.pick}"]`);
        if (!hidden || !Number.isInteger(stepIdx)) return;
        const before = historyBegin();
        hidden.value = pill.dataset.value;
        validateAndUpdateStep(stepIdx);
        historyCommit(before);
        rerenderStepTable();
        paintHistory();
      });
    });
    document.querySelectorAll(".gesture-beats").forEach((input) => {
      input.addEventListener("change", () => {
        const stepIdx = stepIndexOf(input);
        const beats = parseInt(input.value, 10);
        if (Number.isInteger(stepIdx)) setStepBeat(stepIdx, { [input.dataset.beats]: Number.isInteger(beats) ? beats : null });
      });
    });
    document.querySelectorAll(".step-beat-span").forEach((input) => {
      input.addEventListener("change", () => {
        const stepIdx = stepIndexOf(input);
        const beats = parseInt(input.value, 10);
        if (Number.isInteger(stepIdx)) setStepBeat(stepIdx, { spanBeats: Number.isInteger(beats) ? beats : null });
      });
    });

    const stepTypeDefaults = {
      audio: { cmd: "$H" },
      dome: { cmd: ":OP00" },
      domeRotate: { speedPct: 0, durationMs: 0 },
      loop: { body: 2, periodMs: 1846, durationMs: 14000 },
      random: { set: "ring", mode: "flutter", moveMs: 300, jitterMs: 500, distinct: true },
      audioCat: { category: "alert", fallback: "scream" },
      gesture: { set: "ring", spread: "wave" },
      sequence: {},
      end: {},
    };

    // Card expand/collapse listeners
    document.querySelectorAll(".step-card").forEach((card) => {
      const stepIdx = parseInt(card.dataset.stepIndex, 10);
      const header = card.querySelector(".step-card-header");
      const removeBtn = card.querySelector(".step-remove");

      // Handle header click/keyboard to toggle expand
      const toggleExpanded = () => {
        if (editorState.expanded.has(stepIdx)) {
          editorState.expanded.delete(stepIdx);
        } else {
          editorState.expanded.add(stepIdx);
        }
        rerenderStepTable();
      };

      if (header) {
        header.addEventListener("click", toggleExpanded);
        header.addEventListener("keydown", (e) => {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            toggleExpanded();
          }
        });
      }

      // Remove button listener
      if (removeBtn) {
        removeBtn.addEventListener("click", (e) => {
          e.stopPropagation();
          // The same removal the timeline makes, so a loop is kept whole
          // from either view.
          if (confirm("Remove this step?")) removeSteps([stepIdx]);
        });
      }
    });

    // Step type chip selection
    document.querySelectorAll(".step-type-chip").forEach((chip) => {
      chip.addEventListener("click", (e) => {
        e.preventDefault();
        const card = chip.closest(".step-card");
        if (!card) return;
        const stepIdx = parseInt(card.dataset.stepIndex, 10);

        // Logic / PSI, Logic Text and Holo Effect are dome sub-modes: a step in
        // one is a dome step, so it lights the main dome chip beside its own,
        // exactly as the step renders. Every other chip in the card goes dark -
        // the step's type is read back from the first lit chip
        // (validateAndUpdateStep), so a chip left lit from the previous type
        // would turn the step back into that type.
        const domeMode = chip.dataset.domeMode || null;
        const isMainDomeChip = (c) => c.dataset.type === "dome" && !c.dataset.domeMode;
        card.querySelectorAll(".step-type-chip").forEach((c) => {
          const lit = c === chip || (domeMode !== null && isMainDomeChip(c));
          c.classList.toggle("active", lit);
          c.setAttribute("aria-pressed", lit ? "true" : "false");
        });

        const newType = chip.dataset.type;
        const before = historyBegin();
        // Clear all old type-specific fields; keep only t, assign new type + defaults
        const { t } = editorState.current.steps[stepIdx];
        let newDefaults = stepTypeDefaults[newType] || {};

        // A dome sub-mode starts from its own command, which is what tells the
        // step's fields which mode to draw.
        if (domeMode === "text") {
          newDefaults = { cmd: "DT:LOGIC:DEFAULT:5:0:" };
        } else if (domeMode === "holo") {
          newDefaults = { cmd: "DH:A:FLASH" };
        } else if (domeMode === "logic") {
          newDefaults = { cmd: "DL:LOGIC:NORMAL" };
        }

        editorState.current.steps[stepIdx] = { t, type: newType, ...newDefaults };

        const fieldsContainer = card.querySelector(".step-fields");
        renderStepFields(editorState.current.steps[stepIdx], fieldsContainer);

        fieldsContainer.querySelectorAll("[data-field]").forEach((input) => bindStepField(input, stepIdx));

        // If switching to dome type, attach appropriate listeners
        if (newType === "dome") {
          if (domeMode === "text") {
            attachDomeTextListeners(fieldsContainer, stepIdx);
          } else if (domeMode === "holo") {
            attachDomeHoloListeners(fieldsContainer, stepIdx);
          } else if (domeMode === "logic") {
            attachDomeLogicListeners(fieldsContainer, stepIdx);
          } else {
            attachDomePanelIntentListeners(fieldsContainer, stepIdx);
          }
        }

        // If switching to domeRotate type, attach domeRotate-specific listeners
        if (newType === "domeRotate") {
          const directionSelect = fieldsContainer.querySelector(".step-field-direction");
          if (directionSelect) {
            directionSelect.addEventListener("change", () => {
              const direction = directionSelect.value;
              const durationInput = fieldsContainer.querySelector(".step-field-durationMs");
              // If direction is "stop", force duration to 0
              if (direction === "stop" && durationInput) {
                durationInput.value = "0";
              }
              typed(stepIdx);
            });
          }
        }

        validateAndUpdateStep(stepIdx);
        historyCommit(before);
        paintHistory();
      });
    });

    // Step field inputs, and each step's time offset: .step-t sits beside the
    // fields rather than among them, and validateAndUpdateStep() reads it back
    // into the step like any field. Without a listener of its own, a changed
    // time never reached the step, and Save sent the old one.
    document.querySelectorAll(".step-fields [data-field], .step-card .step-t").forEach((input) => {
      const card = input.closest(".step-card");
      bindStepField(input, parseInt(card.dataset.stepIndex, 10));
    });

    // Reference panel toggle (What Each Step Type Does)
    document.querySelectorAll(".step-type-reference-toggle").forEach((toggle) => {
      toggle.addEventListener("click", (e) => {
        e.preventDefault();
        const panel = toggle.nextElementSibling;
        if (!panel) return;
        const isExpanded = toggle.getAttribute("aria-expanded") === "true";
        toggle.setAttribute("aria-expanded", !isExpanded);
        panel.classList.toggle("hidden");
      });
    });

    // Attach dome-specific listeners for each dome step
    document.querySelectorAll(".step-card").forEach((card) => {
      const stepIdx = parseInt(card.dataset.stepIndex, 10);
      const fieldsContainer = card.querySelector(".step-fields");
      const step = editorState.current.steps[stepIdx];
      const typeChip = card.querySelector(".step-type-chip.active");
      if (typeChip && typeChip.dataset.type === "dome" && fieldsContainer) {
        // Check if this is a Logic Text step (DT: command)
        if (step && (step.cmd || "").startsWith("DT:")) {
          attachDomeTextListeners(fieldsContainer, stepIdx);
        } else if (step && (step.cmd || "").startsWith("DL:")) {
          // Check if this is a Logic/PSI step (DL: command)
          attachDomeLogicListeners(fieldsContainer, stepIdx);
        } else if (step && (step.cmd || "").startsWith("DH:")) {
          // Check if this is a Holo Effect step (DH: command)
          attachDomeHoloListeners(fieldsContainer, stepIdx);
        } else {
          attachDomePanelIntentListeners(fieldsContainer, stepIdx);
        }
      }
    });

    // Attach domeRotate-specific listeners for direction changes
    document.querySelectorAll(".step-card").forEach((card) => {
      const stepIdx = parseInt(card.dataset.stepIndex, 10);
      const fieldsContainer = card.querySelector(".step-fields");
      const typeChip = card.querySelector(".step-type-chip.active");
      if (typeChip && typeChip.dataset.type === "domeRotate" && fieldsContainer) {
        const directionSelect = fieldsContainer.querySelector(".step-field-direction");
        if (directionSelect) {
          directionSelect.addEventListener("change", () => {
            const direction = directionSelect.value;
            const durationInput = fieldsContainer.querySelector(".step-field-durationMs");
            // If direction is "stop", force duration to 0 and trigger validation
            if (direction === "stop" && durationInput) {
              durationInput.value = "0";
            }
            typed(stepIdx);
          });
        }
      }
    });


    // Drag-and-drop reordering (local draggedIndex; fresh per rerender)
    let draggedIndex = null;
    document.querySelectorAll(".step-card").forEach((card, idx) => {
      card.addEventListener("dragstart", (e) => {
        draggedIndex = idx;
        card.classList.add("dragging");
        e.dataTransfer.effectAllowed = "move";
      });

      card.addEventListener("dragend", () => {
        card.classList.remove("dragging");
        draggedIndex = null;
      });

      card.addEventListener("dragover", (e) => {
        e.preventDefault();
        e.dataTransfer.dropEffect = "move";
        const rect = card.getBoundingClientRect();
        const midpoint = rect.top + rect.height / 2;
        if (e.clientY < midpoint) {
          card.classList.add("drop-above");
          card.classList.remove("drop-below");
        } else {
          card.classList.add("drop-below");
          card.classList.remove("drop-above");
        }
      });

      card.addEventListener("dragleave", () => {
        card.classList.remove("drop-above", "drop-below");
      });

      card.addEventListener("drop", (e) => {
        e.preventDefault();
        if (draggedIndex !== null && draggedIndex !== idx) {
          historyPush();
          const [movedStep] = editorState.current.steps.splice(draggedIndex, 1);
          const insertIdx = draggedIndex < idx ? idx - 1 : idx;
          editorState.current.steps.splice(insertIdx, 0, movedStep);
          rerenderStepTable();
          edited();
        }
        card.classList.remove("drop-above", "drop-below");
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

    // Populate conditional fields. Only expanded cards have a .step-fields
    // container, so derive the real step index from the card's data-step-index
    // instead of the enumeration order (which is expanded-rank, not step index).
    document.querySelectorAll(".step-fields").forEach((container) => {
      const card = container.closest(".step-card");
      if (!card) return;
      const stepIdx = parseInt(card.dataset.stepIndex, 10);
      renderStepFields(editorState.current.steps[stepIdx], container);
    });

    // Re-attach only step-row listeners (metadata/footer listeners persist)
    attachStepListeners();
  };

  // Re-render only the panel-intent pickers when the dome layout changes.
  // This refreshes the live picker SVG without re-rendering the entire step table.
  // Called via DomeLayout.onChange() when the dome reconnects.
  const rerenderPanelIntentPickers = () => {
    document.querySelectorAll(".dome-svg-picker-container, .dome-picker-container").forEach((container) => {
      const card = container.closest(".step-card");
      if (!card) return;
      const stepIdx = parseInt(card.dataset.stepIndex, 10);
      const step = editorState.current.steps[stepIdx];
      if (!step || step.type !== "dome") return;

      // Detect the current dome mode
      const domeCmd = step.cmd || "";
      let domeMode;
      if (domeCmd.startsWith("DV:")) {
        domeMode = "preset";
      } else if (domeCmd.startsWith("DL:")) {
        domeMode = "logic";
      } else if (domeCmd.startsWith("DH:")) {
        domeMode = "holo";
      } else if (domeCmd.startsWith("DT:")) {
        domeMode = "text";
      } else if (/^(:|)(OP|CL|OF)/.test(domeCmd)) {
        domeMode = "panel";
      } else {
        domeMode = "advanced";
      }

      // Only re-render if currently in panel mode (live picker mode)
      if (domeMode === "panel") {
        const fieldsContainer = card.querySelector(".step-fields");
        if (fieldsContainer) {
          renderStepFields(step, fieldsContainer);
          // Re-attach panel-intent listeners
          attachDomePanelIntentListeners(fieldsContainer, stepIdx);
        }
      }
    });
  };

  // The strip's feedback line: what just happened to the sequence.
  const FEEDBACK_CLASS = { ok: " success", error: " error" };
  const feedbackHtml = (message, kind) =>
    `<p class="feedback${FEEDBACK_CLASS[kind] || ""}">${window.PAUtils.escapeHtml(message)}</p>`;

  const showEditorFeedback = (message, kind = "info") => {
    const feedbackEl = document.getElementById("seq-editor-feedback");
    if (!feedbackEl) return;
    // Whatever badge was here is replaced, so a later read of the droid must
    // not paint it back.
    lastRunBadge = null;
    saidSaved = false;
    feedbackEl.innerHTML = message ? feedbackHtml(message, kind) : "";
  };

  // A run's line of feedback with the Rehearsal's folded badge under it. The
  // badge rehearses `seq`, the copy the droid ran - which is not the edits on
  // screen, and so not what the Rehearsal tab is reading. It is only ever shown
  // after the run has been sent, so nothing it finds can stand in the run's
  // way (#287 specific 6).
  const showRunBadge = (message, seq) => {
    const feedbackEl = document.getElementById("seq-editor-feedback");
    if (!feedbackEl) return;
    lastRunBadge = [message, seq];
    saidSaved = false;
    const rehearsal = window.SeqRehearsal;
    const badge = rehearsal ? rehearsal.badgeHtml(rehearsal.rehearse(seq, rehearsalContext())) : "";
    feedbackEl.innerHTML = feedbackHtml(message, "ok") + badge;
  };

  const handleSave = async () => {
    const validation = SeqProtocolCheck.validateSequence(editorState.current);
    if (!validation.ok) {
      showEditorFeedback(validation.error || "Fix validation errors before saving.", "error");
      return;
    }
    const cap = learnedSequenceCap();
    if (editorState.isNew && cap !== null && sequences.length >= cap) {
      // Over the cap, one delete is not enough: say how many it takes.
      const toDelete = sequences.length - cap + 1;
      showEditorFeedback(`Capacity limit: ${cap} sequences on this droid. Delete ${toDelete === 1 ? "one" : toDelete} first.`, "error");
      return;
    }
    const saveBtn = document.getElementById("seq-editor-save");
    if (saveBtn) saveBtn.disabled = true;
    showEditorFeedback("Saving...", "info");
    // Every sequence saved from here on has a stable id, so another can hold
    // it; one loaded without gets its id on this save and keeps it.
    if (!editorState.current.id) editorState.current.id = mintSequenceId();
    // What is saved is the copy that was sent, taken once, before the request:
    // an edit made while the droid is still answering is not in it, so it
    // still counts as unsaved and is still asked about.
    const sent = JSON.parse(JSON.stringify(editorState.current));
    try {
      await PAApi.postJson("/api/seq", sent);
      // What the Rehearsal finds in the saved routine is in its own tab, which
      // reads the routine as it is; the strip says only that it was saved.
      showEditorFeedback("Saved.", "ok");
      saidSaved = true;
      editorState.isNew = false;
      editorState.tuningFactory = null;
      editorState.original = sent;
      editorState.saved = true;
      paintSession();
      refreshLearned();
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
    showEditorFeedback(`Sending ${seqName} to droid...`, "info");
    try {
      await PAApi.postJson("/api/seq/test", { name: seqName });
      // The droid ran what is saved under that name, not the edits on screen,
      // so the badge rehearses the last saved or cloned copy.
      showRunBadge(`${seqName} dispatched.`, editorState.original);
    } catch (error) {
      showEditorFeedback("Test failed: " + PAApi.messageFor(error), "error");
    } finally {
      if (testBtn) testBtn.disabled = false;
    }
  };

  // =========================================================================
  // Action Handlers (Edit, Test, Duplicate, Memory Wipe, Export)
  // =========================================================================

  // A press on one of a list row's acts.
  const handleSeqAction = async (btn) => {
    const { action, seqName, builtinName } = btn.dataset;
    const rowEl = btn.closest(".seq-item");
    switch (action) {
      case "edit":
        leaveSession(() => handleEditSequence(seqName, rowEl));
        break;
      case "test":
        await handleTestSequence(seqName, rowEl);
        break;
      case "more": {
        // The acts used less often, folded behind the row's More.
        const open = !moreOpen.has(seqName);
        if (open) moreOpen.add(seqName);
        else moreOpen.delete(seqName);
        btn.setAttribute("aria-expanded", String(open));
        rowEl?.querySelector(".seq-item-more")?.classList.toggle("hidden", !open);
        break;
      }
      case "duplicate":
        leaveSession(() => handleDuplicateSequence(seqName));
        break;
      case "memory-wipe":
        handleMemoryWipePrompt(seqName);
        break;
      case "export":
        await handleExportSequence(seqName);
        break;
      case "share":
        await handleShareToProject(seqName);
        break;
      case "tune":
        leaveSession(() => handleCloneBuiltin(builtinName));
        break;
      case "timeline":
        leaveSession(() => handleOpenTimeline(builtinName, rowEl));
        break;
    }
  };

  // Open one of the builder's own sequences in the workspace, on its timeline.
  // A read that fails says so on the row that was pressed.
  const handleEditSequence = async (seqName, rowEl = null) => {
    const sayOnRow = rowSayer(seqName, rowEl);
    let seq = null;
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      seq = result.data;
    } catch (error) {
      sayOnRow(`Could not read ${seqName}: ${PAApi.messageFor(error)}`);
      return;
    }
    if (!seq || !Array.isArray(seq.steps)) {
      sayOnRow(`The droid sent ${seqName} back with no steps.`);
      return;
    }
    currentEditingSeq = seq;
    editorState.isNew = false;
    renderEditorView(currentEditingSeq);
  };

  const handleTestSequence = async (seqName, rowEl) => {
    const feedbackEl = rowEl?.querySelector(".seq-item-feedback");
    if (feedbackEl) {
      feedbackEl.textContent = "Running...";
      feedbackEl.className = "seq-item-feedback feedback info";
      feedbackEl.classList.remove("hidden");
    }
    try {
      await PAApi.postJson("/api/seq/test", { name: seqName });
      if (feedbackEl) {
        feedbackEl.textContent = "Dispatched.";
        feedbackEl.className = "seq-item-feedback feedback success";
      }
    } catch (error) {
      if (feedbackEl) {
        feedbackEl.textContent = PAApi.messageFor(error);
        feedbackEl.className = "seq-item-feedback feedback error";
      }
      return;
    }
    await showRowRehearsal(seqName, rowEl);
  };

  // The badge beside a run from the list. The row holds no steps, so the
  // sequence is read back once the run is already on its way; a read that fails
  // says so on the row rather than leaving an empty space that looks like an
  // all-clear.
  const showRowRehearsal = async (seqName, rowEl) => {
    const badgeEl = rowEl?.querySelector(".seq-item-rehearsal");
    if (!badgeEl || !window.SeqRehearsal) return;
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      badgeEl.innerHTML = window.SeqRehearsal.badgeHtml(window.SeqRehearsal.rehearse(result.data, rehearsalContext()));
    } catch (error) {
      badgeEl.textContent = `Could not read ${seqName} back to rehearse it: ${PAApi.messageFor(error)}`;
    }
  };

  const handleDuplicateSequence = async (seqName) => {
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      const original = result.data;
      // Auto-rename to NAME_copy (avoid _copy_copy by removing existing suffix)
      const baseName = seqName.replace(/_copy(\d*)$/, "");
      original.name = `${baseName}_copy`;

      // Open editor with copy
      currentEditingSeq = original;
      editorState.isNew = true; // Duplicate is a new sequence
      renderEditorView(currentEditingSeq);
    } catch (error) {
      console.error("Error duplicating sequence:", error);
    }
  };

  const handleMemoryWipePrompt = (seqName) => {
    _pendingWipeSeqName = seqName;
    els.wipeSeqName.textContent = `Delete sequence: ${seqName}`;
    els.wipeConfirmInput.value = "";
    els.wipeConfirmInput.placeholder = seqName;
    els.wipeConfirmInput.disabled = false;
    els.wipeDanglingInfo.classList.add("hidden");
    els.modalWipeConfirm.disabled = true;
    els.modalWipeCancel.textContent = "Cancel";

    const updateWipeButton = () => {
      els.modalWipeConfirm.disabled = els.wipeConfirmInput.value !== _pendingWipeSeqName;
    };

    // Remove previous listener before adding to avoid accumulation on reopen
    if (_wipeInputListener) {
      els.wipeConfirmInput.removeEventListener("input", _wipeInputListener);
    }
    _wipeInputListener = updateWipeButton;
    els.wipeConfirmInput.addEventListener("input", updateWipeButton);

    showModal(els.modalWipe);
  };

  const handleMemoryWipeConfirm = async () => {
    const seqName = _pendingWipeSeqName;
    els.modalWipeConfirm.disabled = true;
    try {
      const result = await PAApi.request(`/api/seq?name=${encodeURIComponent(seqName)}`, {
        method: "DELETE",
      });

      const dangling = (result.data && result.data.danglingBindings) || [];
      if (dangling.length > 0) {
        // Keep modal open so the operator reads which bindings are now inert
        let html = "<p><strong>Deleted. These RC bindings are now inert:</strong></p><ul>";
        dangling.forEach((b) => {
          html += `<li>${window.PAUtils.escapeHtml(b.source)} CH${b.channel}</li>`;
        });
        html += "</ul>";
        els.wipeDanglingInfo.innerHTML = html;
        els.wipeDanglingInfo.classList.remove("hidden");
        els.wipeConfirmInput.disabled = true;
        els.modalWipeCancel.textContent = "Close";
      } else {
        hideModal(els.modalWipe);
      }
      refreshLearned();
    } catch (error) {
      els.modalWipeConfirm.disabled = false;
      alert("Error deleting sequence: " + PAApi.messageFor(error));
    }
  };

  const handleExportSequence = async (seqName) => {
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
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
  // Share to project (contribution funnel — ADR 0007)
  // =========================================================================

  const SEQ_REPO_SLUG = "mattiasbrandt/protoArtoo";

  // The editor is served over HTTP on the LAN, where navigator.clipboard is
  // often unavailable (secure-context only). Try the async API, then fall back
  // to a legacy textarea + execCommand so copy still works off a plain-HTTP device.
  const copyToClipboard = async (text) => {
    try {
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(text);
        return true;
      }
    } catch {
      /* fall through to the legacy path */
    }
    try {
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      const ok = document.execCommand("copy");
      document.body.removeChild(ta);
      return ok;
    } catch {
      return false;
    }
  };

  const handleShareToProject = async (seqName) => {
    // Open the pre-filled contribution issue synchronously, inside the click
    // gesture, so the popup is not blocked. The title needs only the name (which
    // we already have); the JSON travels via the clipboard because it can exceed
    // the URL length limit, and GitHub ignores ?body= when ?template= is set.
    const params = new URLSearchParams({
      template: "sequence-contribution.md",
      title: `Sequence: ${seqName}`,
      labels: "dome,feature request",
    });
    window.open(
      `https://github.com/${SEQ_REPO_SLUG}/issues/new?${params.toString()}`,
      "_blank",
      "noopener"
    );

    const showShareFeedback = (msg, level) => {
      const row = [...els.cardsContainer.querySelectorAll(".seq-item")].find(
        (item) => item.dataset.seqName === seqName
      );
      // showFeedback() rewrites className to "feedback <level>", dropping the
      // seq-item-feedback class — match either so repeat clicks still resolve it.
      const fb = row?.querySelector(".seq-item-feedback, .feedback");
      if (fb) {
        PAUtils.showFeedback(fb, msg, level);
        fb.classList.remove("hidden");
      }
    };

    let seqJson;
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      seqJson = result.data;
    } catch (error) {
      showShareFeedback(
        "Opened the GitHub issue, but could not load the sequence to copy: " +
          PAApi.messageFor(error),
        "error"
      );
      return;
    }

    const copied = await copyToClipboard(JSON.stringify(seqJson, null, 2));
    showShareFeedback(
      copied
        ? "Sequence copied. Paste it into the GitHub issue that opened."
        : "Could not copy. Use Export and attach the file to the GitHub issue that opened.",
      copied ? "success" : "warning"
    );
  };

  // =========================================================================
  // Import Modal
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
    // Restore sits beside the title, so it can be pressed with an edit open:
    // that edit is asked about before the restored sequence takes its place.
    leaveSession(() => {
      editorState.isNew = true;
      currentEditingSeq = parsed;
      renderEditorView(parsed);
    });
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
    els.btnImport.addEventListener("click", showImportModal);

    // Empty state buttons
    els.emptyImport.addEventListener("click", showImportModal);

    // Which sequences the list shows: all of them, the builder's own, or the
    // factory's.
    els.filter.querySelectorAll("button").forEach((button) => {
      button.addEventListener("click", () => {
        listShow = button.dataset.value;
        renderListView();
      });
    });

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

    // Discard dialog
    els.modalDiscardKeep.addEventListener("click", () => answerDiscard(false));
    els.modalDiscardConfirm.addEventListener("click", () => answerDiscard(true));

    // Modal overlays close on click
    document.querySelectorAll(".seq-modal-overlay").forEach((overlay) => {
      overlay.addEventListener("click", (e) => {
        if (e.target === overlay) {
          const modal = overlay.closest(".seq-modal");
          dismissModal(modal);
        }
      });
    });

    // Escape key closes modals
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") {
        [els.modalImport, els.modalWipe, els.modalDiscard].forEach((modal) => {
          if (modal && !modal.classList.contains("hidden")) {
            dismissModal(modal);
          }
        });
      }
    });

    // Undo and redo from the keyboard, anywhere in the editor. A text field
    // keeps the browser's own undo for what was typed in it.
    els.editorView.addEventListener("keydown", (e) => {
      if (!(e.ctrlKey || e.metaKey) || e.altKey) return;
      const key = String(e.key).toLowerCase();
      if (key !== "z" && key !== "y") return;
      if (e.target?.closest?.("input, textarea, select")) return;
      e.preventDefault();
      if (key === "y" || e.shiftKey) redo();
      else undo();
    });

    // A reload or a closed tab is leaving too, and the browser's own question
    // is the only one a page may ask there.
    window.addEventListener("beforeunload", (e) => {
      if (!sessionDirty()) return;
      e.preventDefault();
      e.returnValue = "";
    });
  };

  // =========================================================================
  // Initialize on Page Load
  // =========================================================================

  // -------------------------------------------------------------------------
  // Boot — load sequence list and factory sequences
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API loads as sections so the bootstrap
  // can show recovery state if any fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const SECTIONS = [
    [LEARNED_SECTION, loadLearned, "your sequences"],
    [FACTORY_SECTION, loadFactory, "the factory sequences"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadLearned().catch((error) => console.warn("[seq] your sequences unavailable:", error));
      loadFactory().catch((error) => console.warn("[seq] factory sequences unavailable:", error));
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/seq_protocol_check.js": "sequence protocol",
      "/servo_motion.js": "servo motion model",
      "/seq_rehearsal.js": "sequence rehearsal",
      "/seq_timeline.js": "sequence timeline",
      "/body_view.js": "droid picture",
      "/outputs.js": "servo outputs",
      "/seq.js": "sequence editor",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  const init = async () => {
    attachEventListeners();
    // The cap arrives with the droid's identity, on its own schedule relative
    // to the sequence list, and again if the shell replays it to a late mount.
    window.addEventListener("pa:identity-available", () => renderListView());
    window.addEventListener("pa:identity-unavailable", () => renderListView());
    // Paint the waiting list first; each section paints again on its answer.
    renderListView();
    startPageLoad();
    loadRehearsalFacts();
  };

  // Wait for shell and status_stream to be ready
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }

  // Leaving the Sequences surface with unsaved edits holds the unmount while
  // the builder is asked. Discard lets the navigation through; keep editing
  // puts the address back on this surface. Registered in the script body,
  // which is the one moment the shell guarantees is inside this surface's own
  // mount (data/page_bootstrap.js holdUnmount()).
  window.PASurface?.holdUnmount?.(() => {
    if (!sessionDirty()) return false;
    askDiscard(() => window.PASurface.releaseUnmount(), () => window.PASurface.stayOnSurface());
    return true;
  });

  // Expose for testing
  window.__seqEditorForTesting = {
    renderEditorView,
    editorState,
    updateValidationSummary,
    renderListWith: (seqs) => {
      sequences = seqs || [];
      learnedAnswered = true;
      renderListView();
    },
    renderListWithMocks: (seqs, mockBuiltins) => {
      sequences = seqs || [];
      builtins = mockBuiltins || [];
      learnedAnswered = true;
      factoryAnswered = true;
      renderListView();
    },
  };
})();

// =============================================================================
// data/seq.js
//
// The Sequences surface: the list of what is on the droid, and the workspace
// a sequence is edited in (#441, variant C) - a strip, the stage with the
// timeline and the droid, and one drawer under it. Also the dialogs (restore,
// memory wipe, discard) and a Factory sequence's read-only stage.
//
// Where things are. Look up the name; a line number is only a hint.
//   historyBegin / historyPush / historyCommit   undo stack
//   stageSteps / halfRoutine / showHalf          the half on the stage (Opens, Closes)
//   setGroup / startedCloseHalf                  the interrupt group and its close half
//   list, stage, drawer                          the three surfaces
//   pickedHtml                                   inspector rows
//   stepPreview / stepTypeDefaults               a step's words, and a new step's values
//   lightKind / lightFields                      light grammar
//   loadRehearsalFacts                           caches GET /api/servo/outputs
//   window.__seqEditorForTesting                 the test seam
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
  // Both have: only then is a sequence in neither list one that is not on
  // this droid, and a page with nothing to choose one with nothing saved.
  const listsAnswered = () => learnedAnswered && factoryAnswered;
  let currentEditingSeq = null; // The sequence being edited (or null)
  let timeline = null; // a Factory sequence's read-only timeline (data/seq_timeline.js), or null
  let timelineContext = () => rehearsalContext(); // what that timeline is told, by the half it shows
  let sessionTimeline = null; // the timeline on the workspace's stage, over the sequence being edited, or null
  let pickedBlocks = []; // the blocks picked on that timeline, as it last said them
  let pickedShown = null; // the inspector's markup as last written

  // Editor state tracking. One object for the life of the page: it is reset in
  // place, never replaced, so the test seam at the foot of this file holds the
  // object the editor reads.
  const editorState = {
    original: null,   // snapshot at open time (for Revert)
    current: null,    // live edited copy
    isNew: false,     // true for blank/clone/duplicate (unsaved)
    tuningFactory: null, // Factory sequence name when opened via Tune (e.g. "DM:VADER"), or null
    half: "opens",    // which half of it is on the stage: "opens" (steps) or "closes" (closeSteps)
    tab: "block",     // which drawer tab is forward: "block", "parts", "sequence" or "rehearsal"
    saved: false,     // whether this session has saved, for the strip's state word
  };

  // ---------------------------------------------------------------------------
  // Undo and redo (ADR 0057): every edit to the routine, one at a time, on
  // one stack.
  //
  // An entry is a copy of everything a builder authors - the steps, the close
  // half, the interrupt group, the tempo, which re-times every step on a
  // beat, and the takes it holds (#442) - so no edit needs an undo of its own
  // kind. It is bracketed two ways
  // (the pattern is r2d2-astromech-simulator's blockHistPush / blockHistCommit):
  //   historyPush()          BEFORE an edit made in one act that always
  //                          changes something: a drop, a removal, a split
  //   historyCommit(before)  AFTER an edit made over time - a drag, a fader -
  //                          or one that may turn out to change nothing,
  //                          given the copy from historyBegin(). An edit that
  //                          changed nothing records nothing, so a press that
  //                          only selects costs no entry.
  // historyPush() and historyBegin() read the routine as it is when they are
  // called, so each is called before its edit writes anything.
  // Revert is not on the stack: it discards the whole session, history and all.
  // ---------------------------------------------------------------------------
  const HISTORY_DEPTH = 100;
  const HISTORY_FIELDS = ["steps", "closeSteps", "toggleGroup", "tempo", "takes"];
  const history = { undo: [], redo: [] };

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

  const historyPush = () => historyRecord(historyCapture());

  // The copy an edit made over time hands back to historyCommit().
  const historyBegin = () => historyCapture();

  const historyReset = () => {
    history.undo = [];
    history.redo = [];
  };

  // ---------------------------------------------------------------------------
  // The half on the stage (#441, ADR 0062). A sequence in an interrupt group
  // has two halves - `steps` opens, `closeSteps` closes - and the stage shows
  // one at a time (editorState.half; the Opens / Closes switch in its bar).
  //
  // THIS IS THE ONE SEAM. Every tool of the timeline - a drop, a move, a
  // resize, the inspector, Split, removal - reads and writes the half shown
  // through these, so there is one copy of each tool and none of them knows
  // which half it is on:
  //   stageSteps()            the list of steps on the stage, the sequence's own
  //   setStageSteps(list)     another list in its place
  //   halfRoutine(seq, half)  that half as a routine of its own, which is what
  //                           the timeline draws: the opening half is the
  //                           sequence itself; the close half is its
  //                           closeSteps as `steps`, with no close half
  //   stageRun()              the stage's steps at the milliseconds they run at
  //   halfContext(seq, half)  what the timeline is told beside the routine: a
  //                           close half starts with what Opens left open
  //
  // What is NOT the stage's, and reads editorState.current whole:
  //   - Protocol Check's verdict, Save, the history and the unsaved-edits
  //     check, which are of the sequence, both halves;
  //   - an edit tried on a copy (triedOnCopy()), which copies the whole
  //     sequence and changes the half on the stage in it.
  //
  // `half` is the editor's and never the sequence's: nothing of it is saved,
  // so a toggle that is only looked at saves back as it was read.
  // ---------------------------------------------------------------------------
  const HALF_KEY = { opens: "steps", closes: "closeSteps" };
  const stageKey = () => HALF_KEY[editorState.half];
  const stageSteps = () => editorState.current[stageKey()];
  const setStageSteps = (list) => {
    editorState.current[stageKey()] = list;
  };
  // Closes is there to show when the sequence is in a group and has a list to
  // hold it. An empty list is still one to drop into.
  const hasCloseHalf = (seq) => Boolean(seq) && (seq.toggleGroup || "none") !== "none" && Array.isArray(seq.closeSteps);
  // The close half plays its steps only (operator, 2026-10-03,
  // include/take_replay.h), so it is handed over without the takes.
  const halfRoutine = (seq, half) => {
    if (half !== "closes") return seq;
    const { closeSteps, takes, ...rest } = seq;
    return { ...rest, steps: closeSteps };
  };
  const stageRun = () => SeqProtocolCheck.resolveBeats(halfRoutine(editorState.current, editorState.half)).steps;

  // How many characters a sequence's name holds after its DM:. Protocol
  // Check's name rule has the number (data/seq_protocol_check.js).
  const SEQ_NAME_CHARS = SeqProtocolCheck.NAME_CHARS_MAX;

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
  let lastRunBadge = null; // the copy the last run's badge rehearses, repainted when facts land
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
    phrase: phraseSteps,
    take: takeFacts,
  });

  // What the opening half leaves standing open is read off the timeline's own
  // lanes (SeqTimeline.leftOpen()), never worked out here.
  const halfContext = (seq, half) => ({
    ...rehearsalContext(),
    ...(half === "closes" && window.SeqTimeline ? { open: window.SeqTimeline.leftOpen(seq, rehearsalContext()) } : {}),
  });
  const stageContext = () => halfContext(editorState.current, editorState.half);

  // =========================================================================
  // The run (#441, #451)
  //
  // What is running is the Live Reading's run watch (data/live_reading.js,
  // "The run watch"), the one judge the Dashboard reads too: it starts a run,
  // stops it, and tells this page when a run - started here or anywhere else,
  // an RC Channel included - is under way and when it ends. The strip and the
  // list row both show it, and both are painted from it (paintRun() below).
  // =========================================================================
  // The ending last said of a run, so the run being heard again - after a
  // reconnect, say - can take it back rather than leave "Lost touch" under a
  // lamp that reads Running.
  let saidEnding = null; // { name, message }
  const runWatch = window.PALiveReading.watchRuns(({ name, running, outcome }) => {
    paintRun();
    if (running) {
      if (saidEnding?.name === name) unsayOfRun(saidEnding);
      return;
    }
    const ending = window.PALiveReading.runEnding(outcome, name);
    if (!ending) return;
    sayOfRun(name, ending);
    saidEnding = { name, message: ending };
  });

  let _pendingWipeSeqName = null; // sequence name pending deletion (avoids placeholder coupling)
  let _wipeInputListener = null;  // stored to enable removeEventListener on modal reopen

  // Audio fallback slots — the named clips usable as an audioCat "fallback"
  // (played when the chosen category has no available track). VALUES must match
  // the server slot table in src/seq_json.cpp (slotToString/slotFromString) and
  // the client validator set in seq_protocol_check.js. A slot that is a sound
  // action's track is named by that track's label in the one words table
  // (data/web_api.js), so this editor and the Sound page call it one thing;
  // "none" is no track.
  const trackSlot = (value) => ({ value, label: window.PAApi.labelOf(value) });
  const AUDIO_FALLBACK_SLOTS = [
    { value: "none", label: "None" },
    ...["scream", "faint", "leia", "cantina_s", "sw_theme", "imp_march", "cantina_l", "startup", "disco", "happy"]
      .map(trackSlot),
  ];
  const audioFallbackLabel = (value) =>
    (AUDIO_FALLBACK_SLOTS.find((s) => s.value === value) || {}).label || value || "None";

  // The dome's light vocabulary and the words for it are data/dome_lights.js's,
  // which Protocol Check validates against too: this page keeps no label of
  // its own. `lightWord` is a token as a builder reads it, in one of the
  // vocabulary's groups.
  const domeLights = window.DomeLights;
  const lightWord = (group, token) => domeLights.label(group, token) || "Unknown";

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
    importFileTrigger: document.getElementById("seq-import-file-trigger"),
    importTextarea: document.getElementById("seq-import-textarea"),
    importFeedback: document.getElementById("seq-import-feedback"),
    modalImportCancel: document.getElementById("seq-modal-import-cancel"),
    modalImportConfirm: document.getElementById("seq-modal-import-confirm"),

    // Editor view
    editorView: document.getElementById("seq-editor-view"),

    // Timeline view
    timelineView: document.getElementById("seq-timeline-view"),

    // Memory wipe modal
    modalWipe: document.getElementById("seq-modal-memory-wipe"),
    wipeTitle: document.getElementById("seq-modal-wipe-title"),
    wipeWarning: document.getElementById("seq-wipe-warning"),
    wipeFeedback: document.getElementById("seq-wipe-feedback"),
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

  // Each open dialog's Escape, through the one shared guard (data/overlay.js
  // escGuard()): bound when it opens and unbound when it closes, so one press
  // closes the dialog on top and nothing under it hears the key. Escape is an
  // answer like the overlay click (dismissModal()). A dialog of a surface that
  // was navigated away from is not on top of anything.
  //
  // The climb above stays this surface's own rather than the shared
  // holdSurface(): that one makes every sibling of the dialog inert, the other
  // dialogs included, so a second dialog opened over the first could not be
  // answered. This one leaves the dialogs out of what it holds.
  const dialogGuards = new Map();

  const showModal = (modal, focusOn = null) => {
    if (!modal) return;
    modal.classList.remove("hidden");
    surfaceBehind(modal).forEach((node) => {
      node.inert = true;
    });
    if (!dialogGuards.has(modal)) {
      dialogGuards.set(modal, window.PAOverlay.escGuard(
        () => !modal.classList.contains("hidden") && modal.isConnected,
        () => dismissModal(modal)
      ));
    }
    dialogGuards.get(modal).bind();
    // Focus where the builder starts, else the first control. Not a trap: Tab
    // leaves the dialog for the chrome, which is where STOP is.
    (focusOn || modal.querySelector("button, input, [tabindex]"))?.focus();
  };

  const hideModal = (modal) => {
    if (!modal) return;
    modal.classList.add("hidden");
    dialogGuards.get(modal)?.unbind();
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
    phrasesListed();
  };

  const STAND_DOWN_SECTION = "seq-stand-down";

  const loadStandDown = async ({ handle = null } = {}) => {
    const answer = await (handle || window.PAApi).get("/api/config");
    // Only an answer that carries the key is one: a droid whose firmware does
    // not know the Setting has not said "never chosen".
    const chosen = answer.data?.seq?.standDown;
    if (typeof chosen !== "string") return;
    standDownChoice = chosen;
    renderListView();
  };

  const loadFactory = async ({ handle = null } = {}) => {
    const answer = await (handle || window.PAApi).get("/api/seq/builtins");
    builtins = answer.data || [];
    factoryAnswered = true;
    renderListView();
    phrasesListed();
  };

  // A list answered while a sequence is open - after a save, which reads the
  // Learned list again: the lists are what the editor names a phrase from, so
  // the Sequences pills, the blocks' names and which phrases can be read are
  // all read again from them - and a phrase read from where its reference no
  // longer resolves is forgotten, so it is read again from where it does.
  const phrasesListed = () => {
    if (!editorState.current || !sessionTimeline) return;
    phrases.read.forEach((entry, ref) => {
      if (phraseSource(ref)?.url !== entry.url) phrases.read.delete(ref);
    });
    paintParts();
    updateValidationSummary();
    sessionTimeline.refresh(stageContext());
    loadPhrases();
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

  // The droid's Stand Down Sequence (GLOSSARY.md, #451): one name, chosen here
  // with the mark beside each row and run by the Dashboard's Rest act. Only
  // that act says Rest (operator, 2026-10-04); this page says Stand Down
  // (GLOSSARY.md "Stand Down Sequence"). The
  // Setting stores an empty name until one is chosen, and the words table says
  // what stands in for it (data/web_api.js, standDownSequence `unset`). Null
  // until GET /api/config has answered with it, and no row is marked or offers
  // the mark until then.
  let standDownChoice = null;
  const standDownName = () =>
    (standDownChoice === null ? null : standDownChoice || PAApi.unsetOf("standDownSequence"));

  // The mark, on a Learned and a Factory row alike: DM:RESET, the default, is
  // a Factory one, and a Factory row has no More to hold it. The chosen row
  // says so in its name cell; every other row offers to be chosen.
  const standDownBadge = (name) =>
    (name === standDownName() ? '<span class="seq-badge">Stand Down</span>' : "");
  const standDownAct = (name) =>
    (standDownName() === null || name === standDownName() ? ""
      : `<button type="button" class="seq-act icon-act" data-action="stand-down" data-seq-name="${window.PAUtils.escapeAttr(name)}">${window.PAUi.actFace("pin-outline", "Use as Stand Down")}</button>`);

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
    // The table was just written as if nothing were running.
    paintRun();
  };

  // ---------------------------------------------------------------------------
  // The run, on the surface: the row of the sequence that is running and the
  // workspace's strip say the same run, from the one watch. A row is written
  // with both its Test and its Stop and the strip with both its halves, and
  // this shows the half that is true - in place, so what a row has said under
  // itself (a refusal, its Rehearsal) is not wiped by a run starting or ending.
  // ---------------------------------------------------------------------------
  const rowOf = (name) =>
    [...els.cardsContainer.querySelectorAll(".seq-item")].find((item) => item.dataset.seqName === name) || null;

  const paintStripRun = () => {
    const name = runWatch.running();
    const show = (id, on) => document.getElementById(id)?.classList.toggle("hidden", !on);
    show("seq-editor-test", name === null);
    // A run uses the copy the droid holds, which is said only while there are
    // edits it does not hold and a run to offer.
    show("seq-editor-test-hint", name === null && sessionDirty());
    show("seq-editor-running", name !== null);
    show("seq-editor-stop", name !== null);
    if (name === null) return;
    const saysEl = document.getElementById("seq-editor-running-name");
    if (saysEl) saysEl.textContent = `Running ${name}`;
    const stopBtn = document.getElementById("seq-editor-stop");
    if (stopBtn) window.PAUi.setAct(stopBtn, `Stop ${name}`);
  };

  const paintRun = () => {
    const name = runWatch.running();
    els.cardsContainer.querySelectorAll(".seq-item").forEach((item) => {
      const running = name !== null && item.dataset.seqName === name;
      item.classList.toggle("is-running", running);
      item.querySelector(".seq-row-run")?.classList.toggle("hidden", !running);
      item.querySelector('[data-action="test"]')?.classList.toggle("hidden", running);
      item.querySelector('[data-action="stop"]')?.classList.toggle("hidden", !running);
    });
    paintStripRun();
  };

  // What the surface says of a run that is not its lamp: on the strip while
  // the workspace is open, and on the sequence's row.
  const sayOfRun = (name, message) => {
    if (editorState.current !== null) showEditorFeedback(message, "error");
    const feedbackEl = rowOf(name)?.querySelector(".seq-item-feedback");
    if (!feedbackEl) return;
    feedbackEl.textContent = message;
    feedbackEl.className = "seq-item-feedback feedback error";
  };

  // Take back an ending sayOfRun() wrote, where it is still what is said: a
  // line rewritten since says something else and is left alone.
  const unsayOfRun = ({ name, message }) => {
    saidEnding = null;
    const editorEl = document.getElementById("seq-editor-feedback");
    if (editorEl?.textContent.includes(message)) showEditorFeedback("");
    const feedbackEl = rowOf(name)?.querySelector(".seq-item-feedback");
    if (feedbackEl?.textContent !== message) return;
    feedbackEl.textContent = "";
    feedbackEl.className = "seq-item-feedback feedback hidden";
  };

  // A press on a run's Stop, on the strip or on its row.
  const handleStopRun = async (btn) => {
    const name = runWatch.running();
    btn.disabled = true;
    try {
      await runWatch.stop();
    } catch (error) {
      if (name !== null) sayOfRun(name, "Stop failed: " + PAApi.messageFor(error));
    } finally {
      btn.disabled = false;
    }
  };

  // When a sequence was last saved, as the list words it: "saved 30 Sep 19:42".
  // Nothing unless the droid gave a time. It gives none today: the `modified`
  // GET /api/seq/list sends is a yes or no - the stored file's meta.modified
  // (src/web/api_seq.cpp) - and a yes read as a time would be 1 Jan 1970.
  const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
  const savedWords = (modified) => {
    const at = typeof modified === "string" || typeof modified === "number" ? new Date(modified) : null;
    if (!at || Number.isNaN(at.getTime())) return "";
    const two = (n) => String(n).padStart(2, "0");
    return `saved ${at.getDate()} ${MONTHS[at.getMonth()]} ${two(at.getHours())}:${two(at.getMinutes())}`;
  };

  // What a list row says about its sequence in What it does, Steps and Runs:
  // only what the droid reported, in the same keys from GET /api/seq/list and
  // GET /api/seq/builtins (src/web/api_seq.cpp). A cell the droid said nothing
  // for stays empty rather than read 0.
  const reportedSteps = (entry) => (Number.isInteger(entry.stepCount) ? entry.stepCount : "");

  // How long a run is: "6 s", "1.5 s". lengthWords() is the same formatter as
  // seconds() in data/seq_rehearsal.js, which words the Rehearsal's "runs 6 s":
  // the two change together. The droid sends 0 for a stored
  // sequence it could not find the end of, which is only ever an invalid one,
  // so that row's Runs stays empty.
  const lengthWords = (ms) => `${Number((ms / 1000).toFixed(2))} s`;
  const reportedLength = (entry) =>
    (!Number.isInteger(entry.lengthMs) || (entry.lengthMs === 0 && entry.valid === false) ? "" : lengthWords(entry.lengthMs));

  // The droid's list keeps the start of a Learned sequence's purpose and says
  // when there was more (`purposeCut`); the whole of it is in the editor's
  // Sequence tab.
  const purposeHtml = (entry) =>
    (entry.purpose
      ? `<span class="seq-purpose">${window.PAUtils.escapeHtml(entry.purpose)}${entry.purposeCut ? "..." : ""}</span> `
      : "");

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
      ? `<button type="button" class="seq-act icon-act" data-action="share" data-seq-name="${name}">${window.PAUi.actFace("share-variant-outline", "Share to project")}</button>`
      : "";

    const testBtnDisabled = seq.valid === false ? "disabled" : `data-seq-name="${name}"`;
    const more = moreOpen.has(seq.name);

    return `
      <tbody class="seq-item" data-seq-name="${name}">
        <tr>
          <th scope="row"><span class="seq-name">${window.PAUtils.escapeHtml(seq.name)}</span>${badges.join("")}${standDownBadge(seq.name)}</th>
          <td>${purposeHtml(seq)}${saved ? `<span class="seq-meta">${saved}</span>` : ""}
            <span class="seq-row-run hidden" role="status"><span class="indicator ok seq-live" aria-hidden="true"></span>Running</span></td>
          <td class="seq-count-cell">${reportedSteps(seq)}</td>
          <td class="seq-count-cell">${reportedLength(seq)}</td>
          <td class="seq-item-acts">
            <span class="seq-acts">
              <button type="button" class="seq-act is-strong icon-act" data-action="edit" data-seq-name="${name}">${window.PAUi.actFace("pencil-outline", "Edit")}</button>
              <button type="button" class="seq-act icon-act act-keeps-words" data-action="test" ${testBtnDisabled}>${window.PAUi.actFace("play", "Test")}</button>
              <button type="button" class="btn btn-sm seq-stop icon-act act-keeps-words hidden" data-action="stop" data-seq-name="${name}">${window.PAUi.actFace("stop", `Stop ${seq.name}`)}</button>
              ${standDownAct(seq.name)}
              <button type="button" class="seq-act seq-disclose" data-action="more" data-seq-name="${name}" aria-expanded="${more}">${chevron}More</button>
              <span class="seq-item-more${more ? "" : " hidden"}">
                <button type="button" class="seq-act icon-act" data-action="duplicate" data-seq-name="${name}">${window.PAUi.actFace("content-copy", "Duplicate")}</button>
                <button type="button" class="seq-act icon-act" data-action="export" data-seq-name="${name}">${window.PAUi.actFace("download-outline", "Export")}</button>
                ${shareBtn}
                <button type="button" class="seq-act seq-act-danger icon-act" data-action="memory-wipe" data-seq-name="${name}">${window.PAUi.actFace("delete-outline", "Memory Wipe")}</button>
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
          <th scope="row"><span class="seq-name">${window.PAUtils.escapeHtml(builtin.name)}</span>${standDownBadge(builtin.name)}</th>
          <td>${purposeHtml(builtin)}${group}</td>
          <td class="seq-count-cell">${reportedSteps(builtin)}</td>
          <td class="seq-count-cell">${reportedLength(builtin)}</td>
          <td class="seq-item-acts">
            <span class="seq-acts">
              <button type="button" class="seq-act is-strong icon-act" data-action="tune" data-builtin-name="${name}">${window.PAUi.actFace("pencil-outline", "Tune")}</button>
              <button type="button" class="seq-act icon-act" data-action="timeline" data-builtin-name="${name}">${window.PAUi.actFace("timeline-outline", "Timeline")}</button>
              ${standDownAct(builtin.name)}
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
  // The builder's own opens in the workspace, where the timeline is the one
  // editor: an edit there is made to editorState.current, the sequence Save
  // sends.
  //
  // Neither moves the droid except on the pose press, and that poses what the
  // droid has stored under the name - never the edits on screen.
  // =========================================================================

  // The stage's markup, for both. `ids` names the three places the timeline
  // draws into, for the workspace, which finds them by id; `above` and
  // `views` are what the workspace adds round the routine.
  const stageHtml = ({ ids = false, above = "", views = "" } = {}) => `
      <div class="seq-stage">
        <div class="seq-stage-main">
          ${above}
          <div class="seq-stagebar">
            ${views}
            <div class="tl-bar"${ids ? ' id="seq-editor-tlbar"' : ""}></div>
          </div>
          <div class="seq-lanes"${ids ? ' id="seq-editor-timeline"' : ""}></div>
        </div>
        <aside class="seq-stage-side"${ids ? ' id="seq-editor-droid"' : ""} aria-label="The droid at the marker"></aside>
      </div>`;

  // The Opens / Closes switch in the stage's bar: which half of a sequence in
  // an interrupt group the timeline shows. The words are the operator's.
  const halfSwitchHtml = (attrs = "") =>
    `<span class="seg seg-sm seq-half"${attrs} role="group" aria-label="Which half is shown">` +
    `<button type="button" data-half="opens" aria-pressed="true">Opens</button>` +
    `<button type="button" data-half="closes" aria-pressed="false">Closes</button></span>`;
  const paintHalfSwitch = (host, half, offered) => {
    host?.classList.toggle("hidden", !offered);
    host?.querySelectorAll("[data-half]").forEach((button) =>
      button.setAttribute("aria-pressed", String(button.dataset.half === half)));
  };
  // The pose is the opening half's: the droid takes a name and an instant,
  // and reads the instant off that sequence's steps (POST /api/seq/pose).
  const POSE_OPENS_ONLY = { text: "Pose works on Opens only.", level: "error" };

  // What opens in place of the list starts at its strip, however far down
  // the list the row that opened it was.
  const showFromTheTop = () => window.scrollTo?.(0, 0);

  // The way back to the list, as the strip's first act.
  const backHtml = (attrs) =>
    `<button type="button" class="seq-act seq-back icon-act" ${attrs}>${window.PAUi.actFace("arrow-u-left-top", "All sequences")}</button>`;

  const closeTimeline = () => {
    if (timeline) {
      timeline.destroy();
      timeline = null;
    }
    timelineContext = () => rehearsalContext();
    els.timelineView.innerHTML = "";
    els.timelineView.classList.add("hidden");
  };

  // The one request the pose press sends: the sequence's name and the instant.
  // What the droid does at that instant, and how far apart, is the firmware's
  // to work out from what it stores (include/sequence_pose.h); a latched estop
  // or Sleep Mode refuses it there, in words this shows. The droid poses
  // from the stored steps only (sequencePosePlan()), never a take (#442), so
  // where the routine holds takes - which the picture here does show - the
  // answer says they are not in it (`aside`).
  const poseOnDroid = (name, atMs, which = "", aside = "") =>
    PAApi.postJson("/api/seq/pose", { name, t: atMs })
      .then(() => ({ text: `Moving the droid to ${(atMs / 1000).toFixed(2)} s${which}, one part at a time.${aside}`, level: "ok" }))
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
          <button type="button" class="seq-act is-strong icon-act" data-stage-act="tune">${window.PAUi.actFace("pencil-outline", "Tune")}</button>
        </div>
        ${stageHtml({ views: halfSwitchHtml() })}
      </div>`;
    els.timelineView.classList.remove("hidden");
    const within = (selector) => els.timelineView.querySelector(selector);
    // A Factory toggle's close half is read here as its opening half is: the
    // same switch, over the sequence as the droid sent it.
    let half = "opens";
    timelineContext = () => halfContext(seq, half);
    paintHalfSwitch(within(".seq-half"), half, hasCloseHalf(seq));
    within(".seq-half").addEventListener("click", (event) => {
      const pressed = event.target?.closest?.("[data-half]")?.dataset.half;
      if (!pressed || pressed === half) return;
      half = pressed;
      paintHalfSwitch(within(".seq-half"), half, true);
      timeline.say(null);
      timeline.refresh(timelineContext());
    });
    within(".seq-strip").addEventListener("click", (event) => {
      const act = event.target?.closest?.("[data-stage-act]")?.dataset.stageAct;
      if (!act) return;
      closeTimeline();
      renderListView();
      if (act === "tune") handleCloneBuiltin(builtinName);
    });
    timeline = window.SeqTimeline.mount(
      { bar: within(".tl-bar"), lanes: within(".seq-lanes"), side: within(".seq-stage-side") },
      () => halfRoutine(seq, half),
      {
        context: timelineContext(),
        describe: stepPreview,
        onPose: (atMs) => (half === "closes" ? Promise.resolve(POSE_OPENS_ONLY) : poseOnDroid(builtinName, atMs)),
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
  // dragged past its neighbour changes places with it in the routine. The
  // end step, and anything written after it, stays where it is.
  //
  // stepOrder() is that order for any list of steps, as the index each place
  // takes its step from, so a change tried on a copy is put in the order the
  // routine itself would be.
  const stepOrder = (steps) => {
    const units = stepUnits(steps);
    const endAt = steps.findIndex((step) => step && step.type === "end");
    const tail = endAt === -1 ? units.length : units.findIndex((unit) => unit.at + unit.size > endAt);
    return [...units.slice(0, tail).sort((a, b) => a.t - b.t), ...units.slice(tail)]
      .flatMap((unit) => Array.from({ length: unit.size }, (_, k) => unit.at + k));
  };
  const orderSteps = () => {
    const steps = stageSteps();
    const order = stepOrder(steps);
    if (order.every((from, to) => from === to)) return;
    setStageSteps(order.map((from) => steps[from]));
  };

  // Remove steps by their place in the half on the stage: the one removal. A
  // loop is one object: removing it takes the steps it repeats,
  // removing one of those shortens it, and a loop left repeating nothing goes
  // too - otherwise it would reach for the step after it.
  //
  // It is one entry on the history. `within` says the removal is part of an
  // edit that is already one (historyBegin()), which then records it and
  // draws the routine again itself.
  const removeSteps = (indices, within = false) => {
    const steps = stageSteps();
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
    if (!within) historyPush();
    shorter.forEach(([loop, left]) => {
      loop.body = left;
    });
    setStageSteps(steps.filter((_, index) => !gone.has(index)));
    if (within) return;
    edited();
  };

  // The picked blocks taken out (the timeline's Delete and Remove): steps by
  // their place in the half on the stage, and takes - entries of the
  // sequence's `takes`, whose files a save then deletes - as one entry on the
  // history.
  const removeBlocks = (indices, takes = []) => {
    if (takes.length === 0) {
      removeSteps(indices);
      return;
    }
    historyPush();
    if (indices.length > 0) removeSteps(indices, true);
    const held = (editorState.current.takes || []).filter((each) => !takes.includes(each));
    if (held.length > 0) editorState.current.takes = held;
    else delete editorState.current.takes;
    edited();
  };

  const closeSessionTimeline = () => {
    if (sessionTimeline) {
      sessionTimeline.destroy();
      sessionTimeline = null;
    }
  };

  // Put the timeline of the routine being edited on the workspace's stage. It
  // is handed a way to read the half of editorState.current that is on the
  // stage (halfRoutine()) - the sequence's own step objects, never a copy - and the
  // three things an edit there needs from the editor: the history's two
  // brackets, and removal, which changes which steps there are.
  const mountSessionTimeline = () => {
    closeSessionTimeline();
    if (!window.SeqTimeline) return;
    const hosts = {
      bar: document.getElementById("seq-editor-tlbar"),
      lanes: document.getElementById("seq-editor-timeline"),
      side: document.getElementById("seq-editor-droid"),
    };
    sessionTimeline = window.SeqTimeline.mount(hosts, () => halfRoutine(editorState.current, editorState.half), {
      context: stageContext(),
      describe: stepPreview,
      onPose: (atMs) => {
        if (editorState.half === "closes") return Promise.resolve(POSE_OPENS_ONLY);
        // The droid poses what it stores, so a sequence never saved has
        // nothing to pose, and unsaved edits are not in the pose.
        const name = editorState.tuningFactory || (editorState.isNew ? null : editorState.original?.name);
        if (!name) return Promise.resolve({ text: "Save it first, so the droid has the routine to move to.", level: "error" });
        const takes = (editorState.current?.takes || []).length > 0 ? " Takes are not in it." : "";
        return poseOnDroid(name, atMs, sessionDirty() ? " of the routine as last saved" : "", takes);
      },
      edit: {
        begin: historyBegin,
        commit: (before) => {
          orderSteps();
          historyCommit(before);
          edited();
        },
        remove: removeBlocks,
      },
      onPicked: showPicked,
    });
  };

  // The switch as the sequence now is: offered when there is a close half.
  const paintHalf = () =>
    paintHalfSwitch(document.getElementById("seq-editor-half"), editorState.half, hasCloseHalf(editorState.current));

  // Put a half on the stage. Closes only where there is a close half. A block
  // held at that moment is let go where the press found it; what was picked
  // is the other half's and is picked no longer (the timeline drops a picked
  // step that is not in the list it now reads); and the word the stage last
  // said was about the other half.
  const showHalf = (half) => {
    const shown = half === "closes" && hasCloseHalf(editorState.current) ? "closes" : "opens";
    if (shown !== editorState.half) {
      sessionTimeline?.cancel();
      editorState.half = shown;
      sayOnStage("");
      sessionTimeline?.refresh(stageContext());
      updateValidationSummary();
    }
    paintHalf();
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

  // What kind of step this is, by name.
  const stepKindName = (step) =>
    (step.type === "dome" ? domeStepName(step) : stepTypeName[step.type] || step.type || "Step");

  // ---------------------------------------------------------------------------
  // The Picked block tab (#441): what the timeline says is picked, and for one
  // block the inspector - where it starts, then the rows of its kind. Every
  // row is a setting row, and every bound and choice in one is the kind
  // table's (STEP_LIMITS and its neighbours). Several blocks can be moved
  // together and removed.
  //
  // RUNS FOR, OPENS TO and MOTION are the decided words for a Part standing
  // open: how long from its open to its close, how far (stored as howFar,
  // absent meaning the whole throw) and the Move Shape. None of them is a
  // speed, an acceleration or an easing: those are the Output's (ADR 0052).
  // On a body light how far is said as BRIGHTNESS (operator, 2026-10-02).
  //
  // The dome's four light commands have rows of their own (lightRows()): each
  // is one `cmd`, read into its fields and written back whole, and only by an
  // edit that changes it. Any other dome command that is not a panel move
  // has one row, SENDS: the command as it is stored, in a box to type in.
  //
  // A dome panel pair and a body pair are read by the one path (moveOf(),
  // moveRows()). They differ in where the Move Shape is stored - a panel
  // spells it in its `cmd`, a Body Step has a `shape` field - and in what a
  // flutter says: a body flutter has a length and may say how far, a panel's
  // has neither (`settles`). How far is the step's `howFar` on both.
  //
  // A Gesture is edited as the one thing it is (gestureRows()): its Parts, the
  // Move Shape, how it travels across them, and - folded away - its pace, its
  // repeat and the feel only a Gesture overrides. Its words and its bounds
  // are the one vocabulary's (data/seq_gesture.js).
  //
  // A sequence inside this one has one row, which sequence it is, and one
  // act: Split into steps, which writes the phrase's steps out in its place
  // (splitPhrase()). Its length is the phrase's own, so there is no Runs for.
  //
  // A Background Track (ADR 0054) has its sound, its volume and how it ends:
  // with the sequence, at a stop step - then it is one block with the stop,
  // and has a Runs for - or playing on after the sequence ends.
  // ---------------------------------------------------------------------------
  const BRICK_SENTENCE = "These settings belong to this brick. The same part dropped somewhere else keeps its own.";

  const settingRow = (name, control, value = "") =>
    `<div class="setting-row"><span class="setting-name">${name}</span>${control}<span class="setting-value">${value}</span></div>`;
  // `optional` is a number that may be left empty, where empty says something
  // of its own: no duration. Given as words, they are what the empty field
  // shows - what the step does when it says no number. Any other number left
  // empty goes back to the one the routine holds.
  const numberCell = (field, value, bounds, label, unit = "ms", optional = false) =>
    `<span class="setting-number"><input class="number-cell" type="number" ${limits(bounds)} step="${unit === "ms" ? 10 : 1}" value="${value}" data-picked="${field}"${optional ? ` data-optional placeholder="${optional === true ? "-" : optional}"` : ""} aria-label="${label}">${unit ? `<span class="setting-unit">${unit}</span>` : ""}</span>`;
  // Up to five peers are a joined bar, more are pills that wrap. An option is
  // [value, words] and, for a bar, whether it cannot be pressed now.
  const segOf = (field, options, current, label) =>
    `<span class="seg seg-sm" role="group" aria-label="${label}">${options
      .map(([value, words, off]) => `<button type="button" data-picked="${field}" data-value="${value}" aria-pressed="${value === current}"${off ? " disabled" : ""}>${words}</button>`)
      .join("")}</span>`;
  const pillsOf = (field, options, current, label) =>
    `<span class="seq-pills" role="radiogroup" aria-label="${label}">${options
      .map(([value, words]) => `<button type="button" class="seq-pill" role="radio" data-picked="${field}" data-value="${value}" aria-checked="${value === current}">${words}</button>`)
      .join("")}</span>`;
  // A length that can be kept in beats (spansBeats()), in a routine with a
  // tempo: the milliseconds, and beside them the beats, empty where the
  // length is in milliseconds alone. Without a tempo, the milliseconds.
  const lengthCells = (step, msCell) => (tempoOf() && spansBeats(step)
    ? `<span class="seq-row-ctl">${msCell}${numberCell("spanBeats", step.spanBeats ?? "", SeqProtocolCheck.SPAN_BEATS, "Runs for, in beats", "beats", true)}</span>`
    : msCell);
  // `unit` follows the number beside the fader as it moves (faderMoved()).
  const faderOf = (field, value, bounds, label, unit = "%") =>
    `<input class="fader" type="range" ${limits(bounds)} step="1" value="${value}" data-picked="${field}" data-unit="${unit}" aria-label="${label}">`;
  const capital = (word) => word[0].toUpperCase() + word.slice(1);

  // A color is picked as the color itself, in Lights' own swatch (the
  // .light-colors / .light-color it draws, data/lights.js swatches()): a dot
  // takes its color from the stylesheet by the token's own name, so no color
  // is written from here, and the picked one is named once, beside them.
  // DEFAULT is the dome's word for the color it already uses and RANDOM, a
  // holo's alone, for one it picks itself: neither dot carries a color.
  const UNCOLORED = { DEFAULT: " light-color-default", RANDOM: " seq-color-random" };
  const swatchesOf = (field, group, tokens, current, label) =>
    `<span class="light-colors" role="radiogroup" aria-label="${label}">${tokens
      .map((token) =>
        `<button type="button" class="light-color${UNCOLORED[token] || ""}" role="radio" data-picked="${field}" data-value="${token}"${UNCOLORED[token] ? "" : ` data-color="${token.toLowerCase()}"`} aria-checked="${token === current}" aria-label="${window.PAUtils.escapeHtml(lightWord(group, token))}"></button>`)
      .join("")}</span>`;

  // The rows of a dome light command, from its fields (lightFields()). Every
  // choice and its word is the dome's vocabulary (domeLights), and every
  // bound the kind table's.
  const lightRows = (fields) => {
    const esc = window.PAUtils.escapeHtml;
    const words = (group, tokens) => tokens.map((token) => [token, esc(lightWord(group, token))]);
    const color = (group, tokens) =>
      settingRow("Color", `<span class="seq-row-ctl">${swatchesOf("color", group, tokens, fields.color, "Color")}<span class="seq-unit">${esc(lightWord(group, fields.color))}</span></span>`);
    // A logic display shows a mode or a text, and this turns the step from
    // the one command into the other. A PSI has no text to show.
    const shows = domeLights.textTargets.includes(fields.target)
      ? settingRow("Shows", segOf("shows", [["DL", "Mode"], ["DT", "Text"]], fields.kind, "What the display shows"))
      : "";
    switch (fields.kind) {
      case "DV":
        return settingRow("Preset", pillsOf("preset", words("presets", domeLights.presets), fields.preset, "Visual preset"));
      case "DL":
        return shows
          + settingRow("Lights", pillsOf("target", words("targets", domeLights.targets), fields.target, "Which lights"))
          + settingRow("Mode", pillsOf("mode", words("modes", domeLights.modes), fields.mode, "Light mode"))
          + color("colors", domeLights.colors)
          + settingRow("Runs for", `<span class="seq-row-ctl">${numberCell("seconds", fields.seconds, STEP_LIMITS.lightCount, "Runs for, in seconds", "s", true)}<span class="hint">empty: until the next mode</span></span>`);
      case "DT":
        // The line break written straight after <textarea> is not part of
        // the text: the HTML parser drops one there, so without it a text
        // that starts with a line break would lose it in the box, and the
        // next edit would store it without.
        return shows
          + settingRow("Lights", segOf("target", words("textTargets", domeLights.textTargets), fields.target, "Which displays"))
          + settingRow("Text", `<textarea class="number-cell text-cell seq-light-text" rows="2" maxlength="${LIGHT_TEXT_CHARS}" data-picked="text" aria-label="Text, at most ${LIGHT_TEXT_CHARS} characters and one line break">\n${esc(fields.text)}</textarea>`)
          + color("textColors", domeLights.textColors)
          + settingRow("Runs for", numberCell("seconds", fields.seconds, STEP_LIMITS.lightCount, "Runs for, in seconds", "s"))
          + settingRow("Scroll", numberCell("speed", fields.speed, STEP_LIMITS.scroll, "Scroll speed", ""));
      default: {
        // A holo effect takes only the colors its rule allows - the row is
        // there when that is more than DEFAULT - and a number only where the
        // rule says so: seconds for a flash, how many times for a wag or nod.
        //
        // A stored command can hold what its rule refuses: a color the effect
        // does not take (DH:A:RAINBOW:RED), a number on an effect that takes
        // none (DH:A:ON:BLUE:5). Protocol Check refuses the step, so the row
        // is drawn all the same and it can be put right: the color row with
        // the colors the effect does take, none of them picked, and the
        // number to be emptied.
        const rule = domeLights.holoRules[fields.effect];
        const colors = rule ? rule.colors : domeLights.holoColors;
        const times = rule?.counts === "times";
        const seconds = rule?.counts === "seconds";
        return settingRow("Holo", segOf("target", words("holoSides", domeLights.holoTargets), fields.target, "Which holo"))
          + settingRow("Effect", pillsOf("effect", words("holoEffects", domeLights.holoEffects), fields.effect, "Holo effect"))
          + (colors.length > 1 || !colors.includes(fields.color) ? color("holoColors", colors) : "")
          + (rule?.duration === "range" || fields.count !== ""
            ? settingRow(times ? "Times" : "Runs for",
              numberCell("count", fields.count, STEP_LIMITS.lightCount, times ? "How many times" : seconds ? "Runs for, in seconds" : "Runs for", seconds ? "s" : "", true))
            : "");
      }
    }
  };

  // One field of a light command, changed. Answers the command it becomes;
  // undefined when it stays as it is - the field already says that, or the
  // value is none it can hold - and null when the edit is refused (a text
  // that cannot be encoded, lightCmd()).
  //
  // A command is rewritten only by an edit that changes it: one that spells
  // out a default (DL:FLD:NORMAL:DEFAULT) saves back byte for byte as it was
  // read until someone changes a field of it.
  const lightEdited = (fields, field, raw) => {
    if (field === "shows") {
      // The other command's starting fields, on the display this one names.
      if (raw === fields.kind || !DOME_SUBMODES[raw]) return undefined;
      return lightCmd({ ...lightFields(DOME_SUBMODES[raw].starts), target: fields.target });
    }
    if (!(field in fields) || field === "kind") return undefined;
    const next = { ...fields };
    if (["seconds", "count", "speed"].includes(field)) {
      const number = parseInt(raw, 10);
      // Only a mode's duration and a holo's number may be left off; a logic
      // text carries every field.
      if (raw === "" && fields.kind !== "DT") next[field] = "";
      else if (Number.isInteger(number)) next[field] = String(number);
      else return undefined;
    } else {
      next[field] = raw;
    }
    if (next[field] === fields[field]) return undefined;
    // Only a new text is encoded. An edit to any other field writes the
    // stored text back as it was stored (lightFields()' `encoded`).
    if (field === "text") delete next.encoded;
    if (field === "effect") {
      // The new effect's rule decides what the command may keep: a color it
      // does not take goes back to DEFAULT, and its number goes unless the
      // new effect takes one that counts the same thing - three wags are not
      // three seconds of flash, and an effect that takes no number is refused
      // with one.
      const rule = domeLights.holoRules[raw];
      if (rule && !rule.colors.includes(next.color)) next.color = "DEFAULT";
      if (rule && rule.counts !== domeLights.holoRules[fields.effect]?.counts) next.count = "";
    }
    return lightCmd(next);
  };

  // A panel command's halves: [":OP07", "OP", "07"], or null.
  const panelIntent = (step) => (step.type === "dome" ? /^:(OP|CL|OF)(.+)$/.exec(step.cmd || "") : null);

  // The Move Shape as a builder reads it, by Part Kind: one stored token, said
  // as open / close / flutter on a servo Part and on / off / flash on a light
  // (GLOSSARY.md "Move Shape").
  const SHAPE_WORDS = {
    servo: { open: "Open", close: "Close", flutter: "Flutter" },
    light: { open: "On", close: "Off", flutter: "Flash" },
  };
  const PANEL_SHAPES = { OP: "open", CL: "close", OF: "flutter" };
  const catalogPart = (id) => (window.DroidParts?.parts || []).find((part) => part.id === id) || null;

  // What a Turn Dome To step can turn to, in the picker's order: the dome's
  // own front, then every dome Part Protocol Check accepts, in catalog order,
  // by its short name where it has one.
  const domeBearingTargets = () => [
    { id: SeqProtocolCheck.DOME_BEARING_FRONT, words: "Dome front" },
    ...(window.DroidParts?.parts || []).filter(SeqProtocolCheck.isDomeBearingPart)
      .map((part) => ({ id: part.id, words: part.shorthand || part.name })),
  ];

  // A step that opens, closes or flutters a Part, as its Move Shape and the
  // words for it: a dome panel command or a Body Step. Null for any other.
  // `settles` is whether the step itself says how far and for how long: a
  // dome panel's flutter does neither (Protocol Check), a body flutter both.
  const moveOf = (step) => {
    const intent = panelIntent(step);
    if (intent) {
      const shape = PANEL_SHAPES[intent[1]];
      return { shape, words: SHAPE_WORDS.servo, body: false, settles: shape === "open" };
    }
    if (step.type !== "body") return null;
    const shape = step.shape || "open";
    const light = Boolean(window.DroidPartKind?.isLight(catalogPart(step.part)));
    return { shape, words: light ? SHAPE_WORDS.light : SHAPE_WORDS.servo, body: true, light, settles: shape !== "close" };
  };

  // RUNS FOR, OPENS TO (BRIGHTNESS on a body light) and MOTION, for a dome
  // panel and a body Part alike.
  // A close is one choice. An open may be a flutter instead, pair or not: a
  // flutter ends closed and owes nothing after it (ADR 0049, amended
  // 2026-10-02). RUNS FOR is the block's length: a pair's span from its open
  // to its close, a body flutter's own length.
  const moveRows = (step, at, move) => {
    const { words } = move;
    if (move.shape === "close") return settingRow("Motion", segOf("motion", [["close", words.close]], "close", "Motion"));
    const pair = sessionTimeline?.standing(at) || null;
    const motion = settingRow("Motion", segOf("motion", [["open", words.open], ["flutter", words.flutter]], move.shape, "Motion"));
    if (!move.settles) return motion;
    const far = step.howFar ?? STEP_LIMITS.howFar[1];
    // How far is the one stored `howFar`, said by Part Kind as the Move Shape
    // is: a servo opens to it, a body light is that bright.
    const howFar = move.light
      ? settingRow("Brightness", faderOf("howFar", far, STEP_LIMITS.howFar, "Brightness, percent of full"), `${far}%`)
      : settingRow("Opens to", faderOf("howFar", far, STEP_LIMITS.howFar, "Opens to, percent of its throw"), `${far}%`);
    const runsFor = move.shape === "flutter"
      ? settingRow("Runs for", lengthCells(step, numberCell("flutterMs", step.flutterMs ?? "", SeqProtocolCheck.BODY_FLUTTER_MS, "Runs for, in milliseconds")))
      : pair ? settingRow("Runs for", numberCell("runs", Math.round(pair.ms), STEP_LIMITS.t, "Runs for, in milliseconds")) : "";
    return runsFor + howFar + motion;
  };

  // Whether the Part's first move is a jump: an Output claims it and nobody
  // has recorded that Output's ends. Read off the facts the timeline draws
  // the move by (data/seq_rehearsal.js bodyMove()), so the two cannot
  // disagree. A Part no Output claims has no move to speak of - the unwired
  // note says so - and a light, by its Part Kind or by the Output it is on,
  // has no ends to record.
  const firstMoveJumps = (step, move) => {
    if (!move.body || move.light) return false;
    const facts = window.SeqRehearsal?.bodyMove(step, rehearsalContext());
    return Boolean(facts && facts.output && !facts.output.light && !facts.timed);
  };

  // ---------------------------------------------------------------------------
  // A Gesture's words (data/seq_gesture.js has the one vocabulary). The first
  // of each list is what a Gesture means when it does not say - the droid
  // stores an absent word as zero - so a word is stored only where it differs
  // from that, and an untouched Gesture saves back as it was read.
  // ---------------------------------------------------------------------------
  const GESTURE_DEFAULTED = ["shape", "spread", "direction", "start"];
  const gestureChoices = (field) => {
    const G = window.SeqGesture;
    const list = { shape: G.SHAPES, spread: G.SPREADS, direction: G.DIRECTIONS, start: G.STARTS }[field];
    return list.map((choice) => (typeof choice === "string" ? { id: choice, label: capital(choice) } : choice));
  };
  const gestureWord = (step, field) => step[field] ?? gestureChoices(field)[0].id;
  // Where the order starts is the Gesture's `start`. Its control in the
  // inspector goes by another name, because "start" there is already the
  // field every block has: the time it starts at.
  const GESTURE_FROM = "from";
  // A Gesture's times: empty, or zero, is absence - its default.
  const GESTURE_TIMES = ["flutterMs", "stepMs", "repeatMs", "extentMs", "speedMs"];
  const GESTURE_BEATS = ["stepBeats", "repeatBeats", "extentBeats"];
  // Whether the fold has been opened by hand: it then stays open through the
  // inspector being written again.
  let gestureMoreOpen = false;

  // The Move Shape's words over a Gesture's set: a light's when every Part it
  // spreads across is a light, a servo's otherwise (GLOSSARY.md "Move Shape").
  const gestureShapeWords = (step) => {
    const members = window.SeqGesture.members(step);
    const lights = members.length > 0 && members.every((id) => window.DroidPartKind?.isLight(catalogPart(id)));
    return lights ? SHAPE_WORDS.light : SHAPE_WORDS.servo;
  };

  // The Gesture's rows: Parts, Move, Travels, Order, How far.
  const gestureRows = (step) => {
    const G = window.SeqGesture;
    if (!G) return "";
    const esc = window.PAUtils.escapeHtml;
    const choices = (field) => gestureChoices(field).map((choice) => [choice.id, esc(choice.label)]);
    // A Gesture over a listed set of Parts names them. The list is not
    // authored here: a Gesture written with one keeps it as it was read.
    const parts = Array.isArray(step.parts)
      ? `<span class="seq-row-ctl">${esc(step.parts.map((id) => catalogPart(id)?.name || id).join(", "))}</span>`
      : pillsOf("set", (window.DroidParts?.sets || []).map((set) => [set.id, esc(set.label)]), step.set, "Which parts");
    const words = gestureShapeWords(step);
    const shape = gestureWord(step, "shape");
    const far = step.howFar ?? STEP_LIMITS.howFar[1];
    return settingRow("Parts", parts)
      + settingRow("Move", segOf("shape", G.SHAPES.map((id) => [id, words[id]]), shape, "Move"))
      + (shape === "flutter"
        ? settingRow("Lasts", numberCell("flutterMs", step.flutterMs ?? "", SeqProtocolCheck.BODY_FLUTTER_MS, "How long each part flutters, in milliseconds", "ms", true))
        : "")
      + settingRow("Travels", pillsOf("spread", choices("spread"), gestureWord(step, "spread"), "How it travels"))
      + settingRow("Order", `<span class="seq-row-ctl">${segOf("direction", choices("direction"), gestureWord(step, "direction"), "Which way round")}${segOf(GESTURE_FROM, choices("start"), gestureWord(step, "start"), "Where it starts")}</span>`)
      + settingRow("How far", faderOf("howFar", far, STEP_LIMITS.howFar, "How far, percent of each part's throw"), `${far}%`);
  };

  // What is rarely set, folded under the Gesture's rows: the pace, the
  // repeat, and the full-throw time and easing only a Gesture overrides
  // (GLOSSARY.md "Body Step"). Every and Again are in beats where the routine
  // has a tempo and in milliseconds where it has none. An
  // empty field shows what the Gesture does when it says nothing.
  const gestureMore = (step) => {
    const G = window.SeqGesture;
    if (!G) return "";
    const beats = SeqProtocolCheck.SPAN_BEATS;
    const tempo = tempoOf();
    const every = tempo
      ? numberCell("stepBeats", step.stepBeats ?? "", beats, "Beats between parts", "beats", "1")
      : numberCell("stepMs", step.stepMs ?? "", G.STEP_MS, "Milliseconds between parts", "ms", String(G.STEP_DEFAULT_MS));
    const again = tempo
      ? numberCell("repeatBeats", step.repeatBeats ?? "", beats, "Repeat every beats", "beats", true)
      : numberCell("repeatMs", step.repeatMs ?? "", G.REPEAT_MS, "Repeat every milliseconds", "ms", true);
    const said = [...GESTURE_BEATS, ...GESTURE_TIMES, "easing"].some((key) => key !== "flutterMs" && step[key]);
    return `<details class="seq-more seq-picked-more"${gestureMoreOpen || said ? " open" : ""}>`
      + `<summary>${chevron}Pace, repeat and feel</summary>`
      + `<div class="setting-rows seq-picked-rows">`
      + settingRow("Every", every)
      + settingRow("Again", again)
      + settingRow("For", numberCell("extentMs", step.extentMs ?? "", [0, G.EXTENT_MS_MAX], "Repeat for milliseconds, to the end when empty", "ms", "end"))
      + settingRow("Full throw", numberCell("speedMs", step.speedMs ?? "", G.SPEED_MS, "Full throw time for each part, the part's own when empty", "ms", "own"))
      + settingRow("Easing", segOf("easing", [["", "Own"], ...G.EASINGS.map((word) => [word, capital(word)])], step.easing || "", "Easing"))
      + `</div></details>`;
  };

  // One field of a Gesture, written. A word is stored only where it differs
  // from what a Gesture means without it, and pressing the choice it already
  // is changes nothing; a time left empty is absence.
  const writeGesture = (step, control, raw) => {
    const field = control === GESTURE_FROM ? "start" : control;
    if (GESTURE_TIMES.includes(field)) {
      const number = parseInt(raw, 10);
      if (raw === "" || number === 0) delete step[field];
      else if (Number.isInteger(number)) step[field] = number;
    } else if (field === "set") {
      if (window.SeqGesture.setOf(raw)) step.set = raw;
    } else if (field === "easing") {
      if (raw === "") delete step.easing;
      else step.easing = raw;
    } else if (GESTURE_DEFAULTED.includes(field) && raw !== gestureWord(step, field)) {
      if (raw === gestureChoices(field)[0].id) delete step[field];
      else step[field] = raw;
      // Only a flutter lasts a time (Protocol Check).
      if (field === "shape" && raw !== "flutter") delete step.flutterMs;
    }
  };

  // The rows of the block's kind, under where it starts.
  const kindRows = (step, at) => {
    const esc = window.PAUtils.escapeHtml;
    const move = moveOf(step);
    if (move) return moveRows(step, at, move);
    switch (step.type) {
      case "dome": {
        // A panel move was read above, and a light command has its rows.
        // What is left is a Dome command: the command itself, typed.
        const light = lightFields(step.cmd);
        return light ? lightRows(light) : settingRow("Sends",
          `<input class="number-cell text-cell" type="text" value="${esc(step.cmd ?? "")}" placeholder="@0T6, *HP0, :SE07" maxlength="${DOME_COMMAND_CHARS}" data-picked="cmd" aria-label="Dome command">`);
      }
      case "domeRotate": {
        const speed = Math.abs(fieldOf(step, "speedPct"));
        const stopped = speed === 0;
        return settingRow("Runs for", lengthCells(step, numberCell("durationMs", fieldOf(step, "durationMs"), STEP_LIMITS.turnMs, "Runs for, in milliseconds")))
          + settingRow("Way", segOf("way", [["left", "Left", stopped], ["right", "Right", stopped]], stopped ? "" : step.speedPct < 0 ? "left" : "right", "Way"))
          + settingRow("Speed", faderOf("speed", speed, STEP_LIMITS.speed, "Dome speed, percent"), `${speed}%`);
      }
      case "domeBearing":
        // What faces front when the turn ends: the dome's own front, or a dome
        // Part. How long it takes is the dome's at run, so it has no Runs for.
        return settingRow("Faces front", pillsOf("target",
          domeBearingTargets().map((target) => [esc(target.id), esc(target.words)]), fieldOf(step, "target"), "What faces front"));
      case "audio":
        return settingRow("Plays",
          `<input class="number-cell text-cell" type="text" value="${esc(step.cmd ?? "")}" placeholder="$H, $N, $D, $A..." data-picked="cmd" aria-label="Sound command">`);
      case "backgroundTrack": {
        // Its sound is picked as a sound step's is. Ends is how it stops,
        // pressed as the timeline draws it, and a choice that would not hold
        // there is not offered (trackEnds()).
        const pair = sessionTimeline?.standing(at) || null;
        const vol = fieldOf(step, "vol");
        const { pressed, open } = trackEnds(step);
        return settingRow("Plays",
          `<input class="number-cell text-cell" type="text" value="${esc(step.cmd ?? "")}" placeholder="$W, $212..." data-picked="cmd" aria-label="Sound command">`)
          + settingRow("Volume", faderOf("vol", vol, STEP_LIMITS.vol, "Volume, 0 to 30", ""), `${vol}`)
          + settingRow("Ends", segOf("ends", [["end", "With the sequence"], ["stop", "At its stop"], ["on", "Keeps playing"]]
            .map(([value, words]) => [value, words, !open.includes(value)]), pressed, "When it stops"))
          + (pair ? settingRow("Runs for", numberCell("runs", Math.round(pair.ms), STEP_LIMITS.t, "Runs for, in milliseconds")) : "");
      }
      case "audioCat":
        return settingRow("Plays", pillsOf("category", AUDIO_CATEGORIES.map((name) => [name, capital(name)]), fieldOf(step, "category"), "Sound category"))
          + settingRow("Fallback", pillsOf("fallback", AUDIO_FALLBACK_SLOTS.map((slot) => [slot.value, esc(slot.label)]), fieldOf(step, "fallback"), "Fallback sound"));
      case "random": {
        // Same pick reuses the pick of the Random Flutter before it, so it is
        // offered only where there is one - or where the step already says it.
        const before = stageSteps().slice(0, at).some((prior) => prior?.type === "random" && prior.set !== "hold");
        const sets = RANDOM_SETS.filter((set) => set !== "hold" || before || step.set === "hold")
          .map((set) => [set, set === "hold" ? "Same pick" : capital(set)]);
        return settingRow("Set", segOf("set", sets, fieldOf(step, "set"), "Which panels it picks from"))
          + settingRow("Action", segOf("mode", RANDOM_MODES.map((mode) => [mode, capital(mode)]), fieldOf(step, "mode"), "What it does to the pick"))
          + settingRow("Distinct", `<input class="switch" type="checkbox" data-picked="distinct"${fieldOf(step, "distinct") ? " checked" : ""} aria-label="Distinct">`)
          + settingRow("Move", numberCell("moveMs", fieldOf(step, "moveMs"), STEP_LIMITS.moveMs, "Move time, in milliseconds"))
          + settingRow("Jitter", numberCell("jitterMs", fieldOf(step, "jitterMs"), STEP_LIMITS.jitterMs, "Jitter, in milliseconds"));
      }
      case "loop":
        return settingRow("Repeats", numberCell("body", fieldOf(step, "body"), loopReach(stageSteps(), at), "Steps it repeats", "steps"))
          + settingRow("Every", numberCell("periodMs", fieldOf(step, "periodMs"), STEP_LIMITS.periodMs, "Every, in milliseconds"))
          + settingRow("For", numberCell("durationMs", fieldOf(step, "durationMs"), STEP_LIMITS.loopMs, "For, in milliseconds"));
      case "gesture":
        return gestureRows(step);
      case "sequence": {
        const choices = phraseChoices();
        return settingRow("Sequence", choices.length
          ? pillsOf("ref", choices.map((choice) => [esc(choice.id), esc(choice.label)]), step.ref, "Sequence")
          : listsAnswered() ? '<span class="seq-unit">Save a sequence first.</span>' : "");
      }
      default:
        return "";
    }
  };

  // Why a sequence inside this one is not drawn as its block and offers no
  // split, or "" when it is read or still being read. Nothing is said of one
  // in neither list until both lists have answered, nor where the heading
  // over the rows already says it (phraseName()).
  const phraseUnread = (step) => {
    if (!step.ref || phraseRead(step.ref)) return "";
    if (!phraseSource(step.ref)) {
      return listsAnswered() && !phraseName(step).endsWith("(not on this droid)") ? "Not on this droid." : "";
    }
    // Pressing its pill again asks again (writePicked(), phraseAgain()).
    return phrases.read.get(step.ref)?.failed ? "The droid did not send it. Press it again to ask." : "";
  };

  // A picked take (#442, ADR 0061): one object, edited whole and never opened
  // step by step - where it starts, and the part of it that plays, which the
  // timeline's edges trim too. Trim start moves where the take starts with
  // it, as the left edge does, so what plays stays where it was in time.
  // Perform again performs a new take in its place (performAgain()).
  const takeHtml = (block, head, acts) => {
    const esc = window.PAUtils.escapeHtml;
    const entry = (editorState.current.takes || [])[block.take] || {};
    const facts = takeFacts(entry.id);
    const least = window.SeqTimeline?.MIN_LENGTH_MS ?? 50;
    const startsAt = settingRow("Starts at", numberCell("start", Math.round(block.t0), STEP_LIMITS.t, "Starts at, in milliseconds"));
    if (!facts) {
      // A file whose read failed - or the droid deleted when it replaced
      // the take - is one this page cannot read; any other is still coming.
      return head(`Take ${block.take + 1}`, takeFiles.read.get(entry.id)?.failed ? "cannot be read" : "reading")
        + `<div class="setting-rows seq-picked-rows">${startsAt}</div>`
        + acts();
    }
    const from = Math.round(Number(entry.from) || 0);
    const to = Math.round(entry.to === undefined ? facts.lengthMs : Number(entry.to));
    const names = facts.parts.map((id) => catalogPart(id)?.name || id).join(", ");
    // What plays of it: the trimmed length of the whole.
    return head(`Take ${block.take + 1}`, `${countOf(facts.parts.length, "part", "parts")} · ${((to - from) / 1000).toFixed(2)} of ${(facts.lengthMs / 1000).toFixed(2)} s`)
      + `<div class="setting-rows seq-picked-rows">${startsAt}`
      + settingRow("Trim start", numberCell("from", from, [0, Math.max(0, to - least)], "Trim start, in milliseconds into the take"))
      + settingRow("Trim end", numberCell("to", to, [Math.min(facts.lengthMs, from + least), Math.round(facts.lengthMs)], "Trim end, in milliseconds into the take"))
      + settingRow("Parts", `<span class="setting-value">${esc(names)}</span>`)
      + `</div>`
      // Not offered while a take runs: Keep is the press then.
      + acts(takePoll === null ? `<button type="button" class="seq-act icon-act act-keeps-words" data-picked="perform-again">${window.PAUi.actFace("record-circle-outline", "Perform again")}</button>` : "");
  };

  const pickedHtml = (blocks) => {
    const esc = window.PAUtils.escapeHtml;
    const head = (name, sub) =>
      `<div class="sect seq-picked-head"><h3>${esc(name)}</h3><span class="sub">${esc(sub)}</span></div>`;
    if (blocks.length === 0) {
      return head("Nothing picked", "no block")
        + '<p class="hint">Press a block to change it. Shift-press adds another.</p>';
    }
    const stepCount = new Set(blocks.flatMap((block) => block.steps)).size;
    const takeCount = blocks.filter((block) => block.take !== undefined).length;
    const removeWords = takeCount === 0 ? (stepCount > 1 ? `Remove ${stepCount} steps` : "Remove")
      : blocks.length > 1 ? `Remove ${blocks.length} blocks` : "Remove";
    const acts = (others = "") =>
      `<div class="seq-picked-acts">${others}<button type="button" class="seq-act icon-act act-keeps-words" data-picked="remove">${window.PAUi.actFace("delete-outline", removeWords)}</button></div>`;
    const remove = acts();
    if (blocks.length > 1) {
      const nudge = window.SeqTimeline;
      const counted = [stepCount > 0 ? countOf(stepCount, "step", "steps") : "", takeCount > 0 ? countOf(takeCount, "take", "takes") : ""];
      return head(`${blocks.length} blocks`, counted.filter(Boolean).join(" · "))
        + `<p class="hint">Drag one and they all move. Arrow keys nudge ${nudge.NUDGE_MS} ms, Shift ${nudge.NUDGE_BIG_MS} ms.</p>`
        + remove;
    }
    const block = blocks[0];
    if (block.take !== undefined) return takeHtml(block, head, acts);
    const at = block.steps[0];
    const step = stageSteps()[at] || {};
    const beat = beatWords(step);
    const startsAt = settingRow("Starts at",
      `<span class="seq-row-ctl">${numberCell("start", Math.round(block.t0), STEP_LIMITS.t, "Starts at, in milliseconds")}`
      + `${beat ? `<span class="seq-unit">${esc(beat)}</span><button type="button" class="seq-act icon-act" data-picked="off-beat">${window.PAUi.actFace("music-note-off-outline", "Off the beat")}</button>` : ""}</span>`);
    // Picking a beat (ADR 0060): the bars and their beats, under where the
    // block starts, for a routine with a tempo. A step a loop repeats is
    // timed from its pass and gets none.
    // Only the beats the block can be moved to are offered: those inside
    // the limits a move of it keeps (the timeline's pickedRange()).
    const range = sessionTimeline?.pickedRange();
    const reachable = (index) => Boolean(range)
      && range.from <= SeqProtocolCheck.tempoBeatMs(tempoOf(), index) && SeqProtocolCheck.tempoBeatMs(tempoOf(), index) <= range.to;
    const beatRow = tempoOf() && window.SeqTempo && !SeqProtocolCheck.loopBodySteps(stageSteps()).has(at)
      ? settingRow("Beat", beatBarsHtml(step, stageReachMs(), (index) => `data-picked="beat" data-value="${index}"`, reachable))
      : "";
    const move = moveOf(step);
    const jumps = move?.settles && firstMoveJumps(step, move)
      ? `<p class="hint seq-brick">${esc(catalogPart(step.part)?.name || step.part)} has no recorded ends, so its first move is a jump, not a ramp.</p>`
      : "";
    // A logic or PSI light says which lights it is: the block can be on
    // several lanes, and then it has no one lane's name.
    const light = step.type === "dome" ? lightFields(step.cmd) : null;
    const lights = light?.kind === "DL" ? lightWord("targets", light.target)
      : light?.kind === "DT" ? lightWord("textTargets", light.target) : "";
    // A Gesture: what the dome makes of one it performs, said under its rows
    // in the Rehearsal's words; its fold; and, for one the body performs, the
    // act that writes it out as the Body Steps it becomes. The dome performs
    // its own as one command, which the body never breaks up (ADR 0046).
    const G = step.type === "gesture" ? window.SeqGesture : null;
    const domeSays = (G?.domeReading(step)?.notes || []).map((note) => `<p class="hint seq-brick">${esc(note)}</p>`).join("");
    // A sequence inside this one is named by the phrase, on however many
    // lanes its block lies, and splits once the droid's copy of it is read.
    const phrase = step.type === "sequence";
    const unread = phrase ? phraseUnread(step) : "";
    const split = (G && !G.onDome(step)) || (phrase && phraseRead(step.ref))
      ? `<button type="button" class="seq-act icon-act" data-picked="split">${window.PAUi.actFace("arrow-split-vertical", "Split into steps")}</button>` : "";
    return head((phrase ? phraseName(step) : block.name) || block.words || stepKindName(step), `${stepKindName(step)}${lights ? ` · ${lights}` : ""} · step ${at + 1}`)
      + `<div class="setting-rows seq-picked-rows">${startsAt}${beatRow}${kindRows(step, at)}</div>`
      + (unread ? `<p class="hint seq-brick">${unread}</p>` : "")
      + domeSays
      + (G ? gestureMore(step) : "")
      + jumps
      + (move?.settles ? `<p class="hint seq-brick">${BRICK_SENTENCE}</p>` : "")
      + acts(split);
  };

  // Called by the timeline whenever it draws the routine: the inspector is
  // written again only when what it says has changed, so a number half typed
  // into it is not wiped by a redraw, and not at all while a fader in it is
  // held - writing it would take the fader out from under the pointer. The
  // press that picked a block brings the tab forward.
  const showPicked = (blocks, pressed = false) => {
    pickedBlocks = blocks;
    const html = pickedHtml(blocks);
    const pane = document.getElementById("seq-picked");
    if (pane && html !== pickedShown && faderRun === null) {
      pane.innerHTML = html;
      pickedShown = html;
    }
    if (pressed) showTab("block");
  };

  // Draw the inspector from the routine as it is, whatever it showed: after
  // an edit made in it, so a field never keeps a value that was not applied.
  const repaintPicked = () => {
    pickedShown = null;
    showPicked(sessionTimeline ? sessionTimeline.picked() : []);
  };

  // An edit made in the inspector writes the inspector again, and the control
  // the keyboard was on is replaced by one like it. Run `edit` and put the
  // focus back on the control that says the same thing - the same field and,
  // for a choice, the same choice - so a builder working by keyboard is not
  // sent back to the top of the page by every edit.
  const keepingFocus = (edit) => {
    const pane = document.getElementById("seq-picked");
    const held = document.activeElement;
    const had = held && held.dataset?.picked && pane?.contains?.(held)
      ? { picked: held.dataset.picked, value: held.dataset.value }
      : null;
    edit();
    if (!had) return;
    [...pane.querySelectorAll(`[data-picked="${had.picked}"]`)].find((control) => control.dataset.value === had.value)?.focus();
  };

  // The pair an open is the start of, as the timeline draws it
  // (sessionTimeline.standing()): the step that closes the Part and how long
  // the pair runs, or null.
  const pairOf = (step) => sessionTimeline?.standing(stageSteps().indexOf(step)) || null;

  // The step that closes what `step` moves, and nothing else: a Body Step
  // that closes the same Part, or the same panel command with close for its
  // word. A group close (:CL00) over a single panel's open is not that - it
  // closes other panels too. A Background Track's is its stop step.
  const closeFor = (step) => (step.type === "body" ? { type: "body", part: step.part, shape: "close" }
    : step.type === "backgroundTrack" ? { type: "backgroundTrackStop" }
      : { type: "dome", cmd: step.cmd.replace(/^:(OP|OF)/, ":CL") });
  const closesOnly = (step, other) => {
    const close = closeFor(step);
    return Boolean(other) && other.type === close.type
      && (close.type === "body" ? other.part === close.part && other.shape === "close"
        : close.type === "backgroundTrackStop" || other.cmd === close.cmd);
  };

  // Turned into a flutter, an open's pair loses its close, inside the edit
  // that is already on the history (writePicked()). A close that closes more
  // than this Part stays.
  const dropPairClose = (step, pair) => {
    if (closesOnly(step, stageSteps()[pair.close])) removeSteps([pair.close], true);
  };

  // How a Background Track's start ends, read off the span the timeline draws
  // for it (SeqTimeline background()), so the Ends bar never says what the
  // routine does not do:
  //   stop  at its stop step - the block's right edge;
  //   end   with the sequence: the droid stops it at the end, because this
  //         start or an earlier one is bounded (the engine's rule, which the
  //         timeline draws);
  //   on    playing on after the sequence ends (boundAudio false here and on
  //         every start before it);
  //   cut   cut short by a later start or a Quiet ($s): none of the three is
  //         what happens, so none is pressed.
  // `open` is the choices that would hold if pressed. A cut start can only be
  // given a stop, and only where one fits before what cuts it (`room`, the
  // ms it plays first). Keeps playing does not hold where an earlier bounded
  // start has the droid stop it at the end anyway (`held`). A stop whose
  // removal would leave it cut short (`cutAfter`) is the only choice that
  // holds there.
  const trackEnds = (step) => {
    const drawn = sessionTimeline?.background(stageSteps().indexOf(step)) || { ends: "end", held: false, room: Infinity };
    const { t, last } = reachOf(step);
    const room = Math.min(drawn.room - 1, last - t);
    const stop = drawn.ends === "stop" || room > 0;
    const open = drawn.ends === "cut" || drawn.cutAfter ? ["stop"] : drawn.held ? ["end", "stop"] : ["end", "stop", "on"];
    return { pressed: drawn.ends === "cut" ? "" : drawn.ends, open: open.filter((value) => value !== "stop" || stop), room };
  };

  // Where `step` runs and how late what it starts may still be going: `t`,
  // the millisecond it runs at, and `last`, the end step's - or, among the
  // steps a loop repeats, the last millisecond of the loop's pass. `outer` is
  // a step before the end step that no loop repeats.
  const reachOf = (step) => {
    const steps = stageSteps();
    const at = steps.indexOf(step);
    const run = stageRun();
    const loop = loopRepeating(steps, at);
    const endAt = steps.findIndex((each) => each?.type === "end");
    const outer = !loop && endAt !== -1 && at < endAt;
    const last = loop ? Math.max(0, (Number(steps[loop.at].periodMs) || 0) - 1)
      : outer ? Number(run[endAt].t) || 0 : STEP_LIMITS.t[1];
    return { t: Number(run[at].t) || 0, last, loop, outer, endAt };
  };

  // Turned back into an open, a flutter is a pair again - and a Background
  // Track given a stop is one (writePicked()'s Ends): a close of the same
  // Part, or the stop, `ms` after the open, never past the end step - or,
  // among the steps a loop repeats, past the loop's pass. It lands before the
  // end step, or among the steps the loop repeats where a loop repeats its
  // open - in time order there, since the droid runs a pass's steps in the
  // order written and nothing reorders them - and the loop then repeats both.
  //
  // Where the next step to move the Part already is that close, none is
  // added: a sequence saved while a flutter still owed a close has one after
  // every flutter, and the open takes it for its pair. A Background Track
  // adopts none: the timeline pairs a start with the first stop before
  // anything else ends it, so a start it found no pair for has no stop that
  // would end it. The caller keeps `ms` short of what does (trackEnds()).
  //
  // `reopen(step)` is what turns the step itself back into an open. The two
  // are one change, tried on a copy first (triedOnCopy()): the close is one
  // step more, and a routine that is full must not become one the droid
  // refuses. Refused, the flutter stays a flutter and the stage says why.
  // Where the close goes is read off the routine before it is changed, and
  // holds for the copy.
  const reopenAsPair = (step, ms, reopen) => {
    const steps = stageSteps();
    const at = steps.indexOf(step);
    const moves = (other) => Boolean(other) && (step.type === "body"
      ? other.type === "body" && other.part === step.part
      : panelIntent(other)?.[2] === panelIntent(step)[2]);
    const hasClose = step.type !== "backgroundTrack" && closesOnly(step, steps.slice(at + 1).find(moves));
    const { t, last, loop, outer, endAt } = reachOf(step);
    const close = { t: Math.min(t + ms, last), ...closeFor(step) };
    // Inside a loop: before the first step the pass runs after the close.
    const run = stageRun();
    const bodyEnd = loop ? loop.at + loop.size : at + 1;
    const later = loop ? run.findIndex((each, i) => i > at && i < bodyEnd && (Number(each.t) || 0) > close.t) : -1;
    const into = outer ? endAt : later === -1 ? bodyEnd : later;
    const place = (list) => {
      reopen(list[at]);
      if (hasClose) return;
      if (loop) list[loop.at].body += 1;
      list.splice(into, 0, { ...close });
    };
    const { refused } = triedOnCopy(place);
    if (refused) sayOnStage(refused.error, "error");
    else place(steps);
  };

  // One field of the picked step, written. `raw` is the control's own value.
  const PICKED_NUMBERS = ["moveMs", "jitterMs", "periodMs", "durationMs"];
  const writePicked = (step, field, raw, way = null) => {
    const number = parseInt(raw, 10);
    const light = step.type === "dome" ? lightFields(step.cmd) : null;
    if (light) {
      const cmd = lightEdited(light, field, raw);
      if (cmd === null) sayOnStage(LIGHT_TEXT_REFUSED, "error");
      else if (cmd !== undefined) {
        step.cmd = cmd;
        // The line that refused a text goes once one is taken - and only
        // that line: the stage's one line also carries the droid's answer to
        // a pose and why a drop landed nothing, which a text edit leaves be.
        if (field === "text" && stageSays(LIGHT_TEXT_REFUSED)) sayOnStage("");
      }
    } else if (field === "howFar") {
      // Stored only where it differs: the whole throw is absence, so a
      // sequence that never said how far saves back as it was read.
      if (!Number.isInteger(number)) return;
      if (number >= STEP_LIMITS.howFar[1]) delete step.howFar;
      else step.howFar = number;
    } else if (step.type === "gesture") {
      writeGesture(step, field, raw);
    } else if (field === "ref") {
      // Which sequence it is, and the label a reader of the file sees: its
      // name now.
      const choice = phraseChoices().find((each) => each.id === raw);
      if (!choice) return;
      // Pressed again on the one it is, it changes nothing - and asks again
      // for a phrase whose read failed.
      phraseAgain(raw);
      if (raw === step.ref) return;
      step.ref = choice.id;
      step.name = choice.label;
    } else if (field === "motion" && step.type === "body") {
      // A body flutter has a length of its own: turned into one, the open
      // lasts what its pair ran for, within a flutter's bounds. With no pair
      // it lasts as long as a dropped Part stands open, or what is left
      // before the end step where that is less, so one set near the end is
      // not cut short at once. The pair's close goes in the same edit: a
      // flutter ends closed. Turned back, it is an open again, stored as
      // absence, with a close where the flutter ended, and an open has no
      // length to keep in ms or in beats.
      if (raw === "flutter" && step.shape !== "flutter") {
        const [least, most] = SeqProtocolCheck.BODY_FLUTTER_MS;
        const pair = pairOf(step);
        const { t, last } = reachOf(step);
        step.shape = "flutter";
        step.flutterMs = Math.max(least, Math.min(most, Math.round(pair ? pair.ms : Math.min(DROPPED_OPEN_MS, last - t))));
        if (pair) dropPairClose(step, pair);
      } else if (raw === "open" && step.shape === "flutter") {
        const lasted = Number(stageRun()[stageSteps().indexOf(step)].flutterMs);
        reopenAsPair(step, lasted > 0 ? lasted : DROPPED_OPEN_MS, (opened) => {
          delete opened.shape;
          delete opened.flutterMs;
          delete opened.spanBeats;
        });
      }
    } else if (field === "flutterMs") {
      // A body flutter's own length, within its bounds. One typed over a
      // span of beats is a millisecond instead (ADR 0058).
      if (!Number.isInteger(number)) return;
      const [least, most] = SeqProtocolCheck.BODY_FLUTTER_MS;
      const lasts = Math.max(least, Math.min(most, number));
      if (lasts !== step.flutterMs) delete step.spanBeats;
      step.flutterMs = lasts;
    } else if (field === "motion") {
      // Open and Flutter are the one step's command, at the same time. Only
      // an open or a close says how far (Protocol Check). The dome ends a
      // flutter closed, so the pair's close goes with the open, and comes
      // back with it: a dome flutter has no length the body knows, so the
      // close lands as long after as a dropped panel's does.
      const shape = PANEL_SHAPES[panelIntent(step)?.[1]];
      if (raw === "flutter" && shape === "open") {
        const pair = pairOf(step);
        step.cmd = step.cmd.replace(/^:OP/, ":OF");
        delete step.howFar;
        if (pair) dropPairClose(step, pair);
      } else if (raw === "open" && shape === "flutter") {
        reopenAsPair(step, DROPPED_OPEN_MS, (opened) => {
          opened.cmd = opened.cmd.replace(/^:OF/, ":OP");
        });
      }
    } else if (field === "vol") {
      // A Background Track's volume, within the interface's 0-30.
      if (!Number.isInteger(number)) return;
      step.vol = Math.max(STEP_LIMITS.vol[0], Math.min(STEP_LIMITS.vol[1], number));
    } else if (field === "ends" && step.type === "backgroundTrack") {
      // How it stops, and only by a choice the Ends bar offers (trackEnds()).
      // A stop lands half way to whatever comes first of the end, the loop
      // pass's end and what would cut the Background Track short, so its
      // edge is there to drag. Taken away, the stop goes in the same edit.
      // Bounded is absence; Keeps playing is the one word stored, boundAudio
      // false.
      const pair = pairOf(step);
      const { open, room } = trackEnds(step);
      if (!open.includes(raw)) return;
      if (raw === "stop") {
        if (pair) return;
        reopenAsPair(step, Math.round(room / 2), (start) => {
          delete start.boundAudio;
        });
        return;
      }
      if (pair) removeSteps([pair.close], true);
      if (raw === "on") step.boundAudio = false;
      else delete step.boundAudio;
    } else if (field === "target" && step.type === "domeBearing") {
      // Only what the picker offers: front, or a dome Part Protocol Check
      // accepts.
      if (domeBearingTargets().some((target) => target.id === raw)) step.target = raw;
    } else if (field === "way") {
      Object.assign(step, turnOf(raw, step.speedPct, step.durationMs));
    } else if (field === "speed") {
      if (!Number.isInteger(number)) return;
      // A stop given a speed becomes a turn in the one edit: it takes the
      // time a turn starts with, or the droid would refuse it until a second
      // edit gave it one.
      const starts = number > 0 && !step.speedPct && !step.durationMs;
      Object.assign(step, turnOf(way || (step.speedPct < 0 ? "left" : "right"), number, starts ? TURN_STARTS_MS : step.durationMs));
    } else if (field === "distinct") {
      step.distinct = raw === true;
    } else if (field === "body") {
      // Held to the steps there are to repeat, whatever was typed.
      if (!Number.isInteger(number)) return;
      const [least, most] = loopReach(stageSteps(), stageSteps().indexOf(step));
      step.body = Math.max(least, Math.min(most, number));
    } else if (PICKED_NUMBERS.includes(field)) {
      if (!Number.isInteger(number)) return;
      // A duration typed over a span of beats is a millisecond instead
      // (ADR 0058).
      if (field === "durationMs" && number !== step.durationMs) delete step.spanBeats;
      step[field] = number;
    } else {
      step[field] = raw;
    }
  };

  // The step the inspector is showing, with its place in the routine: the one
  // picked block's first step, or null.
  const pickedStep = () => {
    if (pickedBlocks.length !== 1) return null;
    const at = pickedBlocks[0].steps[0];
    const step = editorState.current ? stageSteps()[at] : null;
    return step ? { at, step } : null;
  };

  // An edit made in the inspector in one act - a number typed, a choice
  // pressed - on the one history. One that changed nothing records nothing.
  // The take picked alone on the timeline, as its entry in `takes`, or null.
  const pickedTake = () => (pickedBlocks.length === 1 && pickedBlocks[0].take !== undefined
    ? (editorState.current?.takes || [])[pickedBlocks[0].take] || null : null);

  // A trim typed into the inspector, held where the timeline's edges hold
  // it: inside the take, never shorter than a block, and a start that cannot
  // move the take before 0. A trim at the take's own end is stored as none.
  const trimTake = (entry, field, raw) => {
    const facts = takeFacts(entry.id);
    const ms = Math.round(Number(raw));
    if (!facts || !Number.isFinite(ms)) {
      repaintPicked();
      return;
    }
    const least = window.SeqTimeline?.MIN_LENGTH_MS ?? 50;
    const from = Number(entry.from) || 0;
    const to = entry.to === undefined ? facts.lengthMs : Number(entry.to);
    const at = Number(entry.t) || 0;
    const before = historyBegin();
    if (field === "from") {
      const next = Math.max(0, from - at, Math.min(ms, to - least));
      entry.t = at + next - from;
      if (next === 0) delete entry.from;
      else entry.from = next;
    } else {
      const next = Math.max(from + least, Math.min(ms, facts.lengthMs));
      if (next >= facts.lengthMs) delete entry.to;
      else entry.to = next;
    }
    historyCommit(before);
    edited();
    repaintPicked();
  };

  const inspect = (field, raw) => keepingFocus(() => {
    const take = pickedTake();
    if (take && !historyBusy()) {
      if (field === "start") sessionTimeline.movePickedTo(Number(raw));
      else if (field === "from" || field === "to") trimTake(take, field, raw);
      return;
    }
    const picked = pickedStep();
    if (!picked || historyBusy()) return;
    const { at, step } = picked;
    if (field === "start") {
      sessionTimeline.movePickedTo(Number(raw));
    } else if (field === "runs") {
      sessionTimeline.sizeStanding(at, Number(raw));
    } else if (field === "beat") {
      // The beat picked: the whole block starts there, moved as Starts at
      // moves it - within its limits, back in time order, one entry - and
      // placed on the beat (movePickedTo()'s `landed`).
      const beat = parseInt(raw, 10);
      if (Number.isInteger(beat) && tempoOf()) sessionTimeline.movePickedTo(SeqProtocolCheck.tempoBeatMs(tempoOf(), beat), true);
    } else if (field === "spanBeats") {
      // The length in beats, within what a span holds; emptied, the length
      // is its milliseconds again.
      const beats = parseInt(raw, 10);
      const [least, most] = SeqProtocolCheck.SPAN_BEATS;
      setStepBeat(at, { spanBeats: Number.isInteger(beats) ? Math.max(least, Math.min(most, beats)) : null });
    } else if (GESTURE_BEATS.includes(field)) {
      // A Gesture's pace or repeat in beats: resolved from the tempo as the
      // droid resolves it, and one entry of its own (setStepBeat()).
      const beats = parseInt(raw, 10);
      setStepBeat(at, { [field]: Number.isInteger(beats) ? beats : null });
    } else {
      const before = historyBegin();
      writePicked(step, field, raw);
      // A Move Shape changed can add a close or take one away, and the close
      // it adds goes in time order with the rest. So can how a Background
      // Track ends.
      const paired = field === "motion" || field === "ends";
      if (paired) orderSteps();
      historyCommit(before);
      edited();
      // The blocks a step draws change with its Move Shape: a flutter is its
      // own block, and an open is one block with the close after it. A
      // Background Track given a stop is one block with it.
      if (paired) {
        const now = stageSteps().indexOf(step);
        const pair = raw === "open" || raw === "stop" ? sessionTimeline.standing(now) : null;
        sessionTimeline.pick(pair ? [now, pair.close] : [now]);
      }
    }
    repaintPicked();
  });

  // A fader is an edit made over time: the routine follows it as it moves,
  // and the whole run is one entry, recorded when it is let go. `way` is the
  // turn's direction as the run began, which a pass through zero would lose.
  //
  // Let go is the fader's `change`, and the pointer coming up anywhere: a
  // fader brought back to the value it started on sends no `change`, and a
  // run left open would hold Undo and the inspector for good.
  let faderRun = null;

  const faderMoved = (input) => {
    const picked = pickedStep();
    if (!picked || (faderRun === null && historyBusy())) return;
    if (faderRun === null) {
      faderRun = { before: historyBegin(), way: picked.step.speedPct < 0 ? "left" : "right" };
      window.addEventListener("pointerup", faderLetGo);
      window.addEventListener("pointercancel", faderLetGo);
    }
    writePicked(picked.step, input.dataset.picked, input.value, faderRun.way);
    const valueEl = input.closest(".setting-row")?.querySelector(".setting-value");
    if (valueEl) valueEl.textContent = `${input.value}${input.dataset.unit ?? "%"}`;
    edited();
  };

  function faderLetGo() {
    window.removeEventListener("pointerup", faderLetGo);
    window.removeEventListener("pointercancel", faderLetGo);
    if (faderRun === null) return;
    const { before } = faderRun;
    faderRun = null;
    historyCommit(before);
    paintHistory();
    keepingFocus(repaintPicked);
  }

  // ---------------------------------------------------------------------------
  // The library (#441): the Parts tab, and Drop a part beside the inspector.
  // One flat list of Parts, dome and body together; under it, in the Parts
  // tab, the steps that are not a Part. Two things keep a Part from moving,
  // and the list treats them differently:
  //   - not wired: a step can name it, but this droid has nothing to move it
  //     with - no Output claims it, or the dome link is off (notWired()). It
  //     is listed, dashed and counted, and it drops.
  //   - no step for it: no sequence step moves it on any droid (partDrop()
  //     answers null). It is not listed at all.
  // The escape-hatch Output slots and the dome's buttons are not Parts a
  // routine moves, and are not listed either.
  //
  // A press on a pill does nothing to the routine and never moves the droid:
  // it says how to add it. A drag onto the timeline inserts it where the
  // timeline says it lands (data/seq_timeline.js aim()), as one edit. Pointer
  // events, never HTML5 drag-and-drop, which cannot follow the pointer.
  // ---------------------------------------------------------------------------
  const UNLISTED_SECTIONS = ["other_slots", "dome_fixtures"];
  // The steps that are not a Part, in More steps' order: a kind of step by
  // its type, or one of the dome's light commands by its prefix (DOME_SUBMODES)
  // - a Visual Preset and a Holo Effect name no Part, so they are dropped from
  // here and land on the Dome row. With them, as the third dome kind, the Dome
  // command (DOME_COMMAND): a command the dome reads as written, which names
  // no Part either and lands on the same row.
  //
  // Between the two, the Sets: the tokens a Gesture spreads across (the
  // catalog's `sets`). A set dropped is one Gesture over it.
  //
  // Last, the Sequences: every sequence this one can hold (phraseChoices()).
  // One dropped is one step that names it, drawn as one linked block.
  //
  // The Dome command's pill goes by an id of its own, which is neither a step
  // type nor a light command's prefix.
  //
  // Turn Dome To sits beside Spin Dome: one turns for a time, the other until
  // front, or a dome Part, faces front (ADR 0051).
  const DOME_COMMAND_KIND = "domeCommand";
  const LIBRARY_KINDS = ["audio", "audioCat", "backgroundTrack", "domeRotate", "domeBearing", "DV", "DH", DOME_COMMAND_KIND, "random", "loop", "end"];
  const librarySets = () => window.DroidParts?.sets || [];
  // A kind that is a dome step, as its name and the command a dropped one
  // holds; null for a kind that is a step type of its own.
  const libraryDome = (id) => DOME_SUBMODES[id] || (id === DOME_COMMAND_KIND ? DOME_COMMAND : null);
  const libraryKindName = (id) => libraryDome(id)?.name || stepTypeName[id];
  // The Sequence End pill: the one thing that may be dropped past the end.
  const END_PILL = "kind:end";
  // How far the pointer goes before a press on a pill is a drag.
  const LIBRARY_DRAG_PX = 6;
  // How long a dropped panel or body Part stands open: its close lands this
  // long after.
  const DROPPED_OPEN_MS = 1000;

  // What a Part dropped on the timeline makes, or null for a Part no step
  // moves. The one rule, read by the library's two lists (libraryParts()) and
  // by the drop (dropOnTimeline()), so a Part is listed exactly when a drop
  // of it lands something.
  //
  //   panel   a dome panel the dome can be told to move: the command that
  //           opens it.
  //   lights  a dome light the dome answers for by name - a logic display or
  //           a PSI, found by its catalog alias the way Lights finds it
  //           (data/lights.js domeTarget()): that name.
  //   body    a body Part, its lights among them. One no Output claims
  //           counts all the same: that is legal to author, it is listed
  //           dashed, and the note over the lanes says it is not wired.
  //
  // What is left has no step that moves it: a dome panel that is fixed, a
  // dome light the dome has no word for (the Magic Panel, the small upper
  // panel), a holoprojector's servos. Those are hidden, not refused
  // (operator, 2026-10-02): the library does not offer what it would have to
  // turn away.
  const partDrop = (part) => {
    if (!part) return null;
    const panel = window.DomeCommandMap?.resolvePanelCommand(part.shorthand, "open");
    if (panel) return { panel };
    const lights = window.DroidPartKind?.isLight(part)
      ? (part.aliases || []).find((alias) => domeLights.targets.includes(alias))
      : null;
    if (lights) return { lights };
    return part.half === "body" ? { body: true } : null;
  };

  const libraryParts = () =>
    (window.DroidParts?.parts || []).filter((part) => !UNLISTED_SECTIONS.includes(part.section) && partDrop(part));

  let partsFind = "";

  const paintParts = () => {
    const esc = window.PAUtils.escapeHtml;
    const context = rehearsalContext();
    const parts = libraryParts();
    // The timeline's own rule for a lane it dims (data/seq_timeline.js
    // notWired()), which says nothing of a half the droid has not reported.
    const off = (part) => Boolean(window.SeqTimeline?.notWired(part.id, part.half, context));
    const sub = countOf(parts.length, "part", "parts")
      + (context.outputs !== null ? ` · ${parts.filter(off).length} not wired` : "");
    const pill = (lib, short, name, dim = false) =>
      `<button type="button" class="part-pill seq-lib-pill${dim ? " is-off" : ""}" data-lib="${lib}">${short ? `<span class="seq-part-short">${esc(short)}</span>` : ""}${esc(name)}</button>`;
    const pills = (list) => list.map((part) => pill(`part:${part.id}`, part.shorthand, part.name, off(part))).join("");
    const write = (id, text, html = false) => {
      const el = document.getElementById(id);
      if (el) el[html ? "innerHTML" : "textContent"] = text;
    };
    const wanted = partsFind.trim().toLowerCase();
    const found = parts.filter((part) =>
      !wanted || part.name.toLowerCase().includes(wanted) || (part.shorthand || "").toLowerCase().includes(wanted));
    write("seq-editor-parts-sub", sub);
    write("seq-drop-sub", sub);
    write("seq-lib-parts", pills(found) || '<span class="hint">No part by that name.</span>', true);
    // A set none of whose Parts this droid can move is dashed as a Part is.
    const setOff = (set) => set.members.length > 0 && set.members.every((id) => {
      const part = catalogPart(id);
      return Boolean(part) && off(part);
    });
    write("seq-lib-sets", librarySets().map((set) => pill(`set:${set.id}`, "", set.label, setOff(set))).join(""), true);
    write("seq-lib-kinds", LIBRARY_KINDS.map((id) => pill(`kind:${id}`, "", libraryKindName(id))).join(""), true);
    write("seq-lib-phrases", phraseChoices().map((choice) => pill(`seq:${esc(choice.id)}`, "", choice.label)).join("")
      || (listsAnswered() ? '<span class="hint">Save a sequence first.</span>' : ""), true);
    write("seq-drop-parts", pills(parts), true);
  };

  // A pill's key is its group and, after the first colon, its id: a Factory
  // sequence's reference is its name, which has a colon of its own.
  const libraryKey = (lib) => {
    const cut = lib.indexOf(":");
    return [lib.slice(0, cut), lib.slice(cut + 1)];
  };

  // What a pill is called: the Part's name, the set's, the kind of step, or
  // the sequence's name.
  const libraryName = (lib) => {
    const [group, id] = libraryKey(lib);
    if (group === "kind") return libraryKindName(id);
    if (group === "set") return librarySets().find((set) => set.id === id)?.label || id;
    if (group === "seq") return phraseChoices().find((choice) => choice.id === id)?.label || id;
    return libraryParts().find((part) => part.id === id)?.name || id;
  };

  const sayOnStage = (text, level = "") => sessionTimeline?.say(text ? { text, level } : null);
  // Whether the stage's line says exactly `text` now. Read off the line
  // itself (data/seq_timeline.js writes it, for this page and for a pose), so
  // it is true of whatever said it last.
  const stageSays = (text) =>
    document.getElementById("seq-editor-tlbar")?.querySelector(".tl-said")?.textContent === text;

  // The steps a drop made are in the routine: put them in time order, read
  // the routine again, and pick them, so the inspector is on the new block.
  const landed = (made) => {
    orderSteps();
    edited();
    const steps = stageSteps();
    sessionTimeline.pick(made.map((step) => steps.indexOf(step)));
    sayOnStage("");
    showTab("block");
  };

  // An edit tried on a copy first: `change(list)` is made to the half on the
  // stage in a copy of the whole sequence, the copy's steps are put in the
  // order the routine's would be, and Protocol Check reads the copy - both
  // halves, and the sequences either half names. Answers {list,
  // refused}: the copy's stage list, and Protocol Check's verdict where the
  // edit would turn a sequence the droid accepts into one it refuses, else
  // null. An edit to a sequence the droid already refuses is not held to
  // that: it may be the one that mends it.
  const triedOnCopy = (change) => {
    const trial = JSON.parse(JSON.stringify(editorState.current));
    const key = stageKey();
    change(trial[key]);
    trial[key] = stepOrder(trial[key]).map((from) => trial[key][from]);
    const verdict = routineVerdict(editorState.current).ok ? routineVerdict(trial) : { ok: true };
    return { list: trial[key], refused: verdict.ok ? null : verdict };
  };

  // Insert what was dragged from the library at `at` ms, into the half on the
  // stage. Whatever lands is one entry in the history, so one Undo takes the
  // whole drop away; a drop that lands nothing records nothing. Where the
  // routine is what turns it away - a loop with nothing to repeat, a step
  // Protocol Check refuses, one step more than a sequence holds - the stage
  // says why. A pill for something the library does not list - a Part, a set
  // or a sequence that is not there - lands nothing and says nothing: no such
  // pill is drawn.
  //
  // EVERY DROP IS TRIED ON A COPY FIRST (land()): a drop never turns a
  // sequence the droid accepts into one it refuses. What refuses it is said
  // in Protocol Check's own words. Each kind of drop is therefore a
  // `place(list)`: it puts fresh steps into the list it is given - the copy's,
  // then the routine's - and answers the steps it made. Where they go is
  // worked out from the routine once, here, and holds for the copy, which is
  // the same list.
  const dropOnTimeline = (lib, at) => {
    const [group, id] = libraryKey(lib);
    const steps = stageSteps();
    const endAt = steps.findIndex((step) => step?.type === "end");
    const inLoop = SeqProtocolCheck.loopBodySteps(steps);
    // The beat the drop landed on, where the landing line named one (the
    // timeline's aim()): the first step made, where it starts there, is
    // placed on that beat and not at its millisecond, as a block dragged
    // onto a beat is (ADR 0060).
    const beat = sessionTimeline.beatAt(at);
    // `first` is run once the copy is accepted, before anything lands.
    const land = (place, first = () => {}) => {
      const { refused } = triedOnCopy(place);
      if (refused) {
        sayOnStage(refused.error, "error");
        return;
      }
      first();
      historyPush();
      const made = place(steps);
      if (beat !== null && made[0] && made[0].t === at) made[0].beat = beat;
      landed(made);
    };
    // Before the end step, or last where there is none.
    const beforeEnd = (make) => (list) => {
      const made = make();
      list.splice(endAt === -1 ? list.length : endAt, 0, ...made);
      return made;
    };

    if (group === "part") {
      // What the Part makes is partDrop()'s to say; every listed Part makes
      // something, and one that is not listed has no pill to drop.
      //
      // A dome panel lands as a Part standing open: its open here and its
      // close a second on, never past the end.
      //
      // A dome light lands as a Logic / PSI Mode on its own lane: Normal,
      // with no color and no duration, so it holds until the next mode.
      //
      // A body Part lands as a dome panel does, in Body Steps: its open here
      // - open is the Move Shape a step has when it says none, so none is
      // written - and its close a second on.
      const part = libraryParts().find((each) => each.id === id);
      const drop = partDrop(part);
      if (!drop) return;
      const last = endAt === -1 ? STEP_LIMITS.t[1] : Number(steps[endAt].t) || 0;
      const closes = Math.min(at + DROPPED_OPEN_MS, last);
      land(beforeEnd(() => (drop.lights
        ? [{ t: at, type: "dome", cmd: lightCmd({ ...lightFields(DOME_SUBMODES.DL.starts), target: drop.lights }) }]
        : drop.body
          ? [{ t: at, type: "body", part: part.id }, { t: closes, type: "body", part: part.id, shape: "close" }]
          : [
            { t: at, type: "dome", cmd: drop.panel },
            { t: closes, type: "dome", cmd: window.DomeCommandMap.resolvePanelCommand(part.shorthand, "close") },
          ])));
      return;
    }

    if (group === "set") {
      // One Gesture over the set, and it says nothing but the set: every
      // other word left off is its default - open, all together, clockwise
      // from the front, at the pace a Gesture takes - so it is stored only
      // where a builder makes it differ.
      if (!librarySets().some((set) => set.id === id)) return;
      land(beforeEnd(() => [{ t: at, type: "gesture", set: id }]));
      return;
    }

    if (group === "seq") {
      // One step that names the sequence, and nothing more: its reference,
      // and its name now as the label a reader of the file sees. The droid's
      // copy of it is read after it lands (edited() asks), and that is no
      // edit: one Undo takes the step away. A phrase whose read failed is
      // asked for again by a drop that lands.
      //
      // Into either half. The rules for a sequence inside another - that it
      // is on the droid, is no toggle, closes no cycle and fits once spliced
      // in - are applied to both halves, here and by the droid at Save
      // (protocolCheckNesting(), src/protocol_check.cpp), as the droid
      // splices both when it runs (seqStorePrepare(), src/seq_store.cpp). So
      // what turns one away from Closes is what turns one away from Opens:
      // Protocol Check, in its own words (land()).
      const choice = phraseChoices().find((each) => each.id === id);
      if (!choice) return;
      land(beforeEnd(() => [{ t: at, type: "sequence", ref: choice.id, name: choice.label }]), () => phraseAgain(choice.id));
      return;
    }

    if (id === "loop") {
      // A loop is one object over the steps it repeats: the run of steps that
      // start inside its first period after the drop, up to the first a loop
      // cannot repeat. They keep their moments - a repeated step is timed
      // from the start of a pass, so each is rewritten from there - and leave
      // their beats, which count from nothing inside a pass.
      const first = steps.findIndex((step, i) => !inLoop.has(i) && (Number(step?.t) || 0) >= at);
      let count = 0;
      for (let step = steps[first]; step && !inLoop.has(first + count) && !NOT_REPEATED.includes(step.type)
        && step.t < at + stepTypeDefaults.loop.periodMs; step = steps[first + count]) count += 1;
      if (count === 0) {
        sayOnStage("Servo Loop needs a step after it to repeat.", "error");
        return;
      }
      const wrap = (list) => {
        const loop = { t: at, type: "loop", ...stepTypeDefaults.loop, body: count };
        list.slice(first, first + count).forEach((step) => {
          step.t -= at;
          delete step.beat;
        });
        list.splice(first, 0, loop);
        return [loop];
      };
      // A loop is one more step in a routine that may be full, and there are
      // commands a loop may not repeat.
      land(wrap);
      return;
    }

    if (id === "end" && endAt !== -1) {
      // A routine has one end. Dropped again it is that end, moved - later, or
      // earlier as far as a drag of it would go, which is to its last step.
      sessionTimeline.pick([endAt]);
      sessionTimeline.movePickedTo(at, true);
      sayOnStage("");
      showTab("block");
      return;
    }

    if (id === "end") {
      // The end closes the routine, so it lands last and no earlier than the
      // last step before it.
      const last = Math.max(at, ...steps.filter((_, i) => !inLoop.has(i)).map((step) => Number(step?.t) || 0));
      land((list) => {
        const made = { t: last, type: "end", ...stepTypeDefaults.end };
        list.push(made);
        return [made];
      });
      return;
    }

    const dome = libraryDome(id);
    land(beforeEnd(() => [dome
      ? { t: at, type: "dome", cmd: dome.starts }
      : { t: at, type: id, ...stepTypeDefaults[id] }]));
  };

  // What Split into steps shares, for a Gesture and for a sequence inside
  // this one. Each replaces the one picked step with the steps it becomes.
  //
  // loopRepeating(): the loop that repeats the step at `at`, as its unit, or
  // undefined. A step a loop repeats is timed from the pass.
  const loopRepeating = (steps, at) =>
    stepUnits(steps).find((unit) => unit.size > 1 && at > unit.at && at < unit.at + unit.size);

  // splitPlace(): how the steps made go in, for the routine and for the copy
  // it is tried on: in the place of the step at `at`, and the loop that
  // repeated that step repeats the steps it becomes.
  const splitPlace = (loop, at) => (list, made) => {
    if (loop) list[loop.at].body += made.length - 1;
    list.splice(at, 1, ...made);
  };

  // splitOverfull(): whether `made` steps in place of one is more than a
  // sequence holds; it says so on the stage.
  const splitOverfull = (made) => {
    const most = SeqProtocolCheck.MAX_STEPS;
    const count = stageSteps().length - 1 + made;
    if (count > most) sayOnStage(`That would make ${count} steps. A sequence can have at most ${most}.`, "error");
    return count > most;
  };

  // splitRefused(): the split tried on a copy of the routine - `write()`
  // makes the steps and `place(list, made)` puts them in - and Protocol
  // Check's verdict on it: on each step made, whatever else the routine has
  // wrong, and on the whole routine when it was one the droid accepts. Not
  // ok, nothing lands, and the stage says Protocol Check's reason.
  const splitRefused = (write, place) => {
    const tried = write();
    const { list, refused: whole } = triedOnCopy((copy) => place(copy, tried));
    const refusedStep = tried
      .map((made) => SeqProtocolCheck.validateStep(made, list.indexOf(made), list, true))
      .find((verdict) => !verdict.ok);
    const refused = refusedStep || whole;
    if (refused) sayOnStage(refused.error, "error");
    return Boolean(refused);
  };

  // Split into steps: the picked body Gesture is replaced by the moves it
  // already makes, written out as Body Steps, in one entry of the history.
  //
  // The moves are the one expansion there is (SeqGesture.bodyMoves()), read
  // off the Gesture as the droid runs it - its pace and its extent resolved
  // from the tempo and the end - and only those it makes before the end step,
  // where the droid stops a Gesture. A written step says nothing of a full
  // throw's time or an easing: those are the Output's, and only a Gesture
  // overrides them (GLOSSARY.md "Body Step"). What was generated and paced by
  // the droid is hand-written after this, and keeps the timing written here
  // (GLOSSARY.md "Cadence Floor").
  //
  // A flutter Gesture's members each flutter for the length it states, or for
  // one step of its pace where it states none (seqGestureFlutterMs(),
  // include/sequence_gesture.h). A written flutter must state its length, so
  // each step made states that one.
  //
  // Tried on a copy first. If it would leave a step Protocol Check refuses,
  // nothing lands, and the stage says Protocol Check's reason.
  //
  // Two things are refused before that, each in words that say what to do,
  // and whatever else the routine has wrong:
  //   - more steps than a sequence holds: a Gesture that repeats can make
  //     thousands of moves;
  //   - inside a loop, a move at or past the end of the loop's pass, where a
  //     step the loop repeats cannot be.
  const splitGesture = () => {
    const picked = pickedStep();
    const G = window.SeqGesture;
    if (!picked || picked.step.type !== "gesture" || !G || G.onDome(picked.step) || historyBusy()) return;
    const { at, step } = picked;
    const steps = stageSteps();
    const run = stageRun()[at];
    // A step a loop repeats is timed from the pass, where the end is not.
    const loop = loopRepeating(steps, at);
    const endAt = steps.findIndex((each) => each?.type === "end");
    const last = loop || endAt === -1 ? Infinity : Number(steps[endAt].t) || 0;
    const moves = G.bodyMoves(run, Number(run.t) || 0).filter((move) => move.t < last);
    const flutterMs = Number(run.flutterMs) || Number(run.stepMs) || G.STEP_DEFAULT_MS;
    if (moves.length === 0) {
      sayOnStage("This gesture makes no move before the end.", "error");
      return;
    }
    if (splitOverfull(moves.length)) return;
    const period = loop ? Number(steps[loop.at].periodMs) || 0 : Infinity;
    if (moves.some((move) => move.t >= period)) {
      sayOnStage(`Its moves run past the loop's ${period} ms pass, so it cannot be split inside the loop.`, "error");
      return;
    }
    const write = () => moves.map((move) => ({
      t: move.t,
      type: "body",
      part: move.part,
      ...(move.shape === "open" ? {} : { shape: move.shape }),
      ...(step.howFar === undefined ? {} : { howFar: step.howFar }),
      ...(move.shape === "flutter" ? { flutterMs } : {}),
    }));
    const place = splitPlace(loop, at);
    if (splitRefused(write, place)) return;

    const made = write();
    historyPush();
    place(steps, made);
    landed(made);
    // A written step has no full-throw time or easing of its own, so what
    // the Gesture said of either is gone, and the stage says so.
    if (step.speedMs || step.easing) sayOnStage("The steps move at each part's own speed and easing.");
  };

  // The keys a step holds a beat or a span of beats in (ADR 0058).
  const BEAT_KEYS = ["beat", "spanBeats", ...GESTURE_BEATS];

  // Split into steps, on a sequence inside this one: the step that names the
  // phrase is replaced by the phrase's own steps, in one entry of the
  // history. It is what the droid does each time the routine runs (the
  // splice, src/seq_store.cpp), done once here and then the builder's to
  // change: the phrase is no longer linked, and a later edit of it does not
  // reach this routine.
  //
  // What is written:
  //   - the phrase's steps before its end step, each timed from where the
  //     phrase step starts. A step a loop repeats is timed from the pass, so
  //     it keeps its time and its loop keeps it (stepUnits());
  //   - and of those, only the ones that start before this routine's end
  //     step: the droid cuts what it splices in past the end
  //     (seqStoreSplicePhrase(), include/seq_store_util.h), as a Gesture's
  //     moves past the end are left out of its split. A loop goes or stays
  //     with the steps it repeats;
  //   - at the milliseconds the droid runs them at, by the phrase's own
  //     tempo, and with no beat of their own: this routine's grid is not the
  //     phrase's. That reading (loadPhrases()) also states the extent a
  //     repeating Gesture took from the phrase's end, and the pace one took
  //     from the phrase's tempo;
  //   - a Gesture's pace where the phrase has no tempo, which that reading
  //     leaves unsaid: there it is a Gesture's own default, and here, unsaid,
  //     it would become one beat of this routine's tempo (parseStepBeats(),
  //     src/seq_json.cpp);
  //   - a sequence inside the phrase as it is: one step, still linked.
  //
  // The phrase is already read - an unread one offers no split - so nothing
  // here waits on the droid, and the one entry is made in one go.
  //
  // More steps than a sequence holds is refused first (splitOverfull()). Then
  // it is tried on a copy (splitRefused()): a result Protocol Check refuses -
  // a sequence that would land inside a loop - lands nothing, and the stage
  // says Protocol Check's reason.
  const splitPhrase = () => {
    const picked = pickedStep();
    if (!picked || picked.step.type !== "sequence" || historyBusy()) return;
    const { at, step } = picked;
    const phrase = phraseRead(step.ref);
    if (!phrase) return;
    const steps = stageSteps();
    const startsAt = Number(stageRun()[at].t) || 0;
    const phraseEnd = phrase.steps.findIndex((each) => each?.type === "end");
    const whole = phraseEnd === -1 ? phrase.steps : phrase.steps.slice(0, phraseEnd);
    if (whole.length === 0) {
      sayOnStage(`${phraseName(step)} has no steps before its end.`, "error");
      return;
    }
    // A step a loop repeats is timed from the pass, where the end is not.
    const loop = loopRepeating(steps, at);
    const endAt = steps.findIndex((each) => each?.type === "end");
    const last = loop || endAt === -1 ? Infinity : Number(steps[endAt].t) || 0;
    const kept = stepUnits(whole).filter((unit) => unit.t + startsAt < last);
    if (kept.length === 0) {
      sayOnStage(`${phraseName(step)} starts at the end, so it adds no steps.`, "error");
      return;
    }
    const runs = kept.flatMap((unit) => whole.slice(unit.at, unit.at + unit.size));
    const outer = new Set(stepUnits(runs).map((unit) => unit.at));
    const timed = phrase.seq.tempo !== undefined && SeqProtocolCheck.validateTempo(phrase.seq.tempo).ok;
    const write = () => runs.map((each, index) => {
      const made = JSON.parse(JSON.stringify(each));
      BEAT_KEYS.forEach((key) => delete made[key]);
      if (made.type === "gesture" && !made.stepMs && !timed) made.stepMs = window.SeqGesture.STEP_DEFAULT_MS;
      if (outer.has(index)) made.t = (Number(made.t) || 0) + startsAt;
      return made;
    });
    // A loop cannot repeat a sequence (Protocol Check), and a routine that
    // has one doing so is split by the same rule as any step a loop repeats.
    const place = splitPlace(loop, at);
    if (splitOverfull(runs.length) || splitRefused(write, place)) return;

    const made = write();
    historyPush();
    place(steps, made);
    landed(made);
  };

  // The one act, by what is picked: a body Gesture or a sequence.
  const splitPicked = () => {
    if (pickedStep()?.step.type === "sequence") splitPhrase();
    else splitGesture();
  };

  // A pill held: `ghost` is the pill that follows the pointer once the press
  // has become a drag.
  let libraryDrag = null;

  const libraryMove = (event) => {
    if (!libraryDrag || (event.pointerId !== undefined && event.pointerId !== libraryDrag.pointer)) return;
    const far = Math.abs(event.clientX - libraryDrag.x0) + Math.abs(event.clientY - libraryDrag.y0) > LIBRARY_DRAG_PX;
    if (!libraryDrag.ghost && far) {
      libraryDrag.ghost = document.createElement("div");
      libraryDrag.ghost.className = "seq-lib-ghost";
      libraryDrag.ghost.textContent = libraryName(libraryDrag.lib);
      document.body.appendChild(libraryDrag.ghost);
    }
    if (!libraryDrag.ghost) return;
    libraryDrag.ghost.setAttribute("style", `left:${event.clientX + 10}px;top:${event.clientY + 8}px`);
    sessionTimeline?.aim(event, libraryDrag.lib === END_PILL);
  };

  // Let go of the pill. Over the lanes it lands; anywhere else, and when the
  // drag is cancelled, nothing does.
  const libraryStop = (event = null) => {
    window.removeEventListener("pointermove", libraryMove);
    window.removeEventListener("pointerup", libraryUp);
    window.removeEventListener("pointercancel", libraryCancel);
    const held = libraryDrag;
    libraryDrag = null;
    held?.ghost?.remove();
    const at = held?.ghost && event && sessionTimeline ? sessionTimeline.aim(event, held.lib === END_PILL) : null;
    sessionTimeline?.aim(null);
    return at === null ? null : { lib: held.lib, at };
  };

  function libraryUp(event) {
    if (!libraryDrag || (event.pointerId !== undefined && event.pointerId !== libraryDrag.pointer)) return;
    const drop = libraryStop(event);
    if (drop && !historyBusy()) dropOnTimeline(drop.lib, drop.at);
  }

  function libraryCancel(event) {
    if (libraryDrag && event?.pointerId !== undefined && event.pointerId !== libraryDrag.pointer) return;
    libraryStop();
  }

  const libraryGrab = (event) => {
    const pill = event.target?.closest?.("[data-lib]");
    if (!pill || libraryDrag || event.button > 0) return;
    libraryDrag = { lib: pill.dataset.lib, x0: event.clientX, y0: event.clientY, pointer: event.pointerId, ghost: null };
    window.addEventListener("pointermove", libraryMove);
    window.addEventListener("pointerup", libraryUp);
    window.addEventListener("pointercancel", libraryCancel);
  };

  // A press that was not a drag, by the pointer or from the keyboard.
  const libraryPress = (event) => {
    const pill = event.target?.closest?.("[data-lib]");
    if (pill) sayOnStage(`${libraryName(pill.dataset.lib)}: drag it onto the timeline to add it.`);
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
    stopTakePoll();
    libraryStop();
    closeSessionTimeline();
    els.editorView.classList.add("hidden");
    currentEditingSeq = null;
    gestureMoreOpen = false;
    forgetPhrases();
    forgetTakeFiles();
    Object.assign(editorState, {
      original: null, current: null, isNew: false, tuningFactory: null,
      half: "opens", tab: "block", saved: false,
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
    showModal(els.modalDiscard, els.modalDiscardKeep);
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

  // "an alert", "a happy": a sound category behind its article.
  const aOrAn = (word) => `${/^[aeiou]/.test(word) ? "an" : "a"} ${word}`;

  // Plain-English preview text for each step type
  const stepPreview = (step) => {
    switch (step.type) {
      case "audio":
        return `Play sound (${fieldOf(step, "cmd")})`;
      case "dome": {
        const cmd = step.cmd || "";
        // One of the four light commands, from its fields (lightFields()).
        // One too short to say what it is for is shown as it is stored:
        // lightFields() would fill what is missing from a new step's.
        const light = lightFields(cmd);
        const said = cmd.split(":").length;
        if (light?.kind === "DV") return `Visual preset: ${lightWord("presets", light.preset)}`;
        if (light?.kind === "DL") {
          if (said < 3) return `Logic/PSI: ${cmd.slice(3)}`;
          return `${lightWord("targets", light.target)}: ${lightWord("modes", light.mode)}`
            + (light.color !== "DEFAULT" ? `, ${lightWord("colors", light.color)}` : "")
            + (light.seconds ? `, ${light.seconds}s` : "");
        }
        if (light?.kind === "DT") {
          if (said < 5) return `Logic text: ${cmd.slice(3)}`;
          // A line break is shown as a slash.
          return `${lightWord("textTargets", light.target)} text: "${light.text.replace(/\n/g, " / ")}"`;
        }
        if (light?.kind === "DH") {
          if (said < 3) return `Holo: ${cmd.slice(3)}`;
          return `${lightWord("holoTargets", light.target)}: ${lightWord("holoEffects", light.effect)}`
            + (light.color !== "DEFAULT" ? `, ${lightWord("holoColors", light.color)}` : "")
            + (light.count ? `, ${light.count}` : "");
        }
        // A panel move, by the one reading of what that is (panelIntent()):
        // the inspector and the step's name read it the same way, so a
        // command without its colon is a Dome command in all three.
        const intent = panelIntent(step);
        if (intent) {
          const [, action, target] = intent;
          const actionLabel = action === "OP" ? "Open" : action === "CL" ? "Close" : "Flutter";
          const targetLabel = target === "00" ? "all panels" : target === "14" ? "top group" : target === "15" ? "bottom group" : target.startsWith("P") ? `pie ${target}` : `ring ${target}`;
          return `${actionLabel} ${targetLabel} (:${action}${target})`;
        }
        // A Dome command, as it is stored. An empty one says no command: it
        // sends none, and Protocol Check refuses it until one is typed.
        return cmd ? `Dome command ${cmd}` : "Dome command";
      }
      case "domeRotate": {
        const speedPct = fieldOf(step, "speedPct");
        const durationMs = fieldOf(step, "durationMs");
        if (speedPct === 0) {
          return "Stop dome (neutral)";
        }
        const direction = speedPct < 0 ? "left" : "right";
        const speed = Math.abs(speedPct);
        return `Rotate ${direction} at ${speed}% for ${durationMs}ms`;
      }
      case "domeBearing": {
        const target = fieldOf(step, "target");
        const known = domeBearingTargets().find((each) => each.id === target);
        return target === SeqProtocolCheck.DOME_BEARING_FRONT ? "Dome to front" : `${known ? known.words : target || "a part"} to front`;
      }
      case "loop": {
        const body = fieldOf(step, "body");
        const periodMs = fieldOf(step, "periodMs");
        const durationMs = fieldOf(step, "durationMs");
        return `Repeat next ${body} steps every ${periodMs}ms for ${durationMs}ms`;
      }
      case "random": {
        const setMap = { ring: "ring panels", pie: "pie panels", all: "all panels", hold: "hold" };
        const set = fieldOf(step, "set");
        const moveMs = fieldOf(step, "moveMs");
        const setLabel = setMap[set] || set;
        return `Random flutter on ${setLabel} (move ${moveMs}ms)`;
      }
      case "audioCat": {
        const category = fieldOf(step, "category");
        return `Play ${aOrAn(category)} sound (fallback ${audioFallbackLabel(fieldOf(step, "fallback"))})`;
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
        // that way here (GLOSSARY.md "Move Shape").
        const part = catalogPart(step.part);
        const { shape, words } = moveOf(step);
        const howFar = step.howFar ? `, ${step.howFar}%` : "";
        return `${words[shape] || shape} ${part ? part.name : step.part || "a part"}${howFar}`;
      }
      case "backgroundTrack": {
        const cmd = fieldOf(step, "cmd");
        return cmd ? `Background Track (${cmd})` : "Background Track";
      }
      case "backgroundTrackStop":
        return "Stop Background Track";
      case "end":
        return "End of sequence";
      default:
        return "Unknown step";
    }
  };

  // ---------------------------------------------------------------------------
  // The kinds of step, as their fields: what a new step of each kind starts
  // as, the bounds its numbers are offered within, and the choices its pickers
  // hold. One set, read by the Picked block tab and by a drop, so a kind
  // never has two sets of bounds. Protocol Check has the rules
  // (data/seq_protocol_check.js); these are what the controls offer.
  // ---------------------------------------------------------------------------
  const stepTypeDefaults = {
    audio: { cmd: "$H" },
    dome: { cmd: ":OP00" },
    domeRotate: { speedPct: 0, durationMs: 0 },
    domeBearing: { target: SeqProtocolCheck.DOME_BEARING_FRONT },
    loop: { body: 2, periodMs: 1846, durationMs: 14000 },
    random: { set: "ring", mode: "flutter", moveMs: 300, jitterMs: 500, distinct: true },
    audioCat: { category: "alert", fallback: "scream" },
    // The Star Wars theme, under the routine at a level a vocal is heard
    // over. Bounded, as a sound step is: it says no boundAudio.
    backgroundTrack: { cmd: "$W", vol: 12 },
    backgroundTrackStop: {},
    gesture: { set: "ring", spread: "wave" },
    sequence: {},
    end: {},
  };

  const STEP_LIMITS = {
    t: [0, 120000],
    howFar: [1, 100],
    speed: [0, 100],
    turnMs: [0, 120000],
    body: [1, 96],
    periodMs: [100, 60000],
    loopMs: [100, 120000],
    moveMs: [0, 5000],
    jitterMs: [0, 2000],
    // A light mode's, a logic text's and a holo flash's seconds, and how many
    // times a holo wags or nods: Protocol Check holds each to 0..99.
    lightCount: [0, 99],
    scroll: [0, 9],
    // A Background Track's volume, the interface's 0-30 (Protocol Check's
    // BACKGROUND_TRACK_VOL_MAX).
    vol: [0, 30],
  };
  // How many characters a logic text holds, a line break among them. The box
  // counts characters and the droid counts bytes (Protocol Check, the 32-byte
  // rule), so a text with a letter outside ASCII fits the box sooner than it
  // fits the droid: the verdict says when it is too long.
  const LIGHT_TEXT_CHARS = 32;
  const limits = ([min, max]) => `min="${min}" max="${max}"`;
  // How long a dome turn runs when a stop is first given a speed: a turn with
  // a speed and no time is refused, and a new Spin Dome is the neutral stop.
  const TURN_STARTS_MS = 1000;

  // A field of a step as anything here reads it - an inspector row, the words
  // on a block: the step's own value, or what a step of its kind
  // starts as. The one fallback, so no reader has a default of its own.
  const fieldOf = (step, field) => step[field] ?? stepTypeDefaults[step.type]?.[field];

  const AUDIO_CATEGORIES = ["alert", "chatty", "general", "happy", "humming", "processing", "sad", "sentimental", "scream", "surprised", "whistle"];
  // A Random Flutter's set - "hold" reuses the pick of the one before it
  // (SLOTSET_HOLD, include/sequence_engine.h) - and what it does to the pick.
  const RANDOM_SETS = ["ring", "pie", "all", "hold"];
  const RANDOM_MODES = ["flutter", "open", "close"];

  // ---------------------------------------------------------------------------
  // The dome's four light commands. A dome step holds one as its `cmd`; the
  // Picked block tab reads it with lightFields() and writes it with
  // lightCmd(), so the grammar is spelled once (Protocol
  // Check has the rules, data/seq_protocol_check.js):
  //   DV:<preset>
  //   DL:<target>:<mode>[:<color>[:<seconds>]]
  //   DT:<target>:<color>:<seconds>:<speed>:<encodedText>
  //   DH:<target>:<effect>[:<color>[:<secondsOrCount>]]
  // `starts` is the command a new step of that kind holds.
  // ---------------------------------------------------------------------------
  const DOME_SUBMODES = {
    DV: { name: "Visual Preset", starts: "DV:ROCKMARCH" },
    DL: { name: "Logic / PSI Mode", starts: "DL:LOGIC:NORMAL" },
    DT: { name: "Logic Text", starts: "DT:LOGIC:DEFAULT:5:0:" },
    DH: { name: "Holo Effect", starts: "DH:A:FLASH" },
  };

  // A dome command as the dome itself reads it, sent as written: any dome
  // step whose `cmd` is neither one of the four above nor a panel move
  // (panelIntent()). It is a kind by what the command says and nothing else,
  // so one typed into a light command or a panel move is that from the next
  // draw on, with that kind's rows; nothing pins a step as raw.
  //
  // `starts` is what one dropped from More steps holds: @0T1, the logic
  // displays' reset. Three reasons, and a new command there must keep all
  // three. The droid's Protocol Check and the browser's both accept it
  // (src/protocol_check.cpp classifyDome(), _validateDomeStep()), so the
  // command never makes a routine the droid refuses. The droid counts it as
  // starting no effect (FX_NONE there), so a step nobody has typed into yet
  // does no more than put the logic displays back to normal. And neither
  // lightKind() nor panelIntent() reads it, so the inspector shows the box to
  // type in. Not stepTypeDefaults.dome, which is a panel move.
  const DOME_COMMAND = { name: "Dome command", starts: "@0T1" };
  // The longest command the droid takes, which is Protocol Check's to say.
  // The box holds no more, so a command is never typed past what Save accepts.
  const DOME_COMMAND_CHARS = SeqProtocolCheck.CMD_CHARS_MAX;

  // Which of the four a command is - "DV", "DL", "DT" or "DH" - or null.
  const lightKind = (cmd) => {
    const kind = /^(D[VLTH]):/.exec(String(cmd || ""))?.[1];
    return kind && DOME_SUBMODES[kind] ? kind : null;
  };

  // A logic text travels percent-encoded, so a colon in it is not read as the
  // next field. Only what the droid needs is escaped, because the encoded
  // text is held to 40 characters and every escape spends three of them
  // (docs/dome-visual-authoring-contract.md, "DT"; the decoder is
  // src/protocol_check.cpp): % is %25, : is %3A, a line break is %0A, and
  // printable ASCII otherwise stays as typed - a space, a comma, a question
  // mark. Anything else goes as the bytes of its UTF-8. A text that was not
  // stored percent-encoded is shown as it is stored.
  //
  // encodeLightText() answers null for the one text that cannot be encoded:
  // one holding half of a two-part character (a lone surrogate, which is what
  // a pictograph cut in two leaves behind). encodeURIComponent() throws a
  // URIError on it. Writing the text raw instead would put an unencoded
  // character into the command and say nothing, so the edit is refused and
  // whoever asked says why (LIGHT_TEXT_REFUSED).
  const LIGHT_TEXT_REFUSED = "That text has a broken character in it. Type it again.";
  // Read by code point (the `u` flag), so the two halves of one character are
  // encoded together and only a half on its own throws.
  const LIGHT_TEXT_ESCAPED = /[%:]|[^\x20-\x7E]/gu;
  const encodeLightText = (text) => {
    try {
      return text.replace(LIGHT_TEXT_ESCAPED, (character) => encodeURIComponent(character));
    } catch (error) {
      if (error instanceof URIError) return null;
      throw error;
    }
  };
  const decodeLightText = (encoded) => {
    try {
      return decodeURIComponent(encoded);
    } catch (e) {
      return encoded;
    }
  };

  // A light command as its fields, every one a string, or null when `cmd` is
  // none of the four. A field the command leaves off reads as what leaving it
  // off means: the color DEFAULT, and "" for no duration or count. A field
  // the grammar requires and the command lacks reads as a new step's.
  //
  // A logic text is two fields: `text` as a builder reads it, and `encoded`,
  // the stored slot exactly as it is. One text has many encodings - a space
  // or %20, %3A or %3a, a comma or %2C - and they differ in length against a
  // cap, so lightCmd() writes the stored one back and encodes afresh only
  // when there is none: a text just typed.
  const lightFields = (cmd) => {
    const kind = lightKind(cmd);
    if (!kind) return null;
    const parts = String(cmd).split(":");
    const starts = DOME_SUBMODES[kind].starts.split(":");
    const at = (i) => parts[i] || starts[i];
    switch (kind) {
      case "DV":
        return { kind, preset: String(cmd).slice(3) };
      case "DL":
        return { kind, target: at(1), mode: at(2), color: parts[3] || "DEFAULT", seconds: parts[4] ?? "" };
      case "DT": {
        const encoded = parts.slice(5).join(":");
        return { kind, target: at(1), color: at(2), seconds: at(3), speed: at(4), text: decodeLightText(encoded), encoded };
      }
      default:
        return { kind, target: at(1), effect: at(2), color: parts[3] || "DEFAULT", count: parts[4] ?? "" };
    }
  };

  // The command those fields spell, or null for a logic text that cannot be
  // encoded. The fields are positional, so a duration needs its color slot: a
  // set duration with the color DEFAULT writes DEFAULT there, and with the
  // color DEFAULT and no duration both are left off - stored only where it
  // differs.
  const lightCmd = (fields) => {
    const tail = (color, number) => (number !== "" ? `:${color}:${number}` : color !== "DEFAULT" ? `:${color}` : "");
    switch (fields.kind) {
      case "DV":
        return `DV:${fields.preset}`;
      case "DL":
        return `DL:${fields.target}:${fields.mode}${tail(fields.color, fields.seconds)}`;
      case "DT": {
        const encoded = fields.encoded ?? encodeLightText(fields.text);
        return encoded === null ? null : `DT:${fields.target}:${fields.color}:${fields.seconds}:${fields.speed}:${encoded}`;
      }
      default:
        return `DH:${fields.target}:${fields.effect}${tail(fields.color, fields.count)}`;
    }
  };

  // What a loop cannot repeat: another loop, a sequence inside this one, and
  // the end.
  const NOT_REPEATED = ["loop", "sequence", "end"];

  // How many steps the loop at `at` may repeat: the ones after it, up to the
  // first it cannot repeat, and no more than a loop holds. The droid refuses
  // a loop that reaches past them ("loop body overruns the branch",
  // src/protocol_check.cpp). A loop with nothing after it still asks for one.
  const loopReach = (steps, at) => {
    let room = 0;
    while (room < STEP_LIMITS.body[1] && steps[at + 1 + room] && !NOT_REPEATED.includes(steps[at + 1 + room].type)) room += 1;
    return [STEP_LIMITS.body[0], Math.max(STEP_LIMITS.body[0], room)];
  };

  // A dome turn is stored as one signed speed: negative left, positive right,
  // zero the neutral stop, which runs for no time.
  const turnOf = (direction, speed, durationMs) => {
    const abs = Math.abs(speed) || 0;
    if (direction === "stop" || abs === 0) return { speedPct: 0, durationMs: direction === "stop" ? 0 : durationMs };
    return { speedPct: direction === "left" ? -abs : abs, durationMs };
  };

  // Plain-English type names. An operator surface carries no pictograph
  // beside them (ADR 0066): the word does the whole job.
  const stepTypeName = {
    audio: "Sound",
    dome: "Panel Action",
    domeRotate: "Spin Dome",
    domeBearing: "Turn Dome To",
    loop: "Servo Loop",
    random: "Random Flutter",
    audioCat: "Sound Category",
    backgroundTrack: "Background Track",
    backgroundTrackStop: "Stop Background Track",
    gesture: "Gesture",
    sequence: "Sequence",
    body: "Body Step",
    end: "Sequence End",
  };

  // What kind of dome step its command makes it, by name: one of the four
  // light commands, a Panel Action, or a Dome command.
  const domeStepName = (step) =>
    DOME_SUBMODES[lightKind(step.cmd)]?.name || (panelIntent(step) ? stepTypeName.dome : DOME_COMMAND.name);

  // ---------------------------------------------------------------------------
  // Putting a step on a beat (ADR 0060): the builder picks the beat, from a
  // list of every beat with its bar number, rather than dragging a time until
  // it lands near one. A beat is set on the step directly and the time
  // resolved from it (setStepBeat()).
  // ---------------------------------------------------------------------------
  const tempoOf = () => {
    const tempo = editorState.current?.tempo;
    return tempo && SeqProtocolCheck.validateTempo(tempo).ok ? tempo : null;
  };

  // How far the half on the stage reaches, for how long the beat list runs.
  const stageReachMs = () => Math.max(...stageRun().map((step) => Number(step?.t) || 0), 0);

  // The beat a step is placed on, in words, or "" when it is on none.
  const beatWords = (step) => {
    const tempo = tempoOf();
    if (!tempo || !Number.isInteger(step.beat) || !window.SeqTempo) return "";
    return window.SeqTempo.beatWords(tempo, step.beat);
  };

  // Steps whose duration can be a span of beats: Protocol Check's rule.
  const spansBeats = (step) => SeqProtocolCheck.spansBeats(step);

  // ---------------------------------------------------------------------------
  // A sequence inside a sequence (ADR 0046). A step names its phrase by the
  // phrase's stable reference - a saved sequence's `id`, or a factory
  // sequence's name, which never changes - and shows it by its CURRENT name,
  // so a rename orphans nothing. A saved sequence with no id yet cannot be
  // picked until it is saved again, which mints one.
  //
  // The sequence being edited is never offered to itself: not by the name it
  // has now, and not by its id, which is what the droid compares a reference
  // with (protocolCheckNesting(), src/protocol_check.cpp) and which a rename
  // in this edit does not change.
  // ---------------------------------------------------------------------------
  const phraseChoices = () => [
    ...sequences
      .filter((x) => x.id && x.id !== editorState.current?.id && x.name !== editorState.current?.name && x.toggleGroup === "none")
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

  // ---------------------------------------------------------------------------
  // The phrases this routine names, as the droid holds them (#441): what the
  // timeline draws a linked block from, and what Split into steps writes out.
  //
  // `read` is reference -> { url, seq, steps } - where the reference was read
  // from, the sequence as the droid sent it, and its steps at the
  // milliseconds they run at, resolved with the phrase's own tempo as the
  // droid resolves them when it splices the phrase in - or { url, failed }
  // for a read that failed. A reference not in it has not been read. It
  // lasts one edit: closing the editor starts a new one (forgetPhrases()), and
  // an answer that lands after that is for a session that is gone.
  //
  // A reference is read from where it resolves (phraseSource()), and that can
  // change while the editor is open: a Factory sequence read before the
  // Learned list answered turns out to have a tuned copy, which is the one
  // the droid runs. An entry read from somewhere else than its reference
  // resolves to now is dropped when a list answers (phrasesListed()), and
  // read again.
  //
  // ONE REQUEST AT A TIME. Each read is a JSON document the droid builds, and
  // a routine names up to eight phrases; `reading` is the one loop under way.
  // `leaving` cancels the read in flight when the session closes, so the
  // droid is not left building an answer nobody will read.
  // ---------------------------------------------------------------------------
  const newPhrases = () => ({ read: new Map(), reading: false, leaving: new AbortController() });
  let phrases = newPhrases();
  const forgetPhrases = () => {
    phrases.leaving.abort();
    phrases = newPhrases();
  };

  // Where the droid finds a reference when it runs it (nestResolve(),
  // src/seq_store.cpp): a saved sequence first, by its id or by its name -
  // which is how a tuned Factory sequence stands in for the factory one - and
  // then the Factory list by name. Null for one that is not on this droid.
  const phraseSource = (ref) => {
    const learned = sequences.find((x) => (x.id && x.id === ref) || x.name === ref);
    if (learned) return { name: learned.name, toggleGroup: learned.toggleGroup || "none", url: `/api/seq?name=${encodeURIComponent(learned.name)}` };
    const factory = builtins.find((x) => x.name === ref);
    return factory
      ? { name: factory.name, toggleGroup: factory.toggleGroup || "none", url: `/api/seq/builtins?name=${encodeURIComponent(factory.name)}` }
      : null;
  };

  const phraseRead = (ref) => {
    const entry = phrases.read.get(ref);
    return entry && !entry.failed ? entry : null;
  };
  const phraseSteps = (ref) => phraseRead(ref)?.steps || null;

  // Protocol Check on the routine being edited, or on a copy of it an edit is
  // tried on: with what this page knows of the sequences it names, so the
  // rules the droid applies to them at save are applied here too
  // (SeqProtocolCheck._validateNesting()). Nothing is said of whether a
  // phrase is on the droid until both lists have answered. A phrase is found
  // under any reference that resolves to where it was read from: its id and
  // its name are the one sequence. And with the Factory catalog, so a
  // sequence under a Factory name is held to that Factory sequence's
  // interrupt group; until the catalog has answered there is none to hold
  // it to.
  const routineVerdict = (seq) => SeqProtocolCheck.validateSequence(seq, {
    self: { id: editorState.current?.id, name: editorState.current?.name },
    factory: (name) => builtins.find((entry) => entry.name === name) || null,
    listed: (ref) => (listsAnswered() ? phraseSource(ref) || false : null),
    phrase: (ref) => {
      const url = phraseSource(ref)?.url;
      const read = phraseRead(ref) || [...phrases.read.values()].find((entry) => !entry.failed && entry.url === url);
      return read ? { steps: read.seq.steps, toggleGroup: read.seq.toggleGroup || "none" } : null;
    },
  });

  // Read every phrase the routine names that has not been read, one after the
  // other. It is not an edit and records nothing: when a phrase lands, the
  // timeline draws the routine again and the inspector follows it; when a
  // read fails, the inspector is written again all the same.
  //
  // A read that fails is said once on the stage, and the phrase stays the
  // mark on the Sequence row it was. It is not asked for again until the
  // builder drops or picks that phrase again (phraseAgain()).
  const loadPhrases = async () => {
    const mine = phrases;
    if (mine.reading) return;
    mine.reading = true;
    try {
      for (;;) {
        if (mine !== phrases || !editorState.current) return;
        // Either half can name one, dropped here or written by hand, and the
        // droid splices its phrases into both (seqStorePrepare(),
        // src/seq_store.cpp).
        const ref = [...(editorState.current.steps || []), ...(editorState.current.closeSteps || [])]
          .map((step) => (step?.type === "sequence" ? step.ref : null))
          .find((each) => each && !mine.read.has(each) && phraseSource(each));
        if (!ref) return;
        const source = phraseSource(ref);
        let entry = { url: source.url, failed: true };
        let refused = "";
        try {
          const seq = (await PAApi.get(source.url, { signal: mine.leaving.signal })).data;
          if (seq && Array.isArray(seq.steps)) entry = { url: source.url, seq, steps: SeqProtocolCheck.resolveBeats(seq).steps };
          else refused = `The droid sent ${source.name} back with no steps.`;
        } catch (error) {
          // Cancelled with the session it was read for: nothing failed.
          if (mine !== phrases) return;
          console.error(`[seq] reading ${source.name}, a sequence inside this one:`, error);
          refused = `Could not read ${source.name}: ${PAApi.messageFor(error)}`;
        }
        if (mine !== phrases) return;
        // A list answered while this was being read, and the reference
        // resolves somewhere else now: this answer is not kept, and the loop
        // reads it from there.
        if (phraseSource(ref)?.url !== source.url) continue;
        mine.read.set(ref, entry);
        if (!entry.failed) {
          // What was read counts toward the steps the routine holds once
          // spliced, so the verdict is read again with it.
          updateValidationSummary();
          sessionTimeline?.refresh(stageContext());
        } else {
          sayOnStage(refused, "error");
          // The timeline has nothing new to draw, so nothing has told the
          // inspector: a picked phrase now says its read failed.
          showPicked(sessionTimeline ? sessionTimeline.picked() : []);
        }
      }
    } finally {
      mine.reading = false;
    }
  };

  // The builder dropped or picked this phrase: one whose read failed is asked
  // for again by the next loadPhrases().
  const phraseAgain = (ref) => {
    if (phrases.read.get(ref)?.failed) phrases.read.delete(ref);
  };

  // ---------------------------------------------------------------------------
  // The takes this routine holds, as the droid holds their files (#442): what
  // the timeline draws a take's block from and the Rehearsal reads a take's
  // overlaps by. A take file never changes once kept - a new performance is a
  // new id - so each is read once a session, by its id, from
  // GET /api/take/file, kept or not yet saved alike.
  //
  // `read` is take id -> its facts (readTakeFile()), or { failed } for a read
  // that failed, which is said once on the stage and not asked for again this
  // session. ONE REQUEST AT A TIME, and the read in flight is cancelled with
  // the session, as the phrases' are.
  // ---------------------------------------------------------------------------
  const newTakeFiles = () => ({ read: new Map(), reading: false, leaving: new AbortController() });
  let takeFiles = newTakeFiles();
  const forgetTakeFiles = () => {
    takeFiles.leaving.abort();
    takeFiles = newTakeFiles();
  };

  // A take file's facts, read as the droid writes it (include/take_capture.h,
  // little-endian): its Parts by id, its length in ms, and each sample as
  // {t, part, at} - ms into the take, the index of its Part, and the share of
  // that Part's throw it commanded (0 closed, 1 open). Null for bytes that are
  // not a take this page can read.
  const TAKE_FIXED_BYTES = 16;
  const TAKE_PART_ID_BYTES = 11;
  const readTakeFile = (buffer) => {
    if (!(buffer instanceof ArrayBuffer) || buffer.byteLength < TAKE_FIXED_BYTES) return null;
    const view = new DataView(buffer);
    const magic = String.fromCharCode(...new Uint8Array(buffer, 0, 4));
    const rateHz = view.getUint8(5);
    const partCount = view.getUint8(6);
    const lengthTicks = view.getUint32(8, true);
    const sampleCount = view.getUint32(12, true);
    const head = TAKE_FIXED_BYTES + partCount * TAKE_PART_ID_BYTES;
    if (magic !== "PATK" || view.getUint8(4) !== 1 || rateHz === 0 || buffer.byteLength < head + sampleCount * 4) return null;
    const decoder = new TextDecoder();
    const parts = Array.from({ length: partCount }, (_, p) =>
      decoder.decode(new Uint8Array(buffer, TAKE_FIXED_BYTES + p * TAKE_PART_ID_BYTES, TAKE_PART_ID_BYTES)).replace(/\0[\s\S]*$/, ""));
    const samples = Array.from({ length: sampleCount }, (_, k) => {
      const sample = view.getUint32(head + k * 4, true);
      return { t: ((sample >>> 16) * 1000) / rateHz, part: (sample >>> 10) & 0x3f, at: (sample & 0x3ff) / 1000 };
    });
    return { parts, lengthMs: (lengthTicks * 1000) / rateHz, samples };
  };

  const takeFacts = (id) => {
    const entry = takeFiles.read.get(id);
    return entry && !entry.failed ? entry : null;
  };

  // Read every take the routine names that has not been read, one after the
  // other. It is not an edit and records nothing: when one lands, the
  // timeline draws its block and the Rehearsal reads it.
  const loadTakeFiles = async () => {
    const mine = takeFiles;
    if (mine.reading) return;
    mine.reading = true;
    try {
      for (;;) {
        const seq = editorState.current;
        if (mine !== takeFiles || !seq?.id) return;
        const index = (seq.takes || []).findIndex((each) => typeof each?.id === "string" && !mine.read.has(each.id));
        if (index === -1) return;
        const { id } = seq.takes[index];
        let entry = { failed: true };
        let refused = "";
        try {
          const answer = await PAApi.getBytes(
            `/api/take/file?owner=${encodeURIComponent(seq.id)}&take=${encodeURIComponent(id)}`,
            { signal: mine.leaving.signal, timeoutMs: 10000 },
          );
          entry = readTakeFile(answer.data) || entry;
          if (entry.failed) refused = `Take ${index + 1} is not a take this page can read.`;
        } catch (error) {
          // Cancelled with the session it was read for: nothing failed.
          if (mine !== takeFiles) return;
          console.error(`[seq] reading take ${id}:`, error);
          refused = `Could not read take ${index + 1}: ${PAApi.messageFor(error)}`;
        }
        if (mine !== takeFiles) return;
        mine.read.set(id, entry);
        if (entry.failed) sayOnStage(refused, "error");
        updateValidationSummary();
        sessionTimeline?.refresh(stageContext());
      }
    } finally {
      mine.reading = false;
    }
  };

  // A stable id for a sequence being saved that has none: eight lowercase hex
  // digits, never changed after (protocolCheckSeqIdValid()).
  const mintSequenceId = () => {
    const bytes = new Uint8Array(4);
    (window.crypto || globalThis.crypto).getRandomValues(bytes);
    return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
  };

  // The beats a step can be put on, to pick from: each bar is its number and
  // its beats as one joined bar, the first beat weighted; the bars sit side
  // by side and wrap. `reachMs` is how far the routine it is in reaches,
  // `attrs(index)` what makes it the inspector's to hear, and `offered(index)`
  // whether the step can be put there at all - a beat it cannot reach is
  // drawn and cannot be pressed.
  const beatBarsHtml = (step, reachMs, attrs, offered = () => true) =>
    `<div class="seq-bars">${window.SeqTempo.bars(tempoOf(), Math.max(reachMs, Number(step.t) || 0))
      .map((bar) => {
        const name = bar.bar === 0 ? "Pickup" : `Bar ${bar.bar}`;
        const beats = bar.beats
          .map(
            (b) =>
              `<button type="button" ${b.strong ? 'class="strong" ' : ""}${attrs(b.index)} aria-pressed="${step.beat === b.index ? "true" : "false"}"${offered(b.index) ? "" : " disabled"} aria-label="${name}, beat ${b.beat}">${b.beat}</button>`,
          )
          .join("");
        return `<span class="seq-bar"><span class="seq-bar-num" aria-hidden="true">${bar.bar === 0 ? "-" : bar.bar}</span><span class="seg seg-sm seq-beats" role="group" aria-label="${name}">${beats}</span></span>`;
      })
      .join("")}</div>`;

  // Put the step at `at` on the stage on beat `beat` (null takes it off), or
  // give its duration a span of `spanBeats` beats (null clears it). The time
  // and the duration are resolved from the tempo, as the droid will.
  const setStepBeat = (at, patch) => {
    const step = { ...stageSteps()[at] };
    if ("beat" in patch) {
      if (patch.beat === null) delete step.beat;
      else step.beat = patch.beat;
    }
    ["spanBeats", ...GESTURE_BEATS].forEach((key) => {
      if (!(key in patch)) return;
      if (patch[key] === null) {
        delete step[key];
        // A Gesture's pace, repeat and extent in beats are written beside
        // the milliseconds they resolve to (resolveBeats()). Cleared, the
        // milliseconds go with them: left behind, the field would read empty
        // while the Gesture still kept that pace.
        if (GESTURE_BEATS.includes(key)) delete step[key.replace("Beats", "Ms")];
      } else {
        step[key] = patch[key];
      }
    });
    // The steps picked on the timeline, by index: this edit writes new steps
    // in their place and changes no step's place in the routine, so the same
    // indices are picked again once it is drawn.
    const pickedSteps = [...new Set(pickedBlocks.flatMap((block) => block.steps))];
    const before = historyBegin();
    stageSteps()[at] = step;
    editorState.current = SeqProtocolCheck.resolveBeats(editorState.current, { written: true });
    historyCommit(before);
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
    edited(`${result.landed} of ${result.total} steps landed on a beat.`);
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
    edited();
  };

  // ---------------------------------------------------------------------------
  // The interrupt group and the close half that goes with it (#441; operator,
  // 2026-10-02). The droid refuses a sequence in a group with no close half,
  // and one outside a group with one (protocolCheck(), src/protocol_check.cpp),
  // so the two are set in the one edit, and one Undo takes both back.
  //
  // startedCloseHalf(): the close half a sequence put in a group starts with.
  // It closes what the opening half leaves standing open, and ends:
  //   - one close per Part, in lane order, each its own command: a dome
  //     panel's own :CLxx, a body Part's Body Step with the shape close. Never
  //     a group close (:CL00, :CL14, :CL15): several servos starting at once
  //     browned the dome out (src/tasks/sequence_catalog.cpp, 2026-06-17);
  //   - one at a time. A body Part's close comes a Cadence Floor after the
  //     step before it: the droid's own, which a builder can set
  //     (SeqRehearsal.cadenceFloor()). A dome panel's comes no sooner than the
  //     dome's measured cadence (domeCadenceMs()), however low that floor is
  //     set: a floor set for the body's Outputs does not change what browned
  //     the dome out, and it is the figure the Rehearsal judges panel moves
  //     by, so a started close half never warns on itself;
  //   - the end step that same gap after the last close. With nothing left
  //     open it is the end step alone, which the droid accepts.
  // The pattern is the factory toggles' own close halves (kPiesCloseSteps),
  // less their sound and holo reset, which are those routines' own choices.
  // What is left open is the timeline's reading (SeqTimeline.leftOpen()); a
  // flutter leaves nothing open, and two things are not in it: a random
  // step's pick, which nobody knows until the droid runs, and what a
  // sequence inside this one leaves open, which the timeline draws as one
  // block and does not read into.
  //
  // It fits by steps: a branch holds 96 by itself (protocolCheckBranch()), so
  // one step per Part and an end is never too many, whatever Opens holds. The
  // file's size is another cap, which setGroup() reads.
  //
  // Answers {steps, stays}: the close half, and the names of the Parts left
  // open that it does not close - a dome Part the dome has no close command
  // for. Or {refused}: why none can be started - the timeline did not load,
  // or the motion model did not, and then the page cannot say how far apart
  // the closes go; closes sent together are what the spacing is there to
  // prevent.
  // ---------------------------------------------------------------------------
  const startedCloseHalf = () => {
    if (!window.SeqTimeline) return { refused: "The timeline did not load. Reload the page to try again." };
    const floor = window.SeqRehearsal?.cadenceFloor(rehearsalContext())?.ms;
    const dome = window.SeqRehearsal?.domeCadenceMs();
    if (!(floor > 0) || !(dome > 0)) return { refused: "The spacing between moves did not load. Reload the page to try again." };
    const steps = [];
    const stays = [];
    let t = 0;
    let gap = 0;
    window.SeqTimeline.leftOpen(editorState.current, rehearsalContext()).forEach(({ part }) => {
      const entry = catalogPart(part);
      const onDome = entry?.half === "dome";
      const cmd = onDome ? window.DomeCommandMap?.resolvePanelCommand(entry.shorthand, "close") : null;
      if (onDome && !cmd) {
        stays.push(entry.name);
        return;
      }
      gap = onDome ? Math.max(floor, dome) : floor;
      if (steps.length > 0) t += gap;
      steps.push(onDome ? { t, type: "dome", cmd } : { t, type: "body", part, shape: "close" });
    });
    steps.push({ t: t + gap, type: "end", ...stepTypeDefaults.end });
    return { steps, stays };
  };

  // Put the sequence in `group`.
  //   - Into a group, with no close half: one is started (startedCloseHalf()).
  //     Pressing the group it is already in does the same for a sequence that
  //     came without one.
  //   - Into another group: the close half it has stays.
  //   - Back to None: the close half is dropped. The list goes back to how
  //     the droid sends a sequence with none - empty, where it sent the key
  //     at all - so None pressed straight after a group leaves no edit.
  // The stage says what became of Closes, because the control is in the
  // drawer and the half is on the stage. Its line is the timeline's, in the
  // bar over the routine; where the timeline did not load there is no such
  // line, and the strip says it.
  //
  // A started close half is refused where it would take the file past what
  // this droid stores for one sequence (SEQ_FILE_MAX_BYTES, which the droid
  // reports and the Rehearsal weighs the routine against): Protocol Check
  // here does not read the size, and Save would be the first to say.
  const setGroup = (group) => {
    if (historyBusy()) return;
    const seq = editorState.current;
    const had = Array.isArray(seq.closeSteps) && seq.closeSteps.length > 0;
    const say = (text, level = "") => (sessionTimeline ? sayOnStage(text, level) : showEditorFeedback(text, level || "info"));
    const made = group !== "none" && !had ? startedCloseHalf() : null;
    if (made?.refused) {
      say(made.refused, "error");
      return;
    }
    const started = made ? made.steps : null;
    if (started && window.SeqRehearsal) {
      const { bytes, maxBytes } = window.SeqRehearsal
        .rehearse({ ...seq, toggleGroup: group, closeSteps: started }, rehearsalContext()).figures;
      if (maxBytes !== null && bytes > maxBytes) {
        // By how much, in bytes: rounded to KB, a file just past the cap
        // reads the same as the cap.
        const cap = `${Number((maxBytes / 1024).toFixed(1))} KB`;
        say(`Closes does not fit: the sequence would be ${countOf(bytes - maxBytes, "byte", "bytes")} over the ${cap} this droid stores.`, "error");
        return;
      }
    }
    const before = historyBegin();
    if (started) seq.closeSteps = started;
    if (group === "none") {
      if (editorState.original && "closeSteps" in editorState.original) seq.closeSteps = [];
      else delete seq.closeSteps;
    }
    seq.toggleGroup = group;
    historyCommit(before);
    paintAuthoredHeader();
    edited();
    if (started) {
      const closes = started.length - 1;
      const stays = made.stays.length === 0 ? ""
        : ` ${made.stays.join(", ")} ${made.stays.length === 1 ? "has no close; it stays" : "have no close; they stay"} open.`;
      say((closes > 0 ? `Closes started: ${countOf(closes, "part closes", "parts close")}, one at a time.`
        : stays ? "Closes started: it only ends."
          : "Closes started: Opens leaves nothing open, so it only ends.") + stays);
    } else if (group === "none" && had) {
      say("Closes is gone. Undo brings it back.");
    }
  };

  // The tempo levels of the track dropped in (#14): the tempo heard, its half,
  // two thirds, three halves and double, each with how strongly the track
  // repeats there against the one heard. Shown while the tempo is the one
  // measured from that file; the levels are not stored, so a reopened
  // sequence offers them again when the track is dropped in again.
  const LEVEL_WORDS = { "1/2": "half", "2/3": "two thirds", "1/1": "heard", "3/2": "three halves", "2/1": "double" };
  const LEVEL_MARKS = { "1/2": "1/2", "2/3": "2/3", "1/1": "", "3/2": "3/2", "2/1": "2x" };
  const paintLevels = () => {
    const row = document.getElementById("seq-editor-levels");
    const seg = document.getElementById("seq-editor-levels-seg");
    if (!row || !seg) return;
    const tempo = editorState.current.tempo;
    const levels = droppedTrack?.levels || [];
    const shown = levels.length > 0 && tempo?.source === "analysed" && tempo.hash === droppedTrack.hash;
    row.classList.toggle("hidden", !shown);
    if (!shown) {
      seg.innerHTML = "";
      return;
    }
    seg.innerHTML = levels
      .map((level, i) => {
        const key = `${level.num}/${level.den}`;
        const mark = LEVEL_MARKS[key] ? `${LEVEL_MARKS[key]} ` : "";
        const fit = `${Math.round(level.strength * 100)}%`;
        return `<button type="button" data-level="${i}" aria-pressed="${level.bpm === tempo.bpm ? "true" : "false"}" aria-label="${level.bpm} BPM, ${LEVEL_WORDS[key]}, ${fit} as strong">${mark}${Number(level.bpm)} <span class="setting-unit">${fit}</span></button>`;
      })
      .join("");
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
    paintLevels();
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
    if (undoBtn) undoBtn.disabled = history.undo.length === 0;
    if (redoBtn) redoBtn.disabled = history.redo.length === 0;
  };

  const edited = (receipt = "") => {
    // Closes is on the stage only where there is a close half, and the close
    // half can go in an edit - the group set to None, or an undo of the edit
    // that started it.
    if (!hasCloseHalf(editorState.current)) editorState.half = "opens";
    paintHalf();
    showRetime(receipt);
    updateValidationSummary();
    paintHistory();
    paintTakes();
    if (sessionTimeline) sessionTimeline.refresh(stageContext());
    // An edit can name a phrase not read yet - a drop, a pick, an undo - so
    // every edit asks; with nothing unread it sends nothing.
    loadPhrases();
    loadTakeFiles();
  };

  // UNDO ACROSS THE SWITCH: an undo or a redo that changes one half's list
  // and not the other's puts that half on the stage, so what it took back is
  // in sight. One that changes neither list or both leaves the stage where it
  // is. Where it leaves no close half, edited() puts Opens there.
  const historyRestore = (snapshot) => {
    const kept = JSON.parse(snapshot);
    const moved = (key) => JSON.stringify(kept[key]) !== JSON.stringify(editorState.current[key]);
    if (moved("steps") !== moved("closeSteps")) editorState.half = moved("steps") ? "opens" : "closes";
    // What the stage last said was said of the edit now taken back.
    sayOnStage("");
    HISTORY_FIELDS.forEach((key) => {
      if (kept[key] === undefined) delete editorState.current[key];
      else editorState.current[key] = kept[key];
    });
    paintAuthoredHeader();
    edited();
  };

  // Neither runs while a block is being dragged on the timeline or a fader
  // in the inspector is held: the gesture's writes are in the routine but not
  // yet an entry, and restoring a copy would replace the very steps it is
  // holding.
  const historyBusy = () => !editorState.current || Boolean(sessionTimeline?.dragging()) || faderRun !== null;

  const undo = () => {
    if (historyBusy()) return;
    if (history.undo.length === 0) return;
    history.redo.push(historyCapture());
    historyRestore(history.undo.pop());
  };

  const redo = () => {
    if (historyBusy()) return;
    if (history.redo.length === 0) return;
    history.undo.push(historyCapture());
    historyRestore(history.redo.pop());
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
    editorState.half = "opens";
    historyReset();
    droppedTrack = null;

    // The dome's layout, for the Rehearsal's panel rules (rehearsalContext()).
    // A read that fails leaves those rules what they can say without it.
    if (window.DomeLayout) {
      window.DomeLayout.load().catch((error) => {
        console.warn("[seq] the Rehearsal could not read the dome's layout:", error);
      });
    }

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
            <button id="seq-editor-test" class="btn btn-sm icon-act act-keeps-words" type="button">${window.PAUi.actFace("play", "Test on the droid")}</button>
            <span class="hint hidden" id="seq-editor-test-hint">Runs the last saved copy.</span>
            <span class="seq-running hidden" id="seq-editor-running" role="status"><span class="indicator ok seq-live" aria-hidden="true"></span><span id="seq-editor-running-name"></span></span>
            <button id="seq-editor-stop" class="btn btn-sm seq-stop icon-act act-keeps-words hidden" type="button">${window.PAUi.actFace("stop", "Stop")}</button>
          </span>
          <span class="seq-run">
            <button id="seq-editor-perform" class="btn btn-sm icon-act act-keeps-words" type="button">${window.PAUi.actFace("record-circle-outline", "Perform")}</button>
            <span class="seq-running hidden" id="seq-editor-performing" role="status"><span class="indicator ok seq-live" id="seq-editor-performing-lamp" aria-hidden="true"></span><span id="seq-editor-performing-words"></span></span>
            <button id="seq-editor-keep" class="btn btn-sm seq-stop icon-act hidden" type="button">${window.PAUi.actFace("check", "Keep")}</button>
          </span>
          <span class="seq-strip-seam" aria-hidden="true"></span>
          <span class="seq-verdict" id="seq-editor-validation-summary" aria-live="polite" aria-label="Validation status">
            <!-- Populated by updateValidationSummary() -->
          </span>
          <span class="seq-keep">
            <button id="seq-editor-save" class="btn btn-sm accent icon-act" type="button">${window.PAUi.actFace("content-save-outline", "Save")}</button>
            <button id="seq-editor-undo" class="seq-act icon-act" type="button" disabled>${window.PAUi.actFace("undo", "Undo")}</button>
            <button id="seq-editor-redo" class="seq-act icon-act" type="button" disabled>${window.PAUi.actFace("redo", "Redo")}</button>
            <button id="seq-editor-revert" class="seq-act icon-act" type="button">${window.PAUi.actFace("restore", "Revert")}</button>
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
                <button id="seq-editor-tap-open" class="seq-act icon-act" type="button" aria-expanded="false" aria-controls="seq-editor-tap">${window.PAUi.actFace("metronome", "Tap along")}</button>
                <button id="seq-editor-track-open" class="seq-act icon-act" type="button">${window.PAUi.actFace("waveform", "Analyze a track")}</button>
                <input id="seq-editor-track" class="hidden" type="file" accept="audio/*" aria-label="Your copy of the track">
                <button id="seq-editor-retime" class="seq-act icon-act${seq.tempo ? "" : " hidden"}" type="button">${window.PAUi.actFace("grid", "Retime to the grid")}</button>
              </span>
              <span class="setting-value seq-tempo-source" id="seq-editor-tempo-source">${tempoSourceLabel(seq.tempo)}</span>
            </div>
            <div id="seq-editor-levels" class="setting-row hidden">
              <span class="setting-name">Heard as</span>
              <span id="seq-editor-levels-seg" class="seg seg-sm" role="group" aria-label="Tempo levels heard in the track"></span>
              <span class="setting-value"></span>
            </div>
            <p class="hint seq-receipt" id="seq-editor-retime-receipt" role="status"></p>
            <div id="seq-editor-tap" class="setting-row hidden">
              <span class="setting-name">Tap on the beat</span>
              <span class="seq-row-ctl">
                <button id="seq-editor-tap-play" class="seq-act icon-act act-keeps-words" type="button">${window.PAUi.actFace("play", "Play on the droid")}</button>
                <button id="seq-editor-tap-beat" class="btn btn-sm icon-act" type="button">${window.PAUi.actFace("gesture-tap", "Tap")}</button>
                <span class="setting-unit" id="seq-editor-tap-count" role="status">0 taps</span>
                <button id="seq-editor-tap-use" class="seq-act icon-act" type="button" disabled>${window.PAUi.actFace("check", "Use")}</button>
              </span>
              <span class="setting-value"></span>
            </div>
            <div class="seq-editor-error-text" id="seq-editor-tempo-feedback" aria-live="polite"></div>
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
          ${pane("block", `
            <div class="seq-picked" id="seq-picked"></div>
            <div class="seq-lib" id="seq-drop">
              <div class="sect"><h3>Drop a part</h3><span class="sub" id="seq-drop-sub"></span></div>
              <span class="part-pills" id="seq-drop-parts"></span>
            </div>`)}
          ${pane("parts", `
            <div class="sect"><h3>Parts</h3><span class="sub" id="seq-editor-parts-sub"></span></div>
            <div class="seq-lib" id="seq-editor-parts">
              <input id="seq-editor-find" class="number-cell text-cell" type="search" placeholder="Find a part" aria-label="Find a part">
              <div class="part-pills-group"><span class="part-pills-name">Parts</span><span class="part-pills" id="seq-lib-parts"></span></div>
              <div class="part-pills-group"><span class="part-pills-name">Sets</span><span class="part-pills" id="seq-lib-sets"></span></div>
              <div class="part-pills-group"><span class="part-pills-name">More steps</span><span class="part-pills" id="seq-lib-kinds"></span></div>
              <div class="part-pills-group"><span class="part-pills-name">Sequences</span><span class="part-pills" id="seq-lib-phrases"></span></div>
            </div>`)}
          ${pane("sequence", `
            <div class="sect"><h3>Sequence</h3><span class="sub" id="seq-editor-saved-sub"></span></div>
            <div class="setting-rows seq-settings">
              <label class="setting-row">
                <span class="setting-name">Name</span>
                <input id="seq-editor-name" class="number-cell text-cell" type="text" value="${esc(seq.name || "DM:")}" placeholder="DM:MYSEQ" aria-label="Sequence name (DM:XXXX format)" maxlength="${"DM:".length + SEQ_NAME_CHARS}">
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
              <div class="setting-row" id="seq-editor-takes-row">
                <span class="setting-name">Takes</span>
                <span class="seq-takes" id="seq-editor-takes"></span>
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
        ${stageHtml({ ids: true, above: tempoRows, views: halfSwitchHtml(' id="seq-editor-half"') })}
        ${drawer}
      </div>
    `;

    // The inspector is the timeline's to fill, as it mounts. A library pill
    // still held from the sequence that was open lands nowhere.
    pickedBlocks = [];
    pickedShown = null;
    faderRun = null;
    window.removeEventListener("pointerup", faderLetGo);
    window.removeEventListener("pointercancel", faderLetGo);
    partsFind = "";
    libraryStop();
    showPicked([]);
    mountSessionTimeline();
    paintParts();
    loadPhrases();
    loadTakeFiles();

    attachMetadataListeners();
    updateValidationSummary();
    paintHistory();
    paintHalf();
    paintTakes();
    resumeTake();
    // The timeline is the one editor: without it there is nothing to edit
    // with, and the strip says so.
    if (!sessionTimeline) showEditorFeedback("The timeline did not load. Reload the page to try again.", "error");
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
    paintStripRun();
    // Revert has nothing to take back until there is an edit. Save stays live
    // either way: a sequence being tuned or restored is saved with no edit.
    const revertBtn = document.getElementById("seq-editor-revert");
    if (revertBtn) revertBtn.disabled = !dirty;
    const savedEl = document.getElementById("seq-editor-saved-sub");
    if (savedEl) savedEl.textContent = dirty ? "unsaved edits" : "as saved";
  };

  // Which half a refusal is in, for a sequence that has two: "Opens",
  // "Closes", or "" for one that is in neither - its name, its mute period,
  // its group, its tempo. Protocol Check names the half in the field of
  // every refusal that is in one (`closeSteps[2].beat`, `steps`).
  const refusedHalf = (verdict) => {
    if (verdict.ok || !hasCloseHalf(editorState.current)) return "";
    const field = verdict.field || "";
    return field.startsWith("closeSteps") ? "Closes" : field.startsWith("steps") ? "Opens" : "";
  };

  // The Rehearsal reads one run, and a toggle is two: each half is rehearsed
  // as the routine it is (halfRoutine()). What is of the sequence and not of
  // a run - its tempo - is said once, with Opens. `closes` is null for a
  // sequence with no close half.
  const rehearsedHalves = () => {
    const rehearsal = window.SeqRehearsal;
    const context = rehearsalContext();
    const opens = rehearsal.rehearse(editorState.current, context);
    if (!hasCloseHalf(editorState.current)) return { opens, closes: null };
    const report = rehearsal.rehearse(halfRoutine(editorState.current, "closes"), context);
    const findings = report.findings.filter((item) => !item.code.startsWith("tempo-"));
    const count = (level) => findings.filter((item) => item.level === level).length;
    return { opens, closes: { ...report, findings, counts: { warning: count("warning"), note: count("note") } } };
  };

  const updateValidationSummary = () => {
    const validation = routineVerdict(editorState.current);
    const summaryEl = document.getElementById("seq-editor-validation-summary");
    if (!summaryEl) return;

    // Read off the routine as it is now: its name on the strip, and how many
    // steps the half on the stage has beside the drawer's tabs.
    const steps = stageSteps() || [];
    const nameEl = document.getElementById("seq-editor-sub");
    if (nameEl) nameEl.textContent = editorState.current.name || "a new sequence";
    const routineEl = document.getElementById("seq-editor-routine-sub");
    if (routineEl) routineEl.textContent = countOf(steps.length, "step", "steps");

    // Protocol Check's two outcomes take the signal colors their own meanings
    // already have - a green lamp for a sequence the droid will accept, red
    // for one it would refuse - and the sentence says which on its own
    // (GLOSSARY.md "Status Color", ADR 0044).
    const status = validation.ok ? "valid" : "error";
    // A refusal is a sentence, so it takes a line of its own under the acts.
    // In a sequence with two halves it says which half it is in.
    const half = refusedHalf(validation);
    const refusal = `${half ? `${half}: ` : ""}${validation.error || "Validation error"}`;
    summaryEl.classList.toggle("is-refused", !validation.ok);
    summaryEl.innerHTML = `
      <span class="indicator ${validation.ok ? "ok" : "fail"}" aria-hidden="true"></span>
      <span class="seq-validation-status seq-validation-${status}">${window.PAUtils.escapeHtml(validation.ok ? "Sequence is valid" : refusal)}</span>
    `;

    // Disable save button if invalid
    const saveBtn = document.getElementById("seq-editor-save");
    if (saveBtn) saveBtn.disabled = !validation.ok;

    // The Rehearsal's tab: its counts, what it found and the fix for each, and
    // what the routine weighs. It never feeds the verdict: the save button
    // above answers to Protocol Check alone (ADR 0044). The tab itself says
    // when there is a warning to read.
    //
    // A sequence with two halves is read half by half, each under its name,
    // whichever is on the stage; the tab counts the warnings of both. The
    // figures are those of the half on the stage - a half holds its own steps
    // and runs its own length - but for the size, which is the file's.
    const rehearsalEl = document.getElementById("seq-editor-rehearsal");
    if (rehearsalEl && window.SeqRehearsal) {
      const rehearsal = window.SeqRehearsal;
      const { opens, closes } = rehearsedHalves();
      const said = (report) => rehearsal.countsHtml(report) + rehearsal.listHtml(report);
      const named = (name, report) => `<span class="seq-rehearsal-half">${name}</span>${said(report)}`;
      rehearsalEl.innerHTML = closes ? named("Opens", opens) + named("Closes", closes) : said(opens);
      const shown = closes && editorState.half === "closes" ? closes : opens;
      const figuresEl = document.getElementById("seq-editor-figures");
      if (figuresEl) figuresEl.textContent = rehearsal.figuresText({ figures: { ...shown.figures, bytes: opens.figures.bytes } });
      const warnings = opens.counts.warning + (closes ? closes.counts.warning : 0);
      const tabEl = document.getElementById("seq-editor-tab-rehearsal");
      if (tabEl) {
        tabEl.textContent = warnings > 0
          ? `Rehearsal · ${countOf(warnings, "warning", "warnings")}`
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
    // A toggle's run is either half, by which way its group is latched, so
    // the Outputs of both are named.
    const seq = editorState.current;
    const html = window.SeqRehearsal.unmeasuredHtml(
      window.SeqRehearsal.unmeasuredOutputs({ ...seq, steps: [...(seq.steps || []), ...(seq.closeSteps || [])] }, rehearsalContext())
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
    if (lastRunBadge) showRunBadge(lastRunBadge);
    if (timeline) timeline.refresh(timelineContext());
    if (sessionTimeline) sessionTimeline.refresh(stageContext());
  };

  // Called once from renderEditorView: the strip, the tempo row and the
  // drawer, which are drawn once per open, so their listeners never stack.
  const attachMetadataListeners = () => {
    const nameInput = document.getElementById("seq-editor-name");
    const purposeInput = document.getElementById("seq-editor-purpose");
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
        btn.addEventListener("click", () => setGroup(btn.dataset.value));
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
        const shut = tapPanel.classList.toggle("hidden");
        tapOpen.setAttribute("aria-expanded", String(!shut));
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
          await runWatch.start(editorState.original.name);
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
        tapOpen?.setAttribute("aria-expanded", "false");
      });
    }
    const trackInput = document.getElementById("seq-editor-track");
    if (trackInput) {
      // A real button, not a <label>, so Analyze a track is a Tab stop.
      document.getElementById("seq-editor-track-open")?.addEventListener("click", () => trackInput.click());
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
          paintLevels();
          updateValidationSummary();
          return;
        }
        tempoFeedback("");
        setTempo(window.SeqTempo.analyzedTempo(result));
      });
    }

    // A level picked: the same measurement at that tempo. Where beat 1 sits,
    // and the bar, stay where the builder put them.
    document.getElementById("seq-editor-levels-seg")?.addEventListener("click", (event) => {
      const level = droppedTrack?.levels?.[Number(event.target?.closest?.("button[data-level]")?.dataset.level)];
      const held = editorState.current.tempo;
      if (!level || !held || level.bpm === held.bpm) return;
      setTempo({ ...window.SeqTempo.analyzedTempo(droppedTrack), bpm: level.bpm, phase: held.phase, barLen: held.barLen, barPhase: held.barPhase });
    });

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
    DRAWER_TABS.forEach((tab) =>
      document.getElementById(`seq-editor-tab-${tab}`)?.addEventListener("click", () => showTab(tab)));
    document.getElementById("seq-editor-half")?.addEventListener("click", (event) => {
      const half = event.target?.closest?.("[data-half]")?.dataset.half;
      if (half && !historyBusy()) showHalf(half);
    });

    // The inspector is written again whenever the selection changes, so its
    // controls are heard on the inspector itself: a press on an act or a
    // choice, a field's change, and a fader as it moves.
    const pickedPane = document.getElementById("seq-picked");
    if (pickedPane) {
      pickedPane.addEventListener("click", (event) => {
        const pressed = event.target?.closest?.("button[data-picked]");
        if (!pressed) return;
        const act = pressed.dataset.picked;
        if (act === "remove") sessionTimeline?.removePicked();
        else if (act === "perform-again") performAgain();
        else if (act === "split") splitPicked();
        else if (act === "off-beat") {
          if (pickedBlocks.length === 1) setStepBeat(pickedBlocks[0].steps[0], { beat: null });
        } else inspect(act, pressed.dataset.value);
      });
      // A Gesture's fold, opened or shut: `toggle` does not bubble, so it is
      // heard on its way down.
      pickedPane.addEventListener("toggle", (event) => {
        if (event.target?.classList?.contains("seq-picked-more")) gestureMoreOpen = event.target.open;
      }, true);
      // Which control a field is, read from the markup the inspector wrote.
      const kindOf = (input) => input?.getAttribute?.("type") || "";
      pickedPane.addEventListener("input", (event) => {
        if (kindOf(event.target) === "range" && event.target.dataset.picked) faderMoved(event.target);
      });
      pickedPane.addEventListener("change", (event) => {
        const input = event.target;
        const field = input?.dataset?.picked;
        if (!field || !sessionTimeline) return;
        if (kindOf(input) === "range") faderLetGo();
        else if (kindOf(input) === "checkbox") inspect(field, input.checked);
        // A number left empty is no number: the field goes back to the one
        // the routine holds. So does a start the block could not take - it
        // is held at its limit - because inspect() draws the inspector again
        // from where the block is.
        // A number that may be left empty (numberCell()'s `optional`) is
        // written empty: that is its "no duration".
        else if (kindOf(input) === "number" && input.value === "" && input.dataset.optional === undefined) repaintPicked();
        else inspect(field, input.value);
      });
    }

    // The library's two lists. A pill is taken hold of on its list, and the
    // drag is followed on the window, so it is not stranded when the pointer
    // leaves the drawer.
    ["seq-editor-parts", "seq-drop"].forEach((id) => {
      const list = document.getElementById(id);
      list?.addEventListener("pointerdown", libraryGrab);
      list?.addEventListener("click", libraryPress);
    });
    const findInput = document.getElementById("seq-editor-find");
    findInput?.addEventListener("input", () => {
      partsFind = findInput.value;
      paintParts();
    });

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

    const testBtn = document.getElementById("seq-editor-test");
    const saveBtn = document.getElementById("seq-editor-save");
    const revertBtn = document.getElementById("seq-editor-revert");
    // All sequences: the way back to the list, which asks before it drops
    // unsaved edits.
    const cancelBtn = document.getElementById("seq-editor-cancel");

    if (testBtn) testBtn.addEventListener("click", handleTestOnDroid);
    document.getElementById("seq-editor-perform")?.addEventListener("click", handlePerform);
    document.getElementById("seq-editor-keep")?.addEventListener("click", keepTake);
    document.getElementById("seq-editor-takes")?.addEventListener("click", (event) => {
      const btn = event.target.closest("[data-take-out]");
      if (btn) takeOut(btn.dataset.takeOut);
    });
    const stopBtn = document.getElementById("seq-editor-stop");
    if (stopBtn) stopBtn.addEventListener("click", () => handleStopRun(stopBtn));
    if (saveBtn) saveBtn.addEventListener("click", handleSave);

    if (revertBtn) {
      revertBtn.addEventListener("click", () => renderEditorView(editorState.original));
    }

    if (cancelBtn) {
      cancelBtn.addEventListener("click", () => leaveSession(() => {}));
    }
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

  // The Rehearsal's folded badge for a run the droid accepted, in place of
  // the line that said it was being sent: the lamp on the strip is the word
  // that it is running, and a line saying so would outlive the run. The badge
  // rehearses `seq`, the copy the droid ran - which is not the edits on
  // screen, and so not what the Rehearsal tab is reading. It is only ever shown
  // after the run has been sent, so nothing it finds can stand in the run's
  // way (#287 specific 6).
  const showRunBadge = (seq) => {
    const feedbackEl = document.getElementById("seq-editor-feedback");
    if (!feedbackEl) return;
    lastRunBadge = seq;
    saidSaved = false;
    const rehearsal = window.SeqRehearsal;
    feedbackEl.innerHTML = rehearsal ? rehearsal.badgeHtml(rehearsal.rehearse(seq, rehearsalContext())) : "";
  };

  const handleSave = async () => {
    const validation = routineVerdict(editorState.current);
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
      await runWatch.start(seqName);
      // The droid ran what is saved under that name, not the edits on screen,
      // so the badge rehearses the last saved or cloned copy.
      showRunBadge(editorState.original);
    } catch (error) {
      showEditorFeedback("Test failed: " + PAApi.messageFor(error), "error");
    } finally {
      if (testBtn) testBtn.disabled = false;
    }
  };

  // ---------------------------------------------------------------------------
  // Perform: a take off the sticks, kept into the sequence that is open
  // (#442, ADR 0061).
  //
  // Perform arms a take on the droid for this sequence as it is saved; the
  // builder performs on the sticks set to Perform a Part (the RC page) and
  // presses Keep - or the droid stops the take itself, full or on the estop,
  // and it is kept the same way. Keeping places the receipt into the routine
  // as ONE edit, so one Undo takes all of it back:
  //   - the take, as an entry in the sequence's `takes` - the droid holds its
  //     motion in a file of its own, drawn on the timeline as one block;
  //   - each cue pressed during it, as the step that does what the cue did,
  //     at the moment it was pressed (cueStep()). A cue with no such step, or
  //     one Protocol Check would refuse where it lands, is named in the
  //     receipt and not placed;
  //   - the end step moved out to cover the take and the last cue.
  // The take's file stays the droid's "not yet saved" take until the sequence
  // is saved naming it; the receipt says so.
  //
  // Asks for no Non-RC Control consent: a take is RC motion (ADR 0064).
  // ---------------------------------------------------------------------------
  const TAKE_POLL_MS = 500;
  // How many reads in a row may go unanswered while a take runs before the
  // strip stops watching and says so.
  const TAKE_POLL_MISSES = 4;
  let takePoll = null;
  let takeMisses = 0;
  let takeKeeping = false;

  // The name the droid holds this sequence under: a take belongs to the
  // saved sequence, so an unsaved one, or an unsaved rename, has none yet.
  const takeSeqName = () => (editorState.isNew ? null : editorState.original?.name || null);

  const clockWords = (ms) => {
    const sec = Math.floor((Number(ms) || 0) / 1000);
    return `${Math.floor(sec / 60)}:${String(sec % 60).padStart(2, "0")}`;
  };

  const paintPerform = (status = null) => {
    const performing = status?.state === "performing";
    const show = (id, on) => document.getElementById(id)?.classList.toggle("hidden", !on);
    show("seq-editor-perform", !performing);
    show("seq-editor-performing", performing);
    show("seq-editor-keep", performing);
    if (!performing) return;
    const words = document.getElementById("seq-editor-performing-words");
    if (words) words.textContent = `Performing ${clockWords(status.elapsedMs)}${status.nearlyFull ? ", nearly full" : ""}`;
    const lamp = document.getElementById("seq-editor-performing-lamp");
    if (lamp) lamp.className = `indicator ${status.nearlyFull ? "warn" : "ok"} seq-live`;
  };

  // Starting and stopping the watch writes the inspector again: a picked
  // take offers Perform again only while no take runs (takeHtml()).
  const stopTakePoll = () => {
    if (takePoll !== null) clearInterval(takePoll);
    takePoll = null;
    takeMisses = 0;
    paintPerform(null);
    if (editorState.current) repaintPicked();
  };

  // One read of the take while it runs: still performing, it is painted;
  // stopped by the droid, it is kept. A take of another sequence - the
  // builder left this one - is no longer this strip's to watch.
  const readTake = async () => {
    let status = null;
    try {
      status = (await PAApi.get("/api/take")).data;
      takeMisses = 0;
    } catch (error) {
      takeMisses += 1;
      if (takeMisses >= TAKE_POLL_MISSES) {
        stopTakePoll();
        showEditorFeedback(`The droid stopped answering about the take: ${PAApi.messageFor(error)}`, "error");
      }
      return;
    }
    if (takePoll === null) return;
    if (!editorState.current || status?.seq !== takeSeqName()) {
      stopTakePoll();
      return;
    }
    if (status.state === "performing") {
      paintPerform(status);
      return;
    }
    stopTakePoll();
    if (status.state === "stopped") keepTake();
  };

  const watchTake = () => {
    if (takePoll !== null) return;
    takePoll = setInterval(readTake, TAKE_POLL_MS);
    repaintPicked();
    readTake();
  };

  // A sequence opened while a take of it is on the droid - the page was
  // reloaded mid-take, or the builder came back to it - picks the take up.
  const resumeTake = async () => {
    const name = takeSeqName();
    if (!name) return;
    let status = null;
    try {
      status = (await PAApi.get("/api/take")).data;
    } catch {
      return;  // nothing to pick up that this page can see; Perform still asks the droid
    }
    if (status?.seq !== name || takeSeqName() !== name) return;
    if (status.state === "performing") watchTake();
    else if (status.state === "stopped") keepTake();
  };

  // The take Perform again replaces, by its id, from the press until the
  // receipt is placed (placeTake()); null for a plain Perform.
  let performOver = null;

  // Perform again, on a picked take: the new take is kept in its place - where
  // it starts and its place in the list, which decides who wins an overlap -
  // and the old one goes in the same edit, so one Undo puts it back. Two
  // presses after the pick: Perform again, then Keep. The old take's file goes
  // when the sequence is saved without it. A board whose takes are all saved
  // into sequences (the artoo-esp32 keeps one) refuses to arm, saying so; the
  // builder removes the take and saves first.
  const performAgain = () => {
    const entry = pickedTake();
    if (entry && takePoll === null) handlePerform(entry.id);
  };

  // `over` is set as performOver only once the droid has armed: a press it
  // refuses - one made while a take runs - must not take the running take's
  // target away from it.
  const handlePerform = async (over = null) => {
    const name = takeSeqName();
    if (!name) {
      showEditorFeedback("Save the sequence first.", "error");
      return;
    }
    const btn = document.getElementById("seq-editor-perform");
    if (btn) btn.disabled = true;
    try {
      await PAApi.request(`/api/take/arm?seq=${encodeURIComponent(name)}`, { method: "POST" });
      performOver = typeof over === "string" ? over : null;
      showEditorFeedback("");
      watchTake();
    } catch (error) {
      showEditorFeedback(PAApi.messageFor(error), "error");
    } finally {
      if (btn) btn.disabled = false;
    }
  };

  // The step that does what a cue did, or null where none does:
  //   a random sound            -> Sound Category, that category
  //   a $ command               -> Sound command
  //   a : or # line the dome    -> Dome command, as the cue sent it
  //   answers (`owner` "dome")
  //   a sequence (dome_seq)     -> Sequence: a Learned one by its id, as the
  //                                library places one (ADR 0046), else by name
  // A line the body answers - a body routine :SE30-36, a panel number that is
  // one of the body's Outputs, a full-droid :SEnn - has no step: a sequence's
  // dome step reaches the dome only, so placing it would move something else
  // (Command Ownership, ADR 0055; the droid says `owner` in the receipt). Nor
  // do a toggle, a mode, the estop, Sleep or the speed preset.
  const CUE_SOUND_PREFIX = "sound_rand_";
  const cueStep = ({ action = "", payload = "", owner = "" }) => {
    if (action.startsWith(CUE_SOUND_PREFIX)) {
      const category = action.slice(CUE_SOUND_PREFIX.length);
      return AUDIO_CATEGORIES.includes(category) ? { type: "audioCat", ...stepTypeDefaults.audioCat, category } : null;
    }
    if (action === "cmd" && payload.startsWith("$")) return { type: "audio", cmd: payload };
    if ((action === "cmd" || action === "seq") && owner === "dome") {
      return { type: "dome", cmd: action === "seq" ? `:SE${payload}` : payload };
    }
    if (action === "dome_seq" && payload) {
      const learned = sequences.find((x) => x.id && x.name === payload);
      return learned ? { type: "sequence", ref: learned.id, name: learned.name } : { type: "sequence", ref: payload };
    }
    return null;
  };

  // The RC actions' names, for a receipt that names cues it could not place.
  // Read once, when first needed; a token stands in where the droid did not
  // answer.
  let actionNames = null;
  const cueNames = async (tokens) => {
    if (tokens.length === 0) return [];
    if (actionNames === null) {
      try {
        const listed = (await PAApi.get("/api/actions")).data;
        actionNames = new Map((Array.isArray(listed) ? listed : [])
          .filter((entry) => typeof entry?.token === "string")
          .map((entry) => [entry.token, entry.display_name || entry.name || entry.token]));
      } catch {
        actionNames = new Map();
      }
    }
    return [...new Set(tokens)].map((token) => actionNames.get(token) || token);
  };

  // The receipt, placed into the routine (see the block comment above).
  const placeTake = async (receipt) => {
    if (!editorState.current) return;
    const cues = Array.isArray(receipt?.cues) ? receipt.cues : [];
    const take = receipt?.take || null;
    const unplaced = [];
    let placed = 0;
    // Perform again: the take it replaces, if the sequence still holds it.
    // The new one goes where that one started, and so do the cues pressed
    // during it.
    const takes = editorState.current.takes || [];
    const over = takes.find((each) => each.id === performOver) || null;
    const overNumber = takes.indexOf(over) + 1;
    performOver = null;
    const at = over ? Number(over.t) || 0 : 0;
    // The droid deleted the unsaved take it replaced, so its file is gone: an
    // Undo that brings its entry back draws it as a take that cannot be read,
    // not from a copy this page still holds (loadTakeFiles()).
    if (receipt?.replaced) takeFiles.read.set(receipt.replaced, { failed: true });
    if (take || cues.length > 0 || receipt?.replaced) {
      historyPush();
      editorState.half = "opens";
      const fresh = take ? { id: take.id, t: at } : null;
      const held = takes
        .map((each) => (each === over && fresh ? fresh : each))
        .filter((each) => each.id !== receipt.replaced || each === fresh);
      if (fresh && !over) held.push(fresh);
      if (held.length > 0) editorState.current.takes = held;
      else delete editorState.current.takes;

      const steps = stageSteps();
      const endAt = steps.findIndex((each) => each?.type === "end");
      const reach = Math.min(STEP_LIMITS.t[1],
        Math.max(take ? at + (Number(take.lengthMs) || 0) : 0, ...cues.map((cue) => at + (Number(cue.t) || 0) + 1)));
      if (endAt !== -1 && (Number(steps[endAt].t) || 0) < reach) {
        steps[endAt].t = reach;
        delete steps[endAt].beat;
      }
      for (const cue of cues) {
        const step = cueStep(cue);
        const landed = step ? { t: Math.min(at + (Number(cue.t) || 0), reach - 1), ...step } : null;
        const place = (list) => {
          const end = list.findIndex((each) => each?.type === "end");
          list.splice(end === -1 ? list.length : end, 0, { ...landed });
        };
        if (landed && !triedOnCopy(place).refused) {
          place(stageSteps());
          orderSteps();
          placed += 1;
        } else {
          unplaced.push(cue.action);
        }
      }
      edited();
    }

    const stop = { full: " - the take filled up", estop: " - the estop stopped it" }[receipt?.stopped] || "";
    const lines = [`Kept: ${take ? "1 take" : "no take (nothing moved)"}, ${placed} cue ${placed === 1 ? "step" : "steps"}${stop}.`];
    if (take && over) lines.push(`It replaces take ${overNumber}.`);
    else if (receipt?.replaced) lines.push("It replaces the take you had not saved.");
    const names = await cueNames(unplaced);
    if (names.length > 0) lines.push(`No step for: ${names.join(", ")}.`);
    if (receipt?.cuesPast > 0) lines.push(`${receipt.cuesPast} more ${receipt.cuesPast === 1 ? "press was" : "presses were"} not kept.`);
    if (take || placed > 0) lines.push("Save the sequence to hold them on the droid.");
    window.PAOverlay.receipt(lines.join(" "));
  };

  const keepTake = async () => {
    if (takeKeeping) return;
    takeKeeping = true;
    stopTakePoll();
    try {
      placeTake((await PAApi.postJson("/api/take/keep", {})).data);
    } catch (error) {
      showEditorFeedback(`The take was not kept: ${PAApi.messageFor(error)}`, "error");
    } finally {
      takeKeeping = false;
    }
  };

  // The takes the sequence holds, in the Sequence tab: each one, with its
  // Remove - which is how a board that keeps one take frees it for the next.
  // Removing one is an edit like any other; saving deletes its file.
  const paintTakes = () => {
    const el = document.getElementById("seq-editor-takes");
    if (!el || !editorState.current) return;
    const takes = editorState.current.takes || [];
    el.innerHTML = takes.length === 0
      ? `<span class="hint">None</span>`
      : takes.map((take, index) =>
        `<span class="seq-take">Take ${index + 1}<button type="button" class="seq-act icon-act" data-take-out="${window.PAUtils.escapeHtml(take.id)}">${window.PAUi.actFace("delete-outline", "Remove")}</button></span>`).join("");
  };

  const takeOut = (id) => {
    if (historyBusy() || !editorState.current?.takes) return;
    historyPush();
    const held = editorState.current.takes.filter((each) => each.id !== id);
    if (held.length > 0) editorState.current.takes = held;
    else delete editorState.current.takes;
    edited();
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
        await handleTestSequence(seqName, rowEl, btn);
        break;
      case "stop":
        await handleStopRun(btn);
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
        leaveSession(() => handleDuplicateSequence(seqName, rowEl));
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
      case "stand-down":
        await handleChooseStandDown(seqName, rowEl, btn);
        break;
    }
  };

  // Nominate a Sequence as the droid's Stand Down Sequence: one Setting,
  // stored on the droid (POST /api/config standDownSequence). A refusal says
  // so on the row that was pressed, in the words table's terms.
  const handleChooseStandDown = async (seqName, rowEl, btn) => {
    btn.disabled = true;
    try {
      await PAApi.postForm("/api/config", { standDownSequence: seqName }, { timeoutMs: 3000 });
      standDownChoice = seqName;
      renderListView();
    } catch (error) {
      btn.disabled = false;
      const feedbackEl = rowEl?.querySelector(".seq-item-feedback");
      if (!feedbackEl) return;
      feedbackEl.textContent = `Not chosen: ${PAApi.messageFor(error)}`;
      feedbackEl.className = "seq-item-feedback feedback error";
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

  const handleTestSequence = async (seqName, rowEl, btn) => {
    const feedbackEl = rowEl?.querySelector(".seq-item-feedback");
    if (feedbackEl) {
      feedbackEl.textContent = `Sending ${seqName} to droid...`;
      feedbackEl.className = "seq-item-feedback feedback info";
    }
    // Held until the droid has accepted or refused the run: one POST, and one
    // /api/status read before it only when the page holds no frame yet. A
    // second press in that time would send a second run, which preempts the
    // first.
    btn.disabled = true;
    try {
      await runWatch.start(seqName);
      // Accepted: the row's lamp is the word now, and this line would only
      // outlive the run.
      if (feedbackEl) {
        feedbackEl.textContent = "";
        feedbackEl.className = "seq-item-feedback feedback hidden";
      }
    } catch (error) {
      if (feedbackEl) {
        feedbackEl.textContent = PAApi.messageFor(error);
        feedbackEl.className = "seq-item-feedback feedback error";
      }
      return;
    } finally {
      btn.disabled = false;
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

  // Open a copy of one of the builder's own sequences to edit. A read that
  // fails says so on the row that was pressed.
  const handleDuplicateSequence = async (seqName, rowEl = null) => {
    const sayOnRow = rowSayer(seqName, rowEl);
    let original = null;
    try {
      const result = await PAApi.get(`/api/seq?name=${encodeURIComponent(seqName)}`);
      original = result.data;
    } catch (error) {
      sayOnRow(`Could not read ${seqName}: ${PAApi.messageFor(error)}`);
      return;
    }
    if (!original || !Array.isArray(original.steps)) {
      sayOnRow(`The droid sent ${seqName} back with no steps.`);
      return;
    }
    // Auto-rename to NAME_COPY (avoid _COPY_COPY by removing an existing
    // suffix). In capitals: Protocol Check takes no lowercase in a name, so
    // "_copy" opened every duplicate refused until it was renamed. And within
    // the name's length: a long name gives up its tail to the suffix, or the
    // duplicate would open refused for being too long.
    const suffix = "_COPY";
    const stem = seqName.replace(/^DM:/, "").replace(/_COPY(\d*)$/, "").slice(0, SEQ_NAME_CHARS - suffix.length);
    original.name = `DM:${stem}${suffix}`;
    // A duplicate is a new sequence, so it gets an id of its own at save. A
    // sequence inside another is found by id, and the droid takes the first
    // match (src/seq_store.cpp): two sequences sharing one would let a
    // routine that holds the original play the copy.
    delete original.id;
    // A take's file belongs to one sequence, named by its id (#442): the copy
    // starts with none, rather than naming files its original owns and takes
    // with it when it is deleted.
    delete original.takes;

    // Open editor with copy
    currentEditingSeq = original;
    editorState.isNew = true; // Duplicate is a new sequence
    renderEditorView(currentEditingSeq);
  };

  // The question as the markup asks it; a wipe that leaves bindings dangling
  // retitles the dialog as a message, and the next prompt puts this back.
  const WIPE_TITLE = els.wipeTitle.textContent;

  const handleMemoryWipePrompt = (seqName) => {
    _pendingWipeSeqName = seqName;
    els.wipeTitle.textContent = WIPE_TITLE;
    els.wipeSeqName.textContent = `Delete sequence: ${seqName}`;
    els.wipeSeqName.classList.remove("hidden");
    els.wipeWarning.classList.remove("hidden");
    els.wipeConfirmInput.value = "";
    els.wipeConfirmInput.placeholder = seqName;
    els.wipeConfirmInput.disabled = false;
    els.wipeDanglingInfo.classList.add("hidden");
    els.wipeFeedback.classList.add("hidden");
    els.modalWipeConfirm.classList.remove("hidden");
    els.modalWipeConfirm.disabled = true;
    window.PAUi.setAct(els.modalWipeCancel, "Keep it");

    const updateWipeButton = () => {
      els.modalWipeConfirm.disabled = els.wipeConfirmInput.value !== _pendingWipeSeqName;
    };

    // Remove previous listener before adding to avoid accumulation on reopen
    if (_wipeInputListener) {
      els.wipeConfirmInput.removeEventListener("input", _wipeInputListener);
    }
    _wipeInputListener = updateWipeButton;
    els.wipeConfirmInput.addEventListener("input", updateWipeButton);

    showModal(els.modalWipe, els.wipeConfirmInput);
  };

  const handleMemoryWipeConfirm = async () => {
    const seqName = _pendingWipeSeqName;
    els.modalWipeConfirm.disabled = true;
    els.wipeFeedback.classList.add("hidden");
    try {
      const result = await PAApi.request(`/api/seq?name=${encodeURIComponent(seqName)}`, {
        method: "DELETE",
      });

      const dangling = (result.data && result.data.danglingBindings) || [];
      if (dangling.length > 0) {
        // The dialog stays open, as a message with nothing left to decide: the
        // builder reads which bindings now do nothing, and it has one button
        // (the rule PAOverlay.ask() keeps with `no: null`).
        let html = "<p><strong>These RC bindings now do nothing:</strong></p><ul>";
        dangling.forEach((b) => {
          html += `<li>${window.PAUtils.escapeHtml(b.source)} CH${b.channel}</li>`;
        });
        html += "</ul>";
        els.wipeTitle.textContent = `Wiped ${seqName}`;
        els.wipeSeqName.classList.add("hidden");
        els.wipeWarning.classList.add("hidden");
        els.wipeDanglingInfo.innerHTML = html;
        els.wipeDanglingInfo.classList.remove("hidden");
        els.wipeConfirmInput.disabled = true;
        els.modalWipeConfirm.classList.add("hidden");
        window.PAUi.setAct(els.modalWipeCancel, "Close");
        els.modalWipeCancel.focus();
      } else {
        hideModal(els.modalWipe);
        // Its dialog has closed, so the answer is a receipt (GLOSSARY.md
        // "Receipt"). With nothing left dangling, either a factory sequence
        // of the same name took its place or no RC binding played it
        // (src/web/api_seq.cpp).
        const factory = builtins.some((b) => b.name === seqName);
        window.PAOverlay.receipt(factory
          ? `Wiped ${seqName} - the factory ${seqName} plays in its place.`
          : `Wiped ${seqName} - no RC binding played it.`);
      }
      refreshLearned();
    } catch (error) {
      // The dialog is still open, so the refusal is a line inside it
      // (docs/ui-copy-voice.md rule 18), and the builder can press again.
      // Focus goes back to the button: it was disabled while it held focus,
      // which drops focus to <body> behind the open dialog.
      els.modalWipeConfirm.disabled = false;
      els.modalWipeConfirm.focus();
      PAUtils.showFeedback(els.wipeFeedback,
        `Could not wipe ${seqName}: ${PAApi.messageFor(error)} - it is still on the droid.`, "error");
      els.wipeFeedback.classList.remove("hidden");
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
      const fileName = `${seqJson.name.replace(/:/g, "_")}.json`;
      a.href = url;
      a.download = fileName;
      a.click();
      URL.revokeObjectURL(url);
      // A download has no line of its own to answer on, so it gets a receipt.
      window.PAOverlay.receipt(`Exported ${seqName} as ${fileName} - the droid keeps its copy.`);
    } catch (error) {
      window.PAOverlay.receipt(
        `Could not export ${seqName}: ${PAApi.messageFor(error)} - nothing on the droid changed.`,
        "refused"
      );
    }
  };

  // =========================================================================
  // Share to project (contribution funnel — ADR 0007)
  // =========================================================================

  const SEQ_REPO_SLUG = "mattiasbrandt/protoR2";

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

    // A take's file is not in a sequence's export, only its name: imported,
    // the reference would name a file this droid does not hold (#442).
    delete parsed.takes;

    hideModal(els.modalImport);
    // Through leaveSession(), like every way a sequence takes the editor.
    // Restore is pressed from the list, which gives way to an open edit
    // (renderListView()), so today there is no edit here to ask about.
    leaveSession(() => {
      editorState.isNew = true;
      currentEditingSeq = parsed;
      renderEditorView(parsed);
      // Its dialog has closed, so the answer is a receipt (GLOSSARY.md
      // "Receipt"), given where the restore has happened.
      window.PAOverlay.receipt(`Restored ${parsed.name || "the sequence"} - nothing reaches the droid until you save it.`);
    });
  };

  const showImportModal = () => {
    els.importFileInput.value = "";
    els.importTextarea.value = "";
    els.importFeedback.classList.add("hidden");
    els.modalImportConfirm.disabled = true;
    showModal(els.modalImport, els.modalImportCancel);
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

    // The file input is hidden and a <label> is not a Tab stop, so Choose file
    // is a real button that opens the input's chooser.
    els.importFileTrigger.addEventListener("click", () => els.importFileInput.click());
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
    [STAND_DOWN_SECTION, loadStandDown, "the Stand Down Sequence"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadLearned().catch((error) => console.warn("[seq] your sequences unavailable:", error));
      loadFactory().catch((error) => console.warn("[seq] factory sequences unavailable:", error));
      loadStandDown().catch((error) => console.warn("[seq] Stand Down Sequence unavailable:", error));
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "Body Controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/dome_bearing.js": "where the dome points",
      "/dome_lights.js": "the dome's lights",
      "/seq_protocol_check.js": "sequence protocol",
      "/servo_motion.js": "servo motion model",
      "/seq_rehearsal.js": "sequence rehearsal",
      "/seq_timeline.js": "sequence timeline",
      "/body_view.js": "droid picture",
      "/outputs.js": "servo outputs",
      "/seq.js": "sequence editor",
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

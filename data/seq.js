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
//   list, stage, drawer                          the three surfaces
//   pickedHtml                                   inspector rows
//   stepPreview / stepTypeDefaults               card text, and a new step's values
//   lightKind / lightFields                      light grammar
//   renderStepRow / renderStepFields             the card editor
//   loadRehearsalFacts                           caches GET /api/servo/outputs
//   validateAndUpdateStep                        commits one card field
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
  let sessionTimeline = null; // the timeline on the workspace's stage, over the sequence being edited, or null
  let pickedBlocks = []; // the blocks picked on that timeline, as it last said them
  let pickedShown = null; // the inspector's markup as last written
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
  });

  // =========================================================================
  // The run watch (#441, #451)
  //
  // One run started from the browser: it starts it, knows while it is under
  // way, stops it, and says when it has ended. It holds no markup, so another
  // surface that starts runs takes this unit rather than a second poll.
  //
  // The droid answers POST /api/seq/test before anything has started: the run
  // is queued, the dispatcher takes it up later and only then writes the run's
  // record, and it can still refuse it there
  // (src/tasks/sequence_dispatcher.cpp). Nothing on the status stream says a
  // sequence is running, so the record (GET /api/seq/last-run) is read once a
  // second, and ONLY while a run started here is under way: each answer is a
  // multi-KB document the droid has to build.
  //
  // Which record is this run's. The record is read once BEFORE the press, and
  // a later answer is this run's only when it is another record - another
  // start time - under this run's name. Never by name alone: the first answers
  // after a press can still be the previous run of the same sequence, long
  // ended, and that must not end this one.
  // =========================================================================
  const RUN_POLL_MS = 1000;
  // How long the droid gets to take up a run it accepted. It does so within a
  // dispatcher pass, so this is generous. Judged when an answer lands: only an
  // answer can say the record is still the one from before the press.
  const RUN_START_WAIT_MS = 5000;
  // How long the record may go unanswered before the page stops saying the run
  // is under way. A droid that drops off the network mid-run answers nothing,
  // so no answer can end the run: this is judged at each read, before it is
  // sent, from the first read since the last answer - the time the page has
  // been asking, never the time a hidden tab or another surface kept it from
  // asking. The run itself is not stopped, and may still be playing.
  const RUN_QUIET_MS = 5000;

  // `onChange({ name, running, outcome })` is called when a run starts being
  // watched and when it stops being one. `outcome` is the record's own word
  // (completed, aborted, preempted, estop, reconnect), "replaced" when the
  // record became something else's, "not-started" when the droid accepted
  // the run and never began it, or "lost" when the droid stopped answering.
  const createRunWatch = (onChange) => {
    // { name, before, sentAt, seen, unheardSince } while a run started here is
    // under way. `unheardSince` is when the first read since the last answer
    // was sent, or null when the last read was answered.
    let run = null;

    const sameRecord = (a, b) =>
      Boolean(a?.valid) === Boolean(b?.valid) && (!a?.valid || (a.startMs === b.startMs && a.name === b.name));

    // Owned by the surface this is created on: the shell stops it when that
    // surface is left and starts it again on the way back (ADR 0048), so
    // create the watch in the surface's script body. The rejection of a read
    // that got no answer is left to PASurface.poll(), which reports it.
    const poll = window.PASurface.poll(() => {
      const now = Date.now();
      if (run.unheardSince !== null && now - run.unheardSince >= RUN_QUIET_MS) {
        // Nothing was asked this time, so nothing is handed back to be read
        // as an answer.
        end("lost");
        return undefined;
      }
      if (run.unheardSince === null) run.unheardSince = now;
      const asked = run;
      return window.PAApi.get("/api/seq/last-run").then((answer) => {
        // An answer to a question asked about an earlier run says nothing
        // about this one.
        if (run === null || run !== asked) return;
        run.unheardSince = null;
        judge(answer.data || {});
      });
    }, { cadenceMs: RUN_POLL_MS, runOnStart: true, refreshOnReturn: true });

    const end = (outcome) => {
      const { name } = run;
      run = null;
      poll.stop();
      onChange({ name, running: false, outcome });
    };

    const judge = (record) => {
      const fresh = !sameRecord(record, run.before);
      const ours = fresh && record.valid === true && record.name === run.name;
      if (ours && record.running === true) {
        run.seen = true;
        return;
      }
      if (ours) {
        end(record.outcome || "completed");
        return;
      }
      // Not this run's record. Once this run was seen, that is something
      // else having the droid: a later run, or a restart that wiped the
      // record.
      if (run.seen) {
        end("replaced");
        return;
      }
      // Not seen yet. Inside the start wait even a new record decides
      // nothing: a run sent just before this one writes its record first, and
      // this run's follows it. Past the wait, a new record is something else's
      // run, and the record from before the press is a run that never began.
      if (Date.now() - run.sentAt >= RUN_START_WAIT_MS) end(fresh ? "replaced" : "not-started");
    };

    // Resolves once the droid has accepted the run, and rejects when it could
    // not be asked or refused. A droid that cannot answer the read before the
    // press is not sent the run: with no record from before, an earlier run of
    // the same sequence could not be told from this one, and the run would
    // read as ended while it plays, or as running after it never started.
    const start = async (name) => {
      const before = (await window.PAApi.get("/api/seq/last-run")).data || {};
      await window.PAApi.postJson("/api/seq/test", { name });
      // A run already watched is over the moment the droid accepts this one:
      // the later run preempts it.
      run = { name, before, sentAt: Date.now(), seen: false, unheardSince: null };
      poll.start();
      onChange({ name, running: true });
    };

    // The droid's non-latching stop. The run is over when its record says so,
    // not when this is answered.
    const stop = () => window.PAApi.postJson("/api/seq/stop", {});

    return { start, stop, running: () => (run ? run.name : null) };
  };

  // The one run this surface has started: the strip and the list row both
  // show it, and both are painted from it (paintRun() below).
  // The endings the surface has a sentence for. A run that ends by itself, is
  // stopped, or gives way to a later run ends without one: the lamp going out
  // is the word.
  const RUN_ENDINGS = {
    "not-started": (name) => `The droid did not start ${name}.`,
    lost: (name) => `Lost touch with the droid; ${name} may still be running.`,
    // The droid takes a run even under a latched estop and ends it in the same
    // pass (src/tasks/sequence_dispatcher.cpp), so the lamp is on for a moment
    // and then off.
    estop: (name) => `The estop stopped ${name}.`,
  };

  const runWatch = createRunWatch(({ name, running, outcome }) => {
    paintRun();
    if (!running && RUN_ENDINGS[outcome]) sayOfRun(name, RUN_ENDINGS[outcome](name));
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

  // The dome's light vocabulary and the words for it are Protocol Check's
  // (data/seq_protocol_check.js domeLights): this page keeps no label of its
  // own. `lightWord` is a token as a builder reads it, in one of the
  // vocabulary's groups.
  const domeLights = SeqProtocolCheck.domeLights;
  const lightWord = (group, token) => domeLights.label(group, token) || "Unknown";
  // A group's tokens as the <option>s of a step card's picker, `current`
  // chosen. A target is shown with its token after it, as the dome spells it.
  const lightOptions = (group, tokens, current, withToken = false) =>
    tokens
      .map((token) =>
        `<option value="${token}" ${token === current ? "selected" : ""}>${window.PAUtils.escapeHtml(lightWord(group, token))}${withToken ? ` (${token})` : ""}</option>`)
      .join("");

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
    phrasesListed();
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
    sessionTimeline.refresh(rehearsalContext());
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
    if (stopBtn) stopBtn.textContent = `Stop ${name}`;
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
      ? `<button type="button" class="seq-act" data-action="share" data-seq-name="${name}" title="Open a pre-filled GitHub issue to share this sequence with the project">Share to project</button>`
      : "";

    const testBtnDisabled = seq.valid === false ? 'disabled title="Invalid sequence cannot be run — edit to repair"' : `data-seq-name="${name}"`;
    const more = moreOpen.has(seq.name);

    return `
      <tbody class="seq-item" data-seq-name="${name}">
        <tr>
          <th scope="row"><span class="seq-name">${window.PAUtils.escapeHtml(seq.name)}</span>${badges.join("")}</th>
          <td>${purposeHtml(seq)}${saved ? `<span class="seq-meta">${saved}</span>` : ""}
            <span class="seq-row-run hidden" role="status"><span class="indicator ok seq-live" aria-hidden="true"></span>Running</span></td>
          <td class="seq-count-cell">${reportedSteps(seq)}</td>
          <td class="seq-count-cell">${reportedLength(seq)}</td>
          <td class="seq-item-acts">
            <span class="seq-acts">
              <button type="button" class="seq-act is-strong" data-action="edit" data-seq-name="${name}">Edit</button>
              <button type="button" class="seq-act" data-action="test" ${testBtnDisabled}>Test</button>
              <button type="button" class="btn btn-sm seq-stop hidden" data-action="stop" data-seq-name="${name}">Stop ${window.PAUtils.escapeHtml(seq.name)}</button>
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
          <td class="seq-count-cell">${reportedLength(builtin)}</td>
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
    const steps = editorState.current.steps;
    const order = stepOrder(steps);
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
    (step.type === "dome" ? domeStepName(step) : stepTypeName[step.type] || step.type || "Step");

  // ---------------------------------------------------------------------------
  // The Picked block tab (#441): what the timeline says is picked, and for one
  // block the inspector - where it starts, then the rows of its kind. Every
  // row is a setting row, and every bound and choice in one is the kind
  // table's (STEP_LIMITS and its neighbours), the same the step list's cards
  // read. Several blocks can be moved together and removed.
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
  const faderOf = (field, value, bounds, label) =>
    `<input class="fader" type="range" ${limits(bounds)} step="1" value="${value}" data-picked="${field}" aria-label="${label}">`;
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
  // (CONTEXT.md "Move Shape").
  const SHAPE_WORDS = {
    servo: { open: "Open", close: "Close", flutter: "Flutter" },
    light: { open: "On", close: "Off", flutter: "Flash" },
  };
  const PANEL_SHAPES = { OP: "open", CL: "close", OF: "flutter" };
  const catalogPart = (id) => (window.DroidParts?.parts || []).find((part) => part.id === id) || null;

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
  // A close is one choice. A flutter owes a later close (Protocol Check), so
  // it is offered where one already follows - on a pair, which keeps that
  // close - and on the flutter itself.
  const moveRows = (step, at, move) => {
    const { words } = move;
    if (move.shape === "close") return settingRow("Motion", segOf("motion", [["close", words.close]], "close", "Motion"));
    const pair = sessionTimeline?.standing(at) || null;
    const flutter = move.shape === "flutter";
    const motion = settingRow("Motion", segOf("motion",
      [["open", words.open], ...(pair || flutter ? [["flutter", words.flutter]] : [])], move.shape, "Motion"));
    if (!move.settles) return motion;
    const far = step.howFar ?? STEP_LIMITS.howFar[1];
    // How far is the one stored `howFar`, said by Part Kind as the Move Shape
    // is: a servo opens to it, a body light is that bright.
    const howFar = move.light
      ? settingRow("Brightness", faderOf("howFar", far, STEP_LIMITS.howFar, "Brightness, percent of full"), `${far}%`)
      : settingRow("Opens to", faderOf("howFar", far, STEP_LIMITS.howFar, "Opens to, percent of its throw"), `${far}%`);
    return (pair ? settingRow("Runs for", numberCell("runs", Math.round(pair.ms), STEP_LIMITS.t, "Runs for, in milliseconds")) : "")
      + howFar
      + motion;
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
  // spreads across is a light, a servo's otherwise (CONTEXT.md "Move Shape").
  const gestureShapeWords = (step) => {
    const members = window.SeqGesture.members(step);
    const lights = members.length > 0 && members.every((id) => window.DroidPartKind?.isLight(catalogPart(id)));
    return lights ? SHAPE_WORDS.light : SHAPE_WORDS.servo;
  };

  // The Gesture's rows, in the step list card's words: Parts, Move, Travels,
  // Order, How far.
  const gestureRows = (step) => {
    const G = window.SeqGesture;
    if (!G) return "";
    const esc = window.PAUtils.escapeHtml;
    const choices = (field) => gestureChoices(field).map((choice) => [choice.id, esc(choice.label)]);
    // A Gesture over a listed set of Parts names them; the list itself is
    // authored in the step list.
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

  // What is rarely set, folded under the card's own line: the pace, the
  // repeat, and the full-throw time and easing only a Gesture overrides
  // (CONTEXT.md "Body Step"). Every and Again are in beats where the routine
  // has a tempo and in milliseconds where it has none, as on the card. An
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
        return settingRow("Runs for", numberCell("durationMs", fieldOf(step, "durationMs"), STEP_LIMITS.turnMs, "Runs for, in milliseconds"))
          + settingRow("Way", segOf("way", [["left", "Left", stopped], ["right", "Right", stopped]], stopped ? "" : step.speedPct < 0 ? "left" : "right", "Way"))
          + settingRow("Speed", faderOf("speed", speed, STEP_LIMITS.speed, "Dome speed, percent"), `${speed}%`);
      }
      case "audio":
        return settingRow("Plays",
          `<input class="number-cell text-cell" type="text" value="${esc(step.cmd ?? "")}" placeholder="$H, $N, $D, $A..." data-picked="cmd" aria-label="Sound command">`);
      case "audioCat":
        return settingRow("Plays", pillsOf("category", AUDIO_CATEGORIES.map((name) => [name, capital(name)]), fieldOf(step, "category"), "Sound category"))
          + settingRow("Fallback", pillsOf("fallback", AUDIO_FALLBACK_SLOTS.map((slot) => [slot.value, esc(slot.label)]), fieldOf(step, "fallback"), "Fallback sound"));
      case "random": {
        // Same pick reuses the pick of the Random Flutter before it, so it is
        // offered only where there is one - or where the step already says it.
        const before = editorState.current.steps.slice(0, at).some((prior) => prior?.type === "random" && prior.set !== "hold");
        const sets = RANDOM_SETS.filter((set) => set !== "hold" || before || step.set === "hold")
          .map((set) => [set, set === "hold" ? "Same pick" : capital(set)]);
        return settingRow("Set", segOf("set", sets, fieldOf(step, "set"), "Which panels it picks from"))
          + settingRow("Action", segOf("mode", RANDOM_MODES.map((mode) => [mode, capital(mode)]), fieldOf(step, "mode"), "What it does to the pick"))
          + settingRow("Distinct", `<input class="switch" type="checkbox" data-picked="distinct"${fieldOf(step, "distinct") ? " checked" : ""} aria-label="Distinct">`)
          + settingRow("Move", numberCell("moveMs", fieldOf(step, "moveMs"), STEP_LIMITS.moveMs, "Move time, in milliseconds"))
          + settingRow("Jitter", numberCell("jitterMs", fieldOf(step, "jitterMs"), STEP_LIMITS.jitterMs, "Jitter, in milliseconds"));
      }
      case "loop":
        return settingRow("Repeats", numberCell("body", fieldOf(step, "body"), loopReach(editorState.current.steps, at), "Steps it repeats", "steps"))
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

  const pickedHtml = (blocks) => {
    const esc = window.PAUtils.escapeHtml;
    const head = (name, sub) =>
      `<div class="sect seq-picked-head"><h3>${esc(name)}</h3><span class="sub">${esc(sub)}</span></div>`;
    if (blocks.length === 0) {
      return head("Nothing picked", "no block")
        + '<p class="hint">Press a block to change it. Shift-press adds another.</p>';
    }
    const stepCount = new Set(blocks.flatMap((block) => block.steps)).size;
    const acts = (others = "") =>
      `<div class="seq-picked-acts">${others}<button type="button" class="seq-act" data-picked="remove">${stepCount > 1 ? `Remove ${stepCount} steps` : "Remove"}</button></div>`;
    const remove = acts();
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
    const startsAt = settingRow("Starts at",
      `<span class="seq-row-ctl">${numberCell("start", Math.round(block.t0), STEP_LIMITS.t, "Starts at, in milliseconds")}`
      + `${beat ? `<span class="seq-unit">${esc(beat)}</span><button type="button" class="seq-act" data-picked="off-beat">Off the beat</button>` : ""}</span>`);
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
      ? '<button type="button" class="seq-act" data-picked="split">Split into steps</button>' : "";
    return head((phrase ? phraseName(step) : block.name) || block.words || stepKindName(step), `${stepKindName(step)}${lights ? ` · ${lights}` : ""} · step ${at + 1}`)
      + `<div class="setting-rows seq-picked-rows">${startsAt}${kindRows(step, at)}</div>`
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
      // name now, as the step list's card writes it.
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
      // lasts the pair, within a flutter's bounds, and the close it owes
      // stays. Turned back, it is an open again, stored as absence, and an
      // open has no length to keep in ms or in beats.
      if (raw === "flutter" && step.shape !== "flutter") {
        const [least, most] = SeqProtocolCheck.BODY_FLUTTER_MS;
        const pair = sessionTimeline?.standing(editorState.current.steps.indexOf(step));
        step.shape = "flutter";
        step.flutterMs = Math.max(least, Math.min(most, Math.round(pair ? pair.ms : least)));
      } else if (raw === "open") {
        delete step.shape;
        delete step.flutterMs;
        delete step.spanBeats;
      }
    } else if (field === "motion") {
      // Open and Flutter are the one step's command, at the same time. Only
      // an open or a close says how far (Protocol Check).
      step.cmd = step.cmd.replace(/^:(OP|OF)/, raw === "flutter" ? ":OF" : ":OP");
      if (raw === "flutter") delete step.howFar;
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
      const [least, most] = loopReach(editorState.current.steps, editorState.current.steps.indexOf(step));
      step.body = Math.max(least, Math.min(most, number));
    } else if (PICKED_NUMBERS.includes(field)) {
      if (!Number.isInteger(number)) return;
      // A duration typed over a span of beats is a millisecond instead
      // (ADR 0058), as it is in the step list.
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
    const step = editorState.current?.steps[at];
    return step ? { at, step } : null;
  };

  // An edit made in the inspector in one act - a number typed, a choice
  // pressed - on the one history. One that changed nothing records nothing.
  const inspect = (field, raw) => keepingFocus(() => {
    const picked = pickedStep();
    if (!picked || historyBusy()) return;
    const { at, step } = picked;
    if (field === "start") {
      sessionTimeline.movePickedTo(Number(raw));
    } else if (field === "runs") {
      sessionTimeline.sizeStanding(at, Number(raw));
    } else if (GESTURE_BEATS.includes(field)) {
      // A Gesture's pace or repeat in beats: resolved from the tempo as the
      // droid resolves it, and one entry of its own (setStepBeat()).
      const beats = parseInt(raw, 10);
      setStepBeat(at, { [field]: Number.isInteger(beats) ? beats : null });
    } else {
      const before = historyBegin();
      writePicked(step, field, raw);
      historyCommit(before);
      rerenderStepTable();
      edited();
      // The blocks a step draws change with its Move Shape: a dome panel's
      // flutter is its own block and leaves the close it owes as another, and
      // an open takes that close back as the end of the one block it then is.
      // A body flutter has a length, so it and its close stay the one block
      // (data/seq_timeline.js closeAt()).
      if (field === "motion") {
        const pair = raw === "open" || step.type === "body" ? sessionTimeline.standing(at) : null;
        sessionTimeline.pick(pair ? [at, pair.close] : [at]);
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
    if (valueEl) valueEl.textContent = `${input.value}%`;
    rerenderStepTable();
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
  const DOME_COMMAND_KIND = "domeCommand";
  const LIBRARY_KINDS = ["audio", "audioCat", "domeRotate", "DV", "DH", DOME_COMMAND_KIND, "random", "loop", "end"];
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
    rerenderStepTable();
    edited();
    const steps = editorState.current.steps;
    sessionTimeline.pick(made.map((step) => steps.indexOf(step)));
    sayOnStage("");
    showTab("block");
  };

  // Insert what was dragged from the library at `at` ms. Whatever lands is
  // one entry in the history, so one Undo takes the whole drop away; a drop
  // that lands nothing records nothing. Where the routine is what turns it
  // away - a loop with nothing to repeat, a step Protocol Check refuses -
  // the stage says why. A pill for something the library does not list - a
  // Part, a set or a sequence that is not there - lands nothing and says
  // nothing: no such pill is drawn.
  const dropOnTimeline = (lib, at) => {
    const [group, id] = libraryKey(lib);
    const steps = editorState.current.steps;
    const endAt = steps.findIndex((step) => step?.type === "end");
    const inLoop = SeqProtocolCheck.loopBodySteps(steps);

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
      const made = drop.lights
        ? [{ t: at, type: "dome", cmd: lightCmd({ ...lightFields(DOME_SUBMODES.DL.starts), target: drop.lights }) }]
        : drop.body
          ? [{ t: at, type: "body", part: part.id }, { t: closes, type: "body", part: part.id, shape: "close" }]
          : [
            { t: at, type: "dome", cmd: drop.panel },
            { t: closes, type: "dome", cmd: window.DomeCommandMap.resolvePanelCommand(part.shorthand, "close") },
          ];
      historyPush();
      steps.splice(endAt === -1 ? steps.length : endAt, 0, ...made);
      landed(made);
      return;
    }

    if (group === "set") {
      // One Gesture over the set, and it says nothing but the set: every
      // other word left off is its default - open, all together, clockwise
      // from the front, at the pace a Gesture takes - so it is stored only
      // where a builder makes it differ.
      if (!librarySets().some((set) => set.id === id)) return;
      const made = { t: at, type: "gesture", set: id };
      historyPush();
      steps.splice(endAt === -1 ? steps.length : endAt, 0, made);
      landed([made]);
      return;
    }

    if (group === "seq") {
      // One step that names the sequence, and nothing more: its reference,
      // and its name now as the label a reader of the file sees. The droid's
      // copy of it is read after it lands (edited() asks), and that is no
      // edit: one Undo takes the step away.
      //
      // Tried on a copy first, as a loop is below: a sequence holds only so
      // many others, and only so many steps.
      const choice = phraseChoices().find((each) => each.id === id);
      if (!choice) return;
      const place = (list) => {
        const made = { t: at, type: "sequence", ref: choice.id, name: choice.label };
        list.splice(endAt === -1 ? list.length : endAt, 0, made);
        return made;
      };
      const trial = JSON.parse(JSON.stringify(editorState.current));
      place(trial.steps);
      trial.steps = stepOrder(trial.steps).map((from) => trial.steps[from]);
      const refused = routineVerdict(trial);
      if (routineVerdict(editorState.current).ok && !refused.ok) {
        sayOnStage(refused.error, "error");
        return;
      }
      phraseAgain(choice.id);
      historyPush();
      landed([place(steps)]);
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
        return loop;
      };
      // Tried on a copy first: a drop never turns a routine the droid accepts
      // into one it refuses - a loop is one more step in a routine that may
      // be full, and there are commands a loop may not repeat. What refuses
      // it is said in Protocol Check's own words.
      const trial = JSON.parse(JSON.stringify(editorState.current));
      wrap(trial.steps);
      const refused = routineVerdict(trial);
      if (routineVerdict(editorState.current).ok && !refused.ok) {
        sayOnStage(refused.error, "error");
        return;
      }
      historyPush();
      landed([wrap(steps)]);
      return;
    }

    if (id === "end" && endAt !== -1) {
      // A routine has one end. Dropped again it is that end, moved - later, or
      // earlier as far as a drag of it would go, which is to its last step.
      sessionTimeline.pick([endAt]);
      sessionTimeline.movePickedTo(at);
      sayOnStage("");
      showTab("block");
      return;
    }

    const dome = libraryDome(id);
    const made = dome
      ? { t: at, type: "dome", cmd: dome.starts }
      : { t: at, type: id, ...stepTypeDefaults[id] };
    historyPush();
    if (id === "end") {
      // The end closes the routine, so it lands no earlier than its last step.
      made.t = Math.max(at, ...steps.filter((_, i) => !inLoop.has(i)).map((step) => Number(step?.t) || 0));
      steps.push(made);
    } else {
      steps.splice(endAt === -1 ? steps.length : endAt, 0, made);
    }
    landed([made]);
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
    const count = editorState.current.steps.length - 1 + made;
    if (count > most) sayOnStage(`That would make ${count} steps. A sequence can have at most ${most}.`, "error");
    return count > most;
  };

  // splitRefused(): the split tried on a copy of the routine - `write()`
  // makes the steps and `place(list, made)` puts them in - and Protocol
  // Check's verdict on it: on each step made, whatever else the routine has
  // wrong, and on the whole routine when it was one the droid accepts. Not
  // ok, nothing lands, and the stage says Protocol Check's reason.
  const splitRefused = (write, place) => {
    const trial = JSON.parse(JSON.stringify(editorState.current));
    const tried = write();
    place(trial.steps, tried);
    trial.steps = stepOrder(trial.steps).map((from) => trial.steps[from]);
    const refusedStep = tried
      .map((made) => SeqProtocolCheck.validateStep(made, trial.steps.indexOf(made), trial.steps, true))
      .find((verdict) => !verdict.ok);
    const refused = refusedStep
      || (routineVerdict(editorState.current).ok ? routineVerdict(trial) : { ok: true });
    if (!refused.ok) sayOnStage(refused.error, "error");
    return !refused.ok;
  };

  // Split into steps: the picked body Gesture is replaced by the moves it
  // already makes, written out as Body Steps, in one entry of the history.
  //
  // The moves are the one expansion there is (SeqGesture.bodyMoves()), read
  // off the Gesture as the droid runs it - its pace and its extent resolved
  // from the tempo and the end - and only those it makes before the end step,
  // where the droid stops a Gesture. A written step says nothing of a full
  // throw's time or an easing: those are the Output's, and only a Gesture
  // overrides them (CONTEXT.md "Body Step"). What was generated and paced by
  // the droid is hand-written after this, and keeps the timing written here
  // (CONTEXT.md "Cadence Floor").
  //
  // Tried on a copy first. If it would leave a step Protocol Check refuses -
  // a flutter that owed its close to a close Gesture now owes a close of its
  // own Part - nothing lands, and the stage says Protocol Check's reason.
  //
  // Three things are refused before that, each in words that say what to do,
  // and whatever else the routine has wrong:
  //   - a flutter Gesture that states no length: a written flutter must
  //     state one, and the Gesture has none to hand on;
  //   - more steps than a sequence holds: a Gesture that repeats can make
  //     thousands of moves;
  //   - inside a loop, a move at or past the end of the loop's pass, where a
  //     step the loop repeats cannot be.
  const splitGesture = () => {
    const picked = pickedStep();
    const G = window.SeqGesture;
    if (!picked || picked.step.type !== "gesture" || !G || G.onDome(picked.step) || historyBusy()) return;
    const { at, step } = picked;
    const steps = editorState.current.steps;
    const run = SeqProtocolCheck.resolveBeats(editorState.current).steps[at];
    // A step a loop repeats is timed from the pass, where the end is not.
    const loop = loopRepeating(steps, at);
    const endAt = steps.findIndex((each) => each?.type === "end");
    const last = loop || endAt === -1 ? Infinity : Number(steps[endAt].t) || 0;
    const moves = G.bodyMoves(run, Number(run.t) || 0).filter((move) => move.t < last);
    if (moves.length === 0) {
      sayOnStage("This gesture makes no move before the end.", "error");
      return;
    }
    if (moves.some((move) => move.shape === "flutter") && !step.flutterMs) {
      sayOnStage("Set how long it flutters (Lasts) before splitting.", "error");
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
      ...(move.shape === "flutter" && step.flutterMs !== undefined ? { flutterMs: step.flutterMs } : {}),
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
  // a sequence that would land inside a loop, a flutter left without its
  // close - lands nothing, and the stage says Protocol Check's reason.
  const splitPhrase = () => {
    const picked = pickedStep();
    if (!picked || picked.step.type !== "sequence" || historyBusy()) return;
    const { at, step } = picked;
    const phrase = phraseRead(step.ref);
    if (!phrase) return;
    const steps = editorState.current.steps;
    const startsAt = Number(SeqProtocolCheck.resolveBeats(editorState.current).steps[at].t) || 0;
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
    libraryStop();
    closeSessionTimeline();
    els.editorView.classList.add("hidden");
    currentEditingSeq = null;
    gestureMoreOpen = false;
    forgetPhrases();
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
        // that way here (CONTEXT.md "Move Shape").
        const part = catalogPart(step.part);
        const { shape, words } = moveOf(step);
        const howFar = step.howFar ? `, ${step.howFar}%` : "";
        return `${words[shape] || shape} ${part ? part.name : step.part || "a part"}${howFar}`;
      }
      case "end":
        return "End of sequence";
      default:
        return "Unknown step";
    }
  };

  // ---------------------------------------------------------------------------
  // The kinds of step, as their fields: what a new step of each kind starts
  // as, the bounds its numbers are offered within, and the choices its pickers
  // hold. One set, read by the step list's cards and by the Picked block tab,
  // so a kind never has two sets of bounds. Protocol Check has the rules
  // (data/seq_protocol_check.js); these are what the controls offer.
  // ---------------------------------------------------------------------------
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

  // A field of a step as anything here reads it - an inspector row, a card,
  // the words on a block: the step's own value, or what a step of its kind
  // starts as. The one fallback, so no reader has a default of its own.
  const fieldOf = (step, field) => step[field] ?? stepTypeDefaults[step.type]?.[field];

  const AUDIO_CATEGORIES = ["alert", "chatty", "general", "happy", "humming", "processing", "sad", "sentimental", "scream", "surprised", "whistle"];
  // A Random Flutter's set - "hold" reuses the pick of the one before it
  // (SLOTSET_HOLD, include/sequence_engine.h) - and what it does to the pick.
  const RANDOM_SETS = ["ring", "pie", "all", "hold"];
  const RANDOM_MODES = ["flutter", "open", "close"];

  // ---------------------------------------------------------------------------
  // The dome's four light commands. A dome step holds one as its `cmd`; the
  // step list's cards and the Picked block tab both read it with lightFields()
  // and write it with lightCmd(), so the grammar is spelled once (Protocol
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
  // The longest command the droid takes: PC_CMD_MAX (include/protocol_check.h).
  // The box holds no more, so a command is never typed past what Save accepts.
  const DOME_COMMAND_CHARS = 63;

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

  // What kind of dome step its command makes it, by name: one of the four
  // light commands, a Panel Action, or a Dome command.
  const domeStepName = (step) =>
    DOME_SUBMODES[lightKind(step.cmd)]?.name || (panelIntent(step) ? stepTypeName.dome : DOME_COMMAND.name);

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
    // A dome step is named by what its command makes it, as the inspector
    // names it (domeStepName()).
    if (step.type === "dome") {
      typeName = domeStepName(step);
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
                <input class="step-t" type="number" value="${step.t || 0}" ${limits(STEP_LIMITS.t)} aria-label="Step time offset (ms)" placeholder="t (ms)" ${isInvalid && validation.field === "t" ? `aria-invalid="true"` : ""}>
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
  // its name are the one sequence.
  const routineVerdict = (seq) => SeqProtocolCheck.validateSequence(seq, {
    self: { id: editorState.current?.id, name: editorState.current?.name },
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
        const ref = (editorState.current.steps || [])
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
          sessionTimeline?.refresh(rehearsalContext());
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

  // A stable id for a sequence being saved that has none: eight lowercase hex
  // digits, never changed after (protocolCheckSeqIdValid()).
  const mintSequenceId = () => {
    const bytes = new Uint8Array(4);
    (window.crypto || globalThis.crypto).getRandomValues(bytes);
    return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
  };

  // The Gesture's form values: numbers that are optional, and words that are
  // optional (an unset word is its default).
  const GESTURE_NUMBER_FIELDS = ["howFar", ...GESTURE_TIMES];
  const GESTURE_WORD_FIELDS = ["set", ...GESTURE_DEFAULTED, "easing"];

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
           <input class="seq-num step-beat-span" type="number" ${limits(SeqProtocolCheck.SPAN_BEATS)} step="1" value="${Number.isInteger(step.spanBeats) ? step.spanBeats : ""}" placeholder="-" aria-label="How many beats it lasts">
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
        const light = lightFields(cmd);
        if (light?.kind === "DL") {
          // One too short to name its lights and its mode says no more than
          // what kind it is.
          if (cmd.split(":").length < 3) return "Sets logic/PSI mood";
          return `Sets ${lightWord("targets", light.target)} to ${lightWord("modes", light.mode)}`;
        }
        if (/^(:|)(OP|CL|OF)/.test(cmd)) {
          return "Operates dome panels";
        }
        return "Dome command";
      }
      case "visualPreset":
        return "Applies a dome visual preset";
      case "domeRotate": {
        const speedPct = fieldOf(step, "speedPct");
        const durationMs = fieldOf(step, "durationMs");
        if (speedPct === 0) {
          return "Stops dome rotation";
        }
        const direction = speedPct < 0 ? "left" : "right";
        const speed = Math.abs(speedPct);
        return `Rotates ${direction} at ${speed}% speed for ${durationMs}ms total`;
      }
      case "loop": {
        return `Repeats ${fieldOf(step, "body")} step(s) every ${fieldOf(step, "periodMs")}ms for ~${fieldOf(step, "durationMs")}ms total`;
      }
      case "random": {
        const set = fieldOf(step, "set");
        const moveMs = fieldOf(step, "moveMs");
        return `Randomly moves ${set} panels with ${moveMs}ms move time`;
      }
      case "audioCat": {
        return `Plays ${aOrAn(fieldOf(step, "category"))} sound`;
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
        behaviorHtml = `<input class="step-field step-field-cmd" type="text" data-field="cmd" value="${window.PAUtils.escapeHtml(step.cmd ?? "")}" placeholder="$H, $N, $D, $A..." aria-label="Sound command">`;
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
          const presetName = lightFields(step.cmd)?.preset ?? "";
          behaviorHtml = `
            <select class="step-field step-field-preset" data-field="preset" aria-label="Visual preset">
              ${lightOptions("presets", domeLights.presets, presetName)}
            </select>
            <input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(step.cmd || DOME_SUBMODES.DV.starts)}">
            <button type="button" class="dome-mode-toggle seq-act" aria-label="Switch to advanced mode">Advanced</button>
          `;
        } else if (domeMode === "logic") {
          // Logic/PSI Mode (DL:) structured step
          // Grammar: DL:<target>:<mode>[:<color>[:<durationSec>]]
          const cmd = step.cmd || DOME_SUBMODES.DL.starts;
          const { target, mode, color, seconds: duration } = lightFields(cmd);

          targetHtml = `
            <select class="step-field dl-target-select" data-field="target" aria-label="Target">
              ${lightOptions("targets", domeLights.targets, target, true)}
            </select>
          `;

          behaviorHtml = `
            <select class="step-field dl-mode-select" data-field="mode" aria-label="Mode">
              ${lightOptions("modes", domeLights.modes, mode)}
            </select>
            <select class="step-field dl-color-select" data-field="color" aria-label="Color">
              ${lightOptions("colors", domeLights.colors, color)}
            </select>
          `;

          timingHtml = `
            <input class="step-field dl-duration-input" type="number" data-field="duration" value="${duration}" ${limits(STEP_LIMITS.lightCount)} aria-label="Duration (seconds)" placeholder="duration (0-99s)">
            <span class="dome-rotate-label">s</span>
          `;

          // Store hidden cmd field for serialization
          behaviorHtml += `<input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(cmd)}">`;
        } else if (domeMode === "text") {
          // Logic Text Mode (DT:) structured step
          // Grammar: DT:<target>:<color>:<durationSec>:<speed>:<encodedText>
          const cmd = step.cmd || DOME_SUBMODES.DT.starts;
          const { target, color, seconds: duration, speed, text: decodedText } = lightFields(cmd);

          targetHtml = `
            <select class="step-field dt-target-select" data-field="target" aria-label="Target">
              ${lightOptions("textTargets", domeLights.textTargets, target, true)}
            </select>
          `;

          behaviorHtml = `
            <select class="step-field dt-color-select" data-field="color" aria-label="Color">
              ${lightOptions("textColors", domeLights.textColors, color)}
            </select>
            <textarea class="step-field dt-text-input" data-field="text" placeholder="Enter text (max 32 chars, one line break allowed)" aria-label="Display text">${window.PAUtils.escapeHtml(decodedText)}</textarea>
          `;

          timingHtml = `
            <input class="step-field dt-duration-input" type="number" data-field="duration" value="${duration}" ${limits(STEP_LIMITS.lightCount)} aria-label="Duration (seconds)" placeholder="0-99s">
            <span class="dome-rotate-label">s</span>
            <input class="step-field dt-speed-input" type="number" data-field="speed" value="${speed}" ${limits(STEP_LIMITS.scroll)} aria-label="Scroll speed (0-9)" placeholder="0-9">
            <span class="dome-rotate-label">speed</span>
          `;

          // Store hidden cmd field for serialization
          behaviorHtml += `<input type="hidden" class="step-field" data-field="cmd" value="${window.PAUtils.escapeHtml(cmd)}">`;
        } else if (domeMode === "holo") {
          // Holo Effect Mode (DH:) structured step
          // Grammar: DH:<target>:<effect>[:<color>[:<durationOrCount>]]
          const cmd = step.cmd || DOME_SUBMODES.DH.starts;
          const { target, effect, color, count: durationOrCount } = lightFields(cmd);

          targetHtml = `
            <select class="step-field dh-target-select" data-field="target" aria-label="Target">
              ${lightOptions("holoTargets", domeLights.holoTargets, target, true)}
            </select>
          `;

          behaviorHtml = `
            <select class="step-field dh-effect-select" data-field="effect" aria-label="Effect">
              ${lightOptions("holoEffects", domeLights.holoEffects, effect)}
            </select>
            <select class="step-field dh-color-select" data-field="color" aria-label="Color">
              ${lightOptions("holoColors", domeLights.holoColors, color)}
            </select>
          `;

          timingHtml = `
            <input class="step-field dh-duration-input" type="number" data-field="durationOrCount" value="${durationOrCount}" ${limits(STEP_LIMITS.lightCount)} aria-label="Duration / count (0-99)" placeholder="duration/count (0-99)">
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
            ${action === "OF" ? "" : `<span class="seq-row-ctl"><span class="seq-unit">How far</span><input class="seq-num" type="number" data-field="howFar" value="${step.howFar ?? ""}" ${limits(STEP_LIMITS.howFar)} placeholder="100" aria-label="How far, percent of the panel's throw"><span class="seq-unit">%</span></span>`}
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
        const rotateSpeedPct = fieldOf(step, "speedPct");
        const rotateDurationMs = fieldOf(step, "durationMs");

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
          <input class="step-field step-field-speed" type="number" data-field="speed" value="${Math.abs(rotateSpeedPct)}" ${limits(STEP_LIMITS.speed)} step="1" aria-label="Speed (0-100%)" placeholder="0-100">
          <span class="dome-rotate-label">%</span>
        `;

        timingHtml = `
          <input class="step-field step-field-durationMs" type="number" data-field="durationMs" value="${rotateDurationMs}" ${limits(STEP_LIMITS.turnMs)} step="1" aria-label="Run for (ms)" placeholder="duration ms">
          <span class="dome-rotate-label">ms</span>
        `;
        break;
      }

      case "loop": {
        const body = fieldOf(step, "body");
        behaviorHtml = `<input class="step-field step-field-body" type="number" data-field="body" value="${body}" ${limits(STEP_LIMITS.body)} aria-label="Steps to repeat" placeholder="body">`;

        timingHtml = `
          <input class="step-field step-field-periodMs" type="number" data-field="periodMs" value="${fieldOf(step, "periodMs")}" ${limits(STEP_LIMITS.periodMs)} aria-label="Every (ms)" placeholder="periodMs">
          <span class="dome-rotate-label">ms</span>
          <input class="step-field step-field-durationMs" type="number" data-field="durationMs" value="${fieldOf(step, "durationMs")}" ${limits(STEP_LIMITS.loopMs)} aria-label="For (ms)" placeholder="durationMs">
          <span class="dome-rotate-label">ms total</span>
        `;
        break;
      }

      case "random": {
        targetHtml = `<select class="step-field step-field-set" data-field="set" aria-label="Target">
          ${RANDOM_SETS.map((set) => `<option value="${set}" ${fieldOf(step, "set") === set ? "selected" : ""}>${set}</option>`).join("")}
        </select>`;

        behaviorHtml = `
          <select class="step-field step-field-mode" data-field="mode" aria-label="Action">
            ${RANDOM_MODES.map((mode) => `<option value="${mode}" ${fieldOf(step, "mode") === mode ? "selected" : ""}>${mode}</option>`).join("")}
          </select>
          <label class="step-field-checkbox"><input type="checkbox" data-field="distinct" ${fieldOf(step, "distinct") ? "checked" : ""} aria-label="Distinct"> Distinct</label>
        `;

        timingHtml = `
          <input class="step-field step-field-moveMs" type="number" data-field="moveMs" value="${fieldOf(step, "moveMs")}" ${limits(STEP_LIMITS.moveMs)} aria-label="Move time (ms)" placeholder="moveMs">
          <span class="dome-rotate-label">ms</span>
          <input class="step-field step-field-jitterMs" type="number" data-field="jitterMs" value="${fieldOf(step, "jitterMs")}" ${limits(STEP_LIMITS.jitterMs)} aria-label="Jitter (ms)" placeholder="jitterMs">
          <span class="dome-rotate-label">ms</span>
        `;
        break;
      }

      case "audioCat":
        behaviorHtml = `
          <select class="step-field step-field-category" data-field="category" aria-label="Category">
            ${AUDIO_CATEGORIES.map((cat) => `<option value="${cat}" ${fieldOf(step, "category") === cat ? "selected" : ""}>${cat}</option>`).join("")}
          </select>
          <select class="step-field step-field-fallback" data-field="fallback" aria-label="Fallback sound">
            ${AUDIO_FALLBACK_SLOTS.map((s) => `<option value="${s.value}" ${fieldOf(step, "fallback") === s.value ? "selected" : ""}>${s.label}</option>`).join("")}
          </select>
        `;
        break;

      case "gesture": {
        // One grid of rows, each choice drawn as what it is: a joined bar for
        // a few peers, wrapping pills for a longer set. Each writes a hidden
        // form value, so the step still reads back through its [data-field]
        // inputs like every other type. Pace and repeat are rarely set and
        // fold away.
        //
        // The hidden value is what the step stores, not what is shown as
        // picked: a word the Gesture does not say is shown as its default and
        // held empty, so reading the form back does not write the default
        // into a step that never said it.
        const G = window.SeqGesture;
        if (!G) break;
        const esc = window.PAUtils.escapeHtml;
        const hidden = (field) => `<input type="hidden" data-field="${field}" value="${esc(step[field] || "")}">`;
        const shown = (field) => (GESTURE_DEFAULTED.includes(field) ? gestureWord(step, field) : step[field] || "");
        const bar = (field, options) => `
          <span class="seg seg-sm" role="group" aria-label="${field}">
            ${options
              .map(
                (o) =>
                  `<button type="button" class="gesture-pick" data-pick="${field}" data-value="${esc(o.id)}" aria-pressed="${o.id === shown(field) ? "true" : "false"}">${esc(o.label)}</button>`,
              )
              .join("")}
          </span>${hidden(field)}`;
        const pills = (field, options) => `
          <span class="seq-pills" role="radiogroup" aria-label="${field}">
            ${options
              .map(
                (o) =>
                  `<button type="button" class="seq-pill gesture-pick" role="radio" data-pick="${field}" data-value="${esc(o.id)}" aria-checked="${o.id === shown(field) ? "true" : "false"}">${esc(o.label)}</button>`,
              )
              .join("")}
          </span>${hidden(field)}`;
        const num = (field, value, placeholder, label, extra = "") =>
          `<input class="seq-num" type="number" ${extra} value="${value ?? ""}" placeholder="${placeholder}" aria-label="${label}">`;
        const sets = (window.DroidParts?.sets || []).map((x) => ({ id: x.id, label: x.label }));
        const beats = limits(SeqProtocolCheck.SPAN_BEATS);
        const tempo = tempoOf();
        const every = tempo
          ? `${num("stepBeats", step.stepBeats, "1", "Beats between parts", `${beats} data-beats="stepBeats"`).replace('class="seq-num"', 'class="seq-num gesture-beats"')}<span class="seq-unit">beats</span>`
          : `${num("stepMs", step.stepMs, G.STEP_DEFAULT_MS, "Milliseconds between parts", `${limits(G.STEP_MS)} data-field="stepMs"`)}<span class="seq-unit">ms</span>`;
        const again = tempo
          ? `${num("repeatBeats", step.repeatBeats, "-", "Repeat every beats", `${beats} data-beats="repeatBeats"`).replace('class="seq-num"', 'class="seq-num gesture-beats"')}<span class="seq-unit">beats</span>`
          : `${num("repeatMs", step.repeatMs, "-", "Repeat every milliseconds", `${limits(G.REPEAT_MS)} data-field="repeatMs"`)}<span class="seq-unit">ms</span>`;
        fieldsContainer.innerHTML = `
          <div class="seq-rows">
            <span class="seq-row-label">Parts</span>
            <div class="seq-row-ctl">${pills("set", sets)}</div>
            <span class="seq-row-label">Move</span>
            <div class="seq-row-ctl">${bar("shape", G.SHAPES.map((id) => ({ id, label: gestureShapeWords(step)[id] })))}</div>
            <span class="seq-row-label">Travels</span>
            <div class="seq-row-ctl">${pills("spread", gestureChoices("spread"))}</div>
            <span class="seq-row-label">Order</span>
            <div class="seq-row-ctl">${bar("direction", gestureChoices("direction"))}${bar("start", gestureChoices("start"))}</div>
            <span class="seq-row-label">How far</span>
            <div class="seq-row-ctl">${num("howFar", step.howFar, STEP_LIMITS.howFar[1], "How far, percent of each part's throw", `${limits(STEP_LIMITS.howFar)} data-field="howFar"`)}<span class="seq-unit">%</span></div>
          </div>
          <details class="seq-more"${step.stepBeats || step.repeatBeats || step.stepMs || step.repeatMs || step.extentMs || step.speedMs || step.easing ? " open" : ""}>
            <summary><svg class="i chev" aria-hidden="true" focusable="false"><use href="#i-chevron-right"/></svg>Pace, repeat and feel</summary>
            <div class="seq-rows">
              <span class="seq-row-label">Every</span>
              <div class="seq-row-ctl">${every}</div>
              <span class="seq-row-label">Again</span>
              <div class="seq-row-ctl">${again}</div>
              <span class="seq-row-label">For</span>
              <div class="seq-row-ctl">${num("extentMs", step.extentMs, "end", "Repeat for milliseconds, to the end when empty", `${limits([0, G.EXTENT_MS_MAX])} data-field="extentMs"`)}<span class="seq-unit">ms</span></div>
              <span class="seq-row-label">Full throw</span>
              <div class="seq-row-ctl">${num("speedMs", step.speedMs, "own", "Full throw time for each part, the part's own when empty", `${limits(G.SPEED_MS)} data-field="speedMs"`)}<span class="seq-unit">ms</span></div>
              <span class="seq-row-label">Easing</span>
              <div class="seq-row-ctl">${bar("easing", [{ id: "", label: "Own" }, ...G.EASINGS.map((x) => ({ id: x, label: capital(x) }))])}</div>
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
    // An edit can name a phrase not read yet - a drop, a pick in either view,
    // an undo - so every edit asks; with nothing unread it sends nothing.
    loadPhrases();
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

  // Neither runs while a block is being dragged on the timeline or a fader
  // in the inspector is held: the gesture's writes are in the routine but not
  // yet an entry, and restoring a copy would replace the very steps it is
  // holding.
  const historyBusy = () => !editorState.current || Boolean(sessionTimeline?.dragging()) || faderRun !== null;

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
            <span class="seq-running hidden" id="seq-editor-running" role="status"><span class="indicator ok seq-live" aria-hidden="true"></span><span id="seq-editor-running-name"></span></span>
            <button id="seq-editor-stop" class="btn btn-sm seq-stop hidden" type="button"></button>
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
                <button id="seq-editor-tap-open" class="seq-act" type="button" aria-expanded="false" aria-controls="seq-editor-tap">Tap along</button>
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

    // Attach event listeners (the strip, the tempo and the drawer once; step rows on every rerender)
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
    paintStripRun();
    // Revert has nothing to take back until there is an edit. Save stays live
    // either way: a sequence being tuned or restored is saved with no edit.
    const revertBtn = document.getElementById("seq-editor-revert");
    if (revertBtn) revertBtn.disabled = !dirty;
    const savedEl = document.getElementById("seq-editor-saved-sub");
    if (savedEl) savedEl.textContent = dirty ? "unsaved edits" : "as saved";
  };

  const updateValidationSummary = () => {
    const validation = routineVerdict(editorState.current);
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
    // A refusal is a sentence, so it takes a line of its own under the acts.
    summaryEl.classList.toggle("is-refused", !validation.ok);
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
    if (lastRunBadge) showRunBadge(lastRunBadge);
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

    // A kind the card has no chip for - a Body Step - has no active chip, and
    // stays the kind it is.
    const prev = editorState.current.steps[stepIdx] || {};
    const step = {
      t: parseInt(tInput.value || 0, 10),
      type: Array.from(typeButtons).find((btn) => btn.classList.contains("active"))?.dataset.type || prev.type || "audio",
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

    // The card's direction and speed are one signed speed on the step.
    if (step.type === "domeRotate") {
      Object.assign(step, turnOf(step.direction || "stop", step.speed ?? 0, step.durationMs));
      delete step.direction;
      delete step.speed;
    }

    // The step is rebuilt from the form, which has no field for a beat, so a
    // beat-placed step keeps its beat through an edit of anything else. Typing
    // a new time is choosing a millisecond instead, and the beat goes; a span
    // in beats goes the same way when its duration is typed over (ADR 0058).
    if (prev.beat !== undefined && step.t === prev.t) step.beat = prev.beat;
    // A Gesture's times in beats have no form field either, so they ride
    // along the same way (their inputs set them directly, setStepBeat()); and
    // a Gesture over a listed set of parts, which the pickers do not offer,
    // keeps its list.
    // A phrase step keeps a label for a reader of the file: its current name.
    if (step.type === "sequence") {
      if (step.ref === "") delete step.ref;
      // Picked here, a phrase whose read failed is asked for again.
      if (step.ref && step.ref !== prev.ref) phraseAgain(step.ref);
      const label = step.ref ? phraseName(step) : "";
      if (label && !label.endsWith("(not on this droid)")) step.name = label;
      else if (prev.name && prev.ref === step.ref) step.name = prev.name;
    }
    if (step.type === "gesture" && prev.type === "gesture") {
      GESTURE_BEATS.forEach((key) => {
        if (prev[key] !== undefined) step[key] = prev[key];
      });
      if (step.set === undefined && Array.isArray(prev.parts)) step.parts = prev.parts;
      // A time the card shows no field for rides along too: a flutter's
      // length, which is set in the inspector, and the pace and the repeat in
      // milliseconds while a tempo has the card showing them in beats. A
      // flutter's length goes with the flutter (Protocol Check).
      GESTURE_TIMES.forEach((key) => {
        const shownOnCard = fieldsContainer.querySelector(`[data-field="${key}"]`) !== null;
        const kept = key !== "flutterMs" || step.shape === "flutter";
        if (!shownOnCard && kept && prev[key] !== undefined) step[key] = prev[key];
      });
    }
    // A Body Step has no form fields at all - it is authored on the timeline -
    // so everything it says but its time rides along; its span in beats goes
    // by the rule below, as any step's does.
    if (step.type === "body" && prev.type === "body") {
      ["part", "shape", "howFar", "flutterMs"].forEach((key) => {
        if (prev[key] !== undefined) step[key] = prev[key];
      });
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

  // Called once from renderEditorView — the strip, the tempo row and the drawer only.
  // These elements are NOT re-created on rerenderStepTable, so listeners must not accumulate.
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

    const addStepBtn = document.getElementById("seq-editor-add-step");
    if (addStepBtn) {
      addStepBtn.addEventListener("click", () => {
        const newStep = { t: 0, type: "audio", ...stepTypeDefaults.audio };
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
        const cmd = lightCmd({ kind: "DV", preset: presetSelect.value });
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
            editorState.current.steps[stepIdx].cmd = DOME_SUBMODES.DV.starts;
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
      const cmd = lightCmd({
        kind: "DL",
        target: targetSelect.value,
        mode: modeSelect.value,
        color: colorSelect ? colorSelect.value : "DEFAULT",
        seconds: durationInput ? durationInput.value : "",
      });
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
      const cmd = lightCmd({
        kind: "DT",
        target: targetSelect.value,
        color: colorSelect.value,
        seconds: durationInput ? durationInput.value : "5",
        speed: speedInput ? speedInput.value : "0",
        text: textInput ? textInput.value : "",
      });
      if (cmd === null) {
        // The step keeps the text it had.
        showEditorFeedback(LIGHT_TEXT_REFUSED, "error");
        return;
      }
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
      const cmd = lightCmd({
        kind: "DH",
        target: targetSelect.value,
        effect: effectSelect.value,
        color: colorSelect ? colorSelect.value : "DEFAULT",
        count: durationInput ? durationInput.value : "",
      });
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
        // The choice it already shows changes nothing, and the choice a
        // Gesture means without saying is held empty, so it is stored only
        // where it differs (gestureWord()).
        if ((pill.getAttribute("aria-pressed") || pill.getAttribute("aria-checked")) === "true") return;
        const field = pill.dataset.pick;
        const unsaid = GESTURE_DEFAULTED.includes(field) && pill.dataset.value === gestureChoices(field)[0].id;
        const before = historyBegin();
        hidden.value = unsaid ? "" : pill.dataset.value;
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
        const submode = { logic: "DL", text: "DT", holo: "DH" }[domeMode];
        if (submode) newDefaults = { cmd: DOME_SUBMODES[submode].starts };

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

    // Re-attach only step-row listeners (the strip's, the tempo's and the drawer's persist)
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
    // Held until the droid has accepted or refused the run: starting one is
    // two requests, and a second press in that time would send a second run,
    // which preempts the first.
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

    // Open editor with copy
    currentEditingSeq = original;
    editorState.isNew = true; // Duplicate is a new sequence
    renderEditorView(currentEditingSeq);
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


// =============================================================================
// data/setup.js
//
// Guided Setup - the first-run takeover (#351, #297, CONTEXT.md "Setup"), and
// nothing else.
//
// A controller arrives provisioned and inert: every component toggle defaults
// false, so nothing on the droid is described. This is the one guided pass that
// asks what the builder has, in the order they built it, continuing the flow
// that started when they gave the droid their WiFi.
//
// It is a TAKEOVER, not a destination, and what it takes over is
// Configuration: its questions ARE Configuration's controls, so a question
// asked during the run and the same question answered afterwards are the same
// markup and cannot drift (#404). Configuration's surface loads this file after
// data/configuration.js. While the run is live that surface shows the run's
// chrome and one step at a time; when the run ends - by reaching the last step,
// or by the builder stopping - the chrome goes and the surface is
// Configuration's ordinary cards again. There is no Setup entry in the nav and
// no Setup page: the old address, /#setup, is an alias of Configuration
// (data/shell.js).
//
// The run never re-opens by itself. The one way back in is Maintenance's
// (data/maintenance.js), which writes the run back to not-run; the rule that
// reads the droid as set up or not is this file's alone.
//
// THE VISITED RECORD is the load-bearing part. A run with defaults answers
// itself: every question already has a value, so a tick beside one would claim a
// confirmation the builder never gave - and ours default to "not fitted", which
// is a statement about their droid. So a step is VISITED once it has actually
// been on screen, stored on the controller as an ordinary config key, and until
// then its answer renders hollow. The answer still shows, because the default is
// real; it has just not been looked at
// (r2d2-astromech-simulator v1.79.0, src/js/config/wizard.js:2216).
// =============================================================================
(() => {
  const head = document.getElementById("wizard-head");
  if (!head) return;

  // A step's text may be a string or a reader, because one of them names
  // something this page only learns at runtime. Resolved in one place so a step
  // that grows a reader does not also grow a branch at every site that shows it.
  const textOf = (value) => (typeof value === "function" ? value() : value);

  const checked = (id) => Boolean(document.getElementById(id)?.checked);
  const fittedOrNot = (id) => (checked(id) ? "Fitted" : "Not fitted");
  const countFitted = (ids) => ids.filter(checked).length;

  // ---------------------------------------------------------------------------
  // The steps - ONE ordered array
  //
  // The rail, the header, the footer, back and next, and every count read this
  // and nothing else. Inserting a step is adding a row: #368 put the Droid
  // Build at position three and nothing below this array had to move.
  //
  // A LEADING UNDERSCORE means "in the run, not in the count" - a step that is
  // shown rather than asked. Today that is the board, which is whichever one this
  // firmware was built for and so was never a question; the convention
  // generalises to any later step that is prose rather than a question, without a
  // second list to keep in step (r2d2-astromech-simulator v1.79.0,
  // src/js/config/wizard.js:50).
  //
  // Four fields, all required, and `why` is CONTENT rather than a doc comment: a
  // step nobody can explain cannot exist, and an explanation on the same object
  // cannot drift from the step it explains. A fifth field - when this answer
  // takes effect - is #370's, and is deliberately not guessed at here.
  //
  // `answer` reads the controls the step itself shows, so the rail says what the
  // builder is looking at rather than what a second copy of the state believes.
  // ---------------------------------------------------------------------------
  const STEPS = [
    {
      key: "wifi",
      title: "WiFi",
      q: "Which network does this droid join?",
      why: "Every screen you drive from, and every new firmware, comes over this link.",
      answer: () => wifiAnswer,
    },
    {
      key: "_board",
      title: "Body Controller",
      // Composed, never typed: writing the board's own name into a string here
      // would be one more of the hardcoded artoo-only sentences #348 is counting,
      // and only what is SHOWN may differ between boards (ADR 0065).
      q: () => (boardLabel ? `This is your ${boardLabel}.` : "This is the board doing the work."),
      why: "Nothing to pick: this firmware was built for this board. Everything after this plugs into it.",
      answer: () => boardLabel,
    },
    {
      key: "build",
      title: "Droid Build",
      q: "Which droid did you build?",
      why: "Your droid starts with the parts its design carries. Dome and body can come from different designs.",
      // The picker's own summary, so the rail and the cards read one answer
      // (data/droid_build_picker.js).
      answer: () => window.DroidBuildPicker?.summary() || "",
    },
    {
      key: "drive",
      title: "Foot Drive",
      q: "What moves the feet?",
      why: "Off: the droid is a statue. Sticks move, wheels don't.",
      answer: () => fittedOrNot("enable-drive"),
    },
    {
      key: "domerot",
      title: "Dome Rotation",
      q: "What turns the dome?",
      why: "Off, the dome sits still through every sequence.",
      answer: () => fittedOrNot("enable-dome-esc"),
    },
    {
      key: "domectl",
      title: "Dome Controller",
      q: "What runs the board up in the dome?",
      why: "Carries light, panel and sound cues to the dome's board. Off, the body drives and the dome stops listening.",
      answer: () => fittedOrNot("enable-protor2link"),
    },
    {
      key: "servos",
      title: "Body servo controller",
      q: "What moves the arms and the spare outputs?",
      why: "Two utility arms, and three spare lines for a servo, a light or a smoke unit.",
      answer: () => {
        const fitted = countFitted([
          "enable-arm1",
          "enable-arm2",
          "enable-aux1",
          "enable-aux2",
          "enable-aux3",
        ]);
        return fitted === 0 ? "None fitted" : `${fitted} fitted`;
      },
    },
    {
      key: "rc",
      title: "Radio Controller",
      q: "What do you drive it with?",
      why: "The droid listens only to the channels you tick. Leave an unwired channel off.",
      answer: () => {
        const fitted = countFitted([
          "enable-rc-ch1",
          "enable-rc-ch2",
          "enable-rc-ch3",
          "enable-rc-ch4",
          "enable-rc-ch5",
          "enable-rc-ch6",
        ]);
        return fitted === 0 ? "No channels" : `${fitted} channels`;
      },
    },
    {
      key: "sound",
      title: "Sound",
      q: "What gives the droid its voice?",
      why: "Off, sequences still run start to finish, in silence.",
      answer: () => fittedOrNot("enable-audio"),
    },
    {
      key: "name",
      title: "Name",
      q: "What is this droid called?",
      why: "The name lives on the droid, so any computer meets the same droid.",
      answer: () => document.getElementById("droid-name-input")?.value || "",
    },
  ];

  // A step is in the COUNT unless its key says otherwise. Every number the run
  // shows is arithmetic on the array above and none is ever typed: the planning
  // for this run said nine steps in one place and ten in another, and a typed
  // total is exactly how that becomes a line on screen that lies.
  const isQuestion = (step) => step.key.charAt(0) !== "_";
  const questionCount = () => STEPS.filter(isQuestion).length;
  // How many questions the run has reached by `index`, counting the step at it.
  const questionsThrough = (index) => STEPS.slice(0, index + 1).filter(isQuestion).length;

  // ---------------------------------------------------------------------------
  // What the controller says about the run
  // ---------------------------------------------------------------------------
  const RUN_NOT_RUN = "not-run";
  const RUN_SKIPPED = "skipped";
  const RUN_COMPLETED = "completed";

  let runState = RUN_NOT_RUN;
  // A droid configured before this record existed carries no record at all
  // (include/guided_setup.h). Its answers are real, once-considered ones, so it
  // is not walked through a first run and its categories are not reported as
  // never asked. The rule lives here rather than in firmware because only this
  // end knows what the steps are.
  let grandfathered = false;
  const visited = new Set();
  let current = 0;
  let wifiAnswer = "";
  let boardLabel = "";
  let saveTimer = null;
  let ending = false;

  const runHasEnded = () => runState !== RUN_NOT_RUN || grandfathered;

  const configuredBeforeTheRecordExisted = (config) => {
    const components = config?.components || {};
    return Object.values(components).some((entry) => entry && entry.enabled === true);
  };

  // ---------------------------------------------------------------------------
  // What is on screen
  //
  // Three states, and the middle one is why the page does not flash: until the
  // controller has answered, this surface says it is reading rather than showing
  // a configuration page that may be about to be replaced by a run.
  //
  // A card is shown during the run when it CONTAINS the current step's body, so
  // the mapping from step to screen is one attribute in the markup and there is
  // no second list of ids here to keep in step with it.
  // ---------------------------------------------------------------------------
  // add/remove rather than toggle(cls, force), which is the idiom the board
  // picture in data/configuration.js already uses on this same class.
  const show = (element, visible) => {
    if (!element) return;
    if (visible) element.classList.remove("hidden");
    else element.classList.add("hidden");
  };

  // Both of these are this surface's own vocabulary - the Operator Shell's
  // chrome carries no .card and nothing else in the document carries a
  // data-setup-step - so they are asked of the document rather than of a
  // container this module would otherwise have to find. Under the shell that
  // container is not the body, and reaching for it is how a surface ends up
  // holding a reference to the shell's furniture.
  const isCard = (element) => Boolean(element?.classList?.contains("card"));

  const cards = () => Array.from(document.querySelectorAll(".card"));

  const stepHosts = () => Array.from(document.querySelectorAll("[data-setup-step]"));

  // The card a step's body sits in. configuration.html nests no card inside another, so
  // the first one above the body is the one that has to be on screen with it.
  const cardFor = (host) => {
    let node = host;
    while (node && !isCard(node)) {
      node = node.parentElement;
    }
    return node || null;
  };

  const applyLayout = (phase) => {
    const checking = document.getElementById("wizard-checking");
    const foot = document.getElementById("wizard-foot");
    show(checking, phase === "checking");
    show(head, phase === "running");
    show(foot, phase === "running");
    // The surface's own title says which of the two it is being: the run, or
    // Configuration. While the controller is still being asked it is neither
    // yet, and it says Configuration, which is what nearly every visit turns
    // out to be.
    show(document.getElementById("setup-title"), phase === "running");
    show(document.getElementById("configuration-title"), phase !== "running");

    // Only a run that is actually on screen has a current step. While the
    // controller is still being asked, there is no step to show and no card to
    // show it in - and saying that once here is what keeps the two loops below
    // from each having to remember it.
    const hosts = stepHosts();
    const currentHost =
      phase === "running"
        ? hosts.find((host) => host.dataset.setupStep === STEPS[current]?.key)
        : null;
    const currentCard = currentHost ? cardFor(currentHost) : null;

    cards().forEach((card) => {
      if (card === checking || card === head || card === foot) return;
      if (phase === "checking") {
        show(card, false);
        return;
      }
      if (phase === "running") {
        show(card, card === currentCard);
        return;
      }
      // The run has ended: the page is its own cards again, minus the ones that
      // only ever belonged to the run.
      show(card, card.dataset.setupRunOnly === undefined);
    });

    hosts.forEach((host) => {
      show(host, phase === "ended" || host === currentHost);
    });
  };

  // ---------------------------------------------------------------------------
  // The run's chrome
  // ---------------------------------------------------------------------------
  const railHost = document.getElementById("wizard-rail");
  const legend = document.getElementById("wizard-legend");
  const questionText = document.getElementById("wizard-question");
  const stepName = document.getElementById("wizard-step-name");
  const whyText = document.getElementById("wizard-why");
  const position = document.getElementById("wizard-position");
  const backButton = document.getElementById("wizard-back");
  const nextButton = document.getElementById("wizard-next");
  const stopButton = document.getElementById("wizard-stop");
  const feedback = document.getElementById("wizard-feedback");

  const setFeedback = (message, variant = "") => {
    if (!feedback) return;
    feedback.textContent = message;
    feedback.className = variant ? `feedback ${variant}` : "feedback";
  };

  const renderRail = () => {
    if (!railHost) return;
    railHost.innerHTML = "";
    const total = questionCount();
    let anyUnseen = false;

    STEPS.forEach((step, index) => {
      const question = isQuestion(step);
      const seen = visited.has(step.key);
      const answer = String(step.answer?.() || "");
      if (question && !seen) anyUnseen = true;

      const chip = document.createElement("button");
      chip.type = "button";
      chip.className = "wizard-chip";
      if (index === current) chip.classList.add("is-current");
      if (index < current) chip.classList.add("is-behind");
      chip.dataset.step = step.key;

      const label = document.createElement("span");
      label.className = "wizard-chip-label";
      if (question) {
        // Hollow until the question has actually been on screen. The answer
        // below still shows either way, because the default is a real value -
        // it has just not been looked at.
        //
        // One shape in two states, and both out of Geometric Shapes on purpose:
        // a check mark is U+2713, inside the Dingbats block, and an operator
        // surface carries no pictograph (ADR 0066, and
        // tools/check_surface_anatomy.py enforces it). The project's own SVG
        // sprite has no tick to borrow either, and adding one means editing
        // data/shell.js. Filled against hollow says the same thing, restyles
        // with the text around it, and needs nobody's permission.
        const tick = document.createElement("span");
        tick.className = seen ? "wizard-tick" : "wizard-tick is-unseen";
        tick.textContent = seen ? "●" : "○";
        label.appendChild(tick);
      }
      const title = document.createElement("span");
      title.className = "wizard-chip-title";
      title.textContent = step.title;
      label.appendChild(title);
      chip.appendChild(label);

      // Every chip gets the answer slot, even when it has nothing to put in it:
      // the reserved height is in the stylesheet, so a chip does not grow a line
      // the moment it has something to say.
      const answerSlot = document.createElement("span");
      answerSlot.className = "wizard-chip-answer";
      answerSlot.textContent = answer;
      chip.appendChild(answerSlot);

      chip.title = question
        ? `Question ${questionsThrough(index)} of ${total} · ${step.title}${answer ? ` · ${answer}` : ""}${
            seen ? "" : "\nNot asked yet: the default, not your answer."
          }`
        : `${step.title} · shown, not asked${answer ? ` · ${answer}` : ""}`;

      chip.addEventListener("click", () => goTo(index));
      railHost.appendChild(chip);
    });

    // The hollow mark needs a legend in visible text, not only in a tooltip: a
    // bench tablet has no hover at all (ADR 0059). It goes when every question
    // has been on screen, because then it explains nothing.
    show(legend, anyUnseen);
    if (legend && anyUnseen) {
      legend.textContent = "Filled: you answered it. Hollow: not asked yet, showing the default.";
    }
  };

  const renderStep = () => {
    const step = STEPS[current];
    if (!step) return;
    if (questionText) questionText.textContent = textOf(step.q);
    if (stepName) stepName.textContent = step.title;
    if (whyText) whyText.textContent = textOf(step.why);

    // Position and escape, and nothing else. When an answer takes effect is a
    // different question for every step and belongs beside the step that owns it
    // (#370); a blanket promise here would be false for at least three of them.
    const total = questionCount();
    const escape = "stop at any step to end the run";
    if (position) {
      position.textContent = isQuestion(step)
        ? `Question ${questionsThrough(current)} of ${total} · ${escape}.`
        : `${step.title}, shown, not asked · question ${Math.min(questionsThrough(current) + 1, total)} of ${total} next · ${escape}.`;
    }

    const last = current >= STEPS.length - 1;
    if (backButton) backButton.disabled = current === 0;
    if (nextButton) nextButton.textContent = last ? "Finish" : "Next";
    renderRail();
  };

  // ---------------------------------------------------------------------------
  // The visited record
  //
  // A step becomes visited the moment it is on screen, and the record is written
  // to the controller like any other config key, so Backup and Restore carries it
  // (operator, 2026-09-17 on #371). Debounced rather than written per step: a
  // builder rattling through with Next would otherwise spend one flash write per
  // press, and the record only has to be right by the time they stop.
  // ---------------------------------------------------------------------------
  const visitedParam = () => (visited.size > 0 ? [...visited].join(",") : "-");

  const saveVisited = async () => {
    if (!window.PAApi) return;
    try {
      await window.PAApi.postForm(
        "/api/config",
        { guidedSetupVisited: visitedParam() },
        { timeoutMs: 5000 },
      );
    } catch (error) {
      // Not swallowed: if the controller did not take the record, a later screen
      // would report questions the builder WAS asked as never asked, which is the
      // one thing this record exists to prevent.
      console.error("[setup] guided setup visited save failed:", error);
      setFeedback(
        `The droid did not record which questions it has shown you: ${window.PAApi.messageFor(error)}`,
        "error",
      );
    }
  };

  const markVisited = (step) => {
    if (!step || visited.has(step.key)) return;
    visited.add(step.key);
    clearTimeout(saveTimer);
    saveTimer = setTimeout(saveVisited, 400);
  };

  // ---------------------------------------------------------------------------
  // Moving through the run
  // ---------------------------------------------------------------------------
  const goTo = (index) => {
    if (runHasEnded()) return;
    current = Math.max(0, Math.min(STEPS.length - 1, index));
    markVisited(STEPS[current]);
    applyLayout("running");
    renderStep();
  };

  // Skipping IS finishing (#297), so both of these end the run for good. They
  // stay two different facts about the droid: the surfaces that report on it
  // afterwards are owed the difference between a builder who answered and one who
  // walked away at question two.
  const endRun = async (how) => {
    if (ending || !window.PAApi) return;
    ending = true;
    clearTimeout(saveTimer);
    [backButton, nextButton, stopButton].forEach((button) => {
      if (button) button.disabled = true;
    });
    setFeedback("Saving…");
    try {
      await window.PAApi.postForm(
        "/api/config",
        { guidedSetupRun: how, guidedSetupVisited: visitedParam() },
        { timeoutMs: 5000 },
      );
    } catch (error) {
      console.error("[setup] guided setup run end failed:", error);
      setFeedback(
        `The droid did not save the end of setup, so the run is still open: ${window.PAApi.messageFor(error)}`,
        "error",
      );
      ending = false;
      [backButton, nextButton, stopButton].forEach((button) => {
        if (button) button.disabled = false;
      });
      if (backButton) backButton.disabled = current === 0;
      return;
    }
    runState = how;
    ending = false;
    setFeedback("");
    applyLayout("ended");
  };

  if (backButton) backButton.addEventListener("click", () => goTo(current - 1));
  if (nextButton) {
    nextButton.addEventListener("click", () => {
      if (current >= STEPS.length - 1) {
        endRun(RUN_COMPLETED);
        return;
      }
      goTo(current + 1);
    });
  }
  if (stopButton) stopButton.addEventListener("click", () => endRun(RUN_SKIPPED));

  // An answer changed under the builder's hand, so the rail says what they are
  // looking at rather than what it said when the step opened.
  document.getElementById("feature-form")?.addEventListener("change", () => {
    if (!runHasEnded()) renderRail();
  });
  document.getElementById("droid-name-input")?.addEventListener("input", () => {
    if (!runHasEnded()) renderRail();
  });
  // The Droid Build is not a form control: it changes through its seam, and
  // the seam tells every surface when it has.
  window.DroidBuild?.onChange(() => {
    if (!runHasEnded()) renderRail();
  });

  // ---------------------------------------------------------------------------
  // What the run opens on
  // ---------------------------------------------------------------------------
  const renderWifiStep = (config) => {
    const wifi = config?.wifi || {};
    const state = document.getElementById("wizard-wifi-state");
    const line = document.getElementById("wizard-wifi-prose");
    const ssid = String(wifi.staSsid || "");
    if (!wifi.provisioned) {
      wifiAnswer = "Its own network";
      if (state) state.textContent = "not given a network yet";
      if (line) {
        line.textContent =
          "You are on the droid's own network. Give it yours and reach it from anywhere that network does.";
      }
      return;
    }
    if (wifi.mode === "standalone_ap") {
      wifiAnswer = "Standalone AP Mode";
      if (state) state.textContent = "its own network, on purpose";
      if (line) {
        line.textContent =
          "The droid broadcasts its own network, on purpose. Connect to the droid to reach it.";
      }
      return;
    }
    wifiAnswer = ssid || "WiFi Client Mode";
    if (state) state.textContent = ssid ? `joins ${ssid}` : "set to join a network";
    if (line) {
      line.textContent = ssid
        ? `The droid joins ${ssid}. Every screen you drive from comes over it.`
        : "The droid is set to join a network of yours.";
    }
  };

  const applyIdentity = (identity) => {
    const board = identity?.board;
    if (!board) return;
    // BOARD_LABELS is data/configuration.js's, which the surface loads before
    // this file: one word for the board, read by its picture and by this step.
    boardLabel = BOARD_LABELS[board] || String(board);
    if (!runHasEnded()) renderStep();
  };

  window.addEventListener("pa:identity-available", (event) => applyIdentity(event.detail));
  if (window.PAIdentity) applyIdentity(window.PAIdentity);

  // Synchronously, before anything is fetched: this surface does not yet know
  // whether it is a guided run or a configuration page, and showing either one
  // and swapping it a moment later is a page that flickers on every visit.
  // Saying it is reading is what the rest of this page already does while it
  // waits, and it is the honest answer.
  applyLayout("checking");

  const loadRun = async () => {
    const result = await window.PAApi.get("/api/config", { timeoutMs: 5000 });
    const config = result.data;
    const guided = config?.guidedSetup || {};

    runState = typeof guided.run === "string" ? guided.run : RUN_NOT_RUN;
    grandfathered = guided.recorded === false && configuredBeforeTheRecordExisted(config);
    (Array.isArray(guided.visited) ? guided.visited : []).forEach((key) => visited.add(String(key)));

    renderWifiStep(config);

    if (runHasEnded()) {
      applyLayout("ended");
      return true;
    }
    goTo(current);
    return true;
  };

  const RUN_SECTION = "setup-guided-run";

  // No bootstrap to hand the failure to, so this path says so where the
  // builder is looking rather than leaving the card reading "finding out" for
  // ever.
  const loadRunLoose = () =>
    loadRun().catch((error) => {
      console.error("[setup] guided run unavailable:", error);
      const checking = document.getElementById("wizard-checking");
      const line = checking?.querySelector(".hint");
      if (line) {
        line.textContent = `No answer on whether this droid is set up: ${window.PAApi.messageFor(error)}`;
      }
    });

  // Registered as a bootstrap section rather than fetched loose: a read this
  // surface cannot render without belongs to the Page Recovery View, which
  // already says what is missing and retries it, so a failure here is not a
  // page that sits silently on "reading this droid".
  if (window.PABootstrap) {
    window.PABootstrap.registerSection(RUN_SECTION, loadRun, {
      label: "whether this droid has been set up",
    });
  } else {
    loadRunLoose();
  }

  // Maintenance's way back in (#297). It has already written the run back to
  // not-run, and it says so only once the shell has put this surface back in
  // the document (data/maintenance.js). The run is then read again from the
  // controller, through the same section that read it the first time, rather
  // than reopened on the event's word - the controller's answer is what decides
  // whether the droid is set up, here as everywhere else.
  window.addEventListener("pa:guided-setup-reopened", () => {
    current = 0;
    // Ending the run left its buttons disabled - nothing was meant to press
    // them again - and a reopened run nobody can move through is not open.
    [backButton, nextButton, stopButton].forEach((button) => {
      if (button) button.disabled = false;
    });
    applyLayout("checking");
    if (window.PABootstrap) window.PABootstrap.refreshSections([RUN_SECTION]);
    else loadRunLoose();
  });
})();

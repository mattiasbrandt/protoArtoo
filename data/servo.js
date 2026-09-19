// =============================================================================
// servo.js
//
// Servos page controller: one row per Output the droid reports, with Open,
// Close and Stop where the Output carries a servo, and a Test row per such
// Output. SSE-first status delivery (consume `status` events from
// PAStatusStream), with visibility-aware fallback polling when SSE is
// unavailable. Sends open/close/stop/position commands via POST /api/servo.
//
// THIS FILE KNOWS NO OUTPUT. Which Outputs the droid has, what each is called
// and what each carries is the firmware's answer, handed over by
// data/output_settings.js from GET /api/config (every components{} entry
// carrying an `address`, in the firmware's order). An Output is called by what
// its board prints beside the pin - ARM3 on the Artoo PCB, GPIO 4 on the
// FireBeetle 2 - and that word is also what POST /api/servo takes to move it
// (ADR 0033 Amendment 2026-09-19), so a row sends the name it shows, as the
// firmware gave it. Rows join the status payload and the recorded ends by the
// Output's stored id, never by its name.
//
// READS calibration via GET /api/config and never writes it: an end is set on
// Parts, by driving the part and pressing the button for that end, and this
// page's Test Open and Test Close drive to the ends the droid recorded there
// (#400). The `calib` names below are kept because reading the calibration is
// still exactly what they do.
// =============================================================================
(() => {
  const controls = document.getElementById("output-controls");
  const controlsSummary = document.getElementById("output-controls-summary");
  const outputFeedback = document.getElementById("output-feedback");
  const servoTestCard = document.getElementById("servo-test-card");
  const testRows = document.getElementById("servo-test-rows");
  const calibFeedback = document.getElementById("calib-feedback");

  // The Outputs as data/output_settings.js last handed them over - id, name,
  // address - in the firmware's order, and the answer for each: whether it is
  // wired and what it carries.
  let outputs = [];
  let answer = {};
  let lastPayload = {};

  // The recorded ends, read from /api/config and never written from here, keyed
  // by the Output's stored id. Test Open and Test Close drive to these, so what
  // a builder sees is what the droid will really do when something says open.
  //
  // The defaults stand in when a field is absent, which is a real answer rather
  // than a missing one: addServoOutputFields() leaves an Output Address with no
  // live row OUT of the document instead of inventing a number, and names this
  // fallback as the reason it may (src/web/api_config.cpp).
  const DEFAULT_OPEN_US = 2000;
  const DEFAULT_CLOSE_US = 1000;
  let config = null;
  const endOf = (id, end) => {
    const value = Number(config?.[`${id}${end === "open" ? "OpenUs" : "CloseUs"}`]);
    return Number.isFinite(value) && value > 0 ? value : end === "open" ? DEFAULT_OPEN_US : DEFAULT_CLOSE_US;
  };

  const LED_STRIP = "rgb";
  const SERVO_LABELS = { mg996r: "MG996R servo", mg90s: "MG90S servo" };
  const carriesServo = (id) => Boolean(SERVO_LABELS[answer[id]?.type]);

  const element = (tag, className, text) => {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const button = (label, onPress) => {
    const node = element("button", "btn", label);
    node.type = "button";
    node.addEventListener("click", onPress);
    return node;
  };

  const setFeedback = (el, text, cls = "") => {
    if (!el) return;
    el.textContent = text;
    el.className = cls ? `feedback ${cls}` : "feedback";
  };

  // -------------------------------------------------------------------------
  // What a press sends. `output.name` is the firmware's word for the Output,
  // sent exactly as it came: a space in it (GPIO 49) is the board's, and the
  // firmware matches it.
  // -------------------------------------------------------------------------
  const postServoAction = async (output, action) => {
    if (!window.PAApi) return;
    const label = `${output.name} ${action}`;
    try {
      await window.PAApi.postForm("/api/servo", { arm: output.name, action }, { timeoutMs: 3000 });
      setFeedback(outputFeedback, `${label} sent at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      setFeedback(outputFeedback, `${label} failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  const postServoPosition = async (output, pulseUs) => {
    if (!window.PAApi) return;
    const label = `${output.name} → ${pulseUs} µs`;
    try {
      await window.PAApi.postForm("/api/servo",
        { arm: output.name, action: "position", positionUs: String(pulseUs) },
        { timeoutMs: 3000 });
      setFeedback(outputFeedback, `Test ${label} at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      setFeedback(outputFeedback, `Test ${label} failed: ${window.PAApi.messageFor(error)}`, "error");
    }
  };

  // -------------------------------------------------------------------------
  // The rows. An Output is on this page when it is wired or the droid reports
  // it live; what it carries decides what its row offers.
  // -------------------------------------------------------------------------
  const present = (output) => Boolean(answer[output.id]?.enabled) || output.id in lastPayload;

  const detailOf = (output) => {
    const type = SERVO_LABELS[answer[output.id]?.type] || "";
    const live = typeof lastPayload[output.id]?.detail === "string" ? lastPayload[output.id].detail : "";
    return type && live ? `${type} · ${live}` : type || live;
  };

  const controlRow = (output) => {
    const row = element("div", "arm-control-row");
    row.dataset.output = output.id;
    row.appendChild(element("span", "arm-name", output.name));
    if (answer[output.id]?.type === LED_STRIP) {
      row.appendChild(element("span", "arm-position", "LED strip · set its length in Configuration"));
      return row;
    }
    const position = element("span", "arm-position", detailOf(output));
    position.dataset.detail = output.id;
    row.appendChild(position);
    const acts = element("span", "arm-acts");
    for (const [label, action] of [["Open", "open"], ["Close", "close"], ["Stop", "stop"]]) {
      const press = button(label, () => postServoAction(output, action));
      press.dataset.action = action;
      acts.appendChild(press);
    }
    row.appendChild(acts);
    return row;
  };

  const testRow = (output) => {
    const row = element("div", "arm-control-row");
    row.dataset.output = output.id;
    row.appendChild(element("span", "arm-name", output.name));
    row.appendChild(element("span", "arm-position", "type a width, or drive to a recorded end"));
    const acts = element("span", "arm-acts");
    const width = element("input", "input-narrow");
    width.type = "number";
    width.min = "500";
    width.max = "2500";
    width.step = "10";
    width.value = "1500";
    width.setAttribute("aria-label", `${output.name} test pulse width in microseconds`);
    acts.appendChild(width);
    const test = button(`Test ${output.name}`, () => postServoPosition(output, Number(width.value) || 1500));
    test.dataset.action = "test";
    const open = button("Test Open", () => postServoPosition(output, endOf(output.id, "open")));
    open.dataset.action = "test-open";
    const close = button("Test Close", () => postServoPosition(output, endOf(output.id, "close")));
    close.dataset.action = "test-close";
    [test, open, close].forEach((node) => acts.appendChild(node));
    row.appendChild(acts);
    return row;
  };

  const summaryText = (shown) => {
    const wired = outputs.filter((output) => answer[output.id]?.enabled).length;
    const drive = shown.filter((output) => carriesServo(output.id)).length;
    return `${wired} of ${outputs.length} wired · ${drive} ${drive === 1 ? "drives a servo" : "drive a servo"}`;
  };

  // Drawn again only when what the rows are made of changes, so a status frame
  // arriving every second does not rebuild buttons under a builder's finger.
  let drawn = null;
  const render = () => {
    if (!controls) return;
    const shown = outputs.filter(present);
    const drivable = shown.filter((output) => carriesServo(output.id));
    const shape = shown.map((output) => `${output.id}:${output.name}:${answer[output.id]?.type}`).join(",");

    if (shape !== drawn) {
      drawn = shape;
      if (outputs.length === 0) {
        controls.replaceChildren(element("p", "hint", "Reading the outputs from the droid…"));
      } else if (shown.length === 0) {
        const note = element("p", "note");
        note.innerHTML = '<b>No output wired.</b> Mark one wired on <a class="setup-link" href="#wiring">Wiring</a>.';
        controls.replaceChildren(note);
      } else {
        const rows = shown.filter((output) => carriesServo(output.id) || answer[output.id]?.type === LED_STRIP);
        controls.replaceChildren(...(rows.length
          ? rows.map(controlRow)
          : [element("p", "note", "Nothing to drive. The wired outputs carry nothing yet.")]));
      }
      if (testRows) testRows.replaceChildren(...drivable.map(testRow));
    } else {
      shown.forEach((output) => {
        const detail = Array.from(controls.querySelectorAll("[data-detail]"))
          .find((node) => node.dataset.detail === output.id);
        if (detail) detail.textContent = detailOf(output);
      });
    }
    if (controlsSummary) controlsSummary.textContent = outputs.length ? summaryText(shown) : "finding out";
    if (servoTestCard) servoTestCard.classList.toggle("hidden", drivable.length === 0);
  };

  // -------------------------------------------------------------------------
  // Status rendering -- shared by SSE and fallback polling paths
  // -------------------------------------------------------------------------
  const renderStatus = (payload) => {
    lastPayload = payload && typeof payload === "object" ? payload : {};
    render();
  };

  const refreshStatusOnce = async () => {
    if (!window.PAApi) return;
    const result = await window.PAApi.get("/api/status", { timeoutMs: 3000 });
    renderStatus(result.data);
  };

  // -------------------------------------------------------------------------
  // Calibration load -- read only
  // -------------------------------------------------------------------------
  const loadCalib = async ({ handle = null } = {}) => {
    if (!window.PAApi) throw new Error("API helper unavailable");
    setFeedback(calibFeedback, "Loading calibration...");
    try {
      const api = handle || window.PAApi;
      const result = await api.get("/api/config");
      config = result.data || {};
      setFeedback(calibFeedback, `Calibration loaded at ${new Date().toLocaleTimeString()}`, "success");
    } catch (error) {
      console.error("[servo] loadCalib failed:", error);
      setFeedback(calibFeedback, `Failed to load calibration: ${window.PAApi.messageFor(error)}`, "error");
      throw error;
    }
  };

  // -------------------------------------------------------------------------
  // Boot -- load config then start status subscription
  // -------------------------------------------------------------------------

  // Page Recovery: register startup API load as a section so the bootstrap
  // can show recovery state if the config fetch fails.
  // See docs/page-load-recovery-architecture.md and ADR 0019.
  const SECTIONS = [
    ["servo-calibration", loadCalib, "servo calibration"],
  ];

  const startPageLoad = () => {
    if (!window.PABootstrap) {
      loadCalib().catch(() => {});
      return;
    }
    window.PABootstrap.setResourceLabels?.({
      "/web_api.js": "controller connection",
      "/status_stream.js": "live updates",
      "/shell.js": "page layout",
      "/output_settings.js": "the outputs",
      "/servo.js": "servo control",
      "/footer.js": "page footer",
    });
    SECTIONS.forEach(([name, load, label]) =>
      window.PABootstrap.registerSection(name, load, { label })
    );
  };

  startPageLoad();

  // The servo on each output, set here; the wired ticks and the LED strip are
  // Wiring's. One answer drawn on both surfaces (data/output_settings.js), and
  // the rows on this page follow it the moment it changes - an Output just
  // given the LED strip stops offering servo moves at once.
  window.PAOutputSettings?.mount("type", {
    body: document.getElementById("servo-types-body"),
    feedback: document.getElementById("servo-types-feedback"),
  });
  window.PAOutputSettings?.onChange((state, facts) => {
    if (!state || !Array.isArray(facts)) return;
    outputs = facts;
    answer = state;
    render();
  });

  // SSE-first status updates with visibility-aware fallback polling.
  if (window.PAStatusStream?.isSupported()) {
    window.PAStatusStream.subscribe((eventType, payload) => {
      if (eventType === "status") renderStatus(payload);
    });
    // One-shot fetch if SSE hasn't delivered a status frame yet.
    if (!window.PAStatusStream.getLastStatus()) {
      refreshStatusOnce().catch(() => {});
    }
  } else {
    // Fallback: poll every 1 s, suspended while the tab is hidden and while the
    // operator is reading another surface -- the shell stops it on the way out
    // and starts it again on the way back (ADR 0048, #360). A failed read is
    // PASurface.poll()'s to report; catching it here would mark the surface
    // current on a refresh that never landed (#360).
    window.PASurface.poll(refreshStatusOnce, {
      cadenceMs: 1000,
      runOnStart: true,
      refreshOnReturn: true,
    }).start();
  }
})();

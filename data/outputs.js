// =============================================================================
// data/outputs.js
//
// What an Output is, answered once for the whole browser (#415). Wiring,
// Servos, Parts, Lights, Backup and the Dashboard ask this module and none of
// them works the answer out again.
//
// AN OUTPUT IS ITS ROW (ADR 0068). GET /api/servo/outputs answers one row per
// Output, keyed by its Output Address (CONTEXT.md "Output Address"), with
// everything a builder sets on it - its wired tick, what is on the wire, its
// light's LED count, its Motion Profile and boot behaviour, its ends and the
// Parts on it - and what each row can save, as data: `switchable`,
// `lightCapable`. POST /api/config takes the same rows back, in the same shape,
// as `outputs`: that is the one door every Output save goes through, and this
// module is what sends it. GET /api/config carries no Output at all.
//
// THIS FILE KNOWS NO OUTPUT (ADR 0065). It relays what the running firmware
// reports - which Outputs exist, in its order, what each is called, which can
// carry a light and what each can save - and never lists an id, a count or a
// board label of its own (operator, 2026-09-19 on #411: "the outputs is
// supposed to be dynamic").
//
// ONE WIRED RULE. An Output with a wired tick is wired when its tick says so.
// An Output with none - an expander's channel - has no tick anybody could have
// turned off, so it reads as wired, has no wired switch, and is called by its
// address (operator, 2026-09-23 on #415).
//
// WHAT IS ON THE WIRE IS ONE ANSWER IN TWO VOCABULARIES (CONTEXT.md "Output",
// ADR 0067): a servo's model where the wire drives a servo, a Light Type where
// it lights something. One stored field holds either, so `rgb` is not a servo
// model - it is the LED strip Light Type. Both word lists live here and
// nowhere else in data/.
//
// HOW A SERVO MOVES is its Motion Profile (ADR 0052, #414): time to full
// throw, time to get up to speed and the ease. What it does at power-up - its
// boot behaviour - rides the same row, and the ease and boot words live here
// beside the other two vocabularies.
//
// DATA ONLY. Nothing here touches the page: the plates are drawn by
// data/output_settings.js from what this module holds.
// =============================================================================
(() => {
  "use strict";

  // The servos an Output can carry, and "nothing yet" on one that may carry a
  // light instead. An Output that cannot carry a light always carries a servo.
  const SERVO_MODELS = Object.freeze([
    Object.freeze({ id: "mg996r", label: "MG996R" }),
    Object.freeze({ id: "mg90s", label: "MG90S" }),
  ]);
  const NO_SERVO = Object.freeze({ id: "none", label: "None" });

  // The Light Types protoArtoo can put on one of its own wires (CONTEXT.md
  // "Light Type", ADR 0067). One today; the list is what grows when there are
  // more, and the stored token stays the one the firmware already saves.
  const LIGHT_TYPES = Object.freeze([Object.freeze({ id: "rgb", label: "LED strip" })]);

  // The shape of a move (ADR 0052), in the words the firmware stores.
  const EASES = Object.freeze([
    Object.freeze({ id: "none", label: "none" }),
    Object.freeze({ id: "soft", label: "soft" }),
    Object.freeze({ id: "overshoot", label: "overshoot" }),
  ]);

  // What an Output does at power-up (ADR 0052), keyed by the token the firmware
  // stores. Limp is the default and first.
  const BOOTS = Object.freeze([
    Object.freeze({ id: "limp", label: "limp" }),
    Object.freeze({ id: "home-hold", label: "home and hold" }),
    Object.freeze({ id: "home-release", label: "home and release" }),
  ]);

  const lightType = (token) => LIGHT_TYPES.find((type) => type.id === token) || null;
  const servoModel = (token) => SERVO_MODELS.find((model) => model.id === token) || null;

  const text = (value) => (typeof value === "string" ? value : "");

  // ---------------------------------------------------------------------------
  // The two reads, as the droid last answered them
  // ---------------------------------------------------------------------------
  let config = null; // GET /api/config's whole answer, or the last save's
  let rows = null; // GET /api/servo/outputs rows, read by readRow()
  let outputs = Object.freeze([]);
  // What each Output was first reported with this session: its wired tick,
  // its Light Type and its LED count. All three are read once when the droid
  // starts (ADR 0027, src/tasks/aux_led.cpp), so a later answer that differs
  // from this is one waiting for a restart.
  const started = new Map();
  const listeners = new Set();

  const number = (value) => (typeof value === "number" ? value : null);

  // One GET /api/servo/outputs row, in the shape every surface reads.
  const readRow = (row) => ({
    address: String(row.address),
    // What the board prints beside it; "" for an address no board prints.
    printed: text(row.name),
    // The stored config id, where the board has one. Never shown.
    id: text(row.id),
    switchable: row.switchable === true,
    wiredTick: row.wired === true,
    lightCapable: row.lightCapable === true,
    ledCount: number(row.ledCount),
    throwMs: number(row.throwMs),
    accelMs: number(row.accelMs),
    ease: text(row.ease),
    boot: text(row.boot),
    parts: Array.isArray(row.parts) ? row.parts.map(String) : [],
    // A firmware older than the output-first table reports no position at
    // all, which is not the same as an Output with no pulse.
    reported: "commandedUs" in row,
    bandLoUs: Number(row.bandLoUs) || 0,
    bandHiUs: Number(row.bandHiUs) || 0,
    commandedUs: number(row.commandedUs),
    targetUs: number(row.targetUs),
    // How many nudges have ended on this Output (#363); null from a firmware
    // that does not say, which a run must refuse rather than wait on.
    nudgesDone: number(row.nudgesDone),
    // What the calibration dial reads (#364). The three widths are the
    // recorded positions, directional: openUs is whichever end the builder
    // recorded as open, so nothing here sorts the pair.
    component: text(row.component),
    openUs: number(row.openUs),
    centreUs: number(row.centreUs),
    closeUs: number(row.closeUs),
    calibrated: row.calibrated === true,
    // The pair this Output held before the upgrade, when its part's range
    // could not take it and the droid moved it in (#417); null otherwise, and
    // from a firmware that does not say.
    narrowedFrom:
      row.narrowedFrom && typeof row.narrowedFrom.openUs === "number" &&
      typeof row.narrowedFrom.closeUs === "number"
        ? { openUs: row.narrowedFrom.openUs, closeUs: row.narrowedFrom.closeUs }
        : null,
    held: row.held === true,
    // Why there is no pulse, meaningful only while commandedUs is null.
    limp: typeof row.limp === "string" ? row.limp : "off",
  });

  // One Output, from its row.
  //
  //   address          its Output Address: what a command and a save name
  //   id               the stored config id, where the board has one; "" for
  //                    an expander's channel. Never shown.
  //   label            what the board prints beside it, or "" where no board
  //                    prints anything; also the word POST /api/servo moves it
  //                    by (ADR 0033 Amendment 2026-09-19)
  //   name             what a builder calls it: the label, else its address
  //   fromConfig       the board declares it: it has a stored id, a label and
  //                    a wired tick (the name dates from when its settings
  //                    were read from GET /api/config)
  //   fromTable        GET /api/servo/outputs has a row for it - every Output
  //   switchable       it has a wired tick, so a page may offer one
  //   wired            the one wired rule (header)
  //   canLight         a Light Type may go on this wire at all
  //   type             the stored token: a servo model or a Light Type
  //   light, servo     that token as a Light Type or as a servo model, or null
  //   ledCount         how many LEDs its light has
  //   ledCountSettable its row carries an LED count, which it does exactly
  //                    where a light can go
  //   throwMs, accelMs its Motion Profile's two times, or null where the row
  //   ease             reports none; the ease as the builder chose it
  //   motionSettable   its row carries all three
  //   boot             what it does at power-up, as the builder chose it
  //   bootSettable     its row carries one
  //   started          what it was first reported with (above), or null
  //   parts ...        the rest of its row, read by readRow()
  const outputOf = (row) => {
    const { printed, wiredTick, lightCapable, ...table } = row;
    const type = table.component || (lightCapable ? NO_SERVO.id : SERVO_MODELS[0].id);
    const output = {
      ...table,
      label: printed,
      name: printed || table.address,
      fromConfig: table.id !== "",
      fromTable: true,
      wired: table.switchable ? wiredTick : true,
      canLight: lightCapable,
      type,
      light: lightType(type),
      servo: servoModel(type),
      ledCount: table.ledCount || 1,
      ledCountSettable: lightCapable && table.ledCount !== null,
      motionSettable: table.throwMs !== null && table.accelMs !== null && table.ease !== "",
      bootSettable: table.boot !== "",
    };
    if (output.fromConfig && !started.has(output.address)) {
      started.set(output.address, Object.freeze({
        wired: output.wired,
        light: output.light ? output.light.id : null,
        ledCount: output.ledCount,
      }));
    }
    output.started = started.get(output.address) || null;
    output.parts = Object.freeze(output.parts.slice());
    return Object.freeze(output);
  };

  // The Outputs, in the firmware's order (include/board_outputs.h BOARD_OUTPUTS
  // first, then any the table alone holds).
  const join = () => {
    outputs = Object.freeze((rows || []).map(outputOf));
  };

  const publish = () => {
    join();
    listeners.forEach((listener) => listener(outputs));
  };

  const apiFor = (handle) => {
    const api = handle || window.PAApi;
    if (!api) throw new Error("no way to reach the Body Controller");
    return api;
  };

  const readTable = async (api) => {
    const answer = await api.get("/api/servo/outputs");
    const table = answer?.data?.outputs;
    if (!Array.isArray(table)) throw new Error("the droid's outputs answer carries no table");
    return table.map(readRow);
  };

  const readConfig = async (api) => {
    const answer = await api.get("/api/config");
    const data = answer?.data;
    return data && typeof data === "object" ? data : {};
  };

  /**
   * Read the droid: its servo table, which is the Outputs, and its config, as
   * one snapshot. A surface that needs the config for anything else - its
   * lanes, the Droid Build, the log level - takes it from the answer rather
   * than reading GET /api/config a second time.
   *
   * Both are always read. There used to be a `rows: false` for a surface that
   * wanted the config alone, while the Outputs' settings were on the config;
   * an Output is its row now (ADR 0068), so a read without the table has no
   * Outputs in it, and the option is ignored.
   *
   * @param {object} [opts]
   * @param {object} [opts.handle] - the section's request handle, or PAApi
   * @returns {Promise<{config: object, outputs: object[]}>}
   */
  const load = async ({ handle = null } = {}) => {
    const api = apiFor(handle);
    // Both answers land before either is taken, so a half-answered read never
    // publishes a join of a new half with an old one.
    const table = await readTable(api);
    const answer = await readConfig(api);
    rows = table;
    config = answer;
    publish();
    return { config, outputs };
  };

  /**
   * Read the servo table alone: the bench feed's read, and the only read a
   * surface that shows no config needs.
   *
   * @param {object} [opts]
   * @param {object} [opts.handle] - the section's request handle, or PAApi
   * @returns {Promise<object[]>} the Outputs
   */
  const refresh = async ({ handle = null } = {}) => {
    rows = await readTable(apiFor(handle));
    publish();
    return outputs;
  };

  // What a surface may ask to save, and the row key each is saved under
  // (POST /api/config `outputs`, docs/api.md). Each is offered only where the
  // Output's row says it can be saved, and asking for anything else is refused
  // rather than dropped.
  const PATCH_FIELDS = {
    wired: { key: "wired", can: (output) => output.switchable, value: (v) => v === true },
    type: { key: "component", can: () => true, value: String },
    ledCount: { key: "ledCount", can: (output) => output.ledCountSettable },
    throwMs: { key: "throwMs", can: (output) => output.motionSettable },
    accelMs: { key: "accelMs", can: (output) => output.motionSettable },
    ease: { key: "ease", can: (output) => output.motionSettable, value: String },
    boot: { key: "boot", can: (output) => output.bootSettable, value: String },
  };

  // A number goes as a number; anything else as the text it is, so the droid
  // refuses it by its own check rather than this module guessing.
  const asSent = (value) => (typeof value === "number" && Number.isFinite(value) ? value : String(value));

  // What each row key is called on screen. The droid refuses a value by the
  // row key it saves it under (`"field":"ledc:1.throwMs"`), and that name is
  // wire vocabulary that must never reach a builder (#414).
  const SETTING_WORDS = {
    wired: "wired tick",
    component: "what is on the wire",
    ledCount: "LED count",
    throwMs: "time to full throw",
    accelMs: "time to get up to speed",
    ease: "ease",
    boot: "power-up setting",
    openUs: "open end",
    centreUs: "centre",
    closeUs: "close end",
    calibrated: "calibration",
    parts: "parts",
  };

  // The unit a setting's number is in, where it has one.
  const SETTING_UNITS = { throwMs: " ms", accelMs: " ms", openUs: " µs", centreUs: " µs", closeUs: " µs" };

  // What a setting takes, from the refusal's `accepts`: `20..10000` is a range,
  // anything else the words it takes, comma-separated.
  const sayAccepts = (accepts, key) => {
    const range = /^(\d+)\.\.(\d+)$/.exec(accepts);
    if (range) return `${range[1]} to ${range[2]}${SETTING_UNITS[key] || ""}`;
    const words = accepts.split(",");
    return words.length > 1 ? `${words.slice(0, -1).join(", ")} or ${words[words.length - 1]}` : words[0];
  };

  const at = (address) => outputs.find((output) => output.address === address) || null;

  /**
   * A refusal about one of an Output row's fields, put in the page's words: the
   * Output's name and the setting's, and what it takes. Read from the keys the
   * droid answers beside its sentence (docs/api.md "Refusals from a settings
   * write") - `field` is `<address>.<key>` - never from the sentence, which the
   * firmware may reword. Anything else is left exactly as it came, for
   * web_api.js's messageFor().
   *
   * @param {Error} error - what PAApi threw
   * @returns {Error} the same error, reworded where it was about a row
   */
  const sayRefusal = (error) => {
    const field = typeof error?.field === "string" ? error.field : "";
    const dot = field.lastIndexOf(".");
    const address = dot > 0 ? field.slice(0, dot) : "";
    const key = field.slice(dot + 1);
    if (!address || !Object.hasOwn(SETTING_WORDS, key)) return error;
    const output = at(address);
    const name = output ? output.name : address;
    // A Part is on at most one Output (CONTEXT.md "Part"): a row set that puts
    // one on two is refused as a conflict, and says so in a sentence of its own.
    if (error.reason === "conflict" && key === "parts") {
      error.message = `${name} and another output list the same part`;
      return error;
    }
    const setting = `${name}'s ${SETTING_WORDS[key]}`;
    const accepts = typeof error.accepts === "string" ? error.accepts : "";
    error.message = accepts ? `${setting} must be ${sayAccepts(accepts, key)}` : `${setting} was not saved`;
    return error;
  };

  // The rows for a set of changes, in the shape POST /api/config takes them.
  const rowsFor = (changes) => Object.keys(changes).map((address) => {
    const output = at(address);
    if (!output) throw new Error(`${address} is not an Output this droid saves settings for`);
    const patch = changes[address];
    const row = { address };
    Object.keys(patch).forEach((key) => {
      const field = PATCH_FIELDS[key];
      if (!field) throw new Error(`an Output has no setting called ${key}`);
      if (!field.can(output)) throw new Error(`${address} cannot save ${key}`);
      row[field.key] = (field.value || asSent)(patch[key]);
    });
    return row;
  });

  // Saves go out one at a time, in the order they were asked for, so a later
  // answer is never overtaken by an earlier one.
  let queue = Promise.resolve();

  /**
   * Save Output settings: `{ [address]: { wired, type, ledCount, throwMs,
   * accelMs, ease, boot } }`, any of them per Output, through the row door
   * (POST /api/config `outputs`, ADR 0068). The droid's answer becomes what
   * this module holds.
   *
   * @param {object} changes
   * @param {object} [opts]
   * @param {number} [opts.timeoutMs=5000]
   * @returns {Promise<object[]>} the Outputs as the droid now holds them
   */
  const saveAll = (changes, { timeoutMs = 5000 } = {}) => {
    const run = queue.then(async () => {
      const api = apiFor(null);
      const body = { outputs: rowsFor(changes) };
      try {
        const result = await api.postJson("/api/config", body, { timeoutMs });
        // The droid answers a save with the config it now holds
        // (sendConfigSnapshot()); the rows are read again, since the Outputs
        // are theirs.
        const answer = result?.data;
        config = answer && typeof answer === "object" && answer.drive ? answer : await readConfig(api);
        rows = await readTable(api);
      } catch (error) {
        // What the droid holds, not the answer it refused - it may have taken
        // a save whose answer never arrived.
        try {
          rows = await readTable(api);
          config = await readConfig(api);
          publish();
        } catch (reloadError) {
          console.error("[outputs] reading the outputs after a failed save failed:", reloadError);
        }
        throw sayRefusal(error);
      }
      publish();
      return outputs;
    });
    // The queue only orders the saves. Each caller gets its own outcome from
    // `run`; a failed save must not stop the next one from going out.
    queue = run.catch(() => undefined);
    return run;
  };

  const save = (address, patch, opts) => saveAll({ [address]: patch }, opts);

  // The Output a Part is on. The firmware keeps a Part on at most one
  // (CONTEXT.md "Part"), so the first answer is the only answer. `among` is a
  // list a surface is painting from, where it holds one.
  const forPart = (partId, among = outputs) => among.find((output) => output.parts.includes(partId)) || null;

  // Which reads the droid has answered this session. A plate that saves a
  // wired tick needs the table; a surface that reads the lanes needs the
  // config.
  const known = () => ({ config: config !== null, table: rows !== null });

  /**
   * Be told whenever what this module holds changes: a read, a save, or a
   * save's answer. The listener is handed the Outputs.
   *
   * @returns {function} stops the listener
   */
  const onChange = (listener) => {
    listeners.add(listener);
    return () => listeners.delete(listener);
  };

  window.PAOutputs = Object.freeze({
    SERVO_MODELS,
    NO_SERVO,
    LIGHT_TYPES,
    EASES,
    BOOTS,
    lightType,
    servoModel,
    load,
    refresh,
    list: () => outputs,
    at,
    forPart,
    known,
    save,
    saveAll,
    sayRefusal,
    onChange,
  });
})();

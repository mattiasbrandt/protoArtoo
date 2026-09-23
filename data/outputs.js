// =============================================================================
// data/outputs.js
//
// What an Output is, answered once for the whole browser (#415). Wiring,
// Servos, Parts, Lights, Backup and the Dashboard ask this module and none of
// them works the answer out again.
//
// THE OUTPUT ARRIVES IN TWO HALVES, KEYED TWO WAYS, and this is the one place
// they are joined:
//
//   GET /api/config        components{} entries that carry an `address`, keyed
//                          by the stored config id: what the board prints, the
//                          wired tick, what is on the wire, and the fields that
//                          save each (src/web/api_config.cpp).
//   GET /api/servo/outputs the Servo Output rows, keyed by Output Address: the
//                          Parts on each wire, and where it has been told to be
//                          (docs/api.md).
//
// They are joined by Output Address (CONTEXT.md "Output Address"), never by a
// name and never by position.
//
// THIS FILE KNOWS NO OUTPUT (ADR 0065). It relays what the running firmware
// reports - which Outputs exist, in its order, what each is called, which can
// carry a light and which fields save it - and never lists an id, a count or a
// board label of its own (operator, 2026-09-19 on #411: "the outputs is
// supposed to be dynamic").
//
// ONE WIRED RULE. An Output the config describes is wired when its tick says
// so. An Output only the servo table knows - an expander's channel - has no
// tick anybody could have turned off, so it reads as wired, has no wired
// switch, and is called by its address (operator, 2026-09-23 on #415).
//
// WHAT IS ON THE WIRE IS ONE ANSWER IN TWO VOCABULARIES (CONTEXT.md "Output",
// ADR 0067): a servo's model where the wire drives a servo, a Light Type where
// it lights something. One stored field holds either, so `rgb` is not a servo
// model - it is the LED strip Light Type. Both word lists live here and
// nowhere else in data/.
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

  const lightType = (token) => LIGHT_TYPES.find((type) => type.id === token) || null;
  const servoModel = (token) => SERVO_MODELS.find((model) => model.id === token) || null;

  const text = (value) => (typeof value === "string" ? value : "");

  // ---------------------------------------------------------------------------
  // The two reads, as the droid last answered them
  // ---------------------------------------------------------------------------
  let config = null; // GET /api/config's whole answer, or the last save's
  let rows = null; // GET /api/servo/outputs rows, read by readRow()
  let outputs = Object.freeze([]);
  // The config fields each Output is saved under, by address. Kept here and
  // never handed out: a surface says what it wants saved, and this module
  // names the fields (POST /api/config).
  let saveFields = new Map();
  // What each Output was first reported with this session: its wired tick,
  // its Light Type and its LED count. All three are read once when the droid
  // starts (ADR 0027, src/tasks/aux_led.cpp), so a later answer that differs
  // from this is one waiting for a restart.
  const started = new Map();
  const listeners = new Set();

  // One GET /api/servo/outputs row, in the shape every surface reads. `name`
  // is kept apart as `printed`: it is folded into the Output's label below.
  const readRow = (row) => ({
    address: String(row.address),
    printed: text(row.name),
    parts: Array.isArray(row.parts) ? row.parts.map(String) : [],
    // A firmware older than the output-first table reports no position at
    // all, which is not the same as an Output with no pulse.
    reported: "commandedUs" in row,
    bandLoUs: Number(row.bandLoUs) || 0,
    bandHiUs: Number(row.bandHiUs) || 0,
    commandedUs: typeof row.commandedUs === "number" ? row.commandedUs : null,
    targetUs: typeof row.targetUs === "number" ? row.targetUs : null,
    // How many nudges have ended on this Output (#363); null from a firmware
    // that does not say, which a run must refuse rather than wait on.
    nudgesDone: typeof row.nudgesDone === "number" ? row.nudgesDone : null,
    // What the calibration dial reads (#364). The three widths are the
    // recorded positions, directional: openUs is whichever end the builder
    // recorded as open, so nothing here sorts the pair.
    component: text(row.component),
    openUs: typeof row.openUs === "number" ? row.openUs : null,
    centreUs: typeof row.centreUs === "number" ? row.centreUs : null,
    closeUs: typeof row.closeUs === "number" ? row.closeUs : null,
    calibrated: row.calibrated === true,
    held: row.held === true,
    // Why there is no pulse, meaningful only while commandedUs is null.
    limp: typeof row.limp === "string" ? row.limp : "off",
  });

  // What an Output with no servo table row reads as: nothing on it, and no
  // position reported.
  const NO_ROW = Object.freeze(readRow({ address: "", parts: [] }));

  // One Output, from whichever halves describe it.
  //
  //   address          its Output Address: the join, and what a command names
  //   id               its components{} key, the stored config id; "" where
  //                    the config does not describe it. Never shown.
  //   label            what the board prints beside it, or "" where no board
  //                    prints anything; also the word POST /api/servo moves it
  //                    by (ADR 0033 Amendment 2026-09-19)
  //   name             what a builder calls it: the label, else its address
  //   fromConfig       GET /api/config describes it
  //   fromTable        GET /api/servo/outputs has a row for it
  //   switchable       the config names the fields its wired tick and type
  //                    save under, so a page may offer them
  //   wired            the one wired rule (header)
  //   canLight         a Light Type may go on this wire at all
  //   type             the stored token: a servo model or a Light Type
  //   light, servo     that token as a Light Type or as a servo model, or null
  //   ledCount         how many LEDs its light has
  //   ledCountSettable the config names a field that saves ledCount
  //   started          what it was first reported with (above), or null
  //   parts ...        its servo table row, read by readRow()
  const outputOf = (address, id, entry, row) => {
    const { printed, ...table } = row || NO_ROW;
    const canLight = Boolean(entry?.lightCapable);
    const type = entry ? String(entry.type || (canLight ? NO_SERVO.id : SERVO_MODELS[0].id)) : "";
    // The config and the row are named from one lookup (include/board_outputs.h
    // boardOutputLabel()), so they agree; the config's is the one read first.
    const label = text(entry?.label) || printed;
    const output = {
      ...table,
      address,
      id,
      label,
      name: label || address,
      fromConfig: entry !== null,
      fromTable: row !== null,
      switchable: Boolean(entry && text(entry.enabledField) && text(entry.typeField)),
      wired: entry ? entry.enabled === true : true,
      canLight,
      type,
      light: lightType(type),
      servo: servoModel(type),
      ledCount: Number(entry?.ledCount) || 1,
      ledCountSettable: Boolean(entry && text(entry.ledCountField)),
    };
    if (entry && !started.has(address)) {
      started.set(address, Object.freeze({
        wired: output.wired,
        light: output.light ? output.light.id : null,
        ledCount: output.ledCount,
      }));
    }
    output.started = started.get(address) || null;
    output.parts = Object.freeze(output.parts.slice());
    return Object.freeze(output);
  };

  // The join. The Outputs the config describes come first, in its order
  // (include/board_outputs.h BOARD_OUTPUTS), then any the servo table alone
  // knows, in the table's order.
  const join = () => {
    const components = config && config.components && typeof config.components === "object"
      ? config.components : {};
    const byAddress = new Map((rows || []).map((row) => [row.address, row]));
    const described = new Set();
    const fields = new Map();
    const list = [];
    Object.keys(components).forEach((id) => {
      const entry = components[id];
      const address = text(entry?.address);
      if (address === "" || described.has(address)) return;
      described.add(address);
      fields.set(address, {
        wired: text(entry.enabledField),
        type: text(entry.typeField),
        ledCount: text(entry.ledCountField),
      });
      list.push(outputOf(address, id, entry, byAddress.get(address) || null));
    });
    (rows || []).forEach((row) => {
      if (!described.has(row.address)) list.push(outputOf(row.address, "", null, row));
    });
    outputs = Object.freeze(list);
    saveFields = fields;
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
   * Read the droid: its config and, unless `rows` is false, its servo table,
   * as one snapshot. A surface that needs the config for anything else - its
   * lanes, the Droid Build, the log level - takes it from the answer rather
   * than reading GET /api/config a second time.
   *
   * @param {object} [opts]
   * @param {object} [opts.handle] - the section's request handle, or PAApi
   * @param {boolean} [opts.rows=true] - also read GET /api/servo/outputs
   * @returns {Promise<{config: object, outputs: object[]}>}
   */
  const load = async ({ handle = null, rows: withRows = true } = {}) => {
    const api = apiFor(handle);
    // Both answers land before either is taken, so a half-answered read never
    // publishes a join of a new half with an old one.
    const table = withRows ? await readTable(api) : rows;
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

  // The config fields for a set of changes, keyed by the Output's address.
  // What a surface may ask for is exactly what an Output names a field for,
  // and asking for anything else is refused rather than dropped.
  const PATCH_FIELDS = {
    wired: (value) => (value ? "true" : "false"),
    type: (value) => String(value),
    ledCount: (value) => String(value),
  };

  const formFor = (changes) => {
    const form = {};
    Object.keys(changes).forEach((address) => {
      const fields = saveFields.get(address);
      if (!fields) throw new Error(`${address} is not an Output this droid saves settings for`);
      const patch = changes[address];
      Object.keys(patch).forEach((key) => {
        if (!PATCH_FIELDS[key]) throw new Error(`an Output has no setting called ${key}`);
        if (!fields[key]) throw new Error(`${address} names no field to save ${key} under`);
        form[fields[key]] = PATCH_FIELDS[key](patch[key]);
      });
    });
    return form;
  };

  // Saves go out one at a time, in the order they were asked for, so a later
  // answer is never overtaken by an earlier one.
  let queue = Promise.resolve();

  /**
   * Save Output settings: `{ [address]: { wired, type, ledCount } }`, any of
   * the three per Output. The droid's answer becomes what this module holds.
   *
   * @param {object} changes
   * @param {object} [opts]
   * @param {object|URLSearchParams} [opts.alongside] - other POST /api/config
   *   fields that must land in the same request (a Backup restore)
   * @param {number} [opts.timeoutMs=5000]
   * @returns {Promise<object[]>} the Outputs as the droid now holds them
   */
  const saveAll = (changes, { alongside = null, timeoutMs = 5000 } = {}) => {
    const run = queue.then(async () => {
      const api = apiFor(null);
      // A request carrying other fields goes as the form those came in, and an
      // Output-only save as a plain one.
      const fields = formFor(changes);
      let form = fields;
      if (alongside) {
        form = new URLSearchParams(alongside);
        Object.keys(fields).forEach((key) => form.set(key, fields[key]));
      }
      try {
        const result = await api.postForm("/api/config", form, { timeoutMs });
        // The droid answers a save with the config it now holds
        // (sendConfigSnapshot()); an answer without one is read again.
        const answer = result?.data;
        config = answer && typeof answer === "object" && answer.components ? answer : await readConfig(api);
      } catch (error) {
        // What the droid holds, not the answer it refused - it may have taken
        // a save whose answer never arrived.
        try {
          config = await readConfig(api);
          publish();
        } catch (reloadError) {
          console.error("[outputs] reading the config after a failed save failed:", reloadError);
        }
        throw error;
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

  const at = (address) => outputs.find((output) => output.address === address) || null;

  // Which halves the droid has answered this session. A plate that saves a
  // wired tick needs the config; a table of Parts needs only the rows.
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
    onChange,
  });
})();

// =============================================================================
// test/test_web/test_outputs.js
//
// What an Output is, at data/outputs.js's interface (#415): the one module
// every surface asks. It is run on its own against the one fake droid
// (helpers/fake_droid.js), because a surface that drew the right plate from a
// wrong answer would hide the defect this file is for.
//
// Four invariants earn their place:
//   - an Output is its row (ADR 0068): the Outputs in the table's order, each
//     carrying its own row's Parts and settings;
//   - ONE wired rule: an Output with no wired tick has no switch anybody could
//     have turned off, so it reads as wired, offers no switch, and is called
//     by its address (operator, 2026-09-23 on #415);
//   - a save goes out as the Output's row, through the one door
//     (POST /api/config `outputs`), and a setting the row says it cannot save
//     is refused rather than sent - the browser knows no field of its own
//     (ADR 0065);
//   - a row's field is wire vocabulary: when the droid refuses a value by the
//     row key it saves it under (`ledc:0.throwMs`), that name never reaches a
//     builder (#414, the rule #348 set for refusal tokens) - and the page's
//     words come from the refusal's keys, never from its sentence, so a
//     reworded firmware sentence cannot bring the key back (#425). The same
//     holds for every Setting of the droid's, worded by data/web_api.js's one
//     words table (#431).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { servoRow, describe, outputsModule } from "./helpers/fake_droid.js";

const webApiSrc = readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "../../data/web_api.js"), "utf-8");

// The shipped data/web_api.js, answering through `respond(path, init)`, so a
// refusal reaches data/outputs.js the way a browser hands it over: as the
// ApiError the transport builds from the droid's JSON body.
const shippedApi = (respond) => {
  const window = {
    setTimeout, clearTimeout, setInterval, clearInterval,
    addEventListener: () => {},
    location: { origin: "http://droid" },
  };
  const fetch = async (path, init) => {
    const { status, body } = respond(path, init);
    return {
      ok: status >= 200 && status < 300,
      status,
      headers: { get: (name) => (name === "content-type" ? "application/json" : null) },
      json: async () => structuredClone(body),
      text: async () => JSON.stringify(body),
    };
  };
  const context = {
    window, fetch, AbortController, URLSearchParams, JSON, Date,
    setTimeout, clearTimeout, setInterval, clearInterval,
    document: { addEventListener: () => {} },
    console: { warn: () => {}, log: () => {}, error: () => {} },
  };
  context.globalThis = context;
  vm.runInNewContext(webApiSrc, context, { filename: "web_api.js" });
  return window.PAApi;
};

// A droid with two of its board's Outputs - one lit, one switched off - and an
// expander channel no board labels, listed in the table's own order.
const droid = () => {
  const rows = describe([
    servoRow("ledc:0", "GPIO 49", { parts: ["doorFL", "doorFR"] }),
    servoRow("ledc:4", "GPIO 5", { parts: ["dataPanel"] }),
    servoRow("pca:3", "", { parts: ["utilLo"] }),
  ], {
    "ledc:4": { lightCapable: true, type: "rgb", ledCount: 12 },
    "ledc:0": { wired: false },
  });
  return { rows, config: { drive: { speedLimitMax: 300 } } };
};

const boot = (answer = droid()) => {
  const posts = [];
  const api = {
    get: async (path) => {
      if (path === "/api/config") return { data: structuredClone(answer.config) };
      if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(answer.rows) } };
      throw new Error(`unexpected GET ${path}`);
    },
    postJson: async (path, json) => {
      posts.push({ path, json: structuredClone(json) });
      return { data: structuredClone(answer.config) };
    },
  };
  return { outputs: outputsModule(() => api), posts, answer };
};

test("an Output is its row, and one with no wired tick reads as wired with no switch", async () => {
  const { outputs } = boot();
  await outputs.load();
  const list = outputs.list();

  assert.deepEqual(list.map((output) => output.address), ["ledc:0", "ledc:4", "pca:3"],
    "the Outputs in the table's order");
  const door = outputs.at("ledc:0");
  assert.deepEqual([...door.parts], ["doorFL", "doorFR"], "each Output carries its own row's Parts");
  assert.equal(door.name, "GPIO 49");
  assert.equal(door.wired, false, "its tick says not wired");
  assert.equal(outputs.forPart("dataPanel").address, "ledc:4");
  assert.equal(outputs.forPart("dataPanel").light.label, "LED strip");
  assert.equal(outputs.at("ledc:4").ledCount, 12);

  const expander = outputs.at("pca:3");
  assert.equal(expander.wired, true, "no tick anybody could have turned off, so it is wired");
  assert.equal(expander.switchable, false, "and it offers no switch");
  assert.equal(expander.name, "pca:3", "called by its address, never a name made up here");
  assert.equal(outputs.forPart("utilLo"), expander);
});

test("a save goes out as the Output's row through the one door, and a setting its row cannot save is refused", async () => {
  const { outputs, posts } = boot();
  await outputs.load();

  await outputs.save("ledc:4", { wired: false, type: "none", ledCount: 30 });
  assert.deepEqual(posts.at(-1), {
    path: "/api/config",
    json: { outputs: [{ address: "ledc:4", wired: false, component: "none", ledCount: 30 }] },
  });

  const sent = posts.length;
  await assert.rejects(outputs.save("ledc:0", { ledCount: 8 }), /cannot save ledCount/,
    "an Output that cannot carry a light has no count to save");
  await assert.rejects(outputs.save("pca:3", { wired: false }), /cannot save wired/,
    "an Output with no tick has nothing to switch off");
  assert.equal(posts.length, sent, "and neither refusal reached the droid");
});

test("a value the droid refuses is said from the refusal's keys with the Output's name, whatever its sentence says", async () => {
  const answer = droid();
  const refusals = [
    // The droid's 400 for a throw time out of range, its sentence reworded: it
    // no longer carries the range, so the only way to the page's words is the
    // three keys beside it.
    {
      body: { ok: false, error: "refused: ledc:0.throwMs is not kept", field: "ledc:0.throwMs",
        reason: "out-of-range", accepts: "20..10000" },
      says: ["GPIO 49", "20 to 10000"],
    },
    // One Part on two Outputs, which only a whole row set can ask for.
    {
      body: { ok: false, error: "ledc:0.parts names a Part another row names too", field: "ledc:0.parts",
        reason: "conflict", accepts: null },
      says: ["GPIO 49"],
    },
  ];
  for (const refusal of refusals) {
    const api = shippedApi((path, init) => {
      if (init?.method === "POST") return { status: 400, body: refusal.body };
      if (path === "/api/config") return { status: 200, body: answer.config };
      return { status: 200, body: { outputs: answer.rows } };
    });
    const refusing = outputsModule(() => api);
    await refusing.load();

    await assert.rejects(refusing.save("ledc:0", { throwMs: 5 }), (error) => {
      assert.ok(!error.message.includes("ledc:0"), `the row key reached the screen: ${error.message}`);
      refusal.says.forEach((words) => assert.ok(error.message.includes(words),
        `"${words}" is not in what the builder reads: ${error.message}`));
      return true;
    });
  }
});

// The shipped defect this guards (ADR 0059, #431): every page but this one showed
// the droid's sentence, wire name and all (`speedLimitMax must be 0..600`).
// Every Setting's refusal is worded by data/web_api.js from its field, reason
// and accepts - a droid Setting by its form name or its GET path, an Output's
// by its row key, a clash between Settings - and none reaches the page as a
// name or a token the droid uses on the wire.
test("a refused Setting reaches the page in the builder's words, never its wire name", async () => {
  const answer = droid();
  const refusals = [
    { field: "speedLimitMax", reason: "out-of-range", accepts: "0..600", says: "0 to 600" },
    { field: "rc.sbusTimeoutMs", reason: "out-of-range", accepts: "50..5000", says: "50 to 5000 ms" },
    { field: "rcInputMode", reason: "out-of-range", accepts: "standard_pwm,single_sbus,dual_sbus,elrs",
      says: "two SBUS" },
    { field: "ledc:4.throwMs", reason: "out-of-range", accepts: "20..10000", says: "GPIO 5" },
    { field: "speedPresetSlow", reason: "conflict", accepts: null, says: "slow preset" },
    // An audio Setting, named by the key its door takes (#431 addendum).
    { field: "snd_int_quiet", reason: "out-of-range", accepts: "0..3600", says: "0 to 3600 s" },
  ];
  for (const refusal of refusals) {
    const api = shippedApi((path, init) => {
      if (init?.method === "POST") {
        return { status: 400, body: { ok: false, error: `${refusal.field} is refused`, ...refusal } };
      }
      if (path === "/api/config") return { status: 200, body: answer.config };
      return { status: 200, body: { outputs: answer.rows } };
    });
    // The Outputs are read first, as on a page, so a row's refusal can name one.
    const outputs = outputsModule(() => api);
    await outputs.load();

    const error = await api.postForm("/api/config", {}).then(() => null, (thrown) => thrown);
    const said = api.messageFor(error);
    const wire = [refusal.field, ...(refusal.accepts || "").split(",").filter((t) => t.includes("_"))];
    wire.forEach((name) => assert.ok(!said.includes(name), `"${name}" reached the page: ${said}`));
    assert.ok(said.toLowerCase().includes(refusal.says.toLowerCase()), `"${refusal.says}" is not in: ${said}`);
  }
});

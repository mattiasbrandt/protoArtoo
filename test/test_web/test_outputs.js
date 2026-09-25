// =============================================================================
// test/test_web/test_outputs.js
//
// What an Output is, at data/outputs.js's interface (#415): the one module
// every surface asks. It is run on its own against the one fake droid
// (helpers/fake_droid.js), because a surface that drew the right plate from a
// wrong answer would hide the defect this file is for.
//
// Four invariants earn their place:
//   - the two halves are joined by Output Address, never by position or by a
//     name: the config's Outputs in its order, each carrying the Parts its own
//     servo table row lists;
//   - ONE wired rule: an Output only the servo table knows has no switch
//     anybody could have turned off, so it reads as wired, offers no switch,
//     and is called by its address (operator, 2026-09-23 on #415). The two
//     halves disagreed about exactly this before the module existed;
//   - a save goes out under the fields the firmware named for that Output, and
//     a setting the Output names no field for is refused rather than invented
//     - the browser knows no field name of its own (ADR 0065);
//   - a field name is wire vocabulary in the other direction too: when the
//     droid refuses a value by the name it saves it under, that name never
//     reaches a builder (#414, the rule #348 set for refusal tokens) - and the
//     page's words come from the refusal's keys, never from its sentence, so a
//     reworded firmware sentence cannot bring the field name back (#425). A
//     refusal is only put on an Output when it names a field this save sent
//     for that Output: one about a field riding alongside (a Backup restore)
//     is the droid's own answer and is left as it came.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import vm from "node:vm";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";
import { servoRow, configOutputs, outputsModule } from "./helpers/fake_droid.js";

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

// A droid whose servo table lists its rows in a different order from the one
// its config describes them in, with one row the config does not describe at
// all (an expander channel, which no board labels).
const droid = () => {
  const rows = [
    servoRow("pca:3", "", { parts: ["utilLo"] }),
    servoRow("ledc:4", "GPIO 5", { parts: ["dataPanel"] }),
    servoRow("ledc:0", "GPIO 49", { parts: ["doorFL", "doorFR"] }),
  ];
  const config = {
    components: configOutputs([rows[2], rows[1]], {
      "ledc:4": { lightCapable: true, type: "rgb", ledCount: 12 },
      "ledc:0": { enabled: false },
    }),
  };
  return { rows, config };
};

const boot = (answer = droid()) => {
  const posts = [];
  const api = {
    get: async (path) => {
      if (path === "/api/config") return { data: structuredClone(answer.config) };
      if (path === "/api/servo/outputs") return { data: { outputs: structuredClone(answer.rows) } };
      throw new Error(`unexpected GET ${path}`);
    },
    postForm: async (path, form) => {
      posts.push({ path, form: { ...form } });
      return { data: structuredClone(answer.config) };
    },
  };
  return { outputs: outputsModule(() => api), posts, answer };
};

test("the halves are joined by Output Address, and an Output only the servo table knows reads as wired with no switch", async () => {
  const { outputs } = boot();
  await outputs.load();
  const list = outputs.list();

  assert.deepEqual(list.map((output) => output.address), ["ledc:0", "ledc:4", "pca:3"],
    "the config's Outputs in its order, then the one only the servo table knows");
  const door = outputs.at("ledc:0");
  assert.deepEqual([...door.parts], ["doorFL", "doorFR"], "each Output carries its own row's Parts");
  assert.equal(door.name, "GPIO 49");
  assert.equal(door.wired, false, "its config tick says not wired");
  assert.equal(outputs.forPart("dataPanel").address, "ledc:4");
  assert.equal(outputs.forPart("dataPanel").light.label, "LED strip");

  const expander = outputs.at("pca:3");
  assert.equal(expander.wired, true, "no tick anybody could have turned off, so it is wired");
  assert.equal(expander.switchable, false, "and it offers no switch");
  assert.equal(expander.name, "pca:3", "called by its address, never a name made up here");
  assert.equal(outputs.forPart("utilLo"), expander);
});

test("a save goes out under the fields the firmware named, and a setting with no field is refused", async () => {
  const { outputs, posts, answer } = boot();
  await outputs.load();
  const entry = (address) => Object.values(answer.config.components).find((each) => each.address === address);

  await outputs.save("ledc:4", { wired: false, type: "none", ledCount: 30 });
  assert.deepEqual(posts.at(-1), {
    path: "/api/config",
    form: {
      [entry("ledc:4").enabledField]: "false",
      [entry("ledc:4").typeField]: "none",
      [entry("ledc:4").ledCountField]: "30",
    },
  });

  const sent = posts.length;
  await assert.rejects(outputs.save("ledc:0", { ledCount: 8 }), /names no field/,
    "an Output that cannot carry a light names no field for a light's count");
  await assert.rejects(outputs.save("pca:3", { wired: false }), /not an Output this droid saves/,
    "an Output the config does not describe has nothing to save under");
  assert.equal(posts.length, sent, "and neither refusal reached the droid");
});

test("a value the droid refuses is said from the refusal's keys with the Output's name, whatever its sentence says", async () => {
  const answer = droid();
  const entry = Object.values(answer.config.components).find((each) => each.address === "ledc:0");
  // The droid's 400 for a throw time out of range, its sentence reworded: it
  // no longer opens with the field or carries the range, so the only way to the
  // page's words is the three keys beside it.
  const refusal = {
    ok: false,
    error: `refused: a throw of that length is not kept (${entry.throwField})`,
    field: entry.throwField,
    reason: "out-of-range",
    accepts: "20..10000",
  };
  const api = shippedApi((path, init) => {
    if (init?.method === "POST") return { status: 400, body: refusal };
    if (path === "/api/config") return { status: 200, body: answer.config };
    return { status: 200, body: { outputs: answer.rows } };
  });
  const refusing = outputsModule(() => api);
  await refusing.load();

  await assert.rejects(refusing.save("ledc:0", { throwMs: 5 }), (error) => {
    assert.ok(!error.message.includes(entry.throwField), `the field name reached the screen: ${error.message}`);
    assert.ok(error.message.includes("GPIO 49"), `the Output is named as the builder knows it: ${error.message}`);
    assert.ok(error.message.includes("20 to 10000"), `what it takes is said from accepts: ${error.message}`);
    return true;
  });
});

test("a refusal about a field sent alongside the Outputs is left as the droid said it, not pinned on an Output", async () => {
  const answer = droid();
  const refusal = {
    ok: false,
    error: "speedLimitMax must be 0..600",
    field: "speedLimitMax",
    reason: "out-of-range",
    accepts: "0..600",
  };
  const api = shippedApi((path, init) => {
    if (init?.method === "POST") return { status: 400, body: refusal };
    if (path === "/api/config") return { status: 200, body: answer.config };
    return { status: 200, body: { outputs: answer.rows } };
  });
  const restoring = outputsModule(() => api);
  await restoring.load();

  await assert.rejects(
    restoring.saveAll({ "ledc:0": { throwMs: 500 } }, { alongside: { speedLimitMax: "9999" } }),
    (error) => {
      assert.equal(error.message, refusal.error, "the droid's answer, not an Output's setting");
      return true;
    });
});

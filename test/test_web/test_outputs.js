// =============================================================================
// test/test_web/test_outputs.js
//
// What an Output is, at data/outputs.js's interface (#415): the one module
// every surface asks. It is run on its own against the one fake droid
// (helpers/fake_droid.js), because a surface that drew the right plate from a
// wrong answer would hide the defect this file is for.
//
// Three invariants earn their place:
//   - the two halves are joined by Output Address, never by position or by a
//     name: the config's Outputs in its order, each carrying the Parts its own
//     servo table row lists;
//   - ONE wired rule: an Output only the servo table knows has no switch
//     anybody could have turned off, so it reads as wired, offers no switch,
//     and is called by its address (operator, 2026-09-23 on #415). The two
//     halves disagreed about exactly this before the module existed;
//   - a save goes out under the fields the firmware named for that Output, and
//     a setting the Output names no field for is refused rather than invented
//     - the browser knows no field name of its own (ADR 0065).
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { servoRow, configOutputs, outputsModule } from "./helpers/fake_droid.js";

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

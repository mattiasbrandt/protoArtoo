// The Droid Build seam, as the browser actually runs it (#343, ADR 0047).
//
// This file exists to hold down the one thing the decision can get wrong:
// a design SEEDS the Parts and never FENCES them. The reference this seam is
// adapted from deletes a model's complement when the model is hidden, because
// its models are exclusive stage payloads; protoArtoo's are not, and copying
// the removal half would throw away the gripper arm a builder printed. So the
// tests below say "not removed" three different ways on purpose.
//
// Beside it, the distinction A1b handed this ticket: a complement nobody has
// read (`seeds: null`, which `mk4/simple` carries) is not an empty complement
// (`seeds: []`, which `own` carries). Seeding nothing silently from the first
// is the outcome that is wrong, so it is reported instead.
//
// The modules are executed rather than pattern-matched, per test_web/README.md.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const root = path.resolve(__dirname, "../..");

function read(name) {
  return fs.readFileSync(path.join(root, "data", name), "utf8");
}

// A page with the generated catalog loaded and a recording PAApi.
function newPage({ config = null, postOk = true } = {}) {
  const posts = [];
  const gets = [];
  const context = {
    window: {
      PAApi: {
        postForm(pathName, form, opts) {
          posts.push({ path: pathName, form, opts });
          return Promise.resolve({ ok: postOk, status: postOk ? 200 : 400, data: {} });
        },
        get(pathName) {
          gets.push(pathName);
          return Promise.resolve(
            config ? { ok: true, status: 200, data: config } : { ok: false, status: 503, data: {} }
          );
        },
      },
    },
  };
  vm.runInNewContext(read("droid_parts.js"), context);
  vm.runInNewContext(read("droid_build.js"), context);
  return { build: context.window.DroidBuild, parts: context.window.DroidParts, posts, gets };
}

// The dome half of MK4's complex complement, worked out from the catalog the
// page is holding rather than restated here - a list copied into a test goes
// stale the day a Part is added to the design.
function domeSeeds(parts) {
  const mk4 = parts.designs.find((design) => design.id === "mk4");
  const complex = mk4.variants.find((variant) => variant.id === "complex");
  const half = new Map(parts.parts.map((part) => [part.id, part.half]));
  return complex.seeds.filter((id) => half.get(id) === "dome");
}

test("choosing a design fits the parts that design carries, on that half only", async () => {
  const page = newPage();
  const result = await page.build.applyDroidBuild(
    { domeDesign: "mk4", domeVariant: "complex" },
    { persist: false }
  );

  const expected = domeSeeds(page.parts);
  assert.ok(expected.length > 0, "the catalog has no dome complement to seed");
  assert.deepEqual([...result.build.fitted].sort(), [...expected].sort());
  assert.deepEqual([...result.seeded].sort(), [...expected].sort());
  assert.deepEqual([...result.unknownComplement], []);

  // The body half was not answered, so nothing on the body was fitted.
  assert.equal(result.build.body.design, "");
  assert.ok(!result.build.fitted.includes("doorFL"));
});

test("parts already fitted are not removed when the design changes", async () => {
  const page = newPage();
  await page.build.applyDroidBuild({ domeDesign: "mk4", domeVariant: "complex" }, { persist: false });

  // The gripper arm belongs to no design at all - a Common Addition. This is
  // the part the reference pattern's delete-on-hide branch would have thrown
  // away, and the one ADR 0047 is named for.
  await page.build.applyDroidBuild({ fitted: [...page.build.current().fitted, "gripArm"] },
                                   { persist: false });
  assert.ok(page.build.isFitted("gripArm"));

  const after = await page.build.applyDroidBuild({ domeDesign: "own", domeVariant: "" },
                                                 { persist: false });
  assert.ok(after.build.fitted.includes("gripArm"), "a Common Addition was fenced out");
  for (const id of domeSeeds(page.parts)) {
    assert.ok(after.build.fitted.includes(id), `${id} was removed by a design change`);
  }
  assert.deepEqual([...after.seeded], [], "my own build seeds nothing");
});

test("a complement nobody has read is reported, not drawn as an empty droid", async () => {
  const page = newPage();
  const result = await page.build.applyDroidBuild(
    { domeDesign: "mk4", domeVariant: "simple" },
    { persist: false }
  );

  // `mk4/simple` carries `seeds: null` in the catalog: the split between
  // MrBaddeley's simple and complex exports is recorded nowhere in this
  // repository. Seeding nothing silently would read as "your dome carries no
  // panels", which is a claim nobody has the evidence for.
  assert.deepEqual([...result.unknownComplement], ["dome"]);
  assert.deepEqual([...result.seeded], []);
  assert.deepEqual([...result.build.fitted], []);
  assert.equal(page.build.complementFor("mk4", "simple", "dome").known, false);
});

test("my own build seeds nothing, and says so as a real answer", () => {
  const page = newPage();
  const complement = page.build.complementFor("own", "", "dome");
  // `[]` and `null` must not read alike: this one IS known, and is empty.
  assert.equal(complement.known, true);
  assert.deepEqual([...complement.ids], []);
});

test("which half a part is on comes from the catalog, and the escape hatch is on neither", () => {
  const page = newPage();
  assert.equal(page.build.halfOf("pie1"), "dome");
  assert.equal(page.build.halfOf("psiRear"), "dome");
  assert.equal(page.build.halfOf("doorFL"), "body");
  assert.equal(page.build.halfOf("gripArm"), "body");
  // The slots belong to no design, so no design can seed one.
  assert.equal(page.build.halfOf("other1"), null);
  assert.equal(page.build.halfOf("nothing-like-this"), null);
});

test("the boot re-apply adopts what the device holds and seeds nothing over it", async () => {
  // A builder who dropped a Part their design carries. Re-seeding at boot
  // would put it back under them on every page load.
  const page = newPage();
  const stored = {
    droidBuild: {
      domeDesign: "mk4",
      domeVariant: "complex",
      bodyDesign: "mk4",
      bodyVariant: "complex",
      fitted: ["pie1", "doorFL"],
    },
  };
  page.build.adopt(stored);

  const applied = page.build.current();
  assert.deepEqual([...applied.fitted], ["pie1", "doorFL"]);
  assert.equal(applied.dome.variant, "complex");
  assert.equal(page.posts.length, 0, "the boot re-apply must not write back");
});

test("a config payload with no droid build is an older firmware, not an empty droid", () => {
  const page = newPage();
  assert.equal(page.build.fromConfig({}), null);
  assert.equal(page.build.adopt({}), null);
  assert.equal(page.build.current(), null);
});

test("a stated half is written as a pair, with the parts whole", async () => {
  const page = newPage();
  const result = await page.build.applyDroidBuild({ domeDesign: "mk4", domeVariant: "complex" });

  assert.equal(result.persisted, true);
  assert.equal(page.posts.length, 1);
  assert.equal(page.posts[0].path, "/api/config");
  const form = page.posts[0].form;
  // A variant means nothing without the design it belongs to.
  assert.equal(form.domeDesign, "mk4");
  assert.equal(form.domeVariant, "complex");
  // The half that was not answered is not written: a builder changing their
  // dome is saying nothing about their body.
  assert.equal(form.bodyDesign, undefined);
  assert.equal(form.bodyVariant, undefined);
  assert.deepEqual(form.fittedParts.split(","), [...result.build.fitted]);
});

test("a device that refuses the write says so rather than reporting success", async () => {
  const page = newPage({ postOk: false });
  const result = await page.build.applyDroidBuild({ domeDesign: "mk4", domeVariant: "complex" });
  assert.equal(result.persisted, false);
});

test("every surface is told once, and one throwing does not silence the rest", async () => {
  const page = newPage();
  const seen = [];
  page.build.onChange(() => {
    throw new Error("a surface blew up");
  });
  const off = page.build.onChange((applied) => seen.push(applied.dome.design));
  await page.build.applyDroidBuild({ domeDesign: "mk4", domeVariant: "complex" },
                                   { persist: false });
  assert.deepEqual(seen, ["mk4"]);

  off();
  await page.build.applyDroidBuild({ domeDesign: "own", domeVariant: "" }, { persist: false });
  assert.deepEqual(seen, ["mk4"], "an unsubscribed surface kept being told");
});

test("the boot re-apply is fetched once however many surfaces ask", async () => {
  const page = newPage({
    config: { droidBuild: { domeDesign: "own", domeVariant: "", bodyDesign: "own",
                            bodyVariant: "", fitted: [] } },
  });
  await Promise.all([page.build.load(), page.build.load(), page.build.load()]);
  assert.equal(page.gets.length, 1, "the device was asked more than once");
  assert.equal(page.build.current().dome.design, "own");
});

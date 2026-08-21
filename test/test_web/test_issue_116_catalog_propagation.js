// =============================================================================
// test/test_web/test_issue_116_catalog_propagation.js
//
// The audio-catalog bootstrap section (issue #116): it must resolve quietly
// when the backend has no catalog capability, and it must let a real catalog
// failure reach the bootstrap so recovery can retry instead of the page
// silently showing an empty catalog.
//
// Everything below runs the shipped data/sound.js and drives the section
// loader it registers. An earlier version of this file defined its own
// four-line copy of loadAudioCatalogIfSupported and asserted against that, so
// deleting the real one from sound.js would not have failed it. Issue #146.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";

import { loadPageModule, ApiError } from "./helpers/page_module_env.js";

// AudioDriver capability bits, mirroring audio_driver.h. Status query is the
// baseline every backend reports; catalog is the one under test.
const CAP_STATUS_QUERY = 0x01;
const CAP_CATALOG = 0x20;

const MODULE_PATH = "/api/audio";
const CATALOG_PATH = "/api/audio/catalog";

// Brings sound.js up with the given capability word, then settles whatever the
// capability handler kicked off, so each test starts from a quiet module.
const soundPageWith = async ({ capabilities, catalog = () => ({ ready: true, banks: [], entries: [] }) }) => {
  const env = loadPageModule("sound.js", {
    respond: (path) => {
      if (path === MODULE_PATH) return { data: { capabilities, link_ok: true } };
      if (path === CATALOG_PATH) return { data: catalog() };
      return { data: {} };
    },
  });

  await env.runSection("audio-status");
  // applyCapabilityUI starts a fire-and-forget catalog load when the backend
  // supports one; let it settle so the next assertion sees a settled module.
  await new Promise((resolve) => setImmediate(resolve));
  return env;
};

test("sound.js registers the audio-catalog section on the longer catalog deadline", (t) => {
  const env = loadPageModule("sound.js", { respond: () => ({ data: {} }) });

  assert.ok(
    env.sectionNames().includes("audio-catalog"),
    "the catalog load must be a bootstrap section so recovery can see it fail"
  );
  assert.equal(
    env.sectionOptions("audio-catalog").deadlineMs,
    12000,
    "the catalog is the known-longer operation and must carry its own deadline"
  );
});

test("audio-catalog asks the controller for nothing when the backend has no catalog", async (t) => {
  const env = await soundPageWith({ capabilities: CAP_STATUS_QUERY });

  const before = env.pathsRequested().filter((path) => path === CATALOG_PATH).length;
  assert.equal(before, 0, "a backend without the capability must not be probed for a catalog");

  await env.runSection("audio-catalog");

  assert.equal(
    env.pathsRequested().filter((path) => path === CATALOG_PATH).length,
    0,
    "the section must skip the fetch entirely, not fetch and discard"
  );
});

test("audio-catalog resolves when the backend has no catalog, so the page finishes loading", async (t) => {
  const env = await soundPageWith({ capabilities: CAP_STATUS_QUERY });

  // Rejecting here would put the bootstrap into recovery and retry forever for
  // a backend that is behaving correctly.
  await assert.doesNotReject(
    () => env.runSection("audio-catalog"),
    "an absent capability is a success, not a failure to retry"
  );
});

test("audio-catalog fetches the catalog when the backend supports one", async (t) => {
  const env = await soundPageWith({ capabilities: CAP_STATUS_QUERY | CAP_CATALOG });

  await env.runSection("audio-catalog");

  assert.ok(
    env.pathsRequested().includes(CATALOG_PATH),
    "a catalog-capable backend must actually be asked for its catalog"
  );
});

test("a catalog fetch failure reaches the bootstrap instead of being swallowed", async (t) => {
  const env = await soundPageWith({
    capabilities: CAP_STATUS_QUERY | CAP_CATALOG,
    catalog: () => {
      throw new ApiError("Simulated catalog failure", { kind: "network" });
    },
  });

  await assert.rejects(
    () => env.runSection("audio-catalog"),
    /Simulated catalog failure/,
    "the section must propagate so recovery can retry it"
  );
});

test("a failed catalog load is retryable - the next attempt fetches again", async (t) => {
  let failNext = true;
  const env = await soundPageWith({
    capabilities: CAP_STATUS_QUERY | CAP_CATALOG,
    catalog: () => {
      if (failNext) throw new ApiError("Simulated catalog failure", { kind: "network" });
      return { ready: true, banks: [], entries: [] };
    },
  });

  await assert.rejects(() => env.runSection("audio-catalog"));
  const afterFailure = env.pathsRequested().filter((path) => path === CATALOG_PATH).length;

  failNext = false;
  await env.runSection("audio-catalog");

  assert.ok(
    env.pathsRequested().filter((path) => path === CATALOG_PATH).length > afterFailure,
    "an in-flight guard left set by a failure would make every retry a silent no-op"
  );
});

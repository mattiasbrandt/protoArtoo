// =============================================================================
// test/test_web/helpers/shipped_words.cjs
//
// The shipped words table's lookups (data/web_api.js labelOf, timingOf,
// rowTimingOf), for a harness whose PAApi is a stand-in. A page reads a
// Setting's label and when it takes effect from the one table (ADR 0068,
// second amendment, #432), so a stand-in PAApi carries these three from the
// shipped file itself - never a copy of the table, which would be a second
// home for the words the check exists to keep to one. CommonJS, so the suites
// written either way can load it.
// =============================================================================
const vm = require("node:vm");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");

const source = readFileSync(join(__dirname, "../../../data/web_api.js"), "utf-8");

const shippedWords = () => {
  const window = {};
  vm.runInNewContext(source, { window, URLSearchParams });
  const { labelOf, timingOf, rowTimingOf } = window.PAApi;
  return { labelOf, timingOf, rowTimingOf };
};

module.exports = { shippedWords };

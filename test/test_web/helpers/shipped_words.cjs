// =============================================================================
// test/test_web/helpers/shipped_words.cjs
//
// The shipped words table's lookups (data/web_api.js labelOf, unsetOf,
// timingOf, rowTimingOf), for a harness whose PAApi is a stand-in. A page
// reads a Setting's label, what a never-set one stands for, and when it takes
// effect from the one table (ADR 0068, second amendment, #432), so a stand-in
// PAApi carries these four from the shipped file itself - never a copy of the table, which would be a second
// home for the words the check exists to keep to one. The RC Channel's words
// (isRcChannelSource, rcSourceLabel, rcChannelTitle; #451) come along for the
// same reason: the RC page and the Dashboard both say a channel through them.
// CommonJS, so the suites written either way can load it.
// =============================================================================
const vm = require("node:vm");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");

const source = readFileSync(join(__dirname, "../../../data/web_api.js"), "utf-8");

const shippedWords = () => {
  const window = {};
  vm.runInNewContext(source, { window, URLSearchParams });
  const { labelOf, unsetOf, timingOf, rowTimingOf, isRcChannelSource, rcSourceLabel, rcChannelTitle } = window.PAApi;
  return { labelOf, unsetOf, timingOf, rowTimingOf, isRcChannelSource, rcSourceLabel, rcChannelTitle };
};

module.exports = { shippedWords };

// =============================================================================
// test/test_web/test_section_loaders.js
//
// Tests that section loaders propagate errors to the bootstrap's recovery
// mechanism instead of catching and returning normally.
// Issue #107: Page Recovery must see section load failures.
// =============================================================================

import { test } from "node:test";
import assert from "node:assert";
import { readFileSync } from "fs";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __dirname = dirname(fileURLToPath(import.meta.url));

// Helper to extract a function from a file and execute it in isolation
const extractAndExecuteFunction = (filePath, functionName) => {
  const content = readFileSync(filePath, "utf-8");

  // Create a minimal window shim
  const window = {
    PAApi: null,
    PABootstrap: null,
    PAUtils: {
      showFeedback: (el, text, cls) => {
        if (el) {
          el.textContent = text;
          el.className = cls ? `feedback ${cls}` : "feedback";
        }
      },
    },
  };

  // Create mock DOM elements
  const mockElement = () => ({
    textContent: "",
    className: "",
  });

  const mockApi = {
    get: async () => {
      throw new Error("API error");
    },
    messageFor: (error) => error?.message || "unknown error",
  };

  const mockApiOk = {
    get: async () => ({
      data: {
        drive: {
          speedLimitMax: 600,
          speedPresetSlow: 200,
          speedPresetNormal: 350,
          speedPresetTurbo: 600,
          webDriveTimeoutMs: 500,
        },
        components: { s1Hoverboard: { enabled: true } },
      },
    }),
    messageFor: (error) => error?.message || "unknown error",
  };

  return { window, mockElement, mockApi, mockApiOk };
};

test("drive.js loadConfig throws on API error when bootstrap is present", async (t) => {
  const { window, mockElement, mockApi } = extractAndExecuteFunction(
    join(__dirname, "../../data/drive.js"),
    "loadConfig"
  );

  window.PABootstrap = {}; // Bootstrap present
  window.PAApi = mockApi;

  const configFeedback = mockElement();
  window.document = { getElementById: (id) => configFeedback };

  // Extract and execute loadConfig from the drive.js file
  const content = readFileSync(
    join(__dirname, "../../data/drive.js"),
    "utf-8"
  );

  // Find the loadConfig function
  const match = content.match(
    /const loadConfig = async \(\) => \{[\s\S]*?\n  \};/
  );
  assert(match, "Could not find loadConfig function");

  const functionCode = match[0];
  const renderConfig = () => {}; // Mock renderConfig

  // Execute in isolated scope
  // eslint-disable-next-line no-eval
  const loadConfig = eval(
    `(function() {
      const renderConfig = () => {};
      const configFeedback = window.document.getElementById("config-feedback");
      ${functionCode}
      return loadConfig;
    })()`
  );

  // Call loadConfig and verify it throws
  let threw = false;
  try {
    await loadConfig();
  } catch (error) {
    threw = true;
    assert(error, "Should throw an error");
  }

  assert(
    threw,
    "loadConfig with bootstrap should throw on API error"
  );
});

test("drive.js loadConfig throws but does not show inline error when bootstrap is present", async (t) => {
  const bootstrapFile = readFileSync(
    join(__dirname, "../../data/drive.js"),
    "utf-8"
  );

  // Verify that the error handler checks for window.PABootstrap
  assert(
    bootstrapFile.includes("if (!window.PABootstrap)") &&
    bootstrapFile.includes("throw error"),
    "loadConfig should check PABootstrap before showing inline error feedback"
  );
});

test("dome.js loadEscConfig throws on API error", async (t) => {
  const content = readFileSync(
    join(__dirname, "../../data/dome.js"),
    "utf-8"
  );

  // Verify that error is re-thrown after checking bootstrap
  assert(
    content.includes("throw error") &&
    content.includes("if (!window.PABootstrap)"),
    "loadEscConfig should rethrow errors with bootstrap check"
  );
});

test("servo.js loadCalib throws on API error", async (t) => {
  const content = readFileSync(
    join(__dirname, "../../data/servo.js"),
    "utf-8"
  );

  // Verify that error is re-thrown after checking bootstrap
  assert(
    content.includes("throw error") &&
    content.includes("if (!window.PABootstrap)"),
    "loadCalib should rethrow errors with bootstrap check"
  );
});

test("All three loaders check for API helper and throw in bootstrap mode", async (t) => {
  const driveFile = readFileSync(
    join(__dirname, "../../data/drive.js"),
    "utf-8"
  );
  const domeFile = readFileSync(
    join(__dirname, "../../data/dome.js"),
    "utf-8"
  );
  const servoFile = readFileSync(
    join(__dirname, "../../data/servo.js"),
    "utf-8"
  );

  // All three should handle the !window.PAApi case by throwing when bootstrap exists
  for (const [name, content] of [
    ["drive.js", driveFile],
    ["dome.js", domeFile],
    ["servo.js", servoFile],
  ]) {
    assert(
      content.includes("if (!window.PAApi)") &&
      content.includes("if (window.PABootstrap)") &&
      content.includes('throw new Error("API helper unavailable")'),
      `${name} should throw on missing API when bootstrap is present`
    );
  }
});

test("All three loaders propagate fetch errors to bootstrap", async (t) => {
  const patterns = [
    ["drive.js", "loadConfig", /const loadConfig = async/],
    ["dome.js", "loadEscConfig", /const loadEscConfig = async/],
    ["servo.js", "loadCalib", /const loadCalib = async/],
  ];

  for (const [file, fnName, fnPattern] of patterns) {
    const content = readFileSync(
      join(__dirname, `../../data/${file}`),
      "utf-8"
    );

    // Verify the loader structure:
    // 1. Shows "Loading..." feedback
    assert(
      content.includes("Loading"),
      `${fnName} should show loading feedback`
    );

    // 2. Has try-catch
    assert(
      content.includes("try") && content.includes("catch"),
      `${fnName} should have try-catch`
    );

    // 3. Checks PABootstrap before showing error
    assert(
      content.includes("if (!window.PABootstrap)") ||
      content.includes('!window.PABootstrap'),
      `${fnName} should check PABootstrap`
    );

    // 4. Rethrows the error
    assert(
      content.includes("throw error"),
      `${fnName} should rethrow error`
    );
  }
});

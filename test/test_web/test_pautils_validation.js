import { test } from 'node:test';
import assert from 'node:assert';
import fs from 'fs';
import path from 'path';

// Extract PAUtils exported keys from web_api.js
function extractPAUtilsExports() {
  const webApiPath = path.join(process.cwd(), 'data', 'web_api.js');
  const content = fs.readFileSync(webApiPath, 'utf8');

  // Find the window.PAUtils = { ... } block
  const match = content.match(/window\.PAUtils\s*=\s*\{([^}]+)\}/s);
  if (!match) {
    throw new Error('Could not find window.PAUtils export in web_api.js');
  }

  // Extract all keys (identifiers before : or ,)
  const exportBlock = match[1];
  const keys = [];
  const keyRegex = /(\w+)\s*[,:]/g;
  let keyMatch;

  while ((keyMatch = keyRegex.exec(exportBlock)) !== null) {
    keys.push(keyMatch[1]);
  }

  return new Set(keys);
}

// Find all window.PAUtils.<name> references in data/*.js
function findPAUtilsReferences() {
  const dataDir = path.join(process.cwd(), 'data');
  const files = fs.readdirSync(dataDir)
    .filter(f => f.endsWith('.js'))
    .map(f => path.join(dataDir, f));

  const references = new Map(); // fileName -> Set of referenced keys

  files.forEach(filePath => {
    const content = fs.readFileSync(filePath, 'utf8');
    const lines = content.split('\n');

    const fileRefs = new Set();

    lines.forEach((line, idx) => {
      // Match window.PAUtils.<name>( or window.PAUtils.<name> (with word boundary)
      const refRegex = /window\.PAUtils\.(\w+)/g;
      let refMatch;

      while ((refMatch = refRegex.exec(line)) !== null) {
        const key = refMatch[1];
        fileRefs.add({ key, line: idx + 1, context: line.trim().substring(0, 80) });
      }
    });

    if (fileRefs.size > 0) {
      references.set(path.basename(filePath), fileRefs);
    }
  });

  return references;
}

test('PAUtils references are to exported keys', () => {
  const exportedKeys = extractPAUtilsExports();
  const references = findPAUtilsReferences();

  const errors = [];

  references.forEach((refSet, fileName) => {
    refSet.forEach(({ key, line, context }) => {
      if (!exportedKeys.has(key)) {
        errors.push(
          `${fileName}:${line} references undefined PAUtils.${key}\n` +
          `  Context: ${context}\n` +
          `  Exported keys: ${Array.from(exportedKeys).join(', ')}`
        );
      }
    });
  });

  if (errors.length > 0) {
    assert.fail(
      `Found ${errors.length} PAUtils reference error(s):\n\n` +
      errors.join('\n\n')
    );
  }
});

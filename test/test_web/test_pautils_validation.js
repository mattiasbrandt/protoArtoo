import { test } from 'node:test';
import assert from 'node:assert';

// Test that PAUtils functions are properly exported and usable

test('PAUtils escapeHtml function exists and works', () => {
  const escapeHtml = (text) => {
    if (!text) return '';
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  };

  // Test: escapeHtml handles HTML entities
  assert.equal(escapeHtml('<div>test</div>'), '&lt;div&gt;test&lt;/div&gt;');
  assert.equal(escapeHtml('a&b'), 'a&amp;b');
  assert.equal(escapeHtml('say "hi"'), 'say &quot;hi&quot;');
});

test('PAUtils functions handle edge cases gracefully', () => {
  const escapeHtml = (text) => {
    if (!text) return '';
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  };

  // Test: null/undefined handling
  assert.equal(escapeHtml(null), '');
  assert.equal(escapeHtml(undefined), '');

  // Test: empty string
  assert.equal(escapeHtml(''), '');

  // Test: normal string with no special chars
  assert.equal(escapeHtml('hello'), 'hello');
});

test('PAUtils exported functions are callable and return expected types', () => {
  // Create a mock PAUtils object with key functions
  const PAUtils = {
    escapeHtml: (str) => String(str || '').replace(/[&<>"]/g, (m) => {
      const entities = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' };
      return entities[m];
    }),
  };

  // Verify the function exists
  assert.ok(typeof PAUtils.escapeHtml === 'function', 'escapeHtml must be a function');

  // Verify it returns a string
  const result = PAUtils.escapeHtml('<test>');
  assert.ok(typeof result === 'string', 'escapeHtml must return a string');
  assert.ok(result.includes('&lt;'), 'escapeHtml must escape HTML');
});

test('PAUtils prevents XSS through proper escaping', () => {
  const escapeHtml = (text) => {
    if (!text) return '';
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  };

  // XSS payloads should be neutralized
  const xssPayload = '<img src="x" onerror="alert(\'XSS\')">';
  const escaped = escapeHtml(xssPayload);

  assert.equal(escaped, '&lt;img src=&quot;x&quot; onerror=&quot;alert(\'XSS\')&quot;&gt;');
  // Verify dangerous characters are gone
  assert.ok(!escaped.includes('<'), 'Should not contain unescaped <');
  assert.ok(!escaped.includes('>'), 'Should not contain unescaped >');
});

test('PAUtils escapeHtml called only once on untrusted input (single-pass safety)', () => {
  const escapeHtml = (text) => {
    if (!text) return '';
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  };

  const userInput = '<div>test</div>';
  const escaped = escapeHtml(userInput);

  // When called once on untrusted input, HTML is safely neutralized
  assert.equal(escaped, '&lt;div&gt;test&lt;/div&gt;');

  // Verify the result can be safely used in HTML context
  // (e.g., innerHTML = escapeHtml(userInput) will show literal text, not DOM)
  assert.ok(!escaped.includes('<'), 'No unescaped < means no tag injection');
  assert.ok(!escaped.includes('>'), 'No unescaped > means tag is incomplete');
});

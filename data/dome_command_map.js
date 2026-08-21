/**
 * data/dome_command_map.js
 *
 * Body-owned canonical-id -> command-target mapping for dome panels and groups.
 * Translates between structured element IDs + capabilities and Panel Intent
 * command strings, per ADR 0009.
 *
 * FORWARD: resolvePanelCommand(elementId, capability) -> command string
 *   Example: 'P1' + 'open' -> ':OP01'
 *   Example: 'PP1' + 'close' -> ':CLP1'
 *
 * REVERSE: decodeCommandToElement(cmd) -> { id, capability, kind } or null
 *   Example: ':OP01' -> { id: 'P1', capability: 'open', kind: 'ring' }
 *   Example: ':OPP1' -> { id: 'PP1', capability: 'open', kind: 'pie' }
 *   Keys on command FORM (numeric vs alias) to resolve P1 collision.
 *
 * GROUPS: resolveGroupCommand(groupId, capability) -> command string
 *   Example: 'pie' + 'open' -> ':OP14'
 *
 * Static MK4 commandable set: rings P1/P2/P3/P4/P7/P11/P13,
 * pies PP1/PP2/PP3/PP4/PP5/PP6. Fixed panels (P5, P6, P8, P9, P10, P12, P14)
 * are not commandable and will return null when queried.
 */

(function() {
  'use strict';

  /**
   * Static panel command targets, keyed by canonical element ID and kind.
   * Bounded to the MK4 commandable set only.
   *
   * Ring targets are numeric strings (01, 02, ..., 13).
   * Pie targets are alias strings (P1, P2, ..., P6).
   * Fixed panels and non-MK4 elements are omitted.
   */
  const PANEL_COMMAND_TARGETS = {
    // Ring panels: numeric targets
    ring: {
      'P1': '01',
      'P2': '02',
      'P3': '03',
      'P4': '04',
      'P7': '07',
      'P11': '11',
      'P13': '13',
    },
    // Pie panels: alias targets (P1..P6)
    pie: {
      'PP1': 'P1',
      'PP2': 'P2',
      'PP3': 'P3',
      'PP4': 'P4',
      'PP5': 'P5',
      'PP6': 'P6',
    },
  };

  /**
   * Static group targets, keyed by group ID.
   * Groups are body-owned, not layout elements, and are always available
   * when the picker is available (never blocked by inactive members).
   */
  const GROUP_TARGETS = {
    'all': '00',   // All panels
    'pie': '14',   // Pie group
    'ring': '15',  // Ring group
  };

  /**
   * Capability-to-prefix mapping. Defines how generic capabilities
   * map to Panel Intent command prefixes.
   */
  const CAPABILITY_PREFIXES = {
    'open': ':OP',
    'close': ':CL',
    'flutter': ':OF',
  };

  /**
   * FORWARD: Resolve a canonical panel element ID and capability to
   * a Panel Intent command string.
   *
   * @param {string} elementId - Canonical element ID (e.g., 'P1', 'PP1').
   * @param {string} capability - Generic capability (e.g., 'open', 'close', 'flutter').
   * @returns {string|null} Command string (e.g., ':OP01', ':CLP1') or null
   *   if the element is unmapped, unknown, or not commandable.
   *
   * Note: P5 (ring) and P6 (ring) are fixed/not commandable in the MK4.
   *       Return null for these to signal non-actionability.
   */
  function resolvePanelCommand(elementId, capability) {
    const prefix = CAPABILITY_PREFIXES[capability];
    if (!prefix) {
      return null;  // Unknown capability
    }

    // Try to resolve as a ring panel (numeric target)
    const ringTarget = PANEL_COMMAND_TARGETS.ring[elementId];
    if (ringTarget !== undefined) {
      return prefix + ringTarget;
    }

    // Try to resolve as a pie panel (alias target)
    const pieTarget = PANEL_COMMAND_TARGETS.pie[elementId];
    if (pieTarget !== undefined) {
      return prefix + pieTarget;
    }

    // Not found or not commandable
    return null;
  }

  /**
   * REVERSE: Decode a Panel Intent command string back to a structured
   * representation of the element and capability.
   *
   * Decoding keys on command FORM, not alias vocabulary, to resolve
   * the P1 collision: ':OP01' is ring panel P1, but ':OPP1' is pie
   * panel PP1. A numeric target (01, 02, ..., 13) maps to a ring panel;
   * an alias target (P1, P2, ..., P6) maps to a pie panel.
   *
   * @param {string} cmd - Panel Intent command string (e.g., ':OP01', ':OPP1', ':OP14').
   * @returns {Object|null} Structured element { id, capability, kind } or null
   *   if the command is not a recognized panel/group command or is unmapped.
   *   kind: 'ring', 'pie', or 'group'.
   *
   * Examples:
   *   ':OP01' -> { id: 'P1', capability: 'open', kind: 'ring' }
   *   ':OPP1' -> { id: 'PP1', capability: 'open', kind: 'pie' }
   *   ':OP14' -> { id: 'pie', capability: 'open', kind: 'group' } (or null for group)
   *   ':OP99' -> null (unmapped target)
   */
  function decodeCommandToElement(cmd) {
    // Extract prefix and target: e.g., ':OP01' -> prefix='OP', target='01'
    if (!cmd || typeof cmd !== 'string' || !cmd.startsWith(':')) {
      return null;
    }

    const rest = cmd.slice(1);  // Remove leading ':'
    let prefix = '';
    let target = '';

    // Prefix is 2 characters (e.g., 'OP', 'CL', 'OF')
    if (rest.length < 3) {
      return null;  // Too short to be a valid command
    }
    prefix = rest.slice(0, 2);
    target = rest.slice(2);    // Everything after the prefix is the target

    // Resolve capability from prefix
    let capability = null;
    for (const [cap, pref] of Object.entries(CAPABILITY_PREFIXES)) {
      if (pref === ':' + prefix) {
        capability = cap;
        break;
      }
    }
    if (!capability) {
      return null;  // Unknown prefix
    }

    // Determine whether the target is numeric (ring) or alias (pie or group)
    const isNumericTarget = /^\d+$/.test(target);

    if (isNumericTarget) {
      // Numeric target: resolve to ring panel
      for (const [id, tgt] of Object.entries(PANEL_COMMAND_TARGETS.ring)) {
        if (tgt === target) {
          return { id, capability, kind: 'ring' };
        }
      }
      // Also check groups (e.g., '00' for all, '15' for ring group)
      for (const [groupId, tgt] of Object.entries(GROUP_TARGETS)) {
        if (tgt === target) {
          return { id: groupId, capability, kind: 'group' };
        }
      }
      return null;  // Numeric target not mapped
    } else {
      // Alias target (e.g., 'P1', 'P6', 'P14'): resolve to pie panel or group
      // Check pie panels first
      for (const [id, tgt] of Object.entries(PANEL_COMMAND_TARGETS.pie)) {
        if (tgt === target) {
          return { id, capability, kind: 'pie' };
        }
      }
      // Not a pie panel, return null (groups use numeric targets)
      return null;
    }
  }

  /**
   * FORWARD: Resolve a group ID and capability to a Panel Intent
   * group command string.
   *
   * @param {string} groupId - Group ID (e.g., 'all', 'pie', 'ring').
   * @param {string} capability - Generic capability (e.g., 'open', 'close', 'flutter').
   * @returns {string|null} Group command string (e.g., ':OP00', ':OP14', ':OP15')
   *   or null if the group or capability is unknown.
   */
  function resolveGroupCommand(groupId, capability) {
    const prefix = CAPABILITY_PREFIXES[capability];
    if (!prefix) {
      return null;  // Unknown capability
    }

    const target = GROUP_TARGETS[groupId];
    if (target === undefined) {
      return null;  // Unknown group
    }

    return prefix + target;
  }

  // Export public API
  window.DomeCommandMap = {
    PANEL_COMMAND_TARGETS,
    GROUP_TARGETS,
    resolvePanelCommand,
    decodeCommandToElement,
    resolveGroupCommand,
  };
})();

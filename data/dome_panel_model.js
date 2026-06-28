/**
 * data/dome_panel_model.js
 *
 * Dome panel model: identity, command targets, and attributes.
 * Separates physical panel identity (what the operator sees) from command targets (what the protocol uses).
 * Loaded before seq.js by page_loader.
 */

window.DOME_PANEL_MODEL = {
  // Pie panels (wedges, selectable)
  pie: [
    { id: 'PP1', target: 'P1', kind: 'pie', group: 14, serviced: true, selectable: true, label: 'PP1' },
    { id: 'PP2', target: 'P2', kind: 'pie', group: 14, serviced: true, selectable: true, label: 'PP2' },
    { id: 'PP3', target: 'P3', kind: 'pie', group: 14, serviced: false, selectable: true, label: 'PP3 (unserviced)' },
    { id: 'PP4', target: 'P4', kind: 'pie', group: 14, serviced: true, selectable: true, label: 'PP4' },
    { id: 'PP5', target: 'P5', kind: 'pie', group: 14, serviced: false, selectable: true, label: 'PP5 (unserviced)' },
    { id: 'PP6', target: 'P6', kind: 'pie', group: 14, serviced: true, selectable: true, label: 'PP6' },
  ],

  // Ring panels (path-based, physical widths)
  ring: [
    { id: 'P1', target: '01', kind: 'ring', group: 15, serviced: true, selectable: true, label: 'P1' },
    { id: 'P2', target: '02', kind: 'ring', group: 15, serviced: true, selectable: true, label: 'P2' },
    { id: 'P3', target: '03', kind: 'ring', group: 15, serviced: true, selectable: true, label: 'P3' },
    { id: 'P4', target: '04', kind: 'ring', group: 15, serviced: true, selectable: true, label: 'P4 (wide)' },
    { id: 'P7', target: '07', kind: 'ring', group: 15, serviced: true, selectable: true, label: 'P7 (wide)' },
    { id: 'P11', target: '11', kind: 'ring', group: 15, serviced: true, selectable: true, label: 'P11 (narrow)' },
    { id: 'P13', target: '13', kind: 'ring', group: 15, serviced: true, selectable: true, label: 'P13 (narrow)' },
  ],

  // Fixed features (non-selectable)
  fixed: [
    { id: 'P8', target: null, kind: 'fixed', serviced: true, selectable: false, label: 'P8 (fixed, RPSI)' },
    { id: 'P14', target: null, kind: 'fixed', serviced: true, selectable: false, label: 'P14 (fixed, FPSI)' },
    { id: 'P12', target: null, kind: 'fixed', serviced: true, selectable: false, label: 'P12 (fixed, FLDs)' },
    { id: 'P9', target: null, kind: 'fixed', serviced: true, selectable: false, label: 'P9 (fixed, RLD)' },
    { id: 'P10', target: null, kind: 'fixed', serviced: true, selectable: false, label: 'P10 (fixed)' },
    { id: 'P6/MP/P5', target: null, kind: 'fixed', serviced: true, selectable: false, label: 'P6/MP/P5 (merged fixed)' },
  ],

  // Holo projectors (decorative)
  holos: [
    { id: 'HP1', label: 'HP1 (Front Holo)' },
    { id: 'HP2', label: 'HP2 (Rear Holo)' },
    { id: 'HP3', label: 'HP3 (Top Holo, on PP3)' },
  ],

  // Groups
  groups: [
    { id: 'all', target: '00', label: 'All panels' },
    { id: 'pie', target: '14', label: 'Pie / top group' },
    { id: 'ring', target: '15', label: 'Ring / bottom group' },
  ],

  // Helper: Get all selectable panels (pie + ring)
  getAllSelectable() {
    return [...this.pie, ...this.ring].filter(p => p.selectable);
  },

  // Helper: Get panel by target
  getPanelByTarget(target) {
    const all = [...this.pie, ...this.ring];
    return all.find(p => p.target === target);
  },

  // Helper: Get panel by id
  getPanelById(id) {
    const all = [...this.pie, ...this.ring, ...this.fixed];
    return all.find(p => p.id === id);
  },

  // Helper: Check if target is pie
  isPieTarget(target) {
    return this.pie.some(p => p.target === target) || target === '14';
  },

  // Helper: Check if target is unserviced
  isUnserviced(target) {
    const panel = this.getPanelByTarget(target);
    return panel && !panel.serviced;
  },
};

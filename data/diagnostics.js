// Shared heap health thresholds — single source of truth for both index and setup pages.
// Calibrated against T24 validated runtime floors (heapMin >=40 KB during active SSE).
window.PA_HEAP = {
  freeCritical:    40000,   // bytes — genuinely dangerous, system near crash
  freeWarn:        65000,   // bytes — tighter than comfortable, worth monitoring
  minCritical:     36864,   // bytes (36 KB) — below T24 validated floor
  minWarn:         53248,   // bytes (52 KB) — approaching floor
  largestCritical: 20480,   // bytes (20 KB) — severely fragmented
  largestWarn:     36864,   // bytes (36 KB) — fragmentation building
};

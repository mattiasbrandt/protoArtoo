// =============================================================================
// test/test_web/helpers/booted_droid.js
//
// What a fake droid reports it STARTED with, beside what it has saved (#371).
//
// GET /api/config carries `activeToggles` (the Component Toggle ids switched on
// at start) and `rc.activeInputMode` (the receiver mode read at start) from the
// firmware's boot projections (src/web/api_config.cpp addActiveFields()). They
// do not move until the droid restarts, so a fake droid takes them from the
// config it answers its first read with - that read is its boot - and adds them
// to every /api/config answer after it, saves included. A fixture whose config
// already carries `activeToggles` is left as it is.
// =============================================================================

// The config's own report of its boot state, from the answer that boots it.
const bootReportOf = (config) => ({
  toggles: Object.entries(config?.components || {})
    .filter(([, entry]) => entry && entry.enabled === true)
    .map(([id]) => id),
  mode: typeof config?.rc?.inputMode === "string" ? config.rc.inputMode : null,
});

/**
 * @returns {(method: string, path: string, data: any) => any} decorates one
 *   /api/config answer; passes anything else through untouched
 */
export const bootedDroid = () => {
  let booted = null;
  return (method, path, data) => {
    if (path !== "/api/config" || !data || typeof data !== "object") return data;
    // A fixture that states its own boot report is a droid opened after a
    // save it has not started on yet, and keeps what it says.
    if (Array.isArray(data.activeToggles)) return data;
    if (!booted && method === "GET") booted = bootReportOf(data);
    if (!booted) return data;
    const answer = { ...data, activeToggles: [...booted.toggles] };
    if (booted.mode !== null) answer.rc = { ...(data.rc || {}), activeInputMode: booted.mode };
    return answer;
  };
};

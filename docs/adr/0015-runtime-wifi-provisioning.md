# Runtime WiFi provisioning for public release binaries

Public release binaries must be usable by a Public Release Operator without editing
`src/secrets.h`, providing CI secrets, or building from source. WiFi mode is therefore
operator-owned runtime configuration: an Unprovisioned Controller starts WiFi
Provisioning with a documented Default AP Credential, then the operator chooses either
WiFi Client Mode or Standalone AP Mode through the existing WiFi page, backed by the
normal config/API surfaces. The release matrix stays one binary per sound backend
(`protoArtoo_chirp`, `protoArtoo_mp3trigger`); it does not multiply artifacts by
WiFi mode.

Device WiFi Settings are retained on the controller and survive firmware upgrades. Moving
between WiFi Client Mode and Standalone AP Mode is a Staged Network Switch: the WiFi page
saves the new posture and applies it through an explicit reboot or WiFi restart handoff,
rather than attempting a fragile live toggle inside the current browser session. Network
Recovery Mode is entered only by explicit local action and temporarily exposes WiFi
Provisioning so unreachable settings can be repaired; ordinary WiFi Client Mode connection
trouble must not automatically switch the controller into AP mode.

`src/secrets.h` remains only a Developer WiFi Shortcut for local/self-built firmware and is
not part of the public release networking contract.

WLED is a useful ESP32-family reference for regular-user WiFi onboarding. protoArtoo may
borrow lessons from WLED's AP bootstrap, WiFi settings, and reconnect/recovery presentation,
but WLED is prior art rather than a dependency or a binding specification; protoArtoo still
implements provisioning through its own WiFi page, config/API architecture, and safety
constraints.

## WLED prior-art contract

This section captures the useful WLED observations so later protoArtoo implementers do not
need to re-read WLED before building the runtime WiFi provisioning slices.

### Observations

- WLED's quick-start path is usable without source edits: a newly flashed device exposes a
  documented AP SSID, a documented bootstrap password, and a browser address in AP mode.
- WLED keeps WiFi status and editable network settings in a WiFi-focused settings surface:
  station SSID/password, mDNS/client IP, AP SSID/password/channel/open policy, AP IP, and
  WiFi power options are presented together.
- WLED's WiFi page makes the network change an explicit submit action. Its copy tells the
  operator to save/connect, then reconnect the browser to the home WiFi and discover the
  resulting device IP.
- WLED's implementation supports optional scan-assisted SSID entry: scan results are sorted by
  signal strength, duplicate SSIDs collapse to the strongest entry, and an "Other network"
  escape hatch keeps manual entry available.
- WLED stores plaintext WiFi secrets separately from normal config JSON. The non-secret config
  serialization reports password presence by length, while a separate security file owns the
  actual passphrases.
- WLED has a configurable AP-open policy: boot-without-connection, disconnected, always, never,
  or temporary. The temporary mode shuts the AP down after a timeout when there are no clients.
- WLED has a local recovery gesture: holding button 0 for more than 6 seconds opens or resets
  the WiFi AP path, while longer holds escalate to broader flash erasure.

### Borrow for protoArtoo

- Make WiFi Provisioning equally concrete: public release docs and the WiFi page must show the
  AP SSID, Default AP Credential, browser address, and what the operator should do next.
- Keep the existing WiFi page as the primary operator surface for WiFi Provisioning, WiFi Client
  Mode, Standalone AP Mode, Device WiFi Settings, active diagnostics, staged settings, apply
  state, and reconnect guidance.
- Treat a WiFi change as a Staged Network Switch: save Device WiFi Settings first, then present
  an explicit apply/reboot handoff and tell the operator whether to reconnect to the controller
  AP address, `artoo.local`, or the observed STA IP.
- Use write-only password handling: read responses should expose password-set flags or lengths,
  never normal plaintext password fields. Omitted password fields preserve existing saved
  passphrases.
- Consider scan-assisted SSID selection only as a later UI enhancement. It must stay bounded,
  optional, and compatible with protoArtoo's heap and Core 0 WiFi/web budget.
- Present Network Recovery Mode as an explicit local recovery path that temporarily exposes WiFi
  Provisioning without erasing Device WiFi Settings merely by entering recovery.

### Do not borrow

- Do not add WLED as a dependency, copy WLED's runtime config model, or make WLED's JSON files
  part of protoArtoo's persistence contract.
- Do not adopt WLED's automatic AP-open policies. In protoArtoo, ordinary WiFi Client Mode
  connection trouble remains a visible client-mode problem; it does not become automatic AP
  fallback.
- Do not expand this work into WLED feature parity: multi-WiFi profiles, static IP forms, AP
  channel selection, hidden AP SSIDs, WiFi power tuning, Ethernet selectors, OTA locking, and
  WLED app discovery are outside this contract unless a later protoArtoo issue explicitly adds
  them.
- Do not copy WLED's destructive button escalation. Network Recovery Mode may use an explicit
  local gesture, but entering recovery must not erase saved Device WiFi Settings by default.
- Do not replace protoArtoo's existing config/API apply architecture. Web handlers remain
  adapters that validate and stage settings; they do not directly hot-toggle WiFi hardware as a
  side effect of editing form fields.

### Implementation contract

- Add a small, testable WiFi boot decision layer that consumes Device WiFi Settings, explicit
  Network Recovery Mode input, and optional Developer WiFi Shortcut defaults, then returns one
  requested posture: WiFi Provisioning, WiFi Client Mode, or Standalone AP Mode.
- Persist Device WiFi Settings through the existing config serializer/cache path. Include
  provisioned/unprovisioned state, selected mode, STA SSID/password, AP SSID/password, and enough
  staged/apply metadata for the WiFi page to distinguish active settings from pending settings.
- Extend the WiFi page rather than creating a parallel setup-only flow. The setup page may link
  to WiFi settings, but it is not the primary WiFi settings page.
- Keep `/api/wifi` as the diagnostics/readiness surface and extend it only as needed for active
  posture, provisioning state, AP/STA IPs, RSSI, and staged-switch status.
- Route WiFi writes through the existing config/API apply pattern. Validate unsupported modes,
  empty or overlong required SSIDs, invalid AP passwords, malformed staged-switch requests, and
  password omission semantics.
- Make reconnect copy concrete and stateful: after applying WiFi Client Mode, prefer
  `artoo.local` and the observed STA IP; after applying Standalone AP Mode or WiFi Provisioning,
  show the AP SSID and AP address.
- Document Network Recovery Mode with the exact local action, temporary provisioning behavior,
  and rule that saved Device WiFi Settings are preserved unless the operator explicitly changes
  or resets them.

### WLED references

- [WLED getting started WiFi onboarding](https://kno.wled.ge/basics/getting-started/#wifi-setup)
- [WLED WiFi settings overview](https://kno.wled.ge/features/settings/#wifi-settings)
- [WLED WiFi settings page source](https://github.com/wled/WLED/blob/main/wled00/data/settings_wifi.htm#L200-L260)
- [WLED AP defaults](https://github.com/wled/WLED/blob/main/wled00/const.h#L40-L49)
- [WLED AP behavior constants](https://github.com/wled/WLED/blob/main/wled00/const.h#L244-L252)
- [WLED config password-length serialization](https://github.com/wled/WLED/blob/main/wled00/cfg.cpp#L848-L854)
- [WLED security password serialization](https://github.com/wled/WLED/blob/main/wled00/cfg.cpp#L1209-L1262)
- [WLED button recovery documentation](https://kno.wled.ge/features/macros/#buttons)

## Considered options

- **Ship public release binaries with placeholder STA credentials** — rejected: the
  controller would boot unreachable for a non-developer operator.
- **Publish separate AP and STA release artifacts** — rejected: sound backend is the
  release artifact axis; adding WiFi-mode variants makes artifact selection harder and
  preserves the wrong build-time ownership.
- **Make AP mode provisioning-only** — rejected: operators may intentionally keep the
  controller in Standalone AP Mode or switch into it for field use away from the home
  network.
- **Automatically fall back to AP when STA connection fails** — rejected: connection
  failure is too noisy to treat as credential failure; recovery must be explicit.
- **Migrate existing compiled `secrets.h` values into runtime settings for v1.0.0** —
  deferred: useful for some self-build upgrades, but not required for the first public
  binary contract and adds secret-handling complexity.

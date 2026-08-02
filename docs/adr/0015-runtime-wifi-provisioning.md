# Runtime WiFi provisioning for public release binaries

Public release binaries must be usable by a Public Release Operator without editing
`src/secrets.h`, providing CI secrets, or building from source. WiFi mode is therefore
operator-owned runtime configuration: an Unprovisioned Controller starts WiFi
Provisioning with a documented Default AP Credential, then the operator chooses either
WiFi Client Mode or Standalone AP Mode through the existing setup/config surfaces. The
release matrix stays one binary per sound backend (`protoArtoo_chirp`,
`protoArtoo_mp3trigger`); it does not multiply artifacts by WiFi mode.

Device WiFi Settings are retained on the controller and survive firmware upgrades. Moving
between WiFi Client Mode and Standalone AP Mode is a Staged Network Switch: the setup page
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
implements provisioning through its own setup/config/API architecture and safety constraints.

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

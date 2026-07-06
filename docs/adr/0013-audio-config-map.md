# Audio Config Map: one home for the config-to-audio schema mapping

Issue #18 finding 3 described the ~380-line middle of `audio_task.cpp` as an
untested weld between two tested ends. The 2026-07-07 design session sharpened
that diagnosis: the playback policy seam was already deep —
`AudioPlaybackIntent` is a plain-data action carrying its own flag and timer
directives, and `audioPlaybackResolveRequest` / `audioPlaybackResolveRandomTick`
are natively tested — and most of the middle is pure functions trapped as
file-local statics: the `ConfigSnapshot` -> `AudioPlaybackConfig` mapping, the
named-track projection, the two chirp NVS-key tables, the `$`-command table, and
the binding unpackers. The genuinely impure remainder is the raw `Preferences`
binding refresh and `executePlaybackIntent`, which is a legitimate shell (driver
calls, log formatting, local playback state). Also corrected for the record:
issue #15 (TWDT catalog starvation) is closed, its yield guard pinned by
`test_audio_chirp_catalog_yield`; catalog orchestration is out of scope here.

The decision: a new module, **`audio_config_map`**
(`include/audio_config_map.h`, `src/tasks/audio_config_map.cpp`), becomes the
canonical home of the config-to-audio schema knowledge:

- `audioConfigMapBuild(const ConfigSnapshot&, AudioPlaybackConfig*)`
- `audioConfigMapNamedTracks(const AudioPlaybackConfig&, AudioNamedTracks*)`
- `audioChirpKeyForSlot(...)` / `audioChirpKeyForCategory(...)`
- `audioSlotForDollar(const char*)`
- `audioUnpackChirpBinding(...)` / `audioUnpackChirpCategoryBinding(...)`
- `audioBindingsRefresh(const ConfigReader&, bool catalogCapable,
  AudioBindingCache*)` — the ADR 0002 reader seam reused verbatim
  (`PrefsReader` in production, the existing `MapReader` in tests; the refresh
  needs exactly `readU32(key, default)`).

`audio_playback_policy` stays untouched and deliberately config-free.
`audio_task.cpp` keeps the queue pump, the per-command dispatch glue, the
binding cache storage, and `executePlaybackIntent` as its shell; the
per-command `readPlaybackConfig` rebuild stays (cheap, always-fresh semantics).

The module is the single schema home for **both** sides of the audio surface:
the api_audio Apply Core wave (ADR 0011) consumes its tables — slot/category
enumeration, key names, mapping — rather than growing a second home.
`api_audio.cpp` holds roughly fifty references to this schema family today, and
"add a slot or category" is the drift-prone evolution axis (policy enum, two
key tables, mapping, named tracks, config fields, web handlers, sound.js);
after this ADR that axis starts in one file.

Tests are representative spot checks by owner decision: a handful of mapping
fields, binding-refresh accept/reject cases through `MapReader`, and
`$`-table samples. The sentinel-golden full-field mapping case (distinct
sentinel per input field, every output asserted once) was offered and declined;
the recorded caveat is that a mapping typo in an unchecked field can still
slip, mitigated by the mapping now living in one reviewable file. Delivery is
one behavior-preserving slice (module + task adoption + suite + native
`build_src_filter`), queued in the ADR 0011 campaign order on `phase/v1.0.0`;
the interaction with #16's pending hardware pass is the same accepted risk
class recorded in ADR 0011.

## Status

accepted (2026-07-07 design session; decision tree grilled against issue #18
finding 3, with the finding's diagnosis corrected as described above)

## Considered options

- **Fold the statics into `audio_playback_policy`** — rejected: it couples the
  deliberately Arduino- and config-free policy header to
  `ConfigSnapshot`/`config_store.h`; the purity boundary that makes policy
  natively testable is worth a second module.

- **A new resolve+execute "intent core"** (the finding's original wording) —
  rejected: `AudioPlaybackIntent` already is the plain-data action struct;
  wrapping resolve+execute re-wraps a plan and fails the deletion test.

- **Keep raw `Preferences` I/O in the binding refresh** — rejected: routing the
  reads through `ConfigReader` costs one parameter and makes the whole refresh
  loop (capability gate, key iteration, rejection paths) testable with the
  existing `MapReader` double.

- **Driver-owned binding cache** — rejected: bindings are operator
  configuration consumed by the policy layer; moving them into the CHIRP
  driver couples the driver to NVS and to the slot/category schema.

- **Sentinel-golden full-field mapping test** — offered as the lean
  full-coverage option, declined by the owner in favor of representative spot
  checks; caveat recorded above.

# Config persistence seam: ConfigReader / ConfigWriter

`config_store.cpp` grew to 1,443 lines by embedding NVS I/O (direct `Preferences` calls)
alongside serialization logic (field-by-field mapping, RC binding format/parse), validation,
cache management, and business helpers. Because `Preferences` is a concrete Arduino class with
no virtual methods, `configLoad` and `configSave` cannot be exercised in native unit tests.
The result: a silent NVS corruption was found indirectly through behavioral drift after a
firmware upgrade — no test caught it before it shipped.

We introduce a `ConfigReader` / `ConfigWriter` abstract interface pair that `configDeserialize`
and `configSerialize` operate against. `PrefsReader` / `PrefsWriter` are the production
adapters; `MapReader` / `MapWriter` are the test doubles. `configLoad` and `configSave`
remain the public API and now delegate to the adapters plus the pure serializer. A
`test_config_round_trip` suite covers default, typical, RC binding, schema-migration, and
unknown-key-tolerance cases.

Schema migration: `configDeserialize` applies V0→V1 field defaults silently (pure, testable).
`configLoad` independently reads the schema version from `PrefsReader` and emits a warning
log when migration is detected — preserving diagnostic visibility without polluting the pure
serializer with logging calls.

## Considered options

- **Mock `Preferences` directly** — rejected: `Preferences` has ~20 methods; maintaining a
  faithful mock is more work than the interface and drifts silently when the Arduino SDK
  updates. The `ConfigReader`/`ConfigWriter` interface is 8 methods each — far smaller.

- **Flat `KVPairs[160]` intermediate** — rejected: materializing all 143 keys at once costs
  ~12 KB of allocation; it also requires a separate pass to convert typed values to/from
  strings before any field logic runs. The callback-per-field approach of
  `ConfigReader`/`ConfigWriter` avoids the allocation and keeps type handling in the right
  layer.

- **Split file without new interface** — rejected: subdividing `config_store.cpp` into
  domain files without introducing a testable seam fixes navigation but not test coverage.
  The corruption class recurs.

- **Migration logging inside `configDeserialize`** — rejected: pure functions should not log.
  `configLoad` reads `reader.schemaVersion()` independently and emits the warning; the
  serializer applies defaults and returns. Diagnostic visibility and testability are both
  preserved.

# Controller Console: one operation core below the HTTP handlers, two adapters, key=value records

The dashboard's Live Logs command box reaches one path today - `POST
/api/actions/test` with a single token - and prints `OK` when the HTTP request
returns, even though the dispatch beneath it is `void` and a queue-full drop is
only logged. Serial is output-only. Epic #206 makes the complete existing
operation surface - every action, status query, profiling query and non-secret
configuration path - available to an operator from a browser and from a physical
serial terminal, with the same language, help, completion, availability rules,
safety rules and result meanings on both, on the artoo-esp32 and the FireBeetle 2
ESP32-P4. Two surfaces over one behaviour forces a decision about where that
behaviour lives, and the obvious answers were each wrong in a different way.

We decided:

- **One Controller Console module sits *below* the HTTP handlers.** Every
  Operation resolves to one transport-free callable, and both surfaces call the
  same one:
  - a **config** Operation is an Apply Core plus a **Commit Step kept beside
    it** - the complete transport-independent operation: validate, apply,
    synchronize runtime state, persist where required, emit the canonical log
    and result effects. One Commit Step per core, never a global commit
    function. This amends ADR 0011: the HTTP handler no longer "owns every side
    effect"; it and the Console adapter only translate input into that contract
    and render its structured result.
  - a **status** Operation is the existing Zone Snapshot capture rendered as
    Console Records. The registry owns each query's `fields:` list; a record
    field is named by its API JSON key verbatim (`heapFree`); the proven JSON
    builders are not rewritten - a native test checks builder keys, registry
    fields and record names against each other. Registry rows that merely
    describe a field inside an aggregate response stay metadata, never
    artificial standalone commands. A query is answered synchronously, so it
    reports the outcome `completed`: nothing was queued and nothing changed,
    which neither `queued` nor `applied` states truthfully.
  - an **action** Operation is the existing dispatch/guard core, changed to
    return an outcome (`queued`, `queue-full`, `blocked`, `unavailable`,
    `invalid`) instead of nothing, so the browser stops printing a false `OK`.
- **Two Console Adapters and nothing else.** The browser Live Logs console
  sends a raw Console line to one endpoint - an ordinary admitted route through
  the project-owned request seam, so ADR 0021 is unchanged - and renders the
  returned records beside the live log stream. The serial adapter is a
  persistent Core 0 task over the vendored embedded-cli editor. Neither adapter
  carries command rules; a third surface would be a third adapter over the same
  module.
- **Results are Console Records, not JSON.** Newline-delimited key=value
  records (`result`, or `begin` / `field` / `item` / `end`) with a
  firmware-assigned Request ID that is one counter across both adapters.
  Every token the protocol defines - command names, argument keys, outcomes,
  Availability Reasons - is kebab-case, and where the browser already has a
  token with the same meaning the Console reuses it (`not-in-this-build`,
  `not-on-this-board`) rather than inventing a synonym. Human explanations stay
  natural language; tokens stay stable.
- **Provenance and gating.** Commands entering through an adapter carry
  `SRC_SERIAL_CONSOLE` or `SRC_WEB_CONSOLE` as their Command Source. The only
  consent gate is Non-RC Control - the Commanded Mode formerly named "web
  control" - and it gates exactly what it gates today: a non-RC source
  commanding motion while the RC link is unhealthy. Physical serial is a
  trusted local source with no unlock ceremony; every domain safety invariant
  (estop, stationary/sleep, component availability, queue limits, speed caps,
  validation) applies unchanged.
- **Compiled in on both boards, always on.** No Build Feature Flag is needed -
  nothing is compiled out on any board and the native test environment selects
  files by build filter - and no runtime toggle exists, because a console that
  is off by default defeats recovery after the network has already failed. If
  measurement shows the artoo-esp32 cannot carry the persistent task within ADR
  0017, the answer is to present the numbers, not to default it off.

## Considered options

- **A second in-image `WebRequest` backend fed by serial**, reusing route
  assembly. Rejected: it keeps method/path/body/session concepts and JSON
  response construction on a path whose agreed result is not JSON, so serial
  would serialize the wrong representation and parse it back; it also inherits
  the handlers' single-task static-buffer assumptions.
- **The Console adapter re-implements each handler's post-apply sequence**
  (cache sync, Commanded Mode setters, persistence, log replay), leaving ADR
  0011 as written. Rejected: serial configuration is meant to be
  feature-complete with the browser; a second copy of the side effects is a
  second implementation of correctness and a standing drift risk - the shape
  #188 found duplicated twice in `api_status.cpp` and `web_server.cpp`.
- **JSON as the canonical Console result with a generic JSON-to-text
  renderer** (and an optional raw mode). Rejected: it couples the module to a
  serializer, costs a multi-kilobyte response buffer on the low-heap board, and
  renders poorly on a terminal; records stream bounded through a sink.
- **Per-query field tables replacing the hand-formatted JSON builders** so one
  table drives both outputs. Rejected: substantial contract risk to
  byte-identical JSON responses for no operator-visible gain; the drift test
  gives one enforced list without the rewrite.
- **An always-on minimal listener plus a transient worker task per rich
  command**, or a default-off `enable_serial_console` toggle on artoo-esp32.
  Rejected: create/delete after `setup()` fragments the low-heap board and
  contradicts the no-dynamic-allocation rule for task loops; a default-off
  console is absent exactly when it is needed.
- **A REPL library that owns the UART (`esp_console`), or a hand-rolled line
  reader.** Rejected in the library survey recorded with the plan: per-line
  allocation and port ownership on the one hand, re-implementing editing,
  history and completion on the other. embedded-cli is vendored with a pinned,
  host-tested patch set instead.

## Consequences

- Every write handler changes shape once: T10/T11 extract the handler-owned
  side effects into the Commit Step beside each Apply Core **before** any
  Console write uses that path, with HTTP responses proven byte-identical.
  `persistSystemConfig(WebRequest&, ...)`, which sends its own HTTP error today,
  is the first such extraction.
- The action dispatch helper shared with the RC path changes from `void` to an
  outcome; that is safety-adjacent and carries a diff proof on both targets.
- The registry gains `fields:` lists for actual status queries and the
  console metadata (argument schemas, aliases, availability, executor
  references) the catalog is generated from; `tools/check_action_registry_drift.py`
  enforces registry <-> inventory <-> catalog one-to-one and reads
  `tools/console_inventory/*.yaml`.
- Request IDs are one global counter: a browser tab may see gaps while a serial
  session is active. They are correlation keys, not per-adapter sequence
  numbers.
- Password writes are excluded (`secret-not-settable`) and the serial editor is
  embedded-cli's own with editor-only cursor sequences; both are recorded in the
  plan and CONTEXT.md rather than here because either can be reversed without
  touching this architecture.
- Implementation location (a normal-review detail, fixed here so the epic's
  concurrency table can name it): the transport-neutral module, catalog and
  record renderer live in `src/console/` with flat `include/console_*.h`
  headers; the serial adapter task in `src/tasks/console_task.cpp`; the browser
  endpoint in `src/web/api_console.cpp`; host-native tests in
  `test/test_console/`; `src/console/*.cpp` join the native build filter.
- The protocol itself - grammar, quoting, line endings, records, outcomes,
  reasons, meta-commands, editor and transport rules - is specified in
  `docs/console-protocol.md`, which the implementing tickets treat as the
  reference.

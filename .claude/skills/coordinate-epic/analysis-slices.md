# Analysis slices

A sub-issue whose deliverable is a classification table: one row per entry,
a tier or verdict per row, and an evidence column that carries the whole
result. Two mechanisms have produced a table that passed every structural
check and was wrong; both are guarded below.

## Skeleton, not spec

Hand the worker the N rows, never "produce a table of N things". Generate the
rows yourself from the fields the source manifest already carries
(`api_path`, `cpp_enum`, `cpp_file`, `sse_event`, `nvs_key`, ...) and put the
literal sentinel `FILL` in every cell the worker must decide. Incompleteness
is then `grep -c FILL`, and a returned table containing `FILL` is unfinished
by definition. Split a large population by domain across workers so no single
agent faces the volume that tempts a shortcut.

Scripts *gather* - extract fields, list call sites, resolve citations. A row
is decided by reading. A script whose fallback assigns a tier is the defect
regardless of how its allowlist was built: #186 attempt 1 classified 185 of
189 rows from an `else: return universal`, and the aggregate was numerically
correct, so no check of the conclusion could see it.

Specify what evidence the **negative** case cites. "Universal" names the
unconditional registration site with a `file:line`; an unspecified negative
is exactly what a default branch fills. Then verify structurally: row count,
zero sentinels, zero empty evidence, every citation resolves, and a
spot-check that cited lines say what is claimed.

## Independent anchors, not headcount

Structural checks prove a table is *honest*. Only a different anchor tests
whether it is *complete*. Five workers on one prompt are five copies of one
blind spot: on #186 every worker and the critic resolved each entry to its
route-registration site, and a second gate one file away - in the handler's
declaration header - went unseen 189 times until an agent with a different
anchor opened the header.

Give parallel workers different anchors and failure modes:

- **Entry-forward**: row -> every manifest anchor -> registration -> handler
  declaration header -> definition -> prerequisite -> env. A row closes only
  with both its registration site *and* its handler's declaration header
  open; a route citation alone never classifies a row. Runs on every row.
- **Gate-reverse**: every `#if` / `PA_*` / `CONFIG_*` site -> the rows it
  affects. Cheap and complete over preprocessor gating; run it first as an
  ordering, never as permission to skip a row.
- **Paired-artefact**: the compiled-object manifest and linker map per
  declared full-app env (`pio run -t idedata`, `.pio/build/<env>/firmware.map`).
  The only direction that sees build-config absence - `build_src_filter`,
  `lib_ignore`, `lib_deps` leave no preprocessor trace. Scope the env set from
  `platformio.ini` and require a build for each; a manifest inferred from
  whatever sits in `.pio` reproduces the original failure one level up.

Your own critic pass uses an anchor the workers did not. Ask, per row, "what
else could compile this out?" and record the answer.

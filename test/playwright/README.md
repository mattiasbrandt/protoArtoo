# Browser scripts

Real-browser checks of the operator UI: what the web suite's DOM
(`test/test_web/helpers/mini_dom.js`) cannot see - layout, hit testing, focus,
a real status stream, a request that is or is not sent. Each one runs against
a droid, or offline against the fixture server.

## Layout

- `<surface>/<rule>.js` - one standing rule per script, named for the rule
  (`servo/estop-leaves-outputs-limp.js`). Its header says the rule, the
  precondition, what it writes and how to run it.
- `_lib/checks.js` - the harness every script shares: `runCheck()`, the write
  guard, preconditions, the PASS/FAIL table, `STEP`, browser launch and close.
- `_lib/fixture_routes.js` - the controller routes the fixture server does not
  answer, loaded only with `FIXTURE=1`. Two droids: `artoo` (the default) and
  `bench`; its header says what each models and from which source.
- A few older multi-rule scripts (`shell/stop-every-surface.js`,
  `shell/status-plate-truth.js`, `parts/parts-surface.js`, `console-sweep.js`)
  keep their own flow and take the `_lib/` pieces one by one.

## Conventions

- **Headed by default**, so the operator watches when an agent runs a script.
  `HEADLESS=true` for no window. `make bench-auto` runs every script with
  `HEADLESS=true` and the sweep without `HEADED` (operator, 2026-09-29).
- **`STEP=1`** waits for Enter between steps; nothing else reads stdin.
- **`BASE_URL`** is the droid (default `http://10.0.0.22`, trailing slash
  dropped). `console-sweep.js` alone takes `BASE` and is headless unless
  `HEADED=1`.
- **1440x900**, desktop only.
- **`FIXTURE=1`** routes the browser through `_lib/fixture_routes.js`.
  `SELFTEST=<name>` (rule scripts, `FIXTURE=1` only) or an older script's
  `SELFTEST_*=1` breaks the check on purpose to prove it can fail.
- **Write guard**: every write a page attempts is recorded, and anything the
  script does not allow is aborted before it leaves the browser. What it
  stopped is listed after the table.
- **`// bench-auto:` target line**: every script under a surface folder
  carries exactly one, and `tools/bench_auto.py` (the automated half of a
  bench session) reads it rather than guessing from the URLs in the file.
  `// bench-auto: droid` runs against the droid with `BASE_URL`, and
  `estop=clear` or `estop=latched` names the estop state its precondition
  needs: the runner orders by it and, before the script, latches or clears
  the estop to match (a Bench-Mode session begins clear; a latch standing
  before a script that needs it clear is cleared, whoever set it).
  `parts=1,2` runs it once per `PART`.
  `// bench-auto: fixture <page>.html` runs it on the runner's own fixture
  server with `FIXTURE=1` and `TARGET_URL` at that page. A new script
  without one stops the runner before it starts.
- **Precondition and exit codes**: a script reads the droid first and refuses
  to run when its rule means nothing in that state.
  `0` every row PASS (NOT ASSESSED beside a PASS allowed),
  `1` any FAIL or the script could not finish,
  `2` the precondition did not hold, or nothing could be assessed.

## Run one

```sh
NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
  node test/playwright/servo/estop-leaves-outputs-limp.js
```

`BASE_URL=http://<board>` for another droid. Screenshots land under
`output/playwright/`.

## Run offline, against the fixture server

The surfaces carry `PA:INCLUDE` markers that a plain static server does not
expand, so use `tools/serve_editor_fixture.py`, not `python3 -m http.server`.
It listens on 4173 by default. Other worktrees may be serving on it too, and
a browser pointed at a shared port reads whichever worktree bound it first, so
start it on a port of your own with `PA_FIXTURE_PORT`:

```sh
PA_FIXTURE_PORT=4186 python3 tools/serve_editor_fixture.py &

NODE_PATH=$HOME/.npm/_npx/e41f203b7505f1fb/node_modules \
  FIXTURE=1 HEADLESS=true BASE_URL=http://127.0.0.1:4186 \
  node test/playwright/servo/estop-leaves-outputs-limp.js
```

Stop the server by its PID when done.

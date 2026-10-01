#!/usr/bin/env python3
"""
PlatformIO pre-build script: build the LittleFS image from GZIPPED web assets.

The web UI is ~1 MB of mostly uncompressed text (JS/CSS/HTML). PsychicHttp's
static handler transparently serves `foo.js.gz` (with Content-Encoding: gzip +
Content-Type from the original extension) when the raw file is absent, so
shipping only the gzipped text shrinks what is staged and speeds page loads — and
frees flash for a coredump partition (#8). Measured on main at 0b55e00f: ~1012 KB
of sources stage to ~325 KB, which LittleFS then rounds into 97 4KB blocks =
397,312 B of the artoo-esp32's 655,360 B partition. The "~180 KB" this line
claimed until 2026-09-11 predated years of UI growth and counted no rounding;
tools/check_build_budgets.py now measures it on every build (#382, ADR 0065).

Mechanism: gzip the text assets from $PROJECT_DATA_DIR into a per-build staging
dir, copy binaries (images, etc.) as-is, and repoint $PROJECT_DATA_DIR at the
staging dir so `buildfs`/`uploadfs` image the gzipped copy. The repo `data/`
stays raw (source of truth); git is untouched.

Excluded from gzip:
  - *version*.json — the firmware opens /fs-version.json DIRECTLY from LittleFS
    (not via serveStatic), so a .gz would break the reported fsVersion.
  - console_help.txt — same shape as version JSON: src/main.cpp opens
    /console_help.txt directly via LittleFS.exists()/.open() in setup() (ADR
    0036, #219), never through PsychicHttp's serveStatic() (the only place a
    .gz is transparently unwrapped). LittleFS.open() has no such fallback, so
    staging only the .gz left the firmware asking for a name that was never on
    the image. .txt IS otherwise gzipped (GZIP_EXTS below), so this is a
    name-based exception, not an extension-based one like version JSON's.
  - images and other binaries — already compressed; copied verbatim.

HTML includes: a page may carry `<!-- PA:INCLUDE _partial.html -->`, which is
replaced with the contents of that file before gzipping. Partials are named with
a leading underscore and are NOT themselves imaged. This exists for the Page
Recovery View kernel, which must be inline on every page the browser renders —
it is the one part of the UI that has to survive a failure that sheds external
assets, so it cannot be an external file — while still living in exactly one
editable source. A missing or unexpanded include is a hard build failure, never
a silently shipped page without recovery — and so is a rendered page that
carries no kernel directive at all, since a page without one fails silently in
exactly the situation recovery exists for.

Since the Operator Shell (ADR 0048) the browser renders one document,
index.html. Every other page is a shell delegate: the shell fetches it and
imports only its <body>, and a direct visit is replaced by the shell before
anything else loads, so a delegate's <head> never runs. A delegate therefore
must NOT carry the kernel — eleven copies cost ten filesystem blocks and could
never execute (#382) — and one that does is refused, so the copies cannot
creep back.

Minification: .js and .css are minified by esbuild (whitespace and comments
only, names kept) before gzipping, so the repo keeps its comments and the image
does not pay for them (#382). A missing esbuild is a hard failure rather than a
quietly larger image. See MINIFY_LOADERS for why it is esbuild.

Markup: after include expansion, every page loses its markup comments, and
whitespace that spans a newline between two tags becomes one newline
(_stage_markup()). Script, style and the other raw-text bodies, the inline
recovery kernel's among them, are staged as written (#461).

Compression: every gzipped asset is written by zopfli, which emits an ordinary
gzip stream that inflates to the same bytes zlib's would, only shorter (#461).
A missing zopfli is a hard failure for the same reason a missing esbuild is.

A partial resolves in the same order the file staging below does: this
environment's asset set first, then the common data root. This lets a set
carry its own partial, and it is why _recovery_kernel.html -- which no set has
ever carried -- keeps resolving from the common root untouched.

Runs after extract_version.py so the freshly-written fs-version.json is included
in the LittleFS staging directory.
"""

import gzip
import os
import re
import shutil
import subprocess
import tempfile

Import("env")  # noqa: F821  (PlatformIO injects this)

# Text types worth gzipping. JSON is intentionally excluded (tiny, and version
# json is read raw by the firmware).
GZIP_EXTS = {".js", ".css", ".html", ".htm", ".svg", ".txt", ".map"}

# Assets the firmware opens by exact name straight off LittleFS (LittleFS.open()
# / .exists()), never through PsychicHttp's serveStatic() -- the only handler
# that transparently falls back from "foo" to "foo.gz". For these, gzipping
# would leave the literal path the firmware asks for missing from the image.
# Extension-excluded assets (version JSON: .json is not in GZIP_EXTS at all)
# don't need an entry here; this set is for names whose extension IS otherwise
# gzipped. See docs/console-protocol.md section 3.4 and src/main.cpp for the
# console_help.txt reader (ADR 0036, #219 D2).
RAW_ASSET_NAMES = {"console_help.txt"}

# Asset sets (ADR 0065). Every board runs the same surfaces, but a board with room
# may carry richer pictures than one without, so a file declares which builds carry
# it by which set directory it sits in -- the fact lives beside the file and cannot
# drift out of step with a list kept somewhere else. Files directly under data/ are
# common to every build; data/asset-sets/<name>/ is staged on top of them, flattened
# to the same paths, for the one set this environment names in platformio.ini
# (custom_asset_set, default "default"). This is the Build Feature Flag tier's answer
# -- "is it in this image" -- asked of a file rather than of a function.
ASSET_SETS_DIR = "asset-sets"
DEFAULT_ASSET_SET = "default"


def _should_gzip(filename):
    if filename in RAW_ASSET_NAMES:
        return False
    if os.path.splitext(filename)[1].lower() not in GZIP_EXTS:
        return False
    return True


# Partials are sources for inlining, not servable assets.
PARTIAL_PREFIX = "_"
# The name may carry one fragment, `_product_art.html#board`, which inlines a
# selection from the partial rather than all of it (see BOARD_FRAGMENT).
INCLUDE_RE = re.compile(r"[ \t]*<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+(?:#[a-z]+)?)\s*-->[ \t]*\n?")

# `#board` on a product-drawing sprite: inline only the running board's own
# drawing (#411). Wiring pictures the Body Controller this image runs on, and
# inlining the whole sprite to show one board cost 11.5 KB of gzipped image on
# the 4 MB board, against about 1 KB for its one symbol. Which product that is
# comes from the Component Registry's own gate - the Body Controller row whose
# `included` test is `(PA_BOARD == <this env's PA_BOARD>)` - so there is no
# second board-to-product map here. A set whose sprite has no symbol for that
# product (the default set carries photographs, not drawings) inlines nothing,
# and the page's frame falls back to the photograph or stays empty.
BOARD_FRAGMENT = "board"
REGISTRY_BODY_CONTROLLER_RE = re.compile(
    r'PA_COMPONENT_PART\(\s*\d+,\s*"([A-Za-z0-9_]+)",[^\n]*COMPONENT_CATEGORY_BODY_CONTROLLER,'
    r"[^\n]*\(PA_BOARD == ([A-Z0-9_]+)\)\)"
)
PA_BOARD_FLAG_RE = re.compile(r"-DPA_BOARD=([A-Z0-9_]+)")
HTML_EXTS = {".html", ".htm"}

# The one partial every rendered page is required to inline. It is checked by
# name rather than by "has some directive" so a page cannot satisfy the guard
# by including something else.
RECOVERY_KERNEL = "_recovery_kernel.html"

# What makes a page a shell delegate (ADR 0048): the line in its <head> that
# hands a direct visit to the Operator Shell. Its <head> never runs otherwise,
# so a delegate must not carry the kernel (see the module docstring).
SHELL_DELEGATE_MARKER = "window.PAShellDelegate = true"

# Minified before gzipping, by esbuild. HTML goes through _stage_markup()
# instead; everything else is staged as written.
# esbuild parses the source, so it removes whitespace and comments without
# touching a string. rjsmin, the regex minifier tried first, rewrote the
# whitespace inside nested template literals in six files -- class="parts-row${`
# ${x}`}" lost its space and joined two class names -- and every file still
# passed `node --check`, so a syntax check is not evidence a minifier is safe.
# Identifiers are never renamed: these are classic scripts sharing globals.
MINIFY_LOADERS = {".js": "js", ".css": "css"}


# Script bundles (#461). Each group is loaded by exactly the same pages, one
# member straight after another in every `data-scripts` chain, so staging ships
# the group as one file: nine fewer requests on a page load and nine fewer
# files on the image. `data/` keeps the members as they are; only the stage
# carries the bundle, and the dev fixture server keeps serving the members.
#
# The rule that keeps this safe is the loader's: data/page_bootstrap.js dedupes
# a resource by name for the whole session (ADD_RESOURCES), so a member that
# some chain loaded on its own would run a second time beside its bundle and
# rebuild its module state. _bundle_chain() therefore refuses to stage a page
# whose chain names any member without its whole group, in order and
# consecutive. The label is what the recovery view shows while the bundle is
# loading or failing; it reuses the words the surfaces give the members.
SCRIPT_BUNDLES = (
    ("/bundle_shell.js", "live updates and page layout",
     ("/status_stream.js", "/live_reading.js", "/health_signals.js", "/shell.js")),
    ("/bundle_configuration.js", "droid configuration", ("/configuration.js", "/setup.js")),
    ("/bundle_body.js", "droid picture", ("/body_art.js", "/body_view.js")),
    ("/bundle_dashboard.js", "home dashboard", ("/dome_control.js", "/app.js")),
    ("/bundle_rehearsal.js", "sequence rehearsal", ("/servo_motion.js", "/seq_rehearsal.js")),
    ("/bundle_seq_moves.js", "sequence timing", ("/seq_tempo.js", "/seq_gesture.js")),
    ("/bundle_seq_editor.js", "sequence editor", ("/seq_timeline.js", "/seq.js")),
)
BUNDLE_OF_MEMBER = {
    member: bundle for bundle, _label, members in SCRIPT_BUNDLES for member in members
}
# The first bundle every session loads (it is in the shell document's own
# chain); it carries every bundle's recovery label, so a later bundle that
# stalls is named. One that stalls before it has run shows the generic words,
# as any script did before its surface had set its labels.
LABELS_BUNDLE = "/bundle_shell.js"
DATA_SCRIPTS_RE = re.compile(r'(<html\b[^>]*?\bdata-scripts=")([^"]*)(")', re.IGNORECASE)
DATA_SCRIPTS_ATTR_RE = re.compile(r"\bdata-scripts\s*=", re.IGNORECASE)
# The two top-level shapes a member may have once minified: an IIFE statement,
# preceded by `const NAME = <object or array literal>` declarations. Anything
# else changes meaning inside the try block that isolates the member, so it
# fails the build.
IIFE_START_RE = re.compile(r"\((?:\(\)=>|function\(\))\{")
GLOBAL_CONST_RE = re.compile(r"const [A-Za-z_$][\w$]*=[\[{]")
JS_KEYWORDS_BEFORE_REGEX = {
    "return", "typeof", "instanceof", "in", "of", "new", "delete", "void", "throw",
    "case", "do", "else", "yield", "await",
}


def _bundle_chain(path, html):
    """Return the staged page with every script group in its `data-scripts`
    chain replaced by its bundle, or fail the build when a chain names a
    member without the rest of its group."""
    match = DATA_SCRIPTS_RE.search(html)
    # Members are never staged on their own, so a chain this cannot read would
    # ship naming files the droid does not have. Exactly one chain, on <html>,
    # double-quoted, or none at all.
    declared = len(DATA_SCRIPTS_ATTR_RE.findall(html))
    if declared > 1 or (declared == 1 and match is None):
        raise SystemExit(
            "[gzip_fsdata] %s declares data-scripts in a form staging does not read; "
            "write it once, on <html>, as data-scripts=\"/a.js,/b.js\"." % path
        )
    if match is None:
        return html
    chain = [name.strip() for name in match.group(2).split(",") if name.strip()]
    for name in chain:
        if any(name == bundle for bundle, _label, _members in SCRIPT_BUNDLES):
            raise SystemExit(
                "[gzip_fsdata] %s names %s, a bundle staging builds; a page names the "
                "bundle's members instead." % (path, name)
            )
    out = []
    i = 0
    while i < len(chain):
        bundle = BUNDLE_OF_MEMBER.get(chain[i])
        if bundle is None:
            out.append(chain[i])
            i += 1
            continue
        members = next(m for b, _label, m in SCRIPT_BUNDLES if b == bundle)
        if tuple(chain[i:i + len(members)]) != members:
            raise SystemExit(
                "[gzip_fsdata] %s loads %s outside its whole group: %s staged as %s needs "
                "exactly %s, consecutive and in that order, wherever any of them is "
                "loaded. A member loaded alone would run twice beside its bundle, "
                "since the loader dedupes by name (data/page_bootstrap.js ADD_RESOURCES)."
                % (path, chain[i], ", ".join(members), bundle, ",".join(members))
            )
        out.append(bundle)
        i += len(members)
    for name in out:
        if name in BUNDLE_OF_MEMBER:
            raise SystemExit("[gzip_fsdata] %s still names %s after bundling." % (path, name))
    return html[:match.start(2)] + ",".join(out) + html[match.end(2):]


def _top_level_statements(path, js):
    """Split minified script text into its top-level statements, each with the
    index (within the statement) where its first bracket closed, or None.

    A small scanner over esbuild's output: strings, template literals with
    nested `${}`, regular expression literals and comments are skipped, and a
    `;` at depth 0 ends a statement. It decides only what _member_parts()
    checks, and a misread fails that check rather than passing it."""
    statements = []
    start = 0
    depth = 0
    first_close = None
    templates = []  # brace depth at each open `${`
    prev = ""  # last significant token: a punctuator character or a word
    i = 0
    n = len(js)

    def regex_allowed():
        if not prev:
            return True
        if prev[-1].isalnum() or prev[-1] in "_$":
            return prev in JS_KEYWORDS_BEFORE_REGEX
        return prev not in (")", "]")

    def skip_template(j):
        # j is just past a backtick or a closing `}` of a substitution.
        while j < n:
            c = js[j]
            if c == "\\":
                j += 2
            elif c == "`":
                return j + 1, False
            elif c == "$" and js.startswith("${", j):
                return j + 2, True
            else:
                j += 1
        raise SystemExit("[gzip_fsdata] %s: an unterminated template literal." % path)

    while i < n:
        c = js[i]
        if c in " \t\r\n":
            i += 1
            continue
        if c in "\"'":
            j = i + 1
            while j < n and js[j] != c:
                j += 2 if js[j] == "\\" else 1
            if j >= n:
                raise SystemExit("[gzip_fsdata] %s: an unterminated string." % path)
            i = j + 1
            prev = "a"  # a string ends an operand
            continue
        if c == "`":
            i, opened = skip_template(i + 1)
            if opened:
                templates.append(depth)
                depth += 1
                prev = "{"
            else:
                prev = "a"
            continue
        if js.startswith("//", i):
            j = js.find("\n", i)
            i = n if j < 0 else j
            continue
        if js.startswith("/*", i):
            j = js.find("*/", i + 2)
            if j < 0:
                raise SystemExit("[gzip_fsdata] %s: an unterminated comment." % path)
            i = j + 2
            continue
        if c == "/" and regex_allowed():
            j = i + 1
            in_class = False
            while j < n:
                d = js[j]
                if d == "\\":
                    j += 2
                    continue
                if d == "[":
                    in_class = True
                elif d == "]":
                    in_class = False
                elif d == "/" and not in_class:
                    break
                elif d == "\n":
                    raise SystemExit("[gzip_fsdata] %s: an unterminated regular expression." % path)
                j += 1
            j += 1
            while j < n and (js[j].isalnum() or js[j] == "_"):
                j += 1  # flags
            i = j
            prev = "a"
            continue
        if c.isalnum() or c in "_$":
            j = i
            while j < n and (js[j].isalnum() or js[j] in "_$"):
                j += 1
            prev = js[i:j]
            i = j
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if c == "}" and templates and templates[-1] == depth - 1:
                templates.pop()
                depth -= 1
                i, opened = skip_template(i + 1)
                if opened:
                    templates.append(depth)
                    depth += 1
                    prev = "{"
                else:
                    prev = "a"
                continue
            depth -= 1
            if depth < 0:
                raise SystemExit("[gzip_fsdata] %s: an unbalanced '%s'." % (path, c))
            if depth == 0 and first_close is None:
                first_close = i - start
        elif c == ";" and depth == 0:
            statements.append((js[start:i].strip(), first_close))
            start = i + 1
            first_close = None
            prev = ";"
            i += 1
            continue
        prev = c
        i += 1
    if depth != 0 or templates:
        raise SystemExit("[gzip_fsdata] %s: brackets do not balance." % path)
    tail = js[start:].strip()
    if tail:
        statements.append((tail, first_close))
    return statements


def _member_parts(path, js):
    """Split a minified member into (global declarations, IIFE statement), or
    fail the build when it is any other shape.

    Wrapped in a try block, a top-level `function`, `let`, `const` or `class`
    would become block-scoped and vanish from the page's global scope, so the
    only top-level code allowed inside the wrapper is one IIFE statement. A
    leading `const NAME = {...}` (data/configuration.js's BOARD_LABELS) is kept
    outside the wrapper, where it stays a global binding exactly as it was in
    its own file; an object or array literal cannot throw, so leaving it out of
    the isolation loses nothing."""
    statements = _top_level_statements(path, js)
    if not statements:
        raise SystemExit("[gzip_fsdata] %s is empty; a bundle member must be one IIFE." % path)
    declarations = []
    for text, first_close in statements[:-1]:
        literal = GLOBAL_CONST_RE.match(text)
        if literal is None or first_close != len(text) - 1:
            raise SystemExit(
                "[gzip_fsdata] %s has a top-level statement a bundle cannot isolate: %s... "
                "A member is one IIFE, optionally after `const NAME = {...}` declarations."
                % (path, text[:60])
            )
        declarations.append(text + ";")
    iife, first_close = statements[-1]
    if not (IIFE_START_RE.match(iife) and iife.endswith(")()") and first_close == len(iife) - 3):
        raise SystemExit(
            "[gzip_fsdata] %s does not end in one IIFE statement (%s...); a bundle member "
            "is one IIFE, optionally after `const NAME = {...}` declarations." % (path, iife[:60])
        )
    return "".join(declarations), iife + ";"


def _bundle_text(bundle, members, minified):
    """The bundle's script: each member in group order, its IIFE wrapped so a
    member that throws does not stop the ones after it. Separate files gave
    that for free; the error is rethrown on a fresh task, so it still reaches
    window.onerror and the console exactly as an uncaught error did."""
    parts = []
    if bundle == LABELS_BUNDLE:
        labels = ",".join('"%s":"%s"' % (name, label) for name, label, _m in SCRIPT_BUNDLES)
        parts.append(
            "try{window.PABootstrap&&window.PABootstrap.setResourceLabels&&"
            "window.PABootstrap.setResourceLabels({%s})}catch(e){setTimeout(()=>{throw e})}" % labels
        )
    for member in members:
        declarations, iife = _member_parts(member, minified[member])
        parts.append(declarations)
        parts.append("try{%s}catch(e){setTimeout(()=>{throw e})}" % iife)
    return "\n".join(part for part in parts if part) + "\n"


def _minify(path, text):
    """Return the file's text minified by esbuild. A missing or failing
    esbuild fails the build rather than quietly staging a larger image."""
    esbuild = shutil.which("esbuild")
    if esbuild is None:
        raise SystemExit(
            "[gzip_fsdata] cannot minify %s: esbuild is not on PATH. Install the "
            "esbuild package (pacman -S esbuild), or run `npm ci` and put "
            "node_modules/.bin on PATH." % path
        )
    loader = MINIFY_LOADERS[os.path.splitext(path)[1].lower()]
    result = subprocess.run(
        [esbuild, "--minify-whitespace", "--charset=utf8", "--log-level=warning",
         "--loader=%s" % loader],
        input=text,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if result.returncode != 0:
        raise SystemExit(
            "[gzip_fsdata] esbuild failed on %s (exit %d): %s"
            % (path, result.returncode, result.stderr.strip())
        )
    return result.stdout


def _zopfli(path, payload):
    """Return `payload` gzipped by zopfli. A missing zopfli, or output that
    does not inflate back to `payload`, fails the build rather than quietly
    staging a larger or broken image.

    zopfli writes a stock gzip stream that any inflater reads, so nothing on
    the droid changes; it only searches harder than zlib -9 for a shorter
    encoding of the same bytes, and its header carries no timestamp, so the
    same input stages the same bytes. The CLI reads only a named file (not a
    pipe), and it exits 0 even when it could not open its input, so its exit
    code alone is not evidence: the round trip below is.
    """
    zopfli = shutil.which("zopfli")
    if zopfli is None:
        raise SystemExit(
            "[gzip_fsdata] cannot compress %s: zopfli is not on PATH. Install the "
            "zopfli package (pacman -S zopfli, or apt-get install zopfli)." % path
        )
    with tempfile.TemporaryDirectory(prefix="gzip_fsdata-") as tmp:
        plain = os.path.join(tmp, "payload")
        with open(plain, "wb") as fh:
            fh.write(payload)
        result = subprocess.run([zopfli, "--gzip", "-c", plain], capture_output=True)
    if result.returncode != 0 or result.stderr.strip():
        raise SystemExit(
            "[gzip_fsdata] zopfli failed on %s (exit %d): %s"
            % (path, result.returncode, result.stderr.decode("utf-8", "replace").strip())
        )
    try:
        roundtrip = gzip.decompress(result.stdout)
    except (OSError, EOFError) as exc:
        raise SystemExit("[gzip_fsdata] zopfli wrote no valid gzip stream for %s: %s" % (path, exc))
    if roundtrip != payload:
        raise SystemExit(
            "[gzip_fsdata] zopfli's output for %s does not inflate back to its input." % path
        )
    return result.stdout


def _is_partial(filename):
    return filename.startswith(PARTIAL_PREFIX)


def _running_body_controller(env):
    """The Component Registry id of the Body Controller this env builds for, or
    a hard failure naming why it could not be found."""
    flags = env.GetProjectOption("build_flags", "")
    flags = " ".join(flags) if isinstance(flags, (list, tuple)) else str(flags or "")
    board = PA_BOARD_FLAG_RE.search(flags)
    if board is None:
        raise SystemExit(
            "[gzip_fsdata] a page includes the board's drawing (#%s), but this env's "
            "build_flags carry no -DPA_BOARD=, so there is no board to draw." % BOARD_FRAGMENT
        )
    registry = os.path.join(env.subst("$PROJECT_DIR"), "include", "component_registry.inc")
    with open(registry, "r", encoding="utf-8") as fh:
        rows = REGISTRY_BODY_CONTROLLER_RE.findall(fh.read())
    matches = [product for product, gate in rows if gate == board.group(1)]
    if len(matches) != 1:
        raise SystemExit(
            "[gzip_fsdata] %s has %d Body Controller rows gated on %s; the board's "
            "drawing needs exactly one." % (registry, len(matches), board.group(1))
        )
    return matches[0]


def _board_symbol(partial, product):
    """The sprite's own <svg> wrapper around the one <symbol> for `product`, or
    "" where the sprite carries none (the default set, or a product nobody drew)."""
    symbol = re.search(
        r'<symbol id="art-%s"[\s\S]*?</symbol>' % re.escape(product), partial
    )
    wrapper = re.search(r"<svg\b[^>]*>", partial)
    if symbol is None or wrapper is None:
        return ""
    return "%s%s</svg>" % (wrapper.group(0), symbol.group(0))


def _expand_includes(path, include_roots, board_product=None):
    """Return the file's text with any PA:INCLUDE directives replaced.

    include_roots is searched in order. Callers pass the active asset set
    before the common data root -- the same "set is staged on top of the
    common tree" rule the file walk in main() already applies to whole files
    -- so a page can name a partial without knowing which root this build
    will find it in, and _recovery_kernel.html, which no set has ever
    carried, still falls through to the common root exactly as before.

    Deliberately single-pass and non-recursive: a partial that itself contains a
    directive is rejected rather than quietly half-expanded, because a partially
    expanded recovery kernel is worse than an obvious build failure.

    board_product is a callable returning the running board's product id, asked
    only when a directive carries the #board fragment.
    """
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()

    # Match the directive, never the bare token -- documentation and comments
    # legitimately mention PA:INCLUDE without being one.
    carries_kernel = RECOVERY_KERNEL in INCLUDE_RE.findall(text)
    if SHELL_DELEGATE_MARKER in text:
        if carries_kernel:
            raise SystemExit(
                "[gzip_fsdata] %s is a shell delegate and includes '%s'. Its <head> "
                "never runs -- the Operator Shell imports only its <body> -- so the "
                "kernel would be imaged and never executed. Remove the directive."
                % (path, RECOVERY_KERNEL)
            )
    elif not carries_kernel:
        raise SystemExit(
            "[gzip_fsdata] %s does not include '%s'. Every rendered page inlines the "
            "Page Recovery View kernel; add '<!-- PA:INCLUDE %s -->' to its <head>."
            % (path, RECOVERY_KERNEL, RECOVERY_KERNEL)
        )

    def _replace(match):
        name, _, fragment = match.group(1).partition("#")
        target = None
        for root in include_roots:
            candidate = os.path.join(root, name)
            if os.path.isfile(candidate):
                target = candidate
                break
        if target is None:
            raise SystemExit(
                "[gzip_fsdata] %s includes '%s', which does not exist in %s. "
                "Refusing to build a page without it."
                % (path, name, " or ".join(include_roots))
            )
        with open(target, "r", encoding="utf-8") as pf:
            partial = pf.read()
        if INCLUDE_RE.search(partial):
            raise SystemExit(
                "[gzip_fsdata] nested PA:INCLUDE in '%s' is not supported." % target
            )
        if not fragment:
            return partial
        if fragment != BOARD_FRAGMENT or board_product is None:
            raise SystemExit(
                "[gzip_fsdata] %s includes '%s': '#%s' is not a fragment this build "
                "can select." % (path, match.group(1), fragment)
            )
        return _board_symbol(partial, board_product())

    expanded = INCLUDE_RE.sub(_replace, text)
    if INCLUDE_RE.search(expanded):
        raise SystemExit(
            "[gzip_fsdata] %s still contains an unexpanded PA:INCLUDE directive "
            "after substitution (check the directive syntax)." % path
        )
    return expanded


# The elements whose content the HTML parser reads as text, not markup: a
# `<!--` inside one is part of the script, the style or the title, never a
# comment. Their bodies are copied verbatim; the only look inside one is the
# script-data check below, which refuses a body rather than change it.
RAW_TEXT_ELEMENTS = {
    "script", "style", "textarea", "title", "xmp", "iframe", "noembed", "noframes", "noscript",
}
# Markup inside a <pre> keeps its whitespace as written, so the trim skips it.
PRE_ELEMENT = "pre"
START_TAG_RE = re.compile(
    r"""<([A-Za-z][A-Za-z0-9-]*)(?:\s+[^\s"'>/=]+(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s"'=<>`]+))?)*\s*(/?)>"""
)
END_TAG_RE = re.compile(r"</([A-Za-z][A-Za-z0-9-]*)\s*>")
DOCTYPE_RE = re.compile(r"<!DOCTYPE[^<>]*>", re.IGNORECASE)
# Script data that opens `<!--` and then names `<script` is "double escaped":
# the browser no longer ends the script at the next `</script>`, so where the
# block ends is not where a simple scan would put it.
SCRIPT_DOUBLE_ESCAPE_RE = re.compile(r"<!--[\s\S]*?<script[\s/>]", re.IGNORECASE)
WHITESPACE_ONLY_RE = re.compile(r"[ \t\n\r\f]+")


def _stage_markup(path, text):
    """Return an expanded page with its markup comments removed and the
    newline-bearing whitespace between tags cut to one newline.

    Runs after _expand_includes(), so every PA:INCLUDE directive is already
    replaced by its partial and none can be taken for a comment. `data/` keeps
    its comments: they document the pages and cost nothing once this runs.

    This is a scanner, not a whole-file regex, because a regex cannot tell a
    comment from a `<!--` inside a script string, a template literal, a title
    or a quoted attribute value. It is also not a full HTML tokenizer: it
    understands start tags with quoted attributes, end tags, the doctype,
    comments and the raw-text elements, and on anything else it fails the
    build rather than guess, the posture _minify() takes. A page it refuses is
    rewritten into the shape it reads; the scanner is not widened to accept it.

    The trim keeps one newline rather than none: a whitespace node between two
    inline elements renders as a gap, and dropping it would close up buttons
    and pills written on separate lines. Text with any other character in it is
    left as written, as is everything inside a <pre> or a raw-text element.
    """
    def fail(at, why):
        raise SystemExit(
            "[gzip_fsdata] %s: %s at line %d of the expanded page. The comment "
            "stripper does not guess at markup it does not read; rewrite it."
            % (path, why, text.count("\n", 0, at) + 1)
        )

    out = []
    pending = []  # text since the last tag, comments already dropped
    pre_depth = 0

    def flush():
        run = "".join(pending)
        del pending[:]
        if pre_depth == 0 and "\n" in run and WHITESPACE_ONLY_RE.fullmatch(run):
            run = "\n"
        out.append(run)

    i = 0
    n = len(text)
    while i < n:
        lt = text.find("<", i)
        if lt < 0:
            pending.append(text[i:])
            break
        pending.append(text[i:lt])
        if text.startswith("<!--", lt):
            # Searched from just past `<!`, so `<!-->` and `<!--->` end where
            # the browser ends them.
            end = text.find("-->", lt + 2)
            if end < 0:
                fail(lt, "an unterminated comment")
            if text.find("--!>", lt + 2, end + 3) >= 0:
                fail(lt, "a comment closed by '--!>'")
            i = end + 3
            continue
        if text.startswith("<!", lt):
            match = DOCTYPE_RE.match(text, lt)
            if match is None:
                fail(lt, "a '<!' that is neither a comment nor the doctype")
            flush()
            out.append(match.group(0))
            i = match.end()
            continue
        if text.startswith("</", lt):
            match = END_TAG_RE.match(text, lt)
            if match is None:
                fail(lt, "a '</' that is not an end tag")
            flush()
            out.append(match.group(0))
            if match.group(1).lower() == PRE_ELEMENT:
                pre_depth = max(0, pre_depth - 1)
            i = match.end()
            continue
        if text.startswith("<?", lt):
            fail(lt, "a '<?' processing instruction")
        if lt + 1 < n and text[lt + 1].isalpha():
            match = START_TAG_RE.match(text, lt)
            if match is None:
                fail(lt, "a '<' and a letter that is not a start tag")
            flush()
            out.append(match.group(0))
            i = match.end()
            name = match.group(1).lower()
            if name == PRE_ELEMENT:
                pre_depth += 1
            if name in RAW_TEXT_ELEMENTS:
                if match.group(2):
                    # HTML ignores the slash and reads on to a close tag; inside
                    # an <svg> it really is empty. Which one depends on where it
                    # sits, so it is refused rather than read either way.
                    fail(lt, "a self-closed <%s/>" % name)
                close = re.compile(r"</%s(?=[\s/>])[^>]*>" % name, re.IGNORECASE).search(text, i)
                if close is None:
                    fail(lt, "a <%s> with no closing tag" % name)
                body = text[i:close.start()]
                if name == "script" and SCRIPT_DOUBLE_ESCAPE_RE.search(body):
                    fail(lt, "a '<!--' followed by '<script' inside script data")
                out.append(body)
                out.append(close.group(0))
                i = close.end()
            continue
        # A '<' the parser reads as text: `a < b`, `<=`, a lone '<'.
        pending.append("<")
        i = lt + 1
    flush()
    return "".join(out)


def main():
    # Native (host test) builds have no LittleFS; nothing to do.
    if env.subst("$PIOPLATFORM") == "native":
        return

    src = env.subst("$PROJECT_DATA_DIR")
    if not src or not os.path.isdir(src):
        return

    stage = os.path.join(env.subst("$BUILD_DIR"), "fsdata_gz")

    set_name = env.GetProjectOption("custom_asset_set", DEFAULT_ASSET_SET)
    sets_root = os.path.join(src, ASSET_SETS_DIR)
    set_root = os.path.join(sets_root, set_name)
    if os.path.isdir(sets_root) and not os.path.isdir(set_root):
        # A typo here would ship an image quietly missing every picture, on a
        # surface whose pictures are the thing an operator selects by.
        available = sorted(
            d for d in os.listdir(sets_root) if os.path.isdir(os.path.join(sets_root, d))
        )
        raise SystemExit(
            "[gzip_fsdata] custom_asset_set is '%s', but %s does not exist. "
            "Available sets: %s" % (set_name, set_root, ", ".join(available) or "none")
        )

    # The common tree first, then this environment's set flattened on top of it.
    # A set file at <set>/a/b.webp lands at a/b.webp, so a page names one path
    # whichever set it was built with.
    roots = [(src, False)]
    if os.path.isdir(set_root):
        roots.append((set_root, True))

    # Derived from the walk above rather than rebuilt, so include order and
    # staging order cannot drift apart.
    include_roots = [root for root, in_set in roots if in_set] + [src]

    gz_count = 0
    minified_count = 0
    raw_count = 0
    partial_count = 0
    set_count = 0
    src_bytes = 0
    # Every staged file, keyed by its path in the stage, as either the bytes to
    # write or the source to copy. Built in full before the old stage is
    # removed, so a page or a tool that refuses fails the build with the last
    # good stage still on disk; a set file replaces a common file of the same
    # path, which is what "the set is staged on top" means.
    staged = {}
    stage_dirs = [""]
    bundle_members = {}
    for walk_src, in_set in roots:
        for root, dirs, files in os.walk(walk_src):
            dirs.sort()
            files.sort()
            rel = os.path.relpath(root, walk_src)
            # The set directories are staged by their own pass, never as part of
            # the common tree -- otherwise every build would carry every set.
            if not in_set and (rel == ASSET_SETS_DIR or rel.startswith(ASSET_SETS_DIR + os.sep)):
                continue
            rel_dir = "" if rel == "." else rel
            if rel_dir not in stage_dirs:
                stage_dirs.append(rel_dir)
            for name in files:
                sp = os.path.join(root, name)
                # Partials are inlined into the pages that include them; imaging
                # them too would ship a duplicate nobody requests.
                if _is_partial(name):
                    partial_count += 1
                    continue
                src_bytes += os.path.getsize(sp)
                if in_set:
                    set_count += 1
                if _should_gzip(name):
                    ext = os.path.splitext(name)[1].lower()
                    if ext in HTML_EXTS:
                        payload = _bundle_chain(sp, _stage_markup(sp, _expand_includes(
                            sp, include_roots, board_product=lambda: _running_body_controller(env)
                        ))).encode("utf-8")
                    elif ext in MINIFY_LOADERS:
                        with open(sp, "r", encoding="utf-8") as fi:
                            minified = _minify(sp, fi.read())
                        minified_count += 1
                        member = "/" + "/".join(filter(None, [rel_dir.replace(os.sep, "/"), name]))
                        if member in BUNDLE_OF_MEMBER:
                            # Staged inside its bundle below, never on its own.
                            bundle_members[member] = minified
                            continue
                        payload = minified.encode("utf-8")
                    else:
                        with open(sp, "rb") as fi:
                            payload = fi.read()
                    data = _zopfli(sp, payload)
                    staged[os.path.join(rel_dir, name + ".gz")] = (len(data), data, None)
                    gz_count += 1
                else:
                    staged[os.path.join(rel_dir, name)] = (os.path.getsize(sp), None, sp)
                    raw_count += 1

    for bundle, _label, members in SCRIPT_BUNDLES:
        missing = [member for member in members if member not in bundle_members]
        if len(missing) == len(members):
            continue  # a data root with none of this group (the tools tests' fixtures)
        if missing:
            raise SystemExit(
                "[gzip_fsdata] %s cannot be built: %s not in %s." % (bundle, ", ".join(missing), src)
            )
        rel = bundle.lstrip("/") + ".gz"
        if rel in staged or os.path.exists(os.path.join(src, bundle.lstrip("/"))):
            raise SystemExit("[gzip_fsdata] %s is a bundle name and a file in %s." % (bundle, src))
        data = _zopfli(bundle, _bundle_text(bundle, members, bundle_members).encode("utf-8"))
        staged[rel] = (len(data), data, None)
        gz_count += 1

    if os.path.isdir(stage):
        shutil.rmtree(stage)
    for rel_dir in sorted(stage_dirs):
        os.makedirs(os.path.join(stage, rel_dir), exist_ok=True)
    # Written largest file first, ties by path: the order
    # tools/littlefs_image.py writes the image in, on every host. The order
    # files go into the image moves the block count (measured, not explained:
    # 118 largest first against 118-121 over 200 random orders, #461), and the
    # platform's own builder takes the host filesystem's listing order, so the
    # image's order is owned there, not here. Writing the stage in the same
    # order keeps a creation-order host's listing (btrfs) identical to the
    # image, and makes the stage itself a function of the commit (#429). This
    # order is tools/fs_price.py --order size: directories by path, then the
    # largest file first. Pricing it, or a modified copy of the stage, without
    # buildfs is that script. It is not the number that can fail a build;
    # make check-build-budgets is.
    out_bytes = 0
    for path in sorted(staged, key=lambda p: (-staged[p][0], p)):
        size, data, source = staged[path]
        dp = os.path.join(stage, path)
        if data is None:
            shutil.copy2(source, dp)
        else:
            with open(dp, "wb") as fo:
                fo.write(data)
        out_bytes += size

    env.Replace(PROJECT_DATA_DIR=stage)
    print(
        "[gzip_fsdata] staged %d gzipped (%d minified) + %d raw files (%d partials inlined, "
        "not imaged; %d from asset set '%s'): %d KB -> %d KB (image data dir: %s)"
        % (gz_count, minified_count, raw_count, partial_count, set_count, set_name,
           src_bytes // 1024, out_bytes // 1024, stage)
    )


main()

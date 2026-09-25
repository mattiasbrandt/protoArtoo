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

# Minified before gzipping, by esbuild; everything else is staged as written.
# esbuild parses the source, so it removes whitespace and comments without
# touching a string. rjsmin, the regex minifier tried first, rewrote the
# whitespace inside nested template literals in six files -- class="parts-row${`
# ${x}`}" lost its space and joined two class names -- and every file still
# passed `node --check`, so a syntax check is not evidence a minifier is safe.
# Identifiers are never renamed: these are classic scripts sharing globals.
MINIFY_LOADERS = {".js": "js", ".css": "css"}


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
    """Return the file's bytes with any PA:INCLUDE directives replaced.

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
    return expanded.encode("utf-8")


def main():
    # Native (host test) builds have no LittleFS; nothing to do.
    if env.subst("$PIOPLATFORM") == "native":
        return

    src = env.subst("$PROJECT_DATA_DIR")
    if not src or not os.path.isdir(src):
        return

    stage = os.path.join(env.subst("$BUILD_DIR"), "fsdata_gz")
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(stage, exist_ok=True)

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
    out_bytes = 0
    # Staged in sorted order, because the order files are created in the stage
    # is the order they are written into the image, and that moves the block
    # count. The builder (littlefs-python, in the platform's build_fs_image)
    # walks the stage with Path.rglob, which lists a directory in readdir
    # order, and btrfs - like a small ext4 directory - returns entries in
    # creation order. Unsorted, os.walk hands back data/ in ITS readdir order,
    # which is whatever order git happened to create those files in in this
    # worktree: the same commit imaged as 112 blocks in one worktree and 114 in
    # another. Measured on one stage written in 300 random orders: 112 blocks
    # 297 times, 113 twice, 114 once (#429). Sorted, the count is a function of
    # the commit alone.
    for walk_src, in_set in roots:
        for root, dirs, files in os.walk(walk_src):
            dirs.sort()
            files.sort()
            rel = os.path.relpath(root, walk_src)
            # The set directories are staged by their own pass, never as part of
            # the common tree -- otherwise every build would carry every set.
            if not in_set and (rel == ASSET_SETS_DIR or rel.startswith(ASSET_SETS_DIR + os.sep)):
                continue
            dst_root = stage if rel == "." else os.path.join(stage, rel)
            os.makedirs(dst_root, exist_ok=True)
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
                    dp = os.path.join(dst_root, name + ".gz")
                    ext = os.path.splitext(name)[1].lower()
                    if ext in HTML_EXTS:
                        payload = _expand_includes(
                            sp, include_roots, board_product=lambda: _running_body_controller(env)
                        )
                        with gzip.open(dp, "wb", compresslevel=9) as fo:
                            fo.write(payload)
                    elif ext in MINIFY_LOADERS:
                        with open(sp, "r", encoding="utf-8") as fi:
                            payload = _minify(sp, fi.read()).encode("utf-8")
                        with gzip.open(dp, "wb", compresslevel=9) as fo:
                            fo.write(payload)
                        minified_count += 1
                    else:
                        with open(sp, "rb") as fi, gzip.open(dp, "wb", compresslevel=9) as fo:
                            shutil.copyfileobj(fi, fo)
                    gz_count += 1
                else:
                    dp = os.path.join(dst_root, name)
                    shutil.copy2(sp, dp)
                    raw_count += 1
                out_bytes += os.path.getsize(dp)

    env.Replace(PROJECT_DATA_DIR=stage)
    print(
        "[gzip_fsdata] staged %d gzipped (%d minified) + %d raw files (%d partials inlined, "
        "not imaged; %d from asset set '%s'): %d KB -> %d KB (image data dir: %s)"
        % (gz_count, minified_count, raw_count, partial_count, set_count, set_name,
           src_bytes // 1024, out_bytes // 1024, stage)
    )


main()

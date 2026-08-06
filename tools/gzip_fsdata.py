#!/usr/bin/env python3
"""
PlatformIO pre-build script: build the LittleFS image from GZIPPED web assets.

The web UI is ~656 KB of mostly uncompressed text (JS/CSS/HTML) — 84% of the
filesystem. ESPAsyncWebServer's serveStatic() transparently serves `foo.js.gz`
(with Content-Encoding: gzip + Content-Type from the original extension) when the
raw file is absent, so shipping only the gzipped text shrinks the image to
~180 KB and speeds page loads — and frees flash for a coredump partition (#8).

Mechanism: gzip the text assets from $PROJECT_DATA_DIR into a per-build staging
dir, copy binaries (images, etc.) as-is, and repoint $PROJECT_DATA_DIR at the
staging dir so `buildfs`/`uploadfs` image the gzipped copy. The repo `data/`
stays raw (source of truth); git is untouched.

Excluded from gzip:
  - *version*.json — the firmware opens /fs-version.json DIRECTLY from LittleFS
    (not via serveStatic), so a .gz would break the reported fsVersion.
  - images and other binaries — already compressed; copied verbatim.

HTML includes: a page may carry `<!-- PA:INCLUDE _partial.html -->`, which is
replaced with the contents of that file before gzipping. Partials are named with
a leading underscore and are NOT themselves imaged. This exists for the Page
Recovery View kernel, which must be inline on every page — it is the one part of
the UI that has to survive a failure that sheds external assets, so it cannot be
an external file — while still living in exactly one editable source rather than
ten hand-maintained copies that would drift apart. A missing or unexpanded
include is a hard build failure, never a silently shipped page without recovery
— and so is a served page that carries no kernel directive at all, since a page
without one fails silently in exactly the situation recovery exists for.

Runs after extract_version.py so the freshly-written fs-version.json is included
in the LittleFS staging directory.
"""

import gzip
import os
import re
import shutil

Import("env")  # noqa: F821  (PlatformIO injects this)

# Text types worth gzipping. JSON is intentionally excluded (tiny, and version
# json is read raw by the firmware).
GZIP_EXTS = {".js", ".css", ".html", ".htm", ".svg", ".txt", ".map"}


def _should_gzip(filename):
    if os.path.splitext(filename)[1].lower() not in GZIP_EXTS:
        return False
    return True


# Partials are sources for inlining, not servable assets.
PARTIAL_PREFIX = "_"
INCLUDE_RE = re.compile(r"[ \t]*<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+)\s*-->[ \t]*\n?")
HTML_EXTS = {".html", ".htm"}

# The one partial every served page is required to inline. It is checked by
# name rather than by "has some directive" so a page cannot satisfy the guard
# by including something else.
RECOVERY_KERNEL = "_recovery_kernel.html"


def _is_partial(filename):
    return filename.startswith(PARTIAL_PREFIX)


def _expand_includes(path, src_root):
    """Return the file's bytes with any PA:INCLUDE directives replaced.

    Deliberately single-pass and non-recursive: a partial that itself contains a
    directive is rejected rather than quietly half-expanded, because a partially
    expanded recovery kernel is worse than an obvious build failure.
    """
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()

    # Match the directive, never the bare token -- documentation and comments
    # legitimately mention PA:INCLUDE without being one.
    if RECOVERY_KERNEL not in INCLUDE_RE.findall(text):
        raise SystemExit(
            "[gzip_fsdata] %s does not include '%s'. Every served page inlines the "
            "Page Recovery View kernel; add '<!-- PA:INCLUDE %s -->' to its <head>."
            % (path, RECOVERY_KERNEL, RECOVERY_KERNEL)
        )

    def _replace(match):
        target = os.path.join(src_root, match.group(1))
        if not os.path.isfile(target):
            raise SystemExit(
                "[gzip_fsdata] %s includes '%s', which does not exist. "
                "Refusing to build a page without it." % (path, match.group(1))
            )
        with open(target, "r", encoding="utf-8") as pf:
            partial = pf.read()
        if INCLUDE_RE.search(partial):
            raise SystemExit(
                "[gzip_fsdata] nested PA:INCLUDE in '%s' is not supported." % target
            )
        return partial

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

    gz_count = 0
    raw_count = 0
    partial_count = 0
    src_bytes = 0
    out_bytes = 0
    for root, _dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
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
            if _should_gzip(name):
                dp = os.path.join(dst_root, name + ".gz")
                if os.path.splitext(name)[1].lower() in HTML_EXTS:
                    payload = _expand_includes(sp, src)
                    with gzip.open(dp, "wb", compresslevel=9) as fo:
                        fo.write(payload)
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
        "[gzip_fsdata] staged %d gzipped + %d raw files (%d partials inlined, not imaged): "
        "%d KB -> %d KB (image data dir: %s)"
        % (gz_count, raw_count, partial_count, src_bytes // 1024, out_bytes // 1024, stage)
    )


main()

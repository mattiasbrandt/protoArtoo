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

Runs after extract_version.py so the freshly-written fs-version.json is included
in the LittleFS staging directory.
"""

import gzip
import os
import shutil

Import("env")  # noqa: F821  (PlatformIO injects this)

# Text types worth gzipping. JSON is intentionally excluded (tiny, and version
# json is read raw by the firmware).
GZIP_EXTS = {".js", ".css", ".html", ".htm", ".svg", ".txt", ".map"}


def _should_gzip(filename):
    if os.path.splitext(filename)[1].lower() not in GZIP_EXTS:
        return False
    return True


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
    src_bytes = 0
    out_bytes = 0
    for root, _dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        dst_root = stage if rel == "." else os.path.join(stage, rel)
        os.makedirs(dst_root, exist_ok=True)
        for name in files:
            sp = os.path.join(root, name)
            src_bytes += os.path.getsize(sp)
            if _should_gzip(name):
                dp = os.path.join(dst_root, name + ".gz")
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
        "[gzip_fsdata] staged %d gzipped + %d raw files: %d KB -> %d KB (image data dir: %s)"
        % (gz_count, raw_count, src_bytes // 1024, out_bytes // 1024, stage)
    )


main()

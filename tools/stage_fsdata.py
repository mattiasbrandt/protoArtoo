#!/usr/bin/env python3
"""Stage one web asset set without a firmware build.

Runs tools/gzip_fsdata.py — the same script a firmware build runs — against a
stand-in env, and leaves the servable tree in --out. `name.ext.gz` is what the
controller serves as `name.ext`.

    python3 tools/stage_fsdata.py --set legacy --out /tmp/fs-legacy
    python3 tools/stage_fsdata.py --set default --out /tmp/fs-default --serve 4187

--set legacy is the artoo-esp32 image (PA_BOARD_ARTOO_ESP32). --set default is
the firebeetle2 image (PA_BOARD_FIREBEETLE2). Restage after every data/ edit;
the tree is a copy. --serve sends each .gz with Content-Encoding: gzip and
Cache-Control: no-store, and answers /api/ with 404 so a Playwright fixture
route can own the droid. It does not expand PA:INCLUDE; the stage already did.
"""

from __future__ import annotations

import argparse
import mimetypes
import os
import shutil
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]
GZIP = ROOT / "tools" / "gzip_fsdata.py"

# custom_asset_set and the board flag, as platformio.ini declares them.
SETS = {
    "legacy": "PA_BOARD_ARTOO_ESP32",
    "default": "PA_BOARD_FIREBEETLE2",
}

mimetypes.add_type("application/javascript", ".js")
mimetypes.add_type("image/webp", ".webp")


class _Env:
    """The calls gzip_fsdata.py makes on SCons' env. Nothing else."""

    def __init__(self, data_dir: Path, build_dir: Path, asset_set: str, board: str):
        self._vars = {
            "$PIOPLATFORM": "espressif32",
            "$PROJECT_DATA_DIR": str(data_dir),
            "$BUILD_DIR": str(build_dir),
            "$PROJECT_DIR": str(ROOT),
        }
        self._set = asset_set
        self._flags = f"-DPA_BOARD={board}"

    def subst(self, key: str) -> str:
        return self._vars[key]

    def GetProjectOption(self, name: str, default=None):
        if name == "custom_asset_set":
            return self._set
        if name == "build_flags":
            return self._flags
        return default

    def Replace(self, **kwargs) -> None:
        return None


def stage(asset_set: str, out: Path) -> None:
    if asset_set not in SETS:
        raise SystemExit(f"--set must be one of: {', '.join(SETS)}")
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"{out} is not empty. Pick an empty directory.")
    out.mkdir(parents=True, exist_ok=True)
    build = out / ".build"
    build.mkdir()
    env = _Env(ROOT / "data", build, asset_set, SETS[asset_set])
    source = GZIP.read_text(encoding="utf-8")
    namespace = {"__name__": "gzip_fsdata_staged", "__file__": str(GZIP)}

    def fake_import(name: str) -> None:
        if name != "env":
            raise AssertionError(f"gzip_fsdata.py Imports {name!r}, not only 'env'")
        namespace["env"] = env

    namespace["Import"] = fake_import
    exec(compile(source, str(GZIP), "exec"), namespace)
    staged = build / "fsdata_gz"
    if not staged.is_dir():
        raise SystemExit(f"gzip_fsdata.py produced no {staged}")
    for entry in staged.iterdir():
        shutil.move(str(entry), out / entry.name)
    shutil.rmtree(build)
    print(f"[stage_fsdata] {asset_set} -> {out}")


class _Handler(BaseHTTPRequestHandler):
    server: "_Server"

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("[stage_fsdata] " + (fmt % args) + "\n")

    def do_GET(self) -> None:  # noqa: N802
        path = unquote(urlparse(self.path).path)
        if path == "/api" or path.startswith("/api/"):
            self.send_error(404, "fixture routes own /api/")
            return
        rel = "index.html" if path in ("", "/") else path.lstrip("/")
        root = self.server.out.resolve()
        try:
            target = (root / rel).resolve()
            target.relative_to(root)
        except ValueError:
            self.send_error(404)
            return
        gz = Path(str(target) + ".gz")
        if gz.is_file():
            body = gz.read_bytes()
            encoding = "gzip"
            mime_name = target.name
        elif target.is_file():
            body = target.read_bytes()
            encoding = None
            mime_name = target.name
        else:
            self.send_error(404)
            return
        ctype = mimetypes.guess_type(mime_name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        if encoding:
            self.send_header("Content-Encoding", encoding)
        self.end_headers()
        self.wfile.write(body)


class _Server(ThreadingHTTPServer):
    def __init__(self, addr: tuple[str, int], out: Path):
        self.out = out
        super().__init__(addr, _Handler)


def serve(out: Path, port: int) -> None:
    httpd = _Server(("127.0.0.1", port), out)
    print(f"[stage_fsdata] http://127.0.0.1:{port}/  (Ctrl-C to stop)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        httpd.server_close()


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--set", required=True, choices=sorted(SETS))
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--serve", type=int, metavar="PORT", help="serve --out and block")
    args = p.parse_args(argv)
    stage(args.set, args.out)
    if args.serve is not None:
        serve(args.out, args.serve)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

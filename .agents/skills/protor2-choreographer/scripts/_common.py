"""Shared plumbing for the choreographer scripts: the repo, the Node bridge,
and the decode every analysis starts from."""
from __future__ import annotations

import json
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
# scripts -> protor2-choreographer -> skills -> .agents -> repo
ROOT = HERE.parents[3]
DATA = ROOT / "data"
BRIDGE = HERE / "seq_node.js"
# The rate a browser's AudioContext decodes at on the machines measured here,
# so the editor and this script read the same samples.
SAMPLE_RATE = 48000


def node(command: str, *args: str, payload=None):
    """Run one seq_node.js command; a failure raises with Node's own words."""
    r = subprocess.run(
        ["node", str(BRIDGE), str(DATA), command, *args],
        input=None if payload is None else json.dumps(payload),
        capture_output=True, text=True,
    )
    if r.returncode != 0:
        raise RuntimeError(f"seq_node.js {command} failed: {r.stderr.strip() or r.stdout.strip()}")
    return json.loads(r.stdout)


def decode(path: Path, out: Path) -> int:
    """Decode to mono float32 PCM at SAMPLE_RATE; returns the sample count."""
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-i", str(path), "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "f32le", str(out)],
        check=True,
    )
    return out.stat().st_size // 4


def fingerprint(path: Path) -> str:
    """FNV-1a over the file's bytes, 8 hex digits: the editor's own fingerprint
    (data/seq_tempo.js), so a tempo drafted here pairs with the same file
    dropped into the editor."""
    h = 0x811C9DC5
    for byte in path.read_bytes():
        h ^= byte
        h = (h * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


def load_json(path: Path):
    return json.loads(Path(path).read_text())

#!/usr/bin/env python3
"""Encode Component Picker photographs into the default asset set (#316).

Takes the operator-supplied originals in tasks/product-images/ (gitignored),
cuts a near-white studio background, and writes 400x300 WebP files under
data/asset-sets/default/part_<id>.webp.

The UI is dark-only (data/style.css --bg: #0c1525). White product-shot
backgrounds glare as bright rectangles on a picker card, so every photograph
lands on that one ground.

The per-image cap is 8 KiB: two 4 KiB LittleFS blocks rather than three
(#316, ADR 0065). Quality starts at 80 and walks down only as far as the cap
requires.

Not a build step. Re-run by hand when an original is replaced:

    python3 tools/encode_part_photos.py
"""

from __future__ import annotations

import argparse
import sys
from collections import deque
from io import BytesIO
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "tasks" / "product-images"
DST = ROOT / "data" / "asset-sets" / "default"

# data/style.css:24  --bg: #0c1525
BG = (12, 21, 37)
WIDTH, HEIGHT = 400, 300
MARGIN = 16
CAP_BYTES = 8192
# A studio ground is light, not always 255: the AstroPixels table is 246.
# Flood relative to the corner colour so an off-white table is removed
# without eating a white Xbox controller whose body sits well below 255.
GROUND_MIN = 220
GROUND_TOL = 14

# source filename -> Component Registry id (include/component_registry.inc).
# esp32_gpio_ledc is the MCU's own PWM and has no product photograph.
# elrs-radio.png is a handset, not the ELRS receiver the registry row names.
SOURCES = {
    "artoo-pcb-esp32.jpg": "artoo_pcb",
    "firebeetle2.jpg": "firebeetle2",
    "ds650.jpg": "hotrc_ds650",
    "rc-receiver-pwm.jpg": "rc_transmitter_pwm",
    "rc-receiver-sbus.jpg": "rc_transmitter_sbus",
    "rc-receiver-elrs.jpg": "rc_transmitter_elrs",
    "xbox-360.jpg": "xbox_controller",
    "pca9685.jpg": "pca9685",
    "pololu-maestro.jpg": "pololu_maestro",
    "isdt-esc70.jpg": "isdt_esc70",
    "syren-10.jpg": "syren10",
    "astropixels.jpg": "astropixels_plus",
    "teeces.jpg": "teeces",
    "hoverboard.jpg": "hoverboard",
    "sabertooth.jpg": "sabertooth_2x25",
    "flipsky-mini-v6.jpg": "flipsky_mini_v6_vesc",
    "dy-sv5w.jpg": "dy_sv5w",
    "mp3-trigger.jpg": "mp3_trigger",
    "chirp-audio-trigger.jpg": "chirp",
    "dfplayer-mini.jpg": "dfplayer_mini",
}


def _corner_ground(im: Image.Image) -> tuple[int, int, int] | None:
    rgb = im.convert("RGB")
    w, h = rgb.size
    samples = (
        rgb.getpixel((0, 0)),
        rgb.getpixel((w - 1, 0)),
        rgb.getpixel((0, h - 1)),
        rgb.getpixel((w - 1, h - 1)),
    )
    if not all(min(p) >= GROUND_MIN for p in samples):
        return None
    return samples[0]


def cutout(im: Image.Image) -> Image.Image:
    """Flood-fill a near-white studio ground from the edges, keep the product."""
    im = im.convert("RGBA")
    ground = _corner_ground(im)
    if ground is None:
        return im
    w, h = im.size
    px = im.load()
    bgmask = bytearray(w * h)
    q: deque[tuple[int, int]] = deque()

    def is_bg(x: int, y: int) -> bool:
        r, g, b, a = px[x, y]
        if a == 0:
            return True
        return (
            abs(r - ground[0]) <= GROUND_TOL
            and abs(g - ground[1]) <= GROUND_TOL
            and abs(b - ground[2]) <= GROUND_TOL
        )

    def push(x: int, y: int) -> None:
        i = y * w + x
        if bgmask[i]:
            return
        if is_bg(x, y):
            bgmask[i] = 1
            q.append((x, y))

    for x in range(w):
        push(x, 0)
        push(x, h - 1)
    for y in range(h):
        push(0, y)
        push(w - 1, y)
    while q:
        x, y = q.popleft()
        if x:
            push(x - 1, y)
        if x + 1 < w:
            push(x + 1, y)
        if y:
            push(x, y - 1)
        if y + 1 < h:
            push(x, y + 1)

    out = Image.new("RGBA", (w, h))
    opx = out.load()
    for y in range(h):
        row = y * w
        for x in range(w):
            if bgmask[row + x]:
                opx[x, y] = (0, 0, 0, 0)
                continue
            r, g, b, a = px[x, y]
            fringe = (
                (x and bgmask[row + x - 1])
                or (x + 1 < w and bgmask[row + x + 1])
                or (y and bgmask[row - w + x])
                or (y + 1 < h and bgmask[row + w + x])
            )
            if fringe:
                m = min(r, g, b)
                if m >= 230:
                    a = int(a * (255 - m) / (255 - 230))
            opx[x, y] = (r, g, b, a)
    return out


def fit(im: Image.Image) -> Image.Image:
    canvas = Image.new("RGBA", (WIDTH, HEIGHT), BG + (255,))
    box = (WIDTH - 2 * MARGIN, HEIGHT - 2 * MARGIN)
    fitted = im.copy()
    fitted.thumbnail(box, Image.Resampling.LANCZOS)
    x = (WIDTH - fitted.width) // 2
    y = (HEIGHT - fitted.height) // 2
    canvas.alpha_composite(fitted, (x, y))
    return canvas.convert("RGB")


def encode(im: Image.Image, quality: int) -> bytes:
    buf = BytesIO()
    im.save(buf, format="WEBP", quality=quality, method=6)
    return buf.getvalue()


def fit_cap(im: Image.Image) -> tuple[int, bytes]:
    data = encode(im, 80)
    if len(data) <= CAP_BYTES:
        best_q, best = 80, data
        for trial in range(90, 80, -2):
            d = encode(im, trial)
            if len(d) <= CAP_BYTES:
                return trial, d
        return best_q, best
    lo, hi = 15, 79
    best_q, best = 15, encode(im, 15)
    while lo <= hi:
        mid = (lo + hi) // 2
        d = encode(im, mid)
        if len(d) <= CAP_BYTES:
            best_q, best = mid, d
            lo = mid + 1
        else:
            hi = mid - 1
    return best_q, best


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--src",
        type=Path,
        default=SRC,
        help="directory of operator-supplied originals",
    )
    parser.add_argument(
        "--dst",
        type=Path,
        default=DST,
        help="default asset-set directory",
    )
    args = parser.parse_args()
    if not args.src.is_dir():
        print(f"missing originals: {args.src}", file=sys.stderr)
        return 1
    args.dst.mkdir(parents=True, exist_ok=True)

    total = 0
    over = 0
    for source_name, part_id in SOURCES.items():
        src_path = args.src / source_name
        if not src_path.is_file():
            print(f"MISSING {source_name} -> part_{part_id}.webp", file=sys.stderr)
            return 1
        fitted = fit(cutout(Image.open(src_path)))
        quality, data = fit_cap(fitted)
        dest = args.dst / f"part_{part_id}.webp"
        dest.write_bytes(data)
        total += len(data)
        flag = "OK" if len(data) <= CAP_BYTES else "OVER"
        if flag == "OVER":
            over += 1
        print(f"{flag:4} {len(data):5} B  q={quality:2}  {dest.name}  <- {source_name}")

    print(f"total {total} B  ({len(SOURCES)} files, {over} over cap)")
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())

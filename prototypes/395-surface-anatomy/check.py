#!/usr/bin/env python3
"""#398 - static checks for the mockups. No dependencies.

    python3 prototypes/395-surface-anatomy/check.py

Fails (exit 1) on:
  - a colour literal (#rgb, #rrggbb, rgb(), hsl()) in anatomy.css outside the
    :root block - #327's rule, the one that makes "tokens once" true;
  - an emoji or other pictographic character in any .html, .js or .css file
    here (ADR 0066: no emoji on surfaces);
  - a mockup that references an icon symbol chrome.js does not define.
Prints what it measured either way.
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
PAGES = ["dashboard.html", "parts.html", "setup.html"]
COLOUR = re.compile(r"#[0-9a-fA-F]{3,8}\b|\brgba?\(|\bhsla?\(")
PICTO = re.compile(r"[\U0001F000-\U0001FAFF☀-➿⬀-⯿️]")

failures = []

css = (HERE / "anatomy.css").read_text()
# Strip comments, then split off the :root blocks (screen and print).
body = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
roots = re.findall(r":root\s*\{[^}]*\}", body)
outside = body
for block in roots:
    outside = outside.replace(block, "", 1)
literals = [m.group(0) for m in COLOUR.finditer(outside)]
print(f"anatomy.css: {len(roots)} :root blocks, {len(literals)} colour literals outside them")
if literals:
    failures.append(f"colour literals outside :root: {literals}")

icons = set(re.findall(r'"([a-z0-9-]+)":\s*"M', (HERE / "chrome.js").read_text()))
print(f"chrome.js: {len(icons)} icon symbols")

for name in sorted(p.name for p in HERE.iterdir() if p.suffix in {".html", ".js", ".css"}):
    text = (HERE / name).read_text()
    picto = PICTO.findall(text)
    if picto:
        failures.append(f"{name}: pictographic characters {sorted(set(picto))}")
    used = set(re.findall(r'href="#i-([a-z0-9-]+)"', text))
    missing = used - icons
    if missing:
        failures.append(f"{name}: icons not in chrome.js: {sorted(missing)}")
    print(f"{name}: {len(picto)} pictographs, {len(used)} icon uses, {len(missing)} missing")

if failures:
    print("FAIL")
    for f in failures:
        print(" -", f)
    sys.exit(1)
print("PASS")

# Sizing tools — where did the bytes go?

Every `pio run` leaves a GNU ld map at `.pio/build/<env>/firmware.map`, and these
three scripts turn it into an answer. Start with `mapsize.py`, which walks the
map's "Linker script and memory map" section and credits every input section to
the archive (`libnet80211.a`, `libmbedcrypto.a`, ...) or loose object that owns
it, split by where the bytes land — `flash_text`, `flash_ro`, `iram`,
`dram_data` (in the image *and* in RAM), `dram_bss` (RAM only) and `rtc` — so
the first column, `image`, is the part that counts against
`flash_budget_bytes`. Run it with `object` instead of `archive` to descend into
a single library, or with `outsec` / `check` to see the output-section totals
and confirm the attribution adds up. String literals need `strpool.py`, because
ld merges every mergeable string section into one pool and lists the whole pool
under the **first** contributor it happens to meet — read a per-object string
figure straight off the map and you will blame the wrong file; `strpool.py`
re-derives it from the pre-merge `objdump -h` sizes instead. When you are
comparing two builds — before and after a flag, a toggle or a refactor — keep
both maps and run `armdiff.py`, which diffs the two attributions per archive so
a saving lands on the library that actually gave it up rather than on a net
total. Two cautions: sizes here are linker attribution, not the enforced
budget (that is the `firmware.bin` file size, a few hundred bytes apart), and
strings belonging to objects the linker garbage-collected never reach the pool
at all, so an archive can look free of strings it plainly contains in source.

```bash
BUILD=.pio/build/artoo_esp32
OBJDUMP=~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-objdump

# Biggest owners of image bytes, by archive (default mode; 40 rows).
python3 tools/sizing/mapsize.py $BUILD/firmware.map

# Descend into one library, or into the project's own objects.
python3 tools/sizing/mapsize.py $BUILD/firmware.map object 40 libmbedcrypto
python3 tools/sizing/mapsize.py $BUILD/firmware.map object 40 src/

# Output-section totals, and an attribution self-check.
python3 tools/sizing/mapsize.py $BUILD/firmware.map outsec
python3 tools/sizing/mapsize.py $BUILD/firmware.map check

# Who really owns the merged string pool.
python3 tools/sizing/strpool.py $BUILD/firmware.map $OBJDUMP

# Per-archive delta between two builds (keep a copy of the control map).
python3 tools/sizing/armdiff.py control.map $BUILD/firmware.map
python3 tools/sizing/armdiff.py control.map $BUILD/firmware.map 25 libwpa
```

`armdiff.py` shells out to `mapsize.py` from its own directory, so the three
files travel together. The P4 target works the same way — point `BUILD` at
`.pio/build/firebeetle2` and use the `riscv32-esp-elf-objdump` from
`~/.platformio-p4`.

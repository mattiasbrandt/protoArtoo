#!/usr/bin/env python3
"""Attribute linked bytes to archives / objects from a GNU ld map (ESP32).

usage: mapsize.py firmware.map [archive|object|outsec] [top] [filter-substring]

Only the "Linker script and memory map" part is parsed (discarded input
sections are skipped). Each input section is credited to its owning archive
(basename of lib*.a) or, for loose objects, to the object path relative to the
build dir. Bytes are split by output-section class:

  flash_text  .flash.text
  flash_ro    .flash.rodata / .flash.appdesc / .flash.rodata_noload(excluded)
  iram        .iram0.text / .iram0.vectors
  dram_data   .dram0.data          (in image AND in RAM)
  dram_bss    .dram0.bss           (RAM only, NOLOAD)
  rtc         .rtc.*
  other       everything else (debug etc. is not in the map memory region)
"""
import re, sys, collections, os

map_path = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else 'archive'
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
filt = sys.argv[4] if len(sys.argv) > 4 else None

def classify(outsec):
    if outsec in ('.flash.text',):
        return 'flash_text'
    if outsec in ('.flash.rodata', '.flash.appdesc', '.flash.rodata_noload', '.flash_rodata_dummy'):
        return 'flash_ro' if outsec in ('.flash.rodata', '.flash.appdesc') else 'skip'
    if outsec.startswith('.iram0'):
        return 'iram'
    if outsec == '.dram0.data':
        return 'dram_data'
    if outsec == '.dram0.bss':
        return 'dram_bss'
    if outsec.startswith('.rtc'):
        return 'rtc'
    return 'other'

CLASSES = ['flash_text', 'flash_ro', 'iram', 'dram_data', 'dram_bss', 'rtc', 'other']

out_re = re.compile(r'^(\.[\w.\-]+)(?:\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+))?')
in_re_full = re.compile(r'^ (\.[^\s]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+?)\s*$')
in_re_name = re.compile(r'^ (\.[^\s]+)\s*$')
in_re_cont = re.compile(r'^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+?)\s*$')
fill_re = re.compile(r'^ \*fill\*\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)')
outsec_sizes = collections.OrderedDict()

def owner(path):
    m = re.search(r'([^/\s]+\.a)\((.+)\)$', path)
    if m:
        return (m.group(1), m.group(1) + '(' + m.group(2) + ')')
    # loose object
    p = path
    idx = p.find('.pio/build/')
    if idx >= 0:
        p = p[idx + len('.pio/build/'):]
        p = p.split('/', 1)[1] if '/' in p else p
    return ('<obj>' + p.split('/')[0] if '/' in p else '<obj>', p)

by_key = collections.defaultdict(lambda: collections.Counter())
outsec_input_sum = collections.Counter()
str_re = re.compile(r'\.str1\.\d+$')
def credit(cls, secname, size, path):
    outsec_input_sum[cur_out] += 0
    if str_re.search(secname):
        # GNU ld merges all mergeable string sections into the first one it
        # meets and lists the whole pool there; the others keep their pre-merge
        # size at a non-advancing address. Count the pool once.
        if size > 4096:
            by_key[('*merged-strings*', '*merged-strings*')][cls] += size
            outsec_input_sum[cur_out] += size
        return
    by_key[owner(path)][cls] += size
    outsec_input_sum[cur_out] += size
cur_out = None
pending_in = None
started = False
with open(map_path, 'r', errors='replace') as f:
    for line in f:
        line = line.rstrip('\n')
        if not started:
            if line.startswith('Linker script and memory map'):
                started = True
            continue
        if line.startswith('OUTPUT(') or line.startswith('Cross Reference Table'):
            break
        if not line.strip():
            continue
        if line[0] == '.':
            m = out_re.match(line)
            if m:
                cur_out = m.group(1)
                if m.group(3):
                    outsec_sizes[cur_out] = int(m.group(3), 16)
                pending_in = None
                continue
        if line[0] != ' ':
            # e.g. LOAD, START GROUP, symbols at col0
            pending_in = None
            continue
        if cur_out is None:
            continue
        cls = classify(cur_out)
        m = fill_re.match(line)
        if m:
            if cls != 'skip':
                by_key[('*fill*', '*fill*')][cls] += int(m.group(2), 16)
                outsec_input_sum[cur_out] += int(m.group(2), 16)
            continue
        if line.startswith(' *('):
            pending_in = None
            continue
        m = in_re_full.match(line)
        if m:
            size = int(m.group(3), 16)
            if cls != 'skip' and size:
                credit(cls, m.group(1), size, m.group(4))
            pending_in = None
            continue
        m = in_re_name.match(line)
        if m:
            pending_in = m.group(1)
            continue
        if pending_in:
            m = in_re_cont.match(line)
            if m:
                size = int(m.group(2), 16)
                if cls != 'skip' and size:
                    credit(cls, pending_in, size, m.group(3))
            pending_in = None
            continue

if mode == 'check':
    for k, v in outsec_sizes.items():
        if v and classify(k) not in ('skip', 'other'):
            print(f'{k:<18} output {v:>8}  inputs+fill {outsec_input_sum[k]:>8}  diff {v - outsec_input_sum[k]:>6}')
    sys.exit(0)
if mode == 'outsec':
    tot = 0
    for k, v in outsec_sizes.items():
        if v:
            print(f'{v:>9}  {k}')
    sys.exit(0)

agg = collections.defaultdict(lambda: collections.Counter())
for (arch, obj), c in by_key.items():
    key = arch if mode == 'archive' else obj
    if filt and filt not in key:
        continue
    for cls, n in c.items():
        agg[key][cls] += n

def image_bytes(c):
    return c['flash_text'] + c['flash_ro'] + c['iram'] + c['dram_data'] + c['rtc']

rows = sorted(agg.items(), key=lambda kv: -image_bytes(kv[1]))
hdr = f"{'image':>8} {'flash_text':>10} {'flash_ro':>9} {'iram':>7} {'d.data':>7} {'d.bss':>7} {'rtc':>5}  key"
print(hdr)
tot = collections.Counter()
for key, c in rows[:top]:
    print(f"{image_bytes(c):>8} {c['flash_text']:>10} {c['flash_ro']:>9} {c['iram']:>7} {c['dram_data']:>7} {c['dram_bss']:>7} {c['rtc']:>5}  {key}")
for key, c in rows:
    tot.update(c)
print('-' * len(hdr))
print(f"{image_bytes(tot):>8} {tot['flash_text']:>10} {tot['flash_ro']:>9} {tot['iram']:>7} {tot['dram_data']:>7} {tot['dram_bss']:>7} {tot['rtc']:>5}  TOTAL ({len(rows)} keys)")

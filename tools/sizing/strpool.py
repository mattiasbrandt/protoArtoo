#!/usr/bin/env python3
"""Who contributes to the merged string pool?

1. From the map, collect every linked input file (archive member or loose .o)
   with its full path.
2. objdump -h each and sum the pre-merge sizes of .rodata.str* sections.
3. Report per archive (or per top-level obj dir), plus verify the merged
   .rodata.str1.1 line credited to the first contributor in the map.

usage: strpool.py firmware.map objdump-path [top]
"""
import re, sys, subprocess, collections, os

map_path, objdump = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
root = os.getcwd()

# --- 1. linked inputs -------------------------------------------------------
in_full = re.compile(r'^ (\.[^\s]+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+?)\s*$')
in_cont = re.compile(r'^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(.+?)\s*$')
in_name = re.compile(r'^ (\.[^\s]+)\s*$')
archives = collections.defaultdict(set)   # full archive path -> members
objects = set()
merged_lines = []
started = False
pending = None
cur_out = None
with open(map_path, errors='replace') as f:
    for line in f:
        line = line.rstrip('\n')
        if not started:
            started = line.startswith('Linker script and memory map')
            continue
        if line.startswith('OUTPUT(') or line.startswith('Cross Reference Table'):
            break
        if line and line[0] == '.':
            cur_out = line.split()[0]
            pending = None
            continue
        m = in_full.match(line) or None
        path = None
        sec = None
        if m:
            sec, size, path = m.group(1), int(m.group(3), 16), m.group(4)
            pending = None
        else:
            m2 = in_name.match(line)
            if m2:
                pending = m2.group(1)
                continue
            if pending:
                m3 = in_cont.match(line)
                if m3:
                    sec, size, path = pending, int(m3.group(2), 16), m3.group(3)
                pending = None
        if not path:
            continue
        if re.search(r'\.str1\.\d+$', sec) and size > 4096:
            merged_lines.append((size, sec, path, cur_out))
        am = re.match(r'^(.*\.a)\((.+)\)$', path)
        if am:
            archives[am.group(1)].add(am.group(2))
        else:
            objects.add(path)

print('# merged string sections > 4 KiB as credited by the map')
for size, sec, path, out in sorted(merged_lines, reverse=True):
    print(f'  {size:>8}  {sec:<22} {out:<16} {path}')

# --- 2. pre-merge sizes -------------------------------------------------------
def rodata_str_sizes(path):
    """Return {member: bytes} of .rodata.str* sections via objdump -h."""
    try:
        out = subprocess.run([objdump, '-h', path], capture_output=True, text=True, timeout=120).stdout
    except Exception as e:
        return {}
    res = collections.Counter()
    member = os.path.basename(path)
    for line in out.splitlines():
        mm = re.match(r'^(.+?):\s+file format', line)
        if mm:
            member = mm.group(1)
            # archive members print as "path(member):" -- keep the member
            am = re.match(r'^.*\((.+)\)$', member)
            member = am.group(1) if am else os.path.basename(member)
            continue
        ms = re.match(r'^\s*\d+\s+(\.rodata\S*\.str1\.\d+|\.rodata\.str\S*)\s+([0-9a-fA-F]+)\s', line)
        if ms:
            res[member] += int(ms.group(2), 16)
    return res

per_key = collections.Counter()
per_obj = collections.Counter()
for arch, members in archives.items():
    sizes = rodata_str_sizes(arch)
    key = os.path.basename(arch)
    for mbr in members:
        n = sizes.get(mbr, 0)
        per_key[key] += n
        per_obj[f'{key}({mbr})'] += n
for obj in objects:
    sizes = rodata_str_sizes(obj)
    n = sum(sizes.values())
    rel = obj
    i = rel.find('.pio/build/')
    if i >= 0:
        rel = rel[i + len('.pio/build/'):].split('/', 1)[1]
    key = '<obj>' + (rel.split('/')[0] if '/' in rel else rel)
    per_key[key] += n
    per_obj[rel] += n

total = sum(per_key.values())
print(f'\n# pre-merge .rodata.str* bytes by archive / obj dir (sum {total}; merged pool is smaller by exact-duplicate strings)')
for k, n in per_key.most_common(top):
    print(f'  {n:>8}  {100.0*n/total:5.1f}%  {k}')
print(f'\n# top {top} objects by pre-merge .rodata.str* bytes')
for k, n in per_obj.most_common(top):
    print(f'  {n:>8}  {k}')

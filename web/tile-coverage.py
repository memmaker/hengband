#!/usr/bin/env python3
"""Coverage of a tile set over every monster, base item and terrain of the
JSON data (lib/edit/*Definitions.jsonc, ids at the top array level; id 0 is
the unused dummy).  The pref maps R:<monrace id>, K:<baseitem id>,
F:<terrain id>; an entry counts when it maps to a non-empty tile inside the
sheet.

  python3 web/tile-coverage.py         own 8x8 set (graf-xxx.prf, lib/xtra/graf/8x8.bmp)
  python3 web/tile-coverage.py ab      Adam Bolt 16x16 (graf-new.prf + graf-ab.prf, web/tiles.webp; stage 4)
  python3 web/tile-coverage.py 8x8     own 8x8 set with its stand-ins (graf-xxx.prf + graf-8x8.prf)
  python3 web/tile-coverage.py shb     Shockbolt 64x64 (graf-shb.prf, web/tiles-shb.webp)
Stand-ins (same-set family tiles) are counted apart: every line of
graf-ab/graf-8x8.prf, and the "(stand-in)" entries of graf-shb.prf."""
import re, sys, os
from PIL import Image
L = 'lib'
SET = (sys.argv[1:] or ['own'])[0]
if SET == 'ab':
    prefs, sheet, S = ('graf-new', 'graf-ab'), 'web/tiles.webp', 16
elif SET == '8x8':
    prefs, sheet, S = ('graf-xxx', 'graf-8x8'), f'{L}/xtra/graf/8x8.bmp', 8
elif SET == 'shb':
    prefs, sheet, S = ('graf-shb',), 'web/tiles-shb.webp', 64
else:
    prefs, sheet, S = ('graf-xxx',), f'{L}/xtra/graf/8x8.bmp', 8
img = Image.open(sheet).convert('RGBA')
W, H = img.size
def ok(a, c):
    x, y = (c & 0x7F) * S, (a & 0x7F) * S
    if not (a & 0x80 and c & 0x80) or x + S > W or y + S > H:
        return False
    t = img.crop((x, y, x + S, y + S))   # 8x8: uniform grey (48,48,48) = unused cell
    return t.getbbox() is not None and (S != 8 or t.getcolors(1) != [(S * S, (48, 48, 48, 255))])
maps, stand = {}, set()
for p in prefs:
    prev = ''
    for line in open(f'{L}/pref/{p}.prf', encoding='latin-1'):
        m = re.match(r'([RKF]):(\d+):(0x\w+)[:/](0x\w+)', line)
        if m and not maps.get((m[1], m[2])):
            maps[(m[1], m[2])] = ok(int(m[3], 16), int(m[4], 16))
            if p in ('graf-ab', 'graf-8x8') or '(stand-in)' in prev:
                stand.add((m[1], m[2]))
        prev = line
tot = hit = sts = 0
for kind, f in (('R', 'MonraceDefinitions'), ('K', 'BaseitemDefinitions'), ('F', 'TerrainDefinitions')):
    txt = open(f'{L}/edit/{f}.jsonc', encoding='utf-8').read()
    ids = [i for i in re.findall(r'^      "id": (\d+)', txt, re.M) if i != '0']
    miss = [i for i in ids if not maps.get((kind, i))]
    st = sum((kind, i) in stand for i in ids if (kind, i) not in miss and maps.get((kind, i)))
    print(f'{f}: {len(ids) - len(miss)}/{len(ids)} (real {len(ids) - len(miss) - st}, stand-ins {st})  missing: {miss[:20]}{" ..." if len(miss) > 20 else ""}')
    tot += len(ids); hit += len(ids) - len(miss); sts += st
print(f'total: {hit}/{tot} = {100 * hit / tot:.1f}%, real {hit - sts} = {100 * (hit - sts) / tot:.1f}%, stand-ins {sts}')

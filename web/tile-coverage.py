#!/usr/bin/env python3
"""Coverage of a tile set over every monster, base item and terrain of the
JSON data (lib/edit/*Definitions.jsonc, ids at the top array level; id 0 is
the unused dummy).  The pref maps R:<monrace id>, K:<baseitem id>,
F:<terrain id>; an entry counts when it maps to a non-empty tile inside the
sheet.

  python3 web/tile-coverage.py         own 8x8 set (graf-xxx.prf, lib/xtra/graf/8x8.bmp)
  python3 web/tile-coverage.py shb     Shockbolt (graf-shb.prf, 64x64; stage 4)"""
import re, sys, os
from PIL import Image
L = 'lib'
if sys.argv[1:] == ['shb']:
    prefs, sheet, S = ('graf-shb',), os.path.expanduser('~/Games/tactical-angband/lib/tiles/shockbolt/64x64.png'), 64
else:
    prefs, sheet, S = ('graf-xxx',), f'{L}/xtra/graf/8x8.bmp', 8
img = Image.open(sheet).convert('RGBA')
W, H = img.size
def ok(a, c):
    x, y = (c & 0x7F) * S, (a & 0x7F) * S
    if not (a & 0x80 and c & 0x80) or x + S > W or y + S > H:
        return False
    return img.crop((x, y, x + S, y + S)).getbbox() is not None
maps = {}
for p in prefs:
    for line in open(f'{L}/pref/{p}.prf', encoding='latin-1'):
        m = re.match(r'([RKF]):(\d+):(0x\w+)[:/](0x\w+)', line)
        if m:
            maps[(m[1], m[2])] = maps.get((m[1], m[2])) or ok(int(m[3], 16), int(m[4], 16))
tot = hit = 0
for kind, f in (('R', 'MonraceDefinitions'), ('K', 'BaseitemDefinitions'), ('F', 'TerrainDefinitions')):
    txt = open(f'{L}/edit/{f}.jsonc', encoding='utf-8').read()
    ids = [i for i in re.findall(r'^      "id": (\d+)', txt, re.M) if i != '0']
    miss = [i for i in ids if not maps.get((kind, i))]
    print(f'{f}: {len(ids) - len(miss)}/{len(ids)}  missing: {miss[:20]}{" ..." if len(miss) > 20 else ""}')
    tot += len(ids); hit += len(ids) - len(miss)
print(f'total: {hit}/{tot} = {100 * hit / tot:.1f}%')

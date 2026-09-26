#!/usr/bin/env python3
"""Write the web build's sound.cfg from Hengband's own lib/xtra/sound (CC0
samples, see lib/xtra/sound/readme.txt) and copy only the used .wavs.
Events upstream leaves empty get a close upstream sample (FILL).
Usage: sounds.py <sound.cfg to write> <wav dir>"""
import os, re, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SND = os.path.join(HERE, '../lib/xtra/sound')
# event -> upstream event whose samples it borrows ('' = silent)
FILL = {'walk': '', 'unused': '', 'dig': 'hitwall', 'dig_through': 'hitwall', 'illegal': 'fail',
        'acid': 'destitem', 'fire': 'destitem', 'elec': 'destitem', 'cold': 'destitem',
        'atkspell': 'zap', 'ball': 'explode', 'rocket': 'explode', 'm_spell': 'evil',
        'heal': 'quaff', 'x_heal': 'quaff', 'm_heal': 'quaff', 'buff_expire': 'fail',
        'invuln': 'reflect', 'quest': 'level', 'winner': 'level', 'u_kill': 'kill',
        'wakeup': 'warn', 'backstab_hit': 'good_hit', 'surprise_hit': 'good_hit',
        'fleeing_hit': 'good_hit', 'fatal_spot': 'gouge_hit', 'ninja_critical_hit': 'good_hit',
        'ninja_fatal_hit': 'gouge_hit'}
EVENTS = re.findall(r'"(\w+)"', open(os.path.join(HERE, '../src/main/sound-definitions-table.cpp')).read())
cfg = {}
for line in open(os.path.join(SND, 'sound.cfg'), encoding='utf-8', errors='replace'):
    if '=' in line and not line.lstrip().startswith('#'):
        k, v = line.split('=', 1)
        cfg[k.strip()] = v.split()
cfg_path, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
lines = ["# Hengband web build: Hengband's own CC0 samples (web/sounds.py)", '[Sound]']
for e in EVENTS:
    files = cfg.get(e) or (cfg.get(FILL[e], []) if FILL.get(e) else [])
    assert files or FILL.get(e) == '', 'no sample for ' + e
    for f in files:
        shutil.copy(os.path.join(SND, f), out)
    lines.append(f'{e} = {" ".join(files)}')
open(cfg_path, 'w').write('\n'.join(lines) + '\n')
